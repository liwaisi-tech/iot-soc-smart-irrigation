/**
 * @file read_soil_sensors.c
 * @brief Implementación del use case de lectura de sensores de suelo
 *
 * Smart Irrigation System - ESP32 IoT Project
 * Hexagonal Architecture - Application Layer Use Case
 *
 * Integra el soil_moisture_driver con la aplicación, proporcionando
 * una interfaz de alto nivel para lectura de sensores con validaciones
 * y procesamiento específico de la aplicación.
 *
 * Autor: Claude + Liwaisi Tech Team
 * Versión: 1.0.0 - Application Integration
 */

#include "read_soil_sensors.h"
#include "soil_moisture_driver.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>

static const char* TAG = "read_soil_sensors";

// ============================================================================
// CONFIGURACIÓN Y ESTADO GLOBAL
// ============================================================================

/**
 * @brief Configuración por defecto para lectura de sensores
 */
static const soil_sensors_config_t DEFAULT_CONFIG = {
    // Configuración básica
    .num_sensors_enabled = 3,                // 3 sensores por defecto
    .reading_interval_ms = 1000,             // 1s entre lecturas
    .readings_per_average = 5,               // 5 lecturas para promedio
    .sensor_warmup_time_ms = 500,            // 500ms calentamiento
    .adc_stabilization_time_ms = 100,        // 100ms estabilización ADC

    // Configuración de filtrado
    .enable_median_filter = true,            // Filtro mediana ON
    .enable_outlier_detection = true,        // Detección outliers ON
    .outlier_threshold_percentage = 15.0,    // 15% threshold outliers

    // Configuración de validación
    .min_valid_adc_value = 100,              // ADC mínimo válido
    .max_valid_adc_value = 4000,             // ADC máximo válido
    .min_valid_humidity_percent = 0.0,       // 0% humedad mínima
    .max_valid_humidity_percent = 100.0,     // 100% humedad máxima

    // Configuración de errores
    .max_consecutive_failures = 5,           // 5 fallos consecutivos max
    .sensor_timeout_ms = 2000,               // 2s timeout sensor
};

/**
 * @brief Estado global del use case
 */
static struct {
    bool use_case_initialized;
    safety_limits_t safety_limits;
    soil_sensors_config_t config;
    soil_sensor_calibration_t calibrations[3];  // Calibraciones por sensor
    soil_sensors_statistics_t statistics;
    uint8_t num_sensors_configured;
} g_soil_sensors_context = {
    .use_case_initialized = false,
};

// ============================================================================
// FUNCIONES AUXILIARES PRIVADAS
// ============================================================================

/**
 * @brief Validar número de sensor
 */
static bool is_valid_sensor_number(uint8_t sensor_number) {
    return (sensor_number >= 1 && sensor_number <= g_soil_sensors_context.num_sensors_configured);
}

/**
 * @brief Validar valor ADC dentro de rango esperado
 */
static bool is_valid_adc_value(uint16_t adc_value) {
    return (adc_value >= g_soil_sensors_context.config.min_valid_adc_value &&
            adc_value <= g_soil_sensors_context.config.max_valid_adc_value);
}

/**
 * @brief Validar porcentaje de humedad
 */
static bool is_valid_humidity_percentage(float humidity_percent) {
    return (humidity_percent >= g_soil_sensors_context.config.min_valid_humidity_percent &&
            humidity_percent <= g_soil_sensors_context.config.max_valid_humidity_percent &&
            !isnan(humidity_percent) && !isinf(humidity_percent));
}

/**
 * @brief Inicializar calibración por defecto para un sensor
 */
