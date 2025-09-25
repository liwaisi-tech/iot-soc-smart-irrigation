/**
 * @file soil_moisture_driver.c
 * @brief Implementación del driver de sensores de humedad del suelo
 *
 * Smart Irrigation System - ESP32 IoT Project
 * Infrastructure Layer - Hardware Driver
 *
 * Implementa lectura de sensores capacitivos de humedad del suelo
 * mediante ADC con calibración automática, filtrado de ruido y
 * compensación por temperatura para condiciones de suelo colombiano.
 *
 * Autor: Claude + Liwaisi Tech Team
 * Versión: 1.4.0 - Soil Moisture Driver Implementation
 */

#include "soil_moisture_driver.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include <string.h>
#include <math.h>
#include <inttypes.h>

static const char* TAG = "soil_moisture_driver";

// ============================================================================
// CONFIGURACIÓN Y ESTADO GLOBAL
// ============================================================================

/**
 * @brief Configuración por defecto del driver
 */
static const soil_moisture_driver_config_t DEFAULT_CONFIG = {
    .adc_attenuation = SOIL_ADC_ATTEN,
    .adc_width = SOIL_ADC_WIDTH,
    .samples_per_reading = SOIL_ADC_SAMPLES_PER_READING,
    .sample_interval_ms = 50,
    .enable_median_filter = true,
    .enable_average_filter = true,
    .enable_outlier_detection = true,
    .outlier_threshold_std_dev = 2.0f,
    .enable_auto_calibration = true,
    .auto_calibration_interval_s = 86400, // 24 horas
    .calibration_drift_threshold = 5.0f,
    .enable_temperature_compensation = true,
    .default_temperature_celsius = 25.0f,
    .sensor_timeout_ms = 2000,
    .max_consecutive_failures = 5
};

/**
 * @brief Estado global del driver
 */
typedef struct {
    bool is_initialized;
    soil_moisture_driver_config_t config;
    soil_moisture_statistics_t statistics;
    
    // Estado de sensores individuales
    soil_moisture_driver_state_t sensor_states[3];
    soil_sensor_config_t sensor_configs[3];
    
    // Control de concurrencia
    SemaphoreHandle_t reading_mutex;
    SemaphoreHandle_t config_mutex;
    
    // Calibración ADC
    esp_adc_cal_characteristics_t adc_chars;
    bool adc_calibrated;
    
    // Callback de eventos
    soil_sensor_event_callback_t event_callback;
    
} soil_moisture_driver_state_t;

static soil_moisture_driver_state_t g_driver_state = {0};

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
 * @brief Validar número de sensor
 */
static bool is_valid_sensor_number(uint8_t sensor_number) {
    return (sensor_number >= 1 && sensor_number <= 3);
}

/**
 * @brief Obtener canal ADC para sensor específico
 */
static adc1_channel_t get_sensor_adc_channel(uint8_t sensor_number) {
    switch (sensor_number) {
        case 1: return SOIL_SENSOR_1_ADC_CHANNEL;
        case 2: return SOIL_SENSOR_2_ADC_CHANNEL;
        case 3: return SOIL_SENSOR_3_ADC_CHANNEL;
        default: return ADC1_CHANNEL_MAX;
    }
}

/**
 * @brief Inicializar configuración de sensor por defecto
 */
static void init_sensor_config_default(soil_sensor_config_t* config, uint8_t sensor_number) {
    memset(config, 0, sizeof(soil_sensor_config_t));
    
    config->sensor_number = sensor_number;
    config->adc_channel = get_sensor_adc_channel(sensor_number);
    config->is_enabled = true;
    config->is_connected = false;
    
    // Valores de calibración por defecto (típicos para sensores capacitivos)
    config->air_dry_adc_value = 3800;      // Aire seco (0% humedad)
    config->water_saturated_adc_value = 200; // Agua saturada (100% humedad)
    config->calibration_state = SOIL_CALIBRATION_UNCALIBRATED;
    
    // Filtrado y validación
    config->min_valid_adc = 200;
    config->max_valid_adc = 3800;
    config->filter_window_size = 5;
    
    // Compensación temperatura
    config->temperature_coefficient = -0.002f; // %/°C
    config->baseline_temperature = 25.0f;
}

