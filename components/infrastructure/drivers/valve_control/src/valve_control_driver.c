/**
 * @file valve_control_driver.c
 * @brief Implementación del driver de control de válvulas
 *
 * Smart Irrigation System - ESP32 IoT Project
 * Hexagonal Architecture - Infrastructure Layer Driver
 *
 * Sistema simplificado para 1 válvula principal con protecciones
 * de seguridad críticas para funcionamiento en campo rural.
 *
 * Autor: Claude + Liwaisi Tech Team
 * Versión: 1.0.0 - Sistema Simplificado
 */

#include "valve_control_driver.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <string.h>
#include <math.h>
#include <inttypes.h>

static const char* TAG = "valve_control_driver";

// ============================================================================
// CONFIGURACIÓN Y ESTADO GLOBAL
// ============================================================================

/**
 * @brief Configuración por defecto para sistema simplificado (1 válvula)
 */
static const valve_control_config_t DEFAULT_CONFIG = {
    // Configuración temporal
    .valve_response_timeout_ms = 3000,      // 3s timeout (reducido para batería)
    .relay_settling_time_ms = 100,          // 100ms asentamiento relé
    .interlock_delay_ms = 200,              // 200ms entre operaciones

    // Configuración de seguridad (CRÍTICA para campo rural)
    .max_operation_duration_ms = 1800000,   // 30 min máximo (1800000ms)
    .enable_interlock_protection = true,    // Protección interlock ON
    .enable_timeout_protection = true,      // Protección timeout ON

    // Configuración de monitoreo
    .enable_state_monitoring = true,        // Monitoreo estado ON
    .monitoring_interval_ms = 2000,         // 2s intervalo (optimizado batería)

    // Configuración de energía (CRÍTICA para batería)
    .enable_power_optimization = true,      // Optimización energía ON
    .relay_hold_reduction_delay_ms = 3000,  // 3s antes reducir corriente hold
};

/**
 * @brief Estado global del driver
 */
static struct {
    bool driver_initialized;
    valve_control_config_t config;
    valve_status_t valve_status;            // Solo 1 válvula (sistema simplificado)
    valve_system_statistics_t statistics;
    TimerHandle_t safety_timer;             // Timer para safety timeout
    TimerHandle_t monitoring_timer;         // Timer para monitoreo periódico
} g_valve_driver = {
    .driver_initialized = false,
};

// ============================================================================
// FUNCIONES AUXILIARES PRIVADAS
// ============================================================================

/**
 * @brief Obtener pin GPIO para válvula específica
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
    // Sistema simplificado: solo válvula 1 activa
    return (valve_number == 1);
}

/**
 * @brief Configurar GPIO para válvula
 */