static void init_default_calibration(uint8_t sensor_number) {
    if (sensor_number < 1 || sensor_number > 3) return;

    soil_sensor_calibration_t* calib = &g_soil_sensors_context.calibrations[sensor_number - 1];

    // Valores por defecto del repositorio Liwaisi
    calib->air_dry_adc_value = 2865;              // ADC en aire seco (0%)
    calib->water_saturated_adc_value = 1220;      // ADC saturado agua (100%)
    calib->voltage_air_dry = 2.3;                 // Aproximadamente 2.3V
    calib->voltage_water_saturated = 1.0;         // Aproximadamente 1.0V
    calib->auto_calibration_enabled = false;      // Calibración manual por defecto
    calib->last_calibration_timestamp = 0;

    ESP_LOGI(TAG, "Sensor %d: Default calibration initialized (dry=%d, wet=%d)",
             sensor_number, calib->air_dry_adc_value, calib->water_saturated_adc_value);
}

/**
 * @brief Detectar si sensor está desconectado basado en lectura
 */
static bool detect_sensor_disconnection(const soil_sensor_state_t* sensor_state) {
    // Detección basada en valores ADC extremos
    if (sensor_state->raw_adc_value <= 50 || sensor_state->raw_adc_value >= 4090) {
        return true;  // Valores extremos indican desconexión
    }

    // Detección basada en fallos consecutivos
    if (sensor_state->consecutive_failures >= g_soil_sensors_context.config.max_consecutive_failures) {
        return true;
    }

    // Detección basada en voltaje fuera de rango esperado
    if (sensor_state->raw_voltage <= 0.1 || sensor_state->raw_voltage >= 3.2) {
        return true;
    }

    return false;
}

/**
 * @brief Convertir estado de driver a estado de aplicación
 */
static void convert_driver_state_to_application_state(uint8_t sensor_number,
                                                     float humidity_percent,
                                                     float temperature_celsius,
                                                     soil_sensor_state_t* app_state) {
    // Obtener estado del driver de infraestructura
    soil_moisture_sensor_state_t driver_state;
    esp_err_t ret = soil_moisture_driver_get_sensor_state(sensor_number, &driver_state);

    // Inicializar estado de aplicación
    memset(app_state, 0, sizeof(soil_sensor_state_t));

    app_state->sensor_number = sensor_number;
    app_state->sensor_type = SOIL_SENSOR_TYPE_CAPACITIVE;  // Por defecto capacitivo
    app_state->humidity_percentage = humidity_percent;
    app_state->temperature_celsius = temperature_celsius;
    app_state->last_reading_timestamp = esp_timer_get_time() / 1000000; // segundos

    if (ret == ESP_OK) {
        app_state->is_connected = true;
        app_state->is_calibrated = true;  // Asumimos calibrado si el driver funciona
        app_state->raw_adc_value = driver_state.last_adc_value;
        app_state->raw_voltage = driver_state.last_voltage_mv / 1000.0;  // mV to V
        app_state->consecutive_failures = driver_state.consecutive_errors;
        app_state->reading_status = SOIL_READING_SUCCESS;
    } else {
        app_state->is_connected = false;
        app_state->is_calibrated = false;
        app_state->consecutive_failures = 255;  // Indicar muchos fallos
        app_state->reading_status = SOIL_READING_SENSOR_TIMEOUT;
    }

    // Detectar si está desconectado
    if (detect_sensor_disconnection(app_state)) {
        app_state->is_connected = false;
        app_state->reading_status = SOIL_READING_SENSOR_DISCONNECTED;
    }

    // Validar datos
    if (!is_valid_humidity_percentage(humidity_percent)) {
        app_state->reading_status = SOIL_READING_INVALID_VALUE;
    }

    if (!is_valid_adc_value(app_state->raw_adc_value)) {
        app_state->reading_status = SOIL_READING_INVALID_VALUE;
    }
}

/**
 * @brief Actualizar estadísticas basado en resultado de lectura
 */