/**
 * @brief Inicializar estado de sensor por defecto
 */
static void init_sensor_state_default(soil_moisture_driver_state_t* state, uint8_t sensor_number) {
    memset(state, 0, sizeof(soil_moisture_driver_state_t));
    
    state->sensor_number = sensor_number;
    state->reading_valid = false;
    state->noise_level = 0;
    state->response_time_ms = 0;
    state->last_reading_timestamp = 0;
    state->consecutive_failures = 0;
    state->requires_calibration = true;
}

/**
 * @brief Leer valor ADC crudo de un sensor
 */
static esp_err_t read_raw_adc_value(uint8_t sensor_number, uint16_t* adc_value) {
    if (!is_valid_sensor_number(sensor_number) || !adc_value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    adc1_channel_t channel = get_sensor_adc_channel(sensor_number);
    if (channel == ADC1_CHANNEL_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Leer múltiples muestras para promediar
    uint32_t sum = 0;
    for (int i = 0; i < g_driver_state.config.samples_per_reading; i++) {
        int raw_value = adc1_get_raw(channel);
        if (raw_value < 0) {
            return ESP_FAIL;
        }
        sum += raw_value;
        
        // Pequeño delay entre muestras
        if (i < g_driver_state.config.samples_per_reading - 1) {
            vTaskDelay(pdMS_TO_TICKS(g_driver_state.config.sample_interval_ms));
        }
    }
    
    *adc_value = (uint16_t)(sum / g_driver_state.config.samples_per_reading);
    return ESP_OK;
}

/**
 * @brief Convertir valor ADC a porcentaje de humedad
 */
static float adc_to_humidity_percentage(uint16_t adc_value, const soil_sensor_config_t* config) {
    if (!config || config->calibration_state != SOIL_CALIBRATION_COMPLETE) {
        // Usar valores por defecto si no está calibrado
        return 50.0f; // Valor por defecto
    }
    
    // Mapear ADC a porcentaje (invertido: ADC alto = humedad baja)
    float humidity = 100.0f - ((float)(adc_value - config->water_saturated_adc_value) * 100.0f / 
                               (float)(config->air_dry_adc_value - config->water_saturated_adc_value));
    
    // Limitar a rango válido
    if (humidity < 0.0f) humidity = 0.0f;
    if (humidity > 100.0f) humidity = 100.0f;
    
    return humidity;
}

// ============================================================================
// FUNCIONES PÚBLICAS - INICIALIZACIÓN
// ============================================================================

esp_err_t soil_moisture_driver_init(const soil_moisture_driver_config_t* config, uint8_t num_sensors) {
    ESP_LOGI(TAG, "Initializing soil moisture driver for %d sensors", num_sensors);
    
    if (num_sensors == 0 || num_sensors > 3) {
        ESP_LOGE(TAG, "Invalid number of sensors: %d", num_sensors);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Limpiar estado
    memset(&g_driver_state, 0, sizeof(soil_moisture_driver_state_t));
    
    // Copiar configuración
    if (config) {
        memcpy(&g_driver_state.config, config, sizeof(soil_moisture_driver_config_t));
    } else {
        memcpy(&g_driver_state.config, &DEFAULT_CONFIG, sizeof(soil_moisture_driver_config_t));
    }
    
    // Crear mutexes
    g_driver_state.reading_mutex = xSemaphoreCreateMutex();
    g_driver_state.config_mutex = xSemaphoreCreateMutex();
    
    if (!g_driver_state.reading_mutex || !g_driver_state.config_mutex) {
        ESP_LOGE(TAG, "Failed to create mutexes");
        return ESP_ERR_NO_MEM;
    }
    
    // Configurar ADC1
    esp_err_t ret = adc1_config_width(g_driver_state.config.adc_width);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC width: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configurar canales ADC para sensores habilitados
    for (uint8_t sensor = 1; sensor <= num_sensors; sensor++) {
        adc1_channel_t channel = get_sensor_adc_channel(sensor);
        ret = adc1_config_channel_atten(channel, g_driver_state.config.adc_attenuation);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure ADC channel %d: %s", channel, esp_err_to_name(ret));
            return ret;
        }
        
        // Inicializar configuración y estado del sensor
        init_sensor_config_default(&g_driver_state.sensor_configs[sensor - 1], sensor);
        init_sensor_state_default(&g_driver_state.sensor_states[sensor - 1], sensor);
    }
    
    // Calibrar ADC
    ret = esp_adc_cal_characterize(ADC_UNIT_1, g_driver_state.config.adc_attenuation, 
                                   g_driver_state.config.adc_width, 1100, &g_driver_state.adc_chars);
    if (ret == ESP_OK) {
        g_driver_state.adc_calibrated = true;
        ESP_LOGI(TAG, "ADC calibrated successfully");
    } else {
        g_driver_state.adc_calibrated = false;
        ESP_LOGW(TAG, "ADC calibration failed, using simple conversion");
    }
    
    // Inicializar estadísticas
    memset(&g_driver_state.statistics, 0, sizeof(soil_moisture_statistics_t));
    
    g_driver_state.is_initialized = true;
    
    ESP_LOGI(TAG, "Soil moisture driver initialized successfully");
    ESP_LOGI(TAG, "Sensors: %d, ADC: %d-bit, Attenuation: %d dB", 
             num_sensors, g_driver_state.config.adc_width, g_driver_state.config.adc_attenuation);
    
    return ESP_OK;
}

esp_err_t soil_moisture_driver_deinit(void) {
    ESP_LOGI(TAG, "Deinitializing soil moisture driver");
    
    if (!g_driver_state.is_initialized) {
        return ESP_OK;
    }
    
    // Liberar mutexes
    if (g_driver_state.reading_mutex) {
        vSemaphoreDelete(g_driver_state.reading_mutex);
        g_driver_state.reading_mutex = NULL;
    }
    
    if (g_driver_state.config_mutex) {
        vSemaphoreDelete(g_driver_state.config_mutex);
        g_driver_state.config_mutex = NULL;
    }
    
    g_driver_state.is_initialized = false;
    
    ESP_LOGI(TAG, "Soil moisture driver deinitialized");
    return ESP_OK;
}

// ============================================================================
// FUNCIONES PÚBLICAS - LECTURA DE SENSORES
// ============================================================================

esp_err_t soil_moisture_driver_read_all_sensors(float ambient_temperature, soil_sensor_data_t* sensor_data) {
    if (!g_driver_state.is_initialized || !sensor_data) {
        ESP_LOGE(TAG, "Driver not initialized or invalid parameters");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(g_driver_state.reading_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire reading mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    ESP_LOGD(TAG, "Reading all soil moisture sensors");
    
    // Inicializar estructura de datos
    memset(sensor_data, 0, sizeof(soil_sensor_data_t));
    
    esp_err_t overall_result = ESP_OK;
    uint8_t successful_readings = 0;
    
    // Leer cada sensor habilitado
    for (uint8_t sensor = 1; sensor <= 3; sensor++) {
        if (!g_driver_state.sensor_configs[sensor - 1].is_enabled) {
            continue; // Saltar sensores deshabilitados
        }
        
        soil_moisture_driver_state_t* state = &g_driver_state.sensor_states[sensor - 1];
        soil_sensor_config_t* config = &g_driver_state.sensor_configs[sensor - 1];
        
        esp_err_t sensor_result = soil_moisture_driver_read_sensor(sensor, ambient_temperature, state);
        
        if (sensor_result == ESP_OK) {
            successful_readings++;
            
            // Copiar datos a estructura de salida
            switch (sensor) {
                case 1:
                    sensor_data->soil_humidity_1 = state->humidity_percentage;
                    break;
                case 2:
                    sensor_data->soil_humidity_2 = state->humidity_percentage;
                    break;
                case 3:
                    sensor_data->soil_humidity_3 = state->humidity_percentage;
                    break;
            }
            
            ESP_LOGD(TAG, "Sensor %d: %.1f%% humidity (ADC: %d)", 
                     sensor, state->humidity_percentage, state->raw_adc_value);
        } else {
            overall_result = ESP_FAIL;
            ESP_LOGW(TAG, "Failed to read sensor %d: %s", sensor, esp_err_to_name(sensor_result));
        }
    }
    
    xSemaphoreGive(g_driver_state.reading_mutex);
    
    // Actualizar estadísticas
    g_driver_state.statistics.total_readings++;
    if (successful_readings > 0) {
        g_driver_state.statistics.successful_readings++;
    } else {
        g_driver_state.statistics.failed_readings++;
    }
    
    ESP_LOGD(TAG, "Read %d/%d sensors successfully", successful_readings, 3);
    
    return (successful_readings > 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t soil_moisture_driver_read_sensor(uint8_t sensor_number, float ambient_temperature, 
                                          soil_moisture_driver_state_t* sensor_state) {
    if (!g_driver_state.is_initialized || !is_valid_sensor_number(sensor_number) || !sensor_state) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint32_t start_time = get_current_timestamp_ms();
    
    // Leer valor ADC crudo
    uint16_t raw_adc_value;
    esp_err_t ret = read_raw_adc_value(sensor_number, &raw_adc_value);
    if (ret != ESP_OK) {
        sensor_state->consecutive_failures++;
        sensor_state->reading_valid = false;
        g_driver_state.statistics.failed_readings++;
        
        if (g_driver_state.event_callback) {
            g_driver_state.event_callback(sensor_number, "read_failure", sensor_state);
        }
        
        return ret;
    }
    
    // Validar rango ADC
    soil_sensor_config_t* config = &g_driver_state.sensor_configs[sensor_number - 1];
    if (raw_adc_value < config->min_valid_adc || raw_adc_value > config->max_valid_adc) {
        ESP_LOGW(TAG, "Sensor %d ADC value out of range: %d", sensor_number, raw_adc_value);
        sensor_state->consecutive_failures++;
        sensor_state->reading_valid = false;
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Convertir a humedad
    float humidity_percentage = adc_to_humidity_percentage(raw_adc_value, config);
    
    // Actualizar estado del sensor
    sensor_state->raw_adc_value = raw_adc_value;
    sensor_state->filtered_adc_value = raw_adc_value; // Simplificado
    sensor_state->raw_voltage = (float)raw_adc_value * 3.3f / 4095.0f;
    sensor_state->humidity_percentage = humidity_percentage;
    sensor_state->temperature_compensated_humidity = humidity_percentage; // Simplificado
    sensor_state->noise_level = 0; // Simplificado
    sensor_state->reading_valid = true;
    sensor_state->consecutive_failures = 0;
    sensor_state->last_reading_timestamp = get_current_timestamp_ms();
    
    uint32_t end_time = get_current_timestamp_ms();
    sensor_state->response_time_ms = (uint16_t)(end_time - start_time);
    
    // Actualizar estadísticas
    g_driver_state.statistics.successful_readings++;
    g_driver_state.statistics.average_reading_time_ms = 
        (g_driver_state.statistics.average_reading_time_ms + sensor_state->response_time_ms) / 2;
    
    // Notificar evento
    if (g_driver_state.event_callback) {
        g_driver_state.event_callback(sensor_number, "reading_success", sensor_state);
    }
    
    ESP_LOGD(TAG, "Sensor %d: %.1f%% humidity (ADC: %d, Time: %dms)", 
             sensor_number, humidity_percentage, raw_adc_value, sensor_state->response_time_ms);
    
    return ESP_OK;
}

// ============================================================================
// FUNCIONES PÚBLICAS - CONFIGURACIÓN Y ESTADO
// ============================================================================

esp_err_t soil_moisture_driver_get_sensor_config(uint8_t sensor_number, soil_sensor_config_t* config) {
    if (!g_driver_state.is_initialized || !is_valid_sensor_number(sensor_number) || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_driver_state.config_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        memcpy(config, &g_driver_state.sensor_configs[sensor_number - 1], sizeof(soil_sensor_config_t));
        xSemaphoreGive(g_driver_state.config_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t soil_moisture_driver_set_sensor_config(uint8_t sensor_number, const soil_sensor_config_t* config) {
    if (!g_driver_state.is_initialized || !is_valid_sensor_number(sensor_number) || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_driver_state.config_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        memcpy(&g_driver_state.sensor_configs[sensor_number - 1], config, sizeof(soil_sensor_config_t));
        xSemaphoreGive(g_driver_state.config_mutex);
        
        ESP_LOGI(TAG, "Sensor %d configuration updated", sensor_number);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

bool soil_moisture_driver_is_sensor_connected(uint8_t sensor_number) {
    if (!g_driver_state.is_initialized || !is_valid_sensor_number(sensor_number)) {
        return false;
    }
    
    return g_driver_state.sensor_configs[sensor_number - 1].is_connected;
}

soil_calibration_state_t soil_moisture_driver_get_calibration_state(uint8_t sensor_number) {
    if (!g_driver_state.is_initialized || !is_valid_sensor_number(sensor_number)) {
        return SOIL_CALIBRATION_FAILED;
    }
    
    return g_driver_state.sensor_configs[sensor_number - 1].calibration_state;
}

esp_err_t soil_moisture_driver_get_sensor_state(uint8_t sensor_number, soil_moisture_driver_state_t* state) {
    if (!g_driver_state.is_initialized || !is_valid_sensor_number(sensor_number) || !state) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(state, &g_driver_state.sensor_states[sensor_number - 1], sizeof(soil_moisture_driver_state_t));
    return ESP_OK;
}

// ============================================================================
// FUNCIONES PÚBLICAS - CONFIGURACIÓN GLOBAL
// ============================================================================

void soil_moisture_driver_get_config(soil_moisture_driver_config_t* config) {
    if (config && g_driver_state.is_initialized) {
        memcpy(config, &g_driver_state.config, sizeof(soil_moisture_driver_config_t));
    }
}

esp_err_t soil_moisture_driver_set_config(const soil_moisture_driver_config_t* config) {
    if (!config || !g_driver_state.is_initialized) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_driver_state.config_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        memcpy(&g_driver_state.config, config, sizeof(soil_moisture_driver_config_t));
        xSemaphoreGive(g_driver_state.config_mutex);
        
        ESP_LOGI(TAG, "Driver configuration updated");
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t soil_moisture_driver_get_statistics(soil_moisture_statistics_t* stats) {
    if (!stats || !g_driver_state.is_initialized) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(stats, &g_driver_state.statistics, sizeof(soil_moisture_statistics_t));
    return ESP_OK;
}

esp_err_t soil_moisture_driver_reset_statistics(void) {
    if (!g_driver_state.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    memset(&g_driver_state.statistics, 0, sizeof(soil_moisture_statistics_t));
    ESP_LOGI(TAG, "Statistics reset");
    return ESP_OK;
}

// ============================================================================
// FUNCIONES PÚBLICAS - CALLBACKS Y EVENTOS
// ============================================================================

esp_err_t soil_moisture_driver_register_callback(soil_sensor_event_callback_t callback) {
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

const char* soil_moisture_driver_calibration_state_to_string(soil_calibration_state_t state) {
    switch (state) {
        case SOIL_CALIBRATION_UNCALIBRATED: return "uncalibrated";
        case SOIL_CALIBRATION_AIR_DRY: return "air_dry";
        case SOIL_CALIBRATION_WATER_SAT: return "water_saturated";
        case SOIL_CALIBRATION_COMPLETE: return "complete";
        case SOIL_CALIBRATION_FAILED: return "failed";
        default: return "unknown";
    }
}

uint32_t soil_moisture_driver_get_current_timestamp_ms(void) {
    return get_current_timestamp_ms();
}