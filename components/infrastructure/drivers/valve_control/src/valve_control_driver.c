/**
 * @file valve_control_driver.c
 * @brief Implementación del driver de control de válvulas de riego
 *
 * Smart Irrigation System - ESP32 IoT Project
 * Infrastructure Layer - Hardware Driver
 *
 * Controla válvulas solenoides a través de relés con protecciones de seguridad,
 * monitoreo de estado y gestión de energía optimizada para operación con batería.
 *
 * Autor: Claude + Liwaisi Tech Team
 * Versión: 1.4.0 - Valve Control Driver Implementation
 */

#include "valve_control_driver.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include <string.h>
#include <inttypes.h>

static const char* TAG = "valve_control_driver";

// ============================================================================
// CONFIGURACIÓN Y ESTADO GLOBAL
// ============================================================================

/**
 * @brief Configuración por defecto del driver
 */
static const valve_control_config_t DEFAULT_CONFIG = {
    .valve_response_timeout_ms = 5000,
    .relay_settling_time_ms = 100,
    .interlock_delay_ms = 200,
    .max_operation_duration_ms = 1800000, // 30 minutos
    .enable_interlock_protection = true,
    .enable_timeout_protection = true,
    .enable_state_monitoring = true,
    .monitoring_interval_ms = 1000,
    .enable_power_optimization = true,
    .relay_hold_reduction_delay_ms = 5000
};

/**
 * @brief Estado global del driver
 */
typedef struct {
    bool is_initialized;
    valve_control_config_t config;
    valve_system_statistics_t statistics;
    
    // Estado de válvulas individuales
    struct {
        valve_state_t state;
        uint32_t operation_start_time;
        uint32_t operation_duration_ms;
        valve_error_type_t last_error;
        uint32_t last_operation_timestamp;
        bool is_emergency_stopped;
    } valve_states[3];
    
    // Control de concurrencia
    SemaphoreHandle_t operation_mutex;
    SemaphoreHandle_t state_mutex;
    
    // Control de emergencia
    bool emergency_stop_active;
    uint32_t emergency_stop_timestamp;
    char emergency_stop_reason[64];
    
    // Callback de eventos
    valve_event_callback_t event_callback;
    
    // Task de monitoreo
    TaskHandle_t monitoring_task_handle;
    bool monitoring_task_running;
    
} valve_control_driver_state_t;

static valve_control_driver_state_t g_driver_state = {0};

// ============================================================================
// FUNCIONES AUXILIARES INTERNAS
// ============================================================================

/**
 * @brief Obtener timestamp actual en milisegundos
 */
static uint32_t get_current_timestamp_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/**
 * @brief Obtener GPIO pin para válvula específica
 */
static gpio_num_t get_valve_gpio_pin(uint8_t valve_number) {
    switch (valve_number) {
        case 1: return VALVE_1_GPIO_PIN;
        case 2: return VALVE_2_GPIO_PIN;
        case 3: return VALVE_3_GPIO_PIN;
        default: return GPIO_NUM_NC;
    }
}

/**
 * @brief Validar número de válvula
 */
static bool is_valid_valve_number(uint8_t valve_number) {
    return (valve_number >= 1 && valve_number <= 3);
}

/**
 * @brief Configurar GPIO para válvula
 */
static esp_err_t configure_valve_gpio(gpio_num_t pin) {
    if (pin == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_ARG;
    }
    
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << pin),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d: %s", pin, esp_err_to_name(ret));
        return ret;
    }
    
    // Inicializar en estado cerrado (LOW)
    gpio_set_level(pin, 0);
    
    ESP_LOGI(TAG, "GPIO %d configured for valve control", pin);
    return ESP_OK;
}

/**
 * @brief Controlar relé de válvula
 */