static void update_reading_statistics(const soil_reading_complete_result_t* result) {
    soil_sensors_statistics_t* stats = &g_soil_sensors_context.statistics;

    stats->total_readings_attempted++;
    stats->last_successful_reading_timestamp = esp_timer_get_time() / 1000000;

    if (result->overall_result == SOIL_READING_SUCCESS ||
        result->overall_result == SOIL_READING_PARTIAL_SUCCESS) {
        stats->successful_readings++;

        // Actualizar tendencia de humedad (promedio móvil simple)
        if (stats->successful_readings == 1) {
            stats->average_soil_humidity_trend = result->average_soil_humidity;
        } else {
            // Promedio móvil con factor de suavizado
            float alpha = 0.1;  // Factor de suavizado
            stats->average_soil_humidity_trend =
                alpha * result->average_soil_humidity +
                (1.0 - alpha) * stats->average_soil_humidity_trend;
        }
    } else {
        stats->failed_readings++;
    }

    // Detectar eventos específicos
    for (int i = 0; i < 3; i++) {
        const soil_sensor_state_t* sensor = &result->sensor_states[i];

        if (sensor->reading_status == SOIL_READING_SENSOR_DISCONNECTED) {
            stats->sensors_disconnected_events++;
        }

        if (sensor->reading_status == SOIL_READING_CALIBRATION_REQUIRED) {
            stats->calibration_events++;
        }
    }

    if (result->has_anomalies) {
        stats->anomaly_detections++;
    }

    // Actualizar tiempo promedio de lectura
    if (result->total_reading_time_ms > 0) {
        float total_readings = stats->successful_readings + stats->failed_readings;
        stats->average_reading_time_ms =
            (stats->average_reading_time_ms * (total_readings - 1) + result->total_reading_time_ms) / total_readings;
    }

    ESP_LOGD(TAG, "Stats updated: success=%d, failed=%d, avg_humidity=%.1f%%",
             stats->successful_readings, stats->failed_readings, stats->average_soil_humidity_trend);
}

// ============================================================================
// FUNCIONES PÚBLICAS - INICIALIZACIÓN
// ============================================================================