static esp_err_t configure_valve_gpio(uint8_t valve_number) {
    if (!is_valid_valve_number(valve_number)) {
        ESP_LOGE(TAG, "Invalid valve number for simplified system: %d", valve_number);
        return ESP_ERR_INVALID_ARG;
    }

    gpio_num_t gpio_pin = get_valve_gpio_pin(valve_number);

    gpio_config_t gpio_conf = {
        .pin_bit_mask = (1ULL << gpio_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,  // Pull-down para estado seguro
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t ret = gpio_config(&gpio_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d for valve %d: %s",
                 gpio_pin, valve_number, esp_err_to_name(ret));
        return ret;
    }

    // Establecer estado inicial cerrado (LOW = cerrado, HIGH = abierto)
    gpio_set_level(gpio_pin, 0);

    ESP_LOGI(TAG, "Valve %d configured on GPIO %d", valve_number, gpio_pin);
    return ESP_OK;
}

/**
 * @brief Inicializar estado de válvula
 */
static void init_valve_status(uint8_t valve_number) {
    valve_status_t* status = &g_valve_driver.valve_status;

    memset(status, 0, sizeof(valve_status_t));

    status->valve_number = valve_number;
    status->gpio_pin = get_valve_gpio_pin(valve_number);
    status->current_state = VALVE_STATE_CLOSED;
    status->target_state = VALVE_STATE_CLOSED;
    status->last_error = VALVE_ERROR_NONE;
    status->state_change_timestamp = esp_timer_get_time() / 1000; // ms
    status->is_initialized = true;
    status->is_relay_energized = false;

    ESP_LOGI(TAG, "Valve %d status initialized", valve_number);
}

/**
 * @brief Callback del timer de safety timeout
 */
static void safety_timer_callback(TimerHandle_t xTimer) {
    ESP_LOGW(TAG, "Safety timeout triggered! Closing valve for protection");

    // Cerrar válvula inmediatamente por seguridad
    valve_control_driver_close_valve(1);

    // Actualizar estadísticas
    g_valve_driver.statistics.safety_stops++;
    g_valve_driver.statistics.timeout_errors++;

    // Registrar error en estado de válvula
    g_valve_driver.valve_status.last_error = VALVE_ERROR_TIMEOUT;
    g_valve_driver.valve_status.error_count++;

    ESP_LOGE(TAG, "Valve 1 closed by safety timeout after %.1f minutes",
             g_valve_driver.config.max_operation_duration_ms / 60000.0);
}

/**
 * @brief Callback del timer de monitoreo
 */
static void monitoring_timer_callback(TimerHandle_t xTimer) {
    valve_status_t* status = &g_valve_driver.valve_status;

    // Verificar consistencia de estado GPIO vs lógico
    bool physical_state = gpio_get_level(status->gpio_pin);
    bool expected_state = (status->current_state == VALVE_STATE_OPEN);

    if (physical_state != expected_state) {
        ESP_LOGW(TAG, "State mismatch detected! Physical: %d, Expected: %d",
                 physical_state, expected_state);

        // Corregir estado lógico basado en estado físico
        status->current_state = physical_state ? VALVE_STATE_OPEN : VALVE_STATE_CLOSED;
        status->last_error = VALVE_ERROR_RELAY_FAILURE;
        status->error_count++;
        g_valve_driver.statistics.hardware_errors++;
    }

    // Actualizar tiempo total abierto si está en operación
    if (status->current_state == VALVE_STATE_OPEN && status->current_session_start_time > 0) {
        uint32_t current_time = esp_timer_get_time() / 1000; // ms
        uint32_t session_duration = current_time - status->current_session_start_time;

        // Log de información cada 5 minutos
        if (session_duration % 300000 == 0) { // 300000ms = 5 min
            ESP_LOGI(TAG, "Valve 1 has been open for %.1f minutes", session_duration / 60000.0);
        }
    }

    ESP_LOGD(TAG, "Monitoring: Valve 1 state=%s, GPIO=%d, errors=%d",
             valve_control_driver_state_to_string(status->current_state),
             gpio_get_level(status->gpio_pin), status->error_count);
}

/**
 * @brief Actualizar estadísticas de operación
 */
static void update_operation_statistics(uint8_t valve_number, bool operation_successful,
                                      uint16_t response_time_ms, bool is_open_operation) {
    valve_system_statistics_t* stats = &g_valve_driver.statistics;
    valve_status_t* status = &g_valve_driver.valve_status;

    // Actualizar contadores globales
    if (is_open_operation) {
        stats->total_open_commands++;
        if (operation_successful) {
            status->open_operations_count++;
        }
    } else {
        stats->total_close_commands++;
        if (operation_successful) {
            status->close_operations_count++;
        }
    }

    if (operation_successful) {
        stats->successful_operations++;
        status->successful_operations++;
    } else {
        stats->failed_operations++;
        status->error_count++;
    }

    // Actualizar tiempo promedio de respuesta
    if (response_time_ms > 0) {
        float total_operations = stats->successful_operations + stats->failed_operations;
        stats->average_response_time_ms =
            (stats->average_response_time_ms * (total_operations - 1) + response_time_ms) / total_operations;

        status->response_time_ms = response_time_ms;
    }

    ESP_LOGD(TAG, "Stats updated: successful=%d, failed=%d, avg_time=%.1fms",
             stats->successful_operations, stats->failed_operations,
             stats->average_response_time_ms);
}

// ============================================================================
// FUNCIONES PÚBLICAS - INICIALIZACIÓN
// ============================================================================

esp_err_t valve_control_driver_init(const valve_control_config_t* config) {
    if (g_valve_driver.driver_initialized) {
        ESP_LOGW(TAG, "Driver already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing valve control driver (simplified system - 1 valve)");

    // Usar configuración proporcionada o por defecto
    if (config) {
        memcpy(&g_valve_driver.config, config, sizeof(valve_control_config_t));
    } else {
        memcpy(&g_valve_driver.config, &DEFAULT_CONFIG, sizeof(valve_control_config_t));
        ESP_LOGI(TAG, "Using default configuration");
    }

    // Configurar GPIO para válvula 1 (sistema simplificado)
    esp_err_t ret = configure_valve_gpio(1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure valve GPIO");
        return ret;
    }

    // Inicializar estado de válvula
    init_valve_status(1);

    // Inicializar estadísticas
    memset(&g_valve_driver.statistics, 0, sizeof(valve_system_statistics_t));

    // Crear timer de safety timeout
    g_valve_driver.safety_timer = xTimerCreate(
        "valve_safety_timer",
        pdMS_TO_TICKS(g_valve_driver.config.max_operation_duration_ms),
        pdFALSE,  // One-shot timer
        NULL,
        safety_timer_callback
    );

    if (!g_valve_driver.safety_timer) {
        ESP_LOGE(TAG, "Failed to create safety timer");
        return ESP_ERR_NO_MEM;
    }

    // Crear timer de monitoreo periódico
    if (g_valve_driver.config.enable_state_monitoring) {
        g_valve_driver.monitoring_timer = xTimerCreate(
            "valve_monitoring_timer",
            pdMS_TO_TICKS(g_valve_driver.config.monitoring_interval_ms),
            pdTRUE,  // Periodic timer
            NULL,
            monitoring_timer_callback
        );

        if (!g_valve_driver.monitoring_timer) {
            ESP_LOGE(TAG, "Failed to create monitoring timer");
            return ESP_ERR_NO_MEM;
        }

        // Iniciar timer de monitoreo
        xTimerStart(g_valve_driver.monitoring_timer, 0);
        ESP_LOGI(TAG, "Monitoring timer started with %dms interval",
                 g_valve_driver.config.monitoring_interval_ms);
    }

    g_valve_driver.driver_initialized = true;
    ESP_LOGI(TAG, "Valve control driver initialized successfully");

    return ESP_OK;
}

esp_err_t valve_control_driver_deinit(void) {
    if (!g_valve_driver.driver_initialized) {
        return ESP_OK; // Ya desinicializado
    }

    ESP_LOGI(TAG, "Deinitializing valve control driver");

    // Cerrar todas las válvulas antes de desinicializar
    valve_control_driver_close_all_valves();

    // Detener y eliminar timers
    if (g_valve_driver.safety_timer) {
        xTimerStop(g_valve_driver.safety_timer, portMAX_DELAY);
        xTimerDelete(g_valve_driver.safety_timer, portMAX_DELAY);
        g_valve_driver.safety_timer = NULL;
    }

    if (g_valve_driver.monitoring_timer) {
        xTimerStop(g_valve_driver.monitoring_timer, portMAX_DELAY);
        xTimerDelete(g_valve_driver.monitoring_timer, portMAX_DELAY);
        g_valve_driver.monitoring_timer = NULL;
    }

    // Resetear GPIO a estado seguro
    gpio_set_level(VALVE_1_GPIO_PIN, 0);  // Asegurar válvula cerrada

    g_valve_driver.driver_initialized = false;
    ESP_LOGI(TAG, "Valve control driver deinitialized");

    return ESP_OK;
}

// ============================================================================
// FUNCIONES PÚBLICAS - CONTROL DE VÁLVULAS
// ============================================================================

esp_err_t valve_control_driver_open_valve(uint8_t valve_number) {
    if (!g_valve_driver.driver_initialized) {
        ESP_LOGE(TAG, "Driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!is_valid_valve_number(valve_number)) {
        ESP_LOGE(TAG, "Invalid valve number for simplified system: %d", valve_number);
        return ESP_ERR_INVALID_ARG;
    }

    valve_status_t* status = &g_valve_driver.valve_status;

    // Verificar si ya está abierta
    if (status->current_state == VALVE_STATE_OPEN) {
        ESP_LOGW(TAG, "Valve %d already open", valve_number);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Opening valve %d", valve_number);

    uint32_t operation_start = esp_timer_get_time() / 1000; // ms

    // Actualizar estado a "abriendo"
    status->current_state = VALVE_STATE_OPENING;
    status->target_state = VALVE_STATE_OPEN;
    status->state_change_timestamp = operation_start;

    // Activar GPIO (HIGH = válvula abierta)
    esp_err_t ret = gpio_set_level(status->gpio_pin, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO for valve %d", valve_number);
        status->current_state = VALVE_STATE_ERROR;
        status->last_error = VALVE_ERROR_GPIO_FAILURE;
        update_operation_statistics(valve_number, false, 0, true);
        return ret;
    }

    // Esperar tiempo de asentamiento del relé
    vTaskDelay(pdMS_TO_TICKS(g_valve_driver.config.relay_settling_time_ms));

    // Verificar que el GPIO se activó correctamente
    if (gpio_get_level(status->gpio_pin) != 1) {
        ESP_LOGE(TAG, "Valve %d failed to open - GPIO verification failed", valve_number);
        status->current_state = VALVE_STATE_ERROR;
        status->last_error = VALVE_ERROR_RELAY_FAILURE;
        update_operation_statistics(valve_number, false, 0, true);
        return ESP_FAIL;
    }

    // Válvula abierta exitosamente
    status->current_state = VALVE_STATE_OPEN;
    status->is_relay_energized = true;
    status->current_session_start_time = operation_start;
    status->last_error = VALVE_ERROR_NONE;

    // Calcular tiempo de respuesta
    uint32_t operation_end = esp_timer_get_time() / 1000; // ms
    uint16_t response_time = (uint16_t)(operation_end - operation_start);

    // Actualizar estadísticas
    update_operation_statistics(valve_number, true, response_time, true);

    // Iniciar timer de safety timeout si está habilitado
    if (g_valve_driver.config.enable_timeout_protection && g_valve_driver.safety_timer) {
        xTimerStart(g_valve_driver.safety_timer, 0);
        ESP_LOGI(TAG, "Safety timer started for %.1f minutes maximum operation",
                 g_valve_driver.config.max_operation_duration_ms / 60000.0);
    }

    ESP_LOGI(TAG, "Valve %d opened successfully in %dms", valve_number, response_time);

    return ESP_OK;
}

esp_err_t valve_control_driver_close_valve(uint8_t valve_number) {
    if (!g_valve_driver.driver_initialized) {
        ESP_LOGE(TAG, "Driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!is_valid_valve_number(valve_number)) {
        ESP_LOGE(TAG, "Invalid valve number for simplified system: %d", valve_number);
        return ESP_ERR_INVALID_ARG;
    }

    valve_status_t* status = &g_valve_driver.valve_status;

    // Verificar si ya está cerrada
    if (status->current_state == VALVE_STATE_CLOSED) {
        ESP_LOGW(TAG, "Valve %d already closed", valve_number);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Closing valve %d", valve_number);

    uint32_t operation_start = esp_timer_get_time() / 1000; // ms

    // Actualizar estado a "cerrando"
    status->current_state = VALVE_STATE_CLOSING;
    status->target_state = VALVE_STATE_CLOSED;

    // Detener timer de safety si está activo
    if (g_valve_driver.safety_timer && xTimerIsTimerActive(g_valve_driver.safety_timer)) {
        xTimerStop(g_valve_driver.safety_timer, 0);
        ESP_LOGI(TAG, "Safety timer stopped");
    }

    // Calcular tiempo total de la sesión si estuvo abierta
    if (status->current_session_start_time > 0) {
        uint32_t session_duration = operation_start - status->current_session_start_time;
        status->total_open_time_ms += session_duration;
        g_valve_driver.statistics.total_irrigation_time_ms += session_duration;
        status->current_session_start_time = 0;

        ESP_LOGI(TAG, "Session duration: %.1f minutes, Total irrigation: %.1f minutes",
                 session_duration / 60000.0, status->total_open_time_ms / 60000.0);
    }

    // Desactivar GPIO (LOW = válvula cerrada)
    esp_err_t ret = gpio_set_level(status->gpio_pin, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear GPIO for valve %d", valve_number);
        status->current_state = VALVE_STATE_ERROR;
        status->last_error = VALVE_ERROR_GPIO_FAILURE;
        update_operation_statistics(valve_number, false, 0, false);
        return ret;
    }

    // Esperar tiempo de asentamiento del relé
    vTaskDelay(pdMS_TO_TICKS(g_valve_driver.config.relay_settling_time_ms));

    // Verificar que el GPIO se desactivó correctamente
    if (gpio_get_level(status->gpio_pin) != 0) {
        ESP_LOGE(TAG, "Valve %d failed to close - GPIO verification failed", valve_number);
        status->current_state = VALVE_STATE_ERROR;
        status->last_error = VALVE_ERROR_RELAY_FAILURE;
        update_operation_statistics(valve_number, false, 0, false);
        return ESP_FAIL;
    }

    // Válvula cerrada exitosamente
    status->current_state = VALVE_STATE_CLOSED;
    status->is_relay_energized = false;
    status->state_change_timestamp = operation_start;
    status->last_error = VALVE_ERROR_NONE;

    // Calcular tiempo de respuesta
    uint32_t operation_end = esp_timer_get_time() / 1000; // ms
    uint16_t response_time = (uint16_t)(operation_end - operation_start);

    // Actualizar estadísticas
    update_operation_statistics(valve_number, true, response_time, false);

    ESP_LOGI(TAG, "Valve %d closed successfully in %dms", valve_number, response_time);

    return ESP_OK;
}

esp_err_t valve_control_driver_close_all_valves(void) {
    if (!g_valve_driver.driver_initialized) {
        ESP_LOGE(TAG, "Driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Closing all valves (emergency stop)");

    // En sistema simplificado, solo cerrar válvula 1
    esp_err_t ret = valve_control_driver_close_valve(1);

    if (ret == ESP_OK) {
        g_valve_driver.statistics.safety_stops++;
        ESP_LOGI(TAG, "All valves closed successfully (emergency stop completed)");
    } else {
        ESP_LOGE(TAG, "Failed to close valve 1 during emergency stop");
    }

    return ret;
}

// ============================================================================
// FUNCIONES PÚBLICAS - CONSULTA DE ESTADO
// ============================================================================

valve_state_t valve_control_driver_get_valve_state(uint8_t valve_number) {
    if (!g_valve_driver.driver_initialized || !is_valid_valve_number(valve_number)) {
        return VALVE_STATE_UNKNOWN;
    }

    return g_valve_driver.valve_status.current_state;
}

bool valve_control_driver_is_valve_open(uint8_t valve_number) {
    return valve_control_driver_get_valve_state(valve_number) == VALVE_STATE_OPEN;
}

bool valve_control_driver_is_valve_closed(uint8_t valve_number) {
    return valve_control_driver_get_valve_state(valve_number) == VALVE_STATE_CLOSED;
}

bool valve_control_driver_are_all_valves_closed(void) {
    // Sistema simplificado: verificar solo válvula 1
    return valve_control_driver_is_valve_closed(1);
}

bool valve_control_driver_has_any_valve_open(void) {
    // Sistema simplificado: verificar solo válvula 1
    return valve_control_driver_is_valve_open(1);
}

esp_err_t valve_control_driver_get_valve_status(uint8_t valve_number, valve_status_t* status) {
    if (!g_valve_driver.driver_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!is_valid_valve_number(valve_number) || !status) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(status, &g_valve_driver.valve_status, sizeof(valve_status_t));
    return ESP_OK;
}

uint32_t valve_control_driver_get_valve_open_duration(uint8_t valve_number) {
    if (!g_valve_driver.driver_initialized || !is_valid_valve_number(valve_number)) {
        return 0;
    }

    valve_status_t* status = &g_valve_driver.valve_status;

    if (status->current_state != VALVE_STATE_OPEN || status->current_session_start_time == 0) {
        return 0;
    }

    uint32_t current_time = esp_timer_get_time() / 1000; // ms
    return current_time - status->current_session_start_time;
}

// ============================================================================
// FUNCIONES PÚBLICAS - CONFIGURACIÓN Y ESTADÍSTICAS
// ============================================================================

esp_err_t valve_control_driver_set_config(const valve_control_config_t* config) {
    if (!g_valve_driver.driver_initialized || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&g_valve_driver.config, config, sizeof(valve_control_config_t));
    ESP_LOGI(TAG, "Configuration updated");
    return ESP_OK;
}

esp_err_t valve_control_driver_get_config(valve_control_config_t* config) {
    if (!g_valve_driver.driver_initialized || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(config, &g_valve_driver.config, sizeof(valve_control_config_t));
    return ESP_OK;
}

esp_err_t valve_control_driver_get_statistics(valve_system_statistics_t* stats) {
    if (!g_valve_driver.driver_initialized || !stats) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(stats, &g_valve_driver.statistics, sizeof(valve_system_statistics_t));
    return ESP_OK;
}

esp_err_t valve_control_driver_reset_statistics(void) {
    if (!g_valve_driver.driver_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&g_valve_driver.statistics, 0, sizeof(valve_system_statistics_t));
    ESP_LOGI(TAG, "Statistics reset");
    return ESP_OK;
}

bool valve_control_driver_is_initialized(void) {
    return g_valve_driver.driver_initialized;
}

uint8_t valve_control_driver_get_active_valve_count(void) {
    // Sistema simplificado: máximo 1 válvula activa
    return valve_control_driver_has_any_valve_open() ? 1 : 0;
}

// ============================================================================
// FUNCIONES PÚBLICAS - UTILIDADES Y DEBUG
// ============================================================================

const char* valve_control_driver_state_to_string(valve_state_t state) {
    switch (state) {
        case VALVE_STATE_CLOSED: return "CLOSED";
        case VALVE_STATE_OPENING: return "OPENING";
        case VALVE_STATE_OPEN: return "OPEN";
        case VALVE_STATE_CLOSING: return "CLOSING";
        case VALVE_STATE_ERROR: return "ERROR";
        case VALVE_STATE_UNKNOWN: return "UNKNOWN";
        default: return "INVALID";
    }
}

const char* valve_control_driver_error_to_string(valve_error_type_t error) {
    switch (error) {
        case VALVE_ERROR_NONE: return "NONE";
        case VALVE_ERROR_TIMEOUT: return "TIMEOUT";
        case VALVE_ERROR_SHORT_CIRCUIT: return "SHORT_CIRCUIT";
        case VALVE_ERROR_OPEN_CIRCUIT: return "OPEN_CIRCUIT";
        case VALVE_ERROR_OVERVOLTAGE: return "OVERVOLTAGE";
        case VALVE_ERROR_OVERCURRENT: return "OVERCURRENT";
        case VALVE_ERROR_MECHANICAL_JAM: return "MECHANICAL_JAM";
        case VALVE_ERROR_RELAY_FAILURE: return "RELAY_FAILURE";
        case VALVE_ERROR_GPIO_FAILURE: return "GPIO_FAILURE";
        default: return "UNKNOWN";
    }
}

void valve_control_driver_print_status(uint8_t valve_number) {
    if (!g_valve_driver.driver_initialized) {
        ESP_LOGW(TAG, "Driver not initialized");
        return;
    }

    if (!is_valid_valve_number(valve_number)) {
        ESP_LOGW(TAG, "Invalid valve number for status: %d", valve_number);
        return;
    }

    valve_status_t* status = &g_valve_driver.valve_status;

    ESP_LOGI(TAG, "=== VALVE %d STATUS ===", valve_number);
    ESP_LOGI(TAG, "GPIO Pin: %d", status->gpio_pin);
    ESP_LOGI(TAG, "Current State: %s", valve_control_driver_state_to_string(status->current_state));
    ESP_LOGI(TAG, "Target State: %s", valve_control_driver_state_to_string(status->target_state));
    ESP_LOGI(TAG, "Last Error: %s", valve_control_driver_error_to_string(status->last_error));
    ESP_LOGI(TAG, "Relay Energized: %s", status->is_relay_energized ? "YES" : "NO");
    ESP_LOGI(TAG, "Total Open Time: %.1f minutes", status->total_open_time_ms / 60000.0);
    ESP_LOGI(TAG, "Open Operations: %d", status->open_operations_count);
    ESP_LOGI(TAG, "Close Operations: %d", status->close_operations_count);
    ESP_LOGI(TAG, "Error Count: %" PRIu32, status->error_count);
    ESP_LOGI(TAG, "Response Time: %dms", status->response_time_ms);

    if (status->current_state == VALVE_STATE_OPEN && status->current_session_start_time > 0) {
        uint32_t session_duration = valve_control_driver_get_valve_open_duration(valve_number);
        ESP_LOGI(TAG, "Current Session: %.1f minutes", session_duration / 60000.0);
    }

    ESP_LOGI(TAG, "======================");
}

void valve_control_driver_print_system_statistics(void) {
    if (!g_valve_driver.driver_initialized) {
        ESP_LOGW(TAG, "Driver not initialized");
        return;
    }

    valve_system_statistics_t* stats = &g_valve_driver.statistics;

    ESP_LOGI(TAG, "=== SYSTEM STATISTICS ===");
    ESP_LOGI(TAG, "Total Open Commands: %" PRIu32, stats->total_open_commands);
    ESP_LOGI(TAG, "Total Close Commands: %" PRIu32, stats->total_close_commands);
    ESP_LOGI(TAG, "Successful Operations: %" PRIu32, stats->successful_operations);
    ESP_LOGI(TAG, "Failed Operations: %" PRIu32, stats->failed_operations);

    if (stats->total_open_commands + stats->total_close_commands > 0) {
        float success_rate = (float)stats->successful_operations /
                           (stats->successful_operations + stats->failed_operations) * 100.0;
        ESP_LOGI(TAG, "Success Rate: %.1f%%", success_rate);
    }

    ESP_LOGI(TAG, "Timeout Errors: %" PRIu32, stats->timeout_errors);
    ESP_LOGI(TAG, "Hardware Errors: %" PRIu32, stats->hardware_errors);
    ESP_LOGI(TAG, "Safety Stops: %" PRIu32, stats->safety_stops);
    ESP_LOGI(TAG, "Average Response Time: %.1fms", stats->average_response_time_ms);
    ESP_LOGI(TAG, "Total Irrigation Time: %.1f hours", stats->total_irrigation_time_ms / 3600000.0);
    ESP_LOGI(TAG, "==========================");
}

uint32_t valve_control_driver_get_current_timestamp_ms(void) {
    return esp_timer_get_time() / 1000; // Convertir microsegundos a milisegundos
}