static esp_err_t control_valve_relay(uint8_t valve_number, bool open) {
    if (!is_valid_valve_number(valve_number)) {
        return ESP_ERR_INVALID_ARG;
    }
    
    gpio_num_t pin = get_valve_gpio_pin(valve_number);
    if (pin == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Controlar relé (HIGH = válvula abierta, LOW = válvula cerrada)
    int level = open ? 1 : 0;
    esp_err_t ret = gpio_set_level(pin, level);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Valve %d relay set to %s (GPIO %d = %d)", 
                 valve_number, open ? "OPEN" : "CLOSED", pin, level);
    } else {
        ESP_LOGE(TAG, "Failed to control valve %d relay: %s", valve_number, esp_err_to_name(ret));
    }
    
    return ret;
}

/**
 * @brief Verificar estado físico del relé
 */
static bool get_valve_relay_state(uint8_t valve_number) {
    if (!is_valid_valve_number(valve_number)) {
        return false;
    }
    
    gpio_num_t pin = get_valve_gpio_pin(valve_number);
    if (pin == GPIO_NUM_NC) {
        return false;
    }
    
    return gpio_get_level(pin) == 1;
}

/**
 * @brief Actualizar estado interno de válvula
 */
static void update_valve_state(uint8_t valve_number, valve_state_t new_state) {
    if (!is_valid_valve_number(valve_number)) {
        return;
    }
    
    if (xSemaphoreTake(g_driver_state.state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        valve_state_t old_state = g_driver_state.valve_states[valve_number - 1].state;
        g_driver_state.valve_states[valve_number - 1].state = new_state;
        g_driver_state.valve_states[valve_number - 1].last_operation_timestamp = get_current_timestamp_ms();
        
        xSemaphoreGive(g_driver_state.state_mutex);
        
        // Notificar cambio de estado
        if (old_state != new_state && g_driver_state.event_callback) {
            g_driver_state.event_callback(valve_number, "state_change", &new_state);
        }
        
        ESP_LOGI(TAG, "Valve %d state changed: %s -> %s", 
                 valve_number, 
                 valve_control_driver_state_to_string(old_state),
                 valve_control_driver_state_to_string(new_state));
    }
}

/**
 * @brief Verificar timeout de operación
 */
static bool check_operation_timeout(uint8_t valve_number) {
    if (!is_valid_valve_number(valve_number)) {
        return false;
    }
    
    uint32_t current_time = get_current_timestamp_ms();
    uint32_t start_time = g_driver_state.valve_states[valve_number - 1].operation_start_time;
    uint32_t duration = g_driver_state.valve_states[valve_number - 1].operation_duration_ms;
    
    if (start_time > 0 && duration > 0) {
        return (current_time - start_time) >= duration;
    }
    
    return false;
}

/**
 * @brief Task de monitoreo de válvulas
 */
static void valve_monitoring_task(void *pvParameters) {
    ESP_LOGI(TAG, "Valve monitoring task started");
    
    while (g_driver_state.monitoring_task_running) {
        if (g_driver_state.config.enable_state_monitoring) {
            // Verificar timeouts de operación
            for (uint8_t valve = 1; valve <= 3; valve++) {
                if (g_driver_state.valve_states[valve - 1].state == VALVE_STATE_OPEN) {
                    if (check_operation_timeout(valve)) {
                        ESP_LOGW(TAG, "Valve %d operation timeout detected", valve);
                        
                        if (g_driver_state.config.enable_timeout_protection) {
                            valve_control_driver_close_valve(valve);
                            g_driver_state.statistics.timeout_errors++;
                            
                            if (g_driver_state.event_callback) {
                                g_driver_state.event_callback(valve, "timeout", NULL);
                            }
                        }
                    }
                }
            }
            
            // Verificar estado de emergencia
            if (g_driver_state.emergency_stop_active) {
                // Asegurar que todas las válvulas estén cerradas
                for (uint8_t valve = 1; valve <= 3; valve++) {
                    if (g_driver_state.valve_states[valve - 1].state == VALVE_STATE_OPEN) {
                        ESP_LOGW(TAG, "Emergency stop: forcing valve %d to close", valve);
                        control_valve_relay(valve, false);
                        update_valve_state(valve, VALVE_STATE_CLOSED);
                    }
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(g_driver_state.config.monitoring_interval_ms));
    }
    
    ESP_LOGI(TAG, "Valve monitoring task stopped");
    vTaskDelete(NULL);
}

// ============================================================================
// FUNCIONES PÚBLICAS - INICIALIZACIÓN
// ============================================================================

esp_err_t valve_control_driver_init(const valve_control_config_t* config) {
    ESP_LOGI(TAG, "Initializing valve control driver");
    
    // Limpiar estado
    memset(&g_driver_state, 0, sizeof(valve_control_driver_state_t));
    
    // Copiar configuración
    if (config) {
        memcpy(&g_driver_state.config, config, sizeof(valve_control_config_t));
    } else {
        memcpy(&g_driver_state.config, &DEFAULT_CONFIG, sizeof(valve_control_config_t));
    }
    
    // Crear mutexes
    g_driver_state.operation_mutex = xSemaphoreCreateMutex();
    g_driver_state.state_mutex = xSemaphoreCreateMutex();
    
    if (!g_driver_state.operation_mutex || !g_driver_state.state_mutex) {
        ESP_LOGE(TAG, "Failed to create mutexes");
        return ESP_ERR_NO_MEM;
    }
    
    // Configurar GPIOs para válvulas
    esp_err_t ret = ESP_OK;
    for (uint8_t valve = 1; valve <= 3; valve++) {
        gpio_num_t pin = get_valve_gpio_pin(valve);
        ret = configure_valve_gpio(pin);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure valve %d GPIO", valve);
            return ret;
        }
        
        // Inicializar estado de válvula
        g_driver_state.valve_states[valve - 1].state = VALVE_STATE_CLOSED;
        g_driver_state.valve_states[valve - 1].last_error = VALVE_ERROR_NONE;
    }
    
    // Inicializar estadísticas
    memset(&g_driver_state.statistics, 0, sizeof(valve_system_statistics_t));
    
    // Crear task de monitoreo
    g_driver_state.monitoring_task_running = true;
    BaseType_t task_ret = xTaskCreate(
        valve_monitoring_task,
        "valve_monitor",
        2048,
        NULL,
        2,
        &g_driver_state.monitoring_task_handle
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create monitoring task");
        g_driver_state.monitoring_task_running = false;
        return ESP_ERR_NO_MEM;
    }
    
    g_driver_state.is_initialized = true;
    
    ESP_LOGI(TAG, "Valve control driver initialized successfully");
    ESP_LOGI(TAG, "Valve 1: GPIO %d, Valve 2: GPIO %d, Valve 3: GPIO %d", 
             VALVE_1_GPIO_PIN, VALVE_2_GPIO_PIN, VALVE_3_GPIO_PIN);
    
    return ESP_OK;
}

esp_err_t valve_control_driver_deinit(void) {
    ESP_LOGI(TAG, "Deinitializing valve control driver");
    
    if (!g_driver_state.is_initialized) {
        return ESP_OK;
    }
    
    // Parar task de monitoreo
    g_driver_state.monitoring_task_running = false;
    if (g_driver_state.monitoring_task_handle) {
        vTaskDelete(g_driver_state.monitoring_task_handle);
        g_driver_state.monitoring_task_handle = NULL;
    }
    
    // Cerrar todas las válvulas
    valve_control_driver_close_all_valves();
    
    // Liberar mutexes
    if (g_driver_state.operation_mutex) {
        vSemaphoreDelete(g_driver_state.operation_mutex);
        g_driver_state.operation_mutex = NULL;
    }
    
    if (g_driver_state.state_mutex) {
        vSemaphoreDelete(g_driver_state.state_mutex);
        g_driver_state.state_mutex = NULL;
    }
    
    g_driver_state.is_initialized = false;
    
    ESP_LOGI(TAG, "Valve control driver deinitialized");
    return ESP_OK;
}

// ============================================================================
// FUNCIONES PÚBLICAS - CONTROL DE VÁLVULAS
// ============================================================================

esp_err_t valve_control_driver_open_valve(uint8_t valve_number) {
    if (!g_driver_state.is_initialized) {
        ESP_LOGE(TAG, "Driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!is_valid_valve_number(valve_number)) {
        ESP_LOGE(TAG, "Invalid valve number: %d", valve_number);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_driver_state.operation_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire operation mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    // Verificar estado de emergencia
    if (g_driver_state.emergency_stop_active) {
        ESP_LOGW(TAG, "Cannot open valve %d: emergency stop active", valve_number);
        xSemaphoreGive(g_driver_state.operation_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    
    // Verificar interlock si está habilitado
    if (g_driver_state.config.enable_interlock_protection) {
        if (valve_control_driver_has_open_valves()) {
            ESP_LOGW(TAG, "Cannot open valve %d: interlock protection active", valve_number);
            xSemaphoreGive(g_driver_state.operation_mutex);
            return ESP_ERR_INVALID_STATE;
        }
    }
    
    // Verificar si la válvula ya está abierta
    if (g_driver_state.valve_states[valve_number - 1].state == VALVE_STATE_OPEN) {
        ESP_LOGI(TAG, "Valve %d is already open", valve_number);
        xSemaphoreGive(g_driver_state.operation_mutex);
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Opening valve %d", valve_number);
    
    // Actualizar estado a abriéndose
    update_valve_state(valve_number, VALVE_STATE_OPENING);
    
    // Controlar relé
    esp_err_t ret = control_valve_relay(valve_number, true);
    if (ret != ESP_OK) {
        update_valve_state(valve_number, VALVE_STATE_ERROR);
        g_driver_state.valve_states[valve_number - 1].last_error = VALVE_ERROR_GPIO_FAILURE;
        g_driver_state.statistics.failed_operations++;
        xSemaphoreGive(g_driver_state.operation_mutex);
        return ret;
    }
    
    // Esperar tiempo de asentamiento del relé
    vTaskDelay(pdMS_TO_TICKS(g_driver_state.config.relay_settling_time_ms));
    
    // Actualizar estado a abierta
    update_valve_state(valve_number, VALVE_STATE_OPEN);
    g_driver_state.valve_states[valve_number - 1].operation_start_time = get_current_timestamp_ms();
    g_driver_state.valve_states[valve_number - 1].operation_duration_ms = g_driver_state.config.max_operation_duration_ms;
    
    // Actualizar estadísticas
    g_driver_state.statistics.total_open_commands++;
    g_driver_state.statistics.successful_operations++;
    
    xSemaphoreGive(g_driver_state.operation_mutex);
    
    ESP_LOGI(TAG, "Valve %d opened successfully", valve_number);
    return ESP_OK;
}

esp_err_t valve_control_driver_close_valve(uint8_t valve_number) {
    if (!g_driver_state.is_initialized) {
        ESP_LOGE(TAG, "Driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!is_valid_valve_number(valve_number)) {
        ESP_LOGE(TAG, "Invalid valve number: %d", valve_number);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_driver_state.operation_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire operation mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    // Verificar si la válvula ya está cerrada
    if (g_driver_state.valve_states[valve_number - 1].state == VALVE_STATE_CLOSED) {
        ESP_LOGI(TAG, "Valve %d is already closed", valve_number);
        xSemaphoreGive(g_driver_state.operation_mutex);
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Closing valve %d", valve_number);
    
    // Actualizar estado a cerrándose
    update_valve_state(valve_number, VALVE_STATE_CLOSING);
    
    // Controlar relé
    esp_err_t ret = control_valve_relay(valve_number, false);
    if (ret != ESP_OK) {
        update_valve_state(valve_number, VALVE_STATE_ERROR);
        g_driver_state.valve_states[valve_number - 1].last_error = VALVE_ERROR_GPIO_FAILURE;
        g_driver_state.statistics.failed_operations++;
        xSemaphoreGive(g_driver_state.operation_mutex);
        return ret;
    }
    
    // Esperar tiempo de asentamiento del relé
    vTaskDelay(pdMS_TO_TICKS(g_driver_state.config.relay_settling_time_ms));
    
    // Actualizar estado a cerrada
    update_valve_state(valve_number, VALVE_STATE_CLOSED);
    
    // Calcular tiempo de operación
    uint32_t current_time = get_current_timestamp_ms();
    uint32_t start_time = g_driver_state.valve_states[valve_number - 1].operation_start_time;
    if (start_time > 0) {
        uint32_t operation_time = current_time - start_time;
        g_driver_state.statistics.total_irrigation_time_ms += operation_time;
    }
    
    // Limpiar estado de operación
    g_driver_state.valve_states[valve_number - 1].operation_start_time = 0;
    g_driver_state.valve_states[valve_number - 1].operation_duration_ms = 0;
    
    // Actualizar estadísticas
    g_driver_state.statistics.total_close_commands++;
    g_driver_state.statistics.successful_operations++;
    
    xSemaphoreGive(g_driver_state.operation_mutex);
    
    ESP_LOGI(TAG, "Valve %d closed successfully", valve_number);
    return ESP_OK;
}

esp_err_t valve_control_driver_close_all_valves(void) {
    ESP_LOGI(TAG, "Closing all valves");
    
    esp_err_t ret = ESP_OK;
    esp_err_t last_error = ESP_OK;
    
    for (uint8_t valve = 1; valve <= 3; valve++) {
        esp_err_t valve_ret = valve_control_driver_close_valve(valve);
        if (valve_ret != ESP_OK) {
            last_error = valve_ret;
            ret = ESP_FAIL;
        }
    }
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "All valves closed successfully");
    } else {
        ESP_LOGE(TAG, "Some valves failed to close, last error: %s", esp_err_to_name(last_error));
    }
    
    return ret;
}

// ============================================================================
// FUNCIONES PÚBLICAS - CONSULTA DE ESTADO
// ============================================================================

valve_state_t valve_control_driver_get_valve_state(uint8_t valve_number) {
    if (!g_driver_state.is_initialized || !is_valid_valve_number(valve_number)) {
        return VALVE_STATE_ERROR;
    }
    
    return g_driver_state.valve_states[valve_number - 1].state;
}

bool valve_control_driver_is_valve_open(uint8_t valve_number) {
    return valve_control_driver_get_valve_state(valve_number) == VALVE_STATE_OPEN;
}

bool valve_control_driver_is_valve_closed(uint8_t valve_number) {
    return valve_control_driver_get_valve_state(valve_number) == VALVE_STATE_CLOSED;
}

bool valve_control_driver_has_open_valves(void) {
    for (uint8_t valve = 1; valve <= 3; valve++) {
        if (valve_control_driver_is_valve_open(valve)) {
            return true;
        }
    }
    return false;
}

uint8_t valve_control_driver_get_open_valve_count(void) {
    uint8_t count = 0;
    for (uint8_t valve = 1; valve <= 3; valve++) {
        if (valve_control_driver_is_valve_open(valve)) {
            count++;
        }
    }
    return count;
}

uint32_t valve_control_driver_get_valve_open_time(uint8_t valve_number) {
    if (!g_driver_state.is_initialized || !is_valid_valve_number(valve_number)) {
        return 0;
    }
    
    if (g_driver_state.valve_states[valve_number - 1].state != VALVE_STATE_OPEN) {
        return 0;
    }
    
    uint32_t current_time = get_current_timestamp_ms();
    uint32_t start_time = g_driver_state.valve_states[valve_number - 1].operation_start_time;
    
    if (start_time > 0) {
        return current_time - start_time;
    }
    
    return 0;
}

// ============================================================================
// FUNCIONES PÚBLICAS - CONTROL DE EMERGENCIA
// ============================================================================

esp_err_t valve_control_driver_emergency_stop(const char* reason) {
    ESP_LOGW(TAG, "Emergency stop activated: %s", reason ? reason : "Unknown reason");
    
    if (!g_driver_state.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Activar flag de emergencia
    g_driver_state.emergency_stop_active = true;
    g_driver_state.emergency_stop_timestamp = get_current_timestamp_ms();
    
    if (reason) {
        strncpy(g_driver_state.emergency_stop_reason, reason, sizeof(g_driver_state.emergency_stop_reason) - 1);
        g_driver_state.emergency_stop_reason[sizeof(g_driver_state.emergency_stop_reason) - 1] = '\0';
    }
    
    // Cerrar todas las válvulas inmediatamente
    esp_err_t ret = valve_control_driver_close_all_valves();
    
    // Actualizar estadísticas
    g_driver_state.statistics.safety_stops++;
    
    // Notificar evento
    if (g_driver_state.event_callback) {
        g_driver_state.event_callback(0, "emergency_stop", (void*)reason);
    }
    
    ESP_LOGW(TAG, "Emergency stop completed");
    return ret;
}

bool valve_control_driver_is_emergency_stopped(void) {
    return g_driver_state.emergency_stop_active;
}

esp_err_t valve_control_driver_reset_emergency_stop(void) {
    ESP_LOGI(TAG, "Resetting emergency stop");
    
    if (!g_driver_state.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    g_driver_state.emergency_stop_active = false;
    g_driver_state.emergency_stop_timestamp = 0;
    memset(g_driver_state.emergency_stop_reason, 0, sizeof(g_driver_state.emergency_stop_reason));
    
    ESP_LOGI(TAG, "Emergency stop reset completed");
    return ESP_OK;
}

// ============================================================================
// FUNCIONES PÚBLICAS - CONFIGURACIÓN Y ESTADÍSTICAS
// ============================================================================

esp_err_t valve_control_driver_set_config(const valve_control_config_t* config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_driver_state.state_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        memcpy(&g_driver_state.config, config, sizeof(valve_control_config_t));
        xSemaphoreGive(g_driver_state.state_mutex);
        
        ESP_LOGI(TAG, "Configuration updated");
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t valve_control_driver_get_config(valve_control_config_t* config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_driver_state.state_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        memcpy(config, &g_driver_state.config, sizeof(valve_control_config_t));
        xSemaphoreGive(g_driver_state.state_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t valve_control_driver_get_statistics(valve_system_statistics_t* stats) {
    if (!stats) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_driver_state.state_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        memcpy(stats, &g_driver_state.statistics, sizeof(valve_system_statistics_t));
        xSemaphoreGive(g_driver_state.state_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t valve_control_driver_reset_statistics(void) {
    if (xSemaphoreTake(g_driver_state.state_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        memset(&g_driver_state.statistics, 0, sizeof(valve_system_statistics_t));
        xSemaphoreGive(g_driver_state.state_mutex);
        
        ESP_LOGI(TAG, "Statistics reset");
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

// ============================================================================
// FUNCIONES PÚBLICAS - CALLBACKS Y EVENTOS
// ============================================================================

esp_err_t valve_control_driver_register_callback(valve_event_callback_t callback) {
    if (!callback) {
        return ESP_ERR_INVALID_ARG;
    }
    
    g_driver_state.event_callback = callback;
    ESP_LOGI(TAG, "Event callback registered");
    return ESP_OK;
}

// ============================================================================
// FUNCIONES PÚBLICAS - UTILIDADES
// ============================================================================

const char* valve_control_driver_state_to_string(valve_state_t state) {
    switch (state) {
        case VALVE_STATE_CLOSED: return "closed";
        case VALVE_STATE_OPEN: return "open";
        case VALVE_STATE_OPENING: return "opening";
        case VALVE_STATE_CLOSING: return "closing";
        case VALVE_STATE_ERROR: return "error";
        case VALVE_STATE_MAINTENANCE: return "maintenance";
        default: return "unknown";
    }
}

const char* valve_control_driver_error_to_string(valve_error_type_t error_type) {
    switch (error_type) {
        case VALVE_ERROR_NONE: return "none";
        case VALVE_ERROR_TIMEOUT: return "timeout";
        case VALVE_ERROR_SHORT_CIRCUIT: return "short_circuit";
        case VALVE_ERROR_OPEN_CIRCUIT: return "open_circuit";
        case VALVE_ERROR_OVERVOLTAGE: return "overvoltage";
        case VALVE_ERROR_OVERCURRENT: return "overcurrent";
        case VALVE_ERROR_MECHANICAL_JAM: return "mechanical_jam";
        case VALVE_ERROR_RELAY_FAILURE: return "relay_failure";
        case VALVE_ERROR_GPIO_FAILURE: return "gpio_failure";
        default: return "unknown";
    }
}

uint32_t valve_control_driver_get_current_timestamp_ms(void) {
    return get_current_timestamp_ms();
}