esp_err_t read_soil_sensors_init(const safety_limits_t* safety_limits, uint8_t num_sensors) {
    if (g_soil_sensors_context.use_case_initialized) {
        ESP_LOGW(TAG, "Use case already initialized");
        return ESP_OK;
    }

    if (!safety_limits || num_sensors < 1 || num_sensors > 3) {
        ESP_LOGE(TAG, "Invalid parameters for initialization");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing read soil sensors use case (%d sensors)", num_sensors);

    // Copiar safety limits
    memcpy(&g_soil_sensors_context.safety_limits, safety_limits, sizeof(safety_limits_t));

    // Usar configuración por defecto
    memcpy(&g_soil_sensors_context.config, &DEFAULT_CONFIG, sizeof(soil_sensors_config_t));
    g_soil_sensors_context.config.num_sensors_enabled = num_sensors;
    g_soil_sensors_context.num_sensors_configured = num_sensors;

    // Inicializar calibraciones por defecto
    for (uint8_t i = 1; i <= num_sensors; i++) {
        init_default_calibration(i);
    }

    // Inicializar estadísticas
    memset(&g_soil_sensors_context.statistics, 0, sizeof(soil_sensors_statistics_t));

    // Inicializar el driver de infraestructura
    esp_err_t ret = soil_moisture_driver_init(num_sensors, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize soil moisture driver: %s", esp_err_to_name(ret));
        return ret;
    }

    g_soil_sensors_context.use_case_initialized = true;
    ESP_LOGI(TAG, "Read soil sensors use case initialized successfully");

    return ESP_OK;
}

// ============================================================================
// FUNCIONES PÚBLICAS - LECTURA DE SENSORES
// ============================================================================

soil_reading_complete_result_t read_soil_sensors_read_all(const ambient_sensor_data_t* ambient_data) {
    soil_reading_complete_result_t result = {0};
    uint32_t start_time = esp_timer_get_time() / 1000; // ms

    if (!g_soil_sensors_context.use_case_initialized) {
        ESP_LOGE(TAG, "Use case not initialized");
        result.overall_result = SOIL_READING_SYSTEM_ERROR;
        strcpy(result.error_description, "Use case not initialized");
        return result;
    }

    ESP_LOGI(TAG, "Reading all soil sensors (%d configured)",
             g_soil_sensors_context.num_sensors_configured);

    // Preparar temperatura para compensación
    float temperature_celsius = 25.0;  // Por defecto
    if (ambient_data && ambient_data->ambient_temperature > -50.0 &&
        ambient_data->ambient_temperature < 70.0) {
        temperature_celsius = ambient_data->ambient_temperature;
    }

    // Usar el driver de infraestructura para leer todos los sensores
    soil_sensor_data_t raw_sensor_data = {0};
    esp_err_t driver_result = soil_moisture_driver_read_all_sensors(&raw_sensor_data, temperature_celsius);

    // Procesar resultados del driver y convertir a formato de aplicación
    uint8_t successful_sensors = 0;
    float humidity_sum = 0.0;
    float humidity_values[3] = {0.0, 0.0, 0.0};

    // Procesar cada sensor configurado
    for (uint8_t i = 0; i < g_soil_sensors_context.num_sensors_configured; i++) {
        uint8_t sensor_number = i + 1;
        float humidity = 0.0;

        // Obtener humedad según el número de sensor
        switch (sensor_number) {
            case 1:
                humidity = raw_sensor_data.soil_humidity_1;
                break;
            case 2:
                humidity = raw_sensor_data.soil_humidity_2;
                break;
            case 3:
                humidity = raw_sensor_data.soil_humidity_3;
                break;
        }

        // Convertir a estado de aplicación
        convert_driver_state_to_application_state(sensor_number, humidity, temperature_celsius,
                                                 &result.sensor_states[i]);

        // Contabilizar sensores exitosos
        if (result.sensor_states[i].reading_status == SOIL_READING_SUCCESS) {
            successful_sensors++;
            humidity_sum += humidity;
            humidity_values[i] = humidity;
        } else {
            result.sensors_with_errors++;
            ESP_LOGW(TAG, "Sensor %d failed: %s", sensor_number,
                     read_soil_sensors_result_to_string(result.sensor_states[i].reading_status));
        }
    }

    // Copiar datos finales de sensores
    result.sensor_data = raw_sensor_data;
    result.sensors_read_successfully = successful_sensors;

    // Determinar resultado general
    if (successful_sensors == g_soil_sensors_context.num_sensors_configured) {
        result.overall_result = SOIL_READING_SUCCESS;
    } else if (successful_sensors > 0) {
        result.overall_result = SOIL_READING_PARTIAL_SUCCESS;
        snprintf(result.error_description, sizeof(result.error_description),
                 "%d of %d sensors failed", result.sensors_with_errors,
                 g_soil_sensors_context.num_sensors_configured);
    } else {
        result.overall_result = SOIL_READING_SENSOR_TIMEOUT;
        strcpy(result.error_description, "All sensors failed to read");
    }

    // Calcular estadísticas si hay sensores exitosos
    if (successful_sensors > 0) {
        result.average_soil_humidity = humidity_sum / successful_sensors;

        // Calcular varianza
        float variance_sum = 0.0;
        for (uint8_t i = 0; i < g_soil_sensors_context.num_sensors_configured; i++) {
            if (humidity_values[i] > 0.0) {
                float diff = humidity_values[i] - result.average_soil_humidity;
                variance_sum += diff * diff;
            }
        }
        result.humidity_variance = variance_sum / successful_sensors;
    }

    // Detectar anomalías simples
    result.has_anomalies = (result.humidity_variance > 100.0 || // Varianza alta
                           result.sensors_with_errors > 0);     // Errores presentes

    // Verificar si requiere calibración
    result.requires_calibration = false;
    for (uint8_t i = 0; i < g_soil_sensors_context.num_sensors_configured; i++) {
        if (result.sensor_states[i].reading_status == SOIL_READING_CALIBRATION_REQUIRED ||
            !result.sensor_states[i].is_calibrated) {
            result.requires_calibration = true;
            break;
        }
    }

    // Calcular tiempo total de lectura
    uint32_t end_time = esp_timer_get_time() / 1000; // ms
    result.total_reading_time_ms = end_time - start_time;

    // Actualizar estadísticas
    update_reading_statistics(&result);

    ESP_LOGI(TAG, "Soil sensors read completed: %d/%d successful, avg=%.1f%%, variance=%.1f, time=%dms",
             successful_sensors, g_soil_sensors_context.num_sensors_configured,
             result.average_soil_humidity, result.humidity_variance, result.total_reading_time_ms);

    return result;
}

soil_reading_result_t read_soil_sensors_read_individual(uint8_t sensor_number,
                                                       float ambient_temperature,
                                                       soil_sensor_state_t* sensor_state) {
    if (!g_soil_sensors_context.use_case_initialized) {
        ESP_LOGE(TAG, "Use case not initialized");
        return SOIL_READING_SYSTEM_ERROR;
    }

    if (!is_valid_sensor_number(sensor_number) || !sensor_state) {
        ESP_LOGE(TAG, "Invalid parameters for individual sensor reading");
        return SOIL_READING_SYSTEM_ERROR;
    }

    ESP_LOGD(TAG, "Reading individual sensor %d", sensor_number);

    // Usar el driver de infraestructura para leer sensor individual
    float humidity_percent = 0.0;
    esp_err_t ret = soil_moisture_driver_read_sensor(sensor_number, &humidity_percent, ambient_temperature);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read sensor %d: %s", sensor_number, esp_err_to_name(ret));

        // Inicializar estado con error
        memset(sensor_state, 0, sizeof(soil_sensor_state_t));
        sensor_state->sensor_number = sensor_number;
        sensor_state->is_connected = false;
        sensor_state->reading_status = SOIL_READING_SENSOR_TIMEOUT;

        return SOIL_READING_SENSOR_TIMEOUT;
    }

    // Convertir resultado del driver a estado de aplicación
    convert_driver_state_to_application_state(sensor_number, humidity_percent,
                                             ambient_temperature, sensor_state);

    ESP_LOGD(TAG, "Sensor %d individual read: %.1f%% humidity", sensor_number, humidity_percent);

    return sensor_state->reading_status;
}

// ============================================================================
// FUNCIONES PÚBLICAS - CALIBRACIÓN
// ============================================================================

esp_err_t read_soil_sensors_calibrate_sensor(uint8_t sensor_number, uint8_t calibration_type) {
    if (!g_soil_sensors_context.use_case_initialized) {
        ESP_LOGE(TAG, "Use case not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!is_valid_sensor_number(sensor_number) || calibration_type > 1) {
        ESP_LOGE(TAG, "Invalid parameters for sensor calibration");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Calibrating sensor %d, type: %s", sensor_number,
             calibration_type == 0 ? "air-dry" : "water-saturated");

    // Delegar calibración al driver de infraestructura
    esp_err_t ret;
    int calibrated_value = 0;

    if (calibration_type == 0) {
        // Calibración air-dry (0% humedad)
        ret = soil_moisture_driver_calibrate_air_dry(sensor_number, &calibrated_value);
        if (ret == ESP_OK) {
            g_soil_sensors_context.calibrations[sensor_number - 1].air_dry_adc_value = calibrated_value;
        }
    } else {
        // Calibración water-saturated (100% humedad)
        ret = soil_moisture_driver_calibrate_water_saturated(sensor_number, &calibrated_value);
        if (ret == ESP_OK) {
            g_soil_sensors_context.calibrations[sensor_number - 1].water_saturated_adc_value = calibrated_value;
        }
    }

    if (ret == ESP_OK) {
        g_soil_sensors_context.calibrations[sensor_number - 1].last_calibration_timestamp =
            esp_timer_get_time() / 1000000;
        g_soil_sensors_context.statistics.calibration_events++;

        ESP_LOGI(TAG, "Sensor %d calibrated successfully, ADC value: %d", sensor_number, calibrated_value);
    } else {
        ESP_LOGE(TAG, "Failed to calibrate sensor %d: %s", sensor_number, esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t read_soil_sensors_auto_calibrate(uint8_t sensor_number) {
    ESP_LOGW(TAG, "Auto-calibration not yet implemented for sensor %d", sensor_number);
    return ESP_ERR_NOT_SUPPORTED;
}

// ============================================================================
// FUNCIONES PÚBLICAS - UTILIDADES
// ============================================================================

float read_soil_sensors_adc_to_humidity_percentage(uint8_t sensor_number,
                                                  uint16_t raw_adc_value,
                                                  float ambient_temperature) {
    if (!g_soil_sensors_context.use_case_initialized || !is_valid_sensor_number(sensor_number)) {
        return 0.0;
    }

    // Usar calibración específica del sensor
    soil_sensor_calibration_t* calib = &g_soil_sensors_context.calibrations[sensor_number - 1];

    // Mapeo lineal simple basado en calibración
    float percentage = ((float)(calib->air_dry_adc_value - raw_adc_value)) /
                      ((float)(calib->air_dry_adc_value - calib->water_saturated_adc_value)) * 100.0;

    // Aplicar compensación básica por temperatura
    if (ambient_temperature != 25.0) {
        float temp_compensation = (ambient_temperature - 25.0) * -0.1;  // -0.1% por °C
        percentage += temp_compensation;
    }

    // Limitar a rango válido
    if (percentage < 0.0) percentage = 0.0;
    if (percentage > 100.0) percentage = 100.0;

    return percentage;
}

uint16_t read_soil_sensors_apply_median_filter(const uint16_t* readings, uint8_t num_readings) {
    if (!readings || num_readings == 0) return 0;
    if (num_readings == 1) return readings[0];

    // Crear copia para ordenar
    uint16_t sorted_readings[num_readings];
    memcpy(sorted_readings, readings, num_readings * sizeof(uint16_t));

    // Ordenamiento burbuja simple
    for (uint8_t i = 0; i < num_readings - 1; i++) {
        for (uint8_t j = 0; j < num_readings - i - 1; j++) {
            if (sorted_readings[j] > sorted_readings[j + 1]) {
                uint16_t temp = sorted_readings[j];
                sorted_readings[j] = sorted_readings[j + 1];
                sorted_readings[j + 1] = temp;
            }
        }
    }

    // Retornar mediana
    return sorted_readings[num_readings / 2];
}

uint8_t read_soil_sensors_detect_outliers(const uint16_t* readings, uint8_t num_readings,
                                         float outlier_threshold) {
    if (!readings || num_readings < 3) return 0;

    // Calcular media
    uint32_t sum = 0;
    for (uint8_t i = 0; i < num_readings; i++) {
        sum += readings[i];
    }
    float mean = (float)sum / num_readings;

    // Calcular desviación estándar
    float variance_sum = 0.0;
    for (uint8_t i = 0; i < num_readings; i++) {
        float diff = readings[i] - mean;
        variance_sum += diff * diff;
    }
    float std_dev = sqrt(variance_sum / num_readings);

    // Contar outliers
    uint8_t outlier_count = 0;
    float threshold = std_dev * (outlier_threshold / 100.0);

    for (uint8_t i = 0; i < num_readings; i++) {
        if (fabs(readings[i] - mean) > threshold) {
            outlier_count++;
        }
    }

    return outlier_count;
}

bool read_soil_sensors_is_sensor_disconnected(const soil_sensor_state_t* sensor_state) {
    if (!sensor_state) return true;
    return detect_sensor_disconnection(sensor_state);
}

bool read_soil_sensors_detect_anomalies(const soil_sensor_state_t* sensor_state,
                                       const float* previous_readings,
                                       uint8_t num_previous) {
    if (!sensor_state || !previous_readings || num_previous == 0) {
        return false;
    }

    // Detección simple de anomalías basada en cambio brusco
    float current_humidity = sensor_state->humidity_percentage;

    for (uint8_t i = 0; i < num_previous; i++) {
        float change = fabs(current_humidity - previous_readings[i]);
        if (change > 25.0) {  // Cambio mayor a 25% considerado anomalía
            return true;
        }
    }

    return false;
}

float read_soil_sensors_calculate_humidity_variance(const soil_sensor_state_t* sensor_states,
                                                   uint8_t num_sensors) {
    if (!sensor_states || num_sensors == 0) return 0.0;

    // Calcular media de sensores exitosos
    float sum = 0.0;
    uint8_t valid_sensors = 0;

    for (uint8_t i = 0; i < num_sensors; i++) {
        if (sensor_states[i].reading_status == SOIL_READING_SUCCESS) {
            sum += sensor_states[i].humidity_percentage;
            valid_sensors++;
        }
    }

    if (valid_sensors < 2) return 0.0;

    float mean = sum / valid_sensors;

    // Calcular varianza
    float variance_sum = 0.0;
    for (uint8_t i = 0; i < num_sensors; i++) {
        if (sensor_states[i].reading_status == SOIL_READING_SUCCESS) {
            float diff = sensor_states[i].humidity_percentage - mean;
            variance_sum += diff * diff;
        }
    }

    return variance_sum / valid_sensors;
}

uint8_t read_soil_sensors_find_most_reliable_sensor(const soil_sensor_state_t* sensor_states,
                                                   uint8_t num_sensors) {
    if (!sensor_states || num_sensors == 0) return 0;

    uint8_t best_sensor = 0;
    uint16_t lowest_failures = UINT16_MAX;

    for (uint8_t i = 0; i < num_sensors; i++) {
        if (sensor_states[i].reading_status == SOIL_READING_SUCCESS &&
            sensor_states[i].consecutive_failures < lowest_failures) {
            lowest_failures = sensor_states[i].consecutive_failures;
            best_sensor = sensor_states[i].sensor_number;
        }
    }

    return best_sensor;
}

float read_soil_sensors_calculate_weighted_average(const soil_sensor_state_t* sensor_states,
                                                  uint8_t num_sensors) {
    if (!sensor_states || num_sensors == 0) return 0.0;

    float weighted_sum = 0.0;
    float weight_total = 0.0;

    for (uint8_t i = 0; i < num_sensors; i++) {
        if (sensor_states[i].reading_status == SOIL_READING_SUCCESS) {
            // Peso inversamente proporcional a fallos consecutivos
            float weight = 1.0 / (1.0 + sensor_states[i].consecutive_failures);
            weighted_sum += sensor_states[i].humidity_percentage * weight;
            weight_total += weight;
        }
    }

    return weight_total > 0.0 ? weighted_sum / weight_total : 0.0;
}

// ============================================================================
// FUNCIONES PÚBLICAS - CONFIGURACIÓN Y ESTADÍSTICAS
// ============================================================================

void read_soil_sensors_update_statistics(const soil_reading_complete_result_t* result) {
    if (result) {
        update_reading_statistics(result);
    }
}

void read_soil_sensors_generate_error_description(const soil_reading_complete_result_t* result,
                                                 char* error_buffer, size_t buffer_size) {
    if (!result || !error_buffer || buffer_size == 0) return;

    if (result->overall_result == SOIL_READING_SUCCESS) {
        snprintf(error_buffer, buffer_size, "No errors");
        return;
    }

    snprintf(error_buffer, buffer_size, "Errors: %s", result->error_description);
}

esp_err_t read_soil_sensors_set_config(const soil_sensors_config_t* config) {
    if (!g_soil_sensors_context.use_case_initialized || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&g_soil_sensors_context.config, config, sizeof(soil_sensors_config_t));
    ESP_LOGI(TAG, "Configuration updated");
    return ESP_OK;
}

void read_soil_sensors_get_config(soil_sensors_config_t* config) {
    if (config && g_soil_sensors_context.use_case_initialized) {
        memcpy(config, &g_soil_sensors_context.config, sizeof(soil_sensors_config_t));
    }
}

esp_err_t read_soil_sensors_get_calibration(uint8_t sensor_number,
                                           soil_sensor_calibration_t* calibration) {
    if (!g_soil_sensors_context.use_case_initialized ||
        !is_valid_sensor_number(sensor_number) || !calibration) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(calibration, &g_soil_sensors_context.calibrations[sensor_number - 1],
           sizeof(soil_sensor_calibration_t));
    return ESP_OK;
}

esp_err_t read_soil_sensors_set_calibration(uint8_t sensor_number,
                                           const soil_sensor_calibration_t* calibration) {
    if (!g_soil_sensors_context.use_case_initialized ||
        !is_valid_sensor_number(sensor_number) || !calibration) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&g_soil_sensors_context.calibrations[sensor_number - 1], calibration,
           sizeof(soil_sensor_calibration_t));

    ESP_LOGI(TAG, "Sensor %d calibration updated", sensor_number);
    return ESP_OK;
}

esp_err_t read_soil_sensors_reset_sensor(uint8_t sensor_number) {
    if (!g_soil_sensors_context.use_case_initialized || !is_valid_sensor_number(sensor_number)) {
        return ESP_ERR_INVALID_ARG;
    }

    // Reinicializar calibración a valores por defecto
    init_default_calibration(sensor_number);
    ESP_LOGI(TAG, "Sensor %d reset to default calibration", sensor_number);
    return ESP_OK;
}

void read_soil_sensors_get_statistics(soil_sensors_statistics_t* stats) {
    if (stats && g_soil_sensors_context.use_case_initialized) {
        memcpy(stats, &g_soil_sensors_context.statistics, sizeof(soil_sensors_statistics_t));
    }
}

void read_soil_sensors_reset_statistics(void) {
    if (g_soil_sensors_context.use_case_initialized) {
        memset(&g_soil_sensors_context.statistics, 0, sizeof(soil_sensors_statistics_t));
        ESP_LOGI(TAG, "Statistics reset");
    }
}

// ============================================================================
// FUNCIONES PÚBLICAS - UTILIDADES Y DEBUG
// ============================================================================

const char* read_soil_sensors_result_to_string(soil_reading_result_t result) {
    switch (result) {
        case SOIL_READING_SUCCESS: return "SUCCESS";
        case SOIL_READING_SENSOR_TIMEOUT: return "SENSOR_TIMEOUT";
        case SOIL_READING_SENSOR_DISCONNECTED: return "SENSOR_DISCONNECTED";
        case SOIL_READING_INVALID_VALUE: return "INVALID_VALUE";
        case SOIL_READING_CALIBRATION_REQUIRED: return "CALIBRATION_REQUIRED";
        case SOIL_READING_ELECTROMAGNETIC_INTERFERENCE: return "EMI";
        case SOIL_READING_TEMPERATURE_ERROR: return "TEMPERATURE_ERROR";
        case SOIL_READING_PARTIAL_SUCCESS: return "PARTIAL_SUCCESS";
        case SOIL_READING_SYSTEM_ERROR: return "SYSTEM_ERROR";
        default: return "UNKNOWN";
    }
}

const char* read_soil_sensors_type_to_string(soil_sensor_type_t sensor_type) {
    switch (sensor_type) {
        case SOIL_SENSOR_TYPE_CAPACITIVE: return "CAPACITIVE";
        case SOIL_SENSOR_TYPE_RESISTIVE: return "RESISTIVE";
        case SOIL_SENSOR_TYPE_FREQUENCY_DOMAIN: return "FREQUENCY_DOMAIN";
        case SOIL_SENSOR_TYPE_UNKNOWN: return "UNKNOWN";
        default: return "INVALID";
    }
}

uint32_t read_soil_sensors_get_current_timestamp(void) {
    return esp_timer_get_time() / 1000000; // segundos desde boot
}