#include "soil_moisture_driver.h"
#include <string.h>
#include <math.h>
#include <time.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "nvs_flash.h"
#include "nvs.h"

/**
 * @file soil_moisture_driver.c
 * @brief Implementación del driver de sensores de humedad de suelo
 *
 * Basado en el repositorio de Liwaisi esp32-moisture-soil-sensor,
 * adaptado para arquitectura hexagonal y 3 sensores simultáneos.
 */

static const char *TAG = "SOIL_MOISTURE_DRIVER";

// Variables estáticas globales del driver
static adc_oneshot_unit_handle_t g_adc1_handle = NULL;
static adc_cali_handle_t g_adc1_cali_handle = NULL;
static bool g_driver_initialized = false;
static uint8_t g_num_sensors = 3;

// Configuraciones y estados de sensores
static soil_sensor_config_t g_sensor_configs[3];
static soil_sensor_state_t g_sensor_states[3];
static soil_moisture_driver_config_t g_global_config;
static soil_moisture_statistics_t g_statistics;

// Configuración por defecto basada en repositorio Liwaisi
static const soil_moisture_driver_config_t DEFAULT_CONFIG = {
    .adc_attenuation = ADC_ATTEN_DB_11,           // 0-3.9V range
    .adc_width = ADC_WIDTH_BIT_12,                // 12-bit resolution
    .samples_per_reading = 10,                    // Promedio de 10 muestras (Liwaisi)
    .sample_interval_ms = 10,                     // 10ms entre muestras

    .enable_median_filter = true,                 // Filtro mediana habilitado
    .enable_average_filter = true,                // Filtro promedio habilitado
    .enable_outlier_detection = true,             // Detección outliers habilitada
    .outlier_threshold_std_dev = 2.0f,            // 2 desviaciones estándar

    .enable_auto_calibration = false,             // Auto-calibración manual por seguridad
    .auto_calibration_interval_s = 86400,         // 24 horas
    .calibration_drift_threshold = 5.0f,          // 5% deriva para re-calibrar

    .enable_temperature_compensation = true,      // Compensación temperatura habilitada
    .default_temperature_celsius = 25.0f,         // Temperatura referencia Colombia

    .sensor_timeout_ms = 2000,                    // 2 segundos timeout
    .max_consecutive_failures = 5                 // Máx 5 fallos consecutivos
};

// Configuración por defecto de sensores basada en valores Liwaisi
static void init_default_sensor_config(uint8_t sensor_number, soil_sensor_config_t* config) {
    memset(config, 0, sizeof(soil_sensor_config_t));

    config->sensor_number = sensor_number;
    config->is_enabled = true;
    config->is_connected = false; // Se detectará dinámicamente

    // Asignar canal ADC según sensor
    switch (sensor_number) {
        case 1:
            config->adc_channel = SOIL_SENSOR_1_ADC_CHANNEL; // ADC1_CHANNEL_0 - GPIO 36
            break;
        case 2:
            config->adc_channel = SOIL_SENSOR_2_ADC_CHANNEL; // ADC1_CHANNEL_3 - GPIO 39
            break;
        case 3:
            config->adc_channel = SOIL_SENSOR_3_ADC_CHANNEL; // ADC1_CHANNEL_6 - GPIO 34
            break;
        default:
            config->adc_channel = ADC1_CHANNEL_0;
            break;
    }

    // Calibración por defecto basada en valores Liwaisi para sensores capacitivos
    config->air_dry_adc_value = 2865;            // Valor Liwaisi para aire seco
    config->water_saturated_adc_value = 1220;    // Valor Liwaisi para saturado
    config->calibration_state = SOIL_CALIBRATION_UNCALIBRATED;
    config->last_calibration_timestamp = 0;

    // Límites de validación ADC
    config->min_valid_adc = 200;                  // Mínimo válido
    config->max_valid_adc = 3800;                 // Máximo válido
    config->filter_window_size = 5;               // Ventana filtro mediana

    // Compensación temperatura (valores típicos sensores capacitivos)
    config->temperature_coefficient = -0.002f;   // -0.2% por °C
    config->baseline_temperature = 25.0f;        // 25°C referencia Colombia
}

static void init_sensor_state(uint8_t sensor_number, soil_sensor_state_t* state) {
    memset(state, 0, sizeof(soil_sensor_state_t));

    state->sensor_number = sensor_number;
    state->raw_adc_value = 0;
    state->filtered_adc_value = 0;
    state->raw_voltage = 0.0f;
    state->humidity_percentage = 0.0f;
    state->temperature_compensated_humidity = 0.0f;

    state->reading_valid = false;
    state->noise_level = 0;
    state->response_time_ms = 0;

    // Inicializar historial circular
    memset(state->reading_history, 0, sizeof(state->reading_history));
    state->history_index = 0;
    state->history_count = 0;

    state->last_reading_timestamp = 0;
    state->consecutive_failures = 0;
    state->requires_calibration = true;
}

// Función helper basada en Liwaisi para mapear valores ADC a porcentaje
static float map_value(float value, float in_min, float in_max, float out_min, float out_max) {
    if (in_max == in_min) {
        return out_min; // Evitar división por cero
    }

    float mapped = (value - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;

    // Limitar a rango de salida
    if (mapped < out_min) mapped = out_min;
    if (mapped > out_max) mapped = out_max;

    return mapped;
}

// Inicializar ADC basado en código Liwaisi
static esp_err_t init_adc_unit(void) {
    ESP_LOGI(TAG, "Initializing ADC1 unit");

    // Configuración de la unidad ADC1 (basado en Liwaisi)
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };

    esp_err_t ret = adc_oneshot_new_unit(&init_config1, &g_adc1_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC1 unit: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configurar canales ADC para cada sensor habilitado
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = g_global_config.adc_width,
        .atten = g_global_config.adc_attenuation,
    };

    for (uint8_t i = 0; i < g_num_sensors; i++) {
        if (g_sensor_configs[i].is_enabled) {
            ret = adc_oneshot_config_channel(g_adc1_handle, g_sensor_configs[i].adc_channel, &config);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to configure ADC channel %d: %s",
                        (int)g_sensor_configs[i].adc_channel, esp_err_to_name(ret));
                return ret;
            }
            ESP_LOGI(TAG, "Configured sensor %d on ADC channel %d",
                    (int)(i + 1), (int)g_sensor_configs[i].adc_channel);
        }
    }

    // Inicializar calibración ADC (código Liwaisi adaptado)
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = g_global_config.adc_attenuation,
        .bitwidth = g_global_config.adc_width,
    };

    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &g_adc1_cali_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ADC calibration failed: %s, using raw values", esp_err_to_name(ret));
        g_adc1_cali_handle = NULL; // Continuar sin calibración
    } else {
        ESP_LOGI(TAG, "ADC calibration initialized successfully");
    }

    return ESP_OK;
}

// Leer valor ADC raw adaptado de Liwaisi para multi-sensor
static esp_err_t read_raw_adc_value(adc1_channel_t channel, uint16_t* raw_value) {
    if (!g_adc1_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t adc_reading = 0;
    esp_err_t ret;

    // Promedio de múltiples muestras (técnica de Liwaisi)
    for (int i = 0; i < g_global_config.samples_per_reading; i++) {
        int adc_raw;
        ret = adc_oneshot_read(g_adc1_handle, channel, &adc_raw);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ADC read failed on channel %d: %s", (int)channel, esp_err_to_name(ret));
            return ret;
        }
        adc_reading += adc_raw;

        // Pequeña pausa entre muestras para estabilidad
        if (g_global_config.sample_interval_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(g_global_config.sample_interval_ms));
        }
    }

    *raw_value = (uint16_t)(adc_reading / g_global_config.samples_per_reading);
    return ESP_OK;
}

// Convertir ADC a voltaje usando calibración
static esp_err_t convert_adc_to_voltage(uint16_t adc_value, float* voltage) {
    if (g_adc1_cali_handle) {
        int voltage_mv;
        esp_err_t ret = adc_cali_raw_to_voltage(g_adc1_cali_handle, adc_value, &voltage_mv);
        if (ret == ESP_OK) {
            *voltage = voltage_mv / 1000.0f; // Convertir mV a V
            return ESP_OK;
        }
        ESP_LOGW(TAG, "ADC calibration conversion failed: %s", esp_err_to_name(ret));
    }

    // Fallback: conversión manual sin calibración
    *voltage = ((float)adc_value / 4095.0f) * 3.9f; // Para ADC_ATTEN_DB_11
    return ESP_OK;
}

// Implementación de filtros avanzados (nuevo, no en Liwaisi)
static int compare_uint16(const void* a, const void* b) {
    return (*(uint16_t*)a - *(uint16_t*)b);
}

static uint16_t apply_median_filter(uint16_t* readings, uint8_t count) {
    if (count == 0) return 0;
    if (count == 1) return readings[0];

    // Crear copia para ordenar
    uint16_t sorted[10];
    memcpy(sorted, readings, count * sizeof(uint16_t));

    // Ordenar usando qsort
    qsort(sorted, count, sizeof(uint16_t), compare_uint16);

    // Retornar mediana
    if (count % 2 == 0) {
        return (sorted[count/2 - 1] + sorted[count/2]) / 2;
    } else {
        return sorted[count/2];
    }
}

static bool detect_outlier(uint16_t value, uint16_t* history, uint8_t count, float threshold_std_dev) {
    if (count < 3) return false; // Necesitamos al menos 3 valores

    // Calcular promedio
    uint32_t sum = 0;
    for (uint8_t i = 0; i < count; i++) {
        sum += history[i];
    }
    float mean = (float)sum / count;

    // Calcular desviación estándar
    float variance = 0.0f;
    for (uint8_t i = 0; i < count; i++) {
        float diff = history[i] - mean;
        variance += diff * diff;
    }
    variance /= count;
    float std_dev = sqrtf(variance);

    // Verificar si el valor está fuera del rango aceptable
    float diff = fabsf(value - mean);
    return (diff > threshold_std_dev * std_dev);
}

static void update_sensor_history(soil_sensor_state_t* state, uint16_t new_value) {
    // Agregar nuevo valor al historial circular
    state->reading_history[state->history_index] = new_value;
    state->history_index = (state->history_index + 1) % 10; // Máximo 10 valores

    if (state->history_count < 10) {
        state->history_count++;
    }
}

static float calculate_moving_average(const soil_sensor_state_t* state) {
    if (state->history_count == 0) return 0.0f;

    uint32_t sum = 0;
    for (uint8_t i = 0; i < state->history_count; i++) {
        sum += state->reading_history[i];
    }

    return (float)sum / state->history_count;
}

// Convertir ADC a porcentaje usando calibración del sensor (adaptado de Liwaisi)
static float convert_adc_to_percentage(uint16_t adc_value, const soil_sensor_config_t* config) {
    // Usar map_value de Liwaisi con calibración específica del sensor
    float percentage = map_value((float)adc_value,
                                (float)config->water_saturated_adc_value,  // Valor alto ADC = baja humedad
                                (float)config->air_dry_adc_value,          // Valor bajo ADC = alta humedad
                                100.0f,                                    // 100% humedad
                                0.0f);                                     // 0% humedad

    return percentage;
}

// Aplicar compensación por temperatura (nuevo feature)
static float apply_temperature_compensation(float raw_percentage,
                                          float current_temperature,
                                          const soil_sensor_config_t* config) {
    if (!g_global_config.enable_temperature_compensation) {
        return raw_percentage;
    }

    // Calcular diferencia de temperatura respecto a baseline
    float temp_diff = current_temperature - config->baseline_temperature;

    // Aplicar coeficiente de compensación
    float compensation = temp_diff * config->temperature_coefficient * 100.0f; // Convertir a %

    float compensated = raw_percentage + compensation;

    // Limitar a rango válido 0-100%
    if (compensated < 0.0f) compensated = 0.0f;
    if (compensated > 100.0f) compensated = 100.0f;

    return compensated;
}

// Validar lectura de sensor
static bool validate_sensor_reading(uint16_t adc_value, const soil_sensor_config_t* config) {
    return (adc_value >= config->min_valid_adc && adc_value <= config->max_valid_adc);
}

// Calcular nivel de ruido basado en variabilidad
static uint8_t calculate_noise_level(const soil_sensor_state_t* state) {
    if (state->history_count < 3) return 0;

    // Calcular desviación estándar como medida de ruido
    float mean = calculate_moving_average(state);
    float variance = 0.0f;

    for (uint8_t i = 0; i < state->history_count; i++) {
        float diff = state->reading_history[i] - mean;
        variance += diff * diff;
    }
    variance /= state->history_count;
    float std_dev = sqrtf(variance);

    // Normalizar a escala 0-100
    uint8_t noise_level = (uint8_t)((std_dev / 100.0f) * 100);
    if (noise_level > 100) noise_level = 100;

    return noise_level;
}

esp_err_t soil_moisture_driver_init(const soil_moisture_driver_config_t* config, uint8_t num_sensors) {
    ESP_LOGI(TAG, "Initializing soil moisture driver with %d sensors", num_sensors);

    if (g_driver_initialized) {
        ESP_LOGW(TAG, "Driver already initialized, deinitializing first");
        soil_moisture_driver_deinit();
    }

    // Validar parámetros
    if (num_sensors == 0 || num_sensors > 3) {
        ESP_LOGE(TAG, "Invalid number of sensors: %d (must be 1-3)", num_sensors);
        return ESP_ERR_INVALID_ARG;
    }

    g_num_sensors = num_sensors;

    // Usar configuración por defecto si no se proporciona
    if (config) {
        memcpy(&g_global_config, config, sizeof(soil_moisture_driver_config_t));
    } else {
        memcpy(&g_global_config, &DEFAULT_CONFIG, sizeof(soil_moisture_driver_config_t));
        ESP_LOGI(TAG, "Using default configuration");
    }

    // Inicializar configuraciones y estados de sensores
    for (uint8_t i = 0; i < g_num_sensors; i++) {
        init_default_sensor_config(i + 1, &g_sensor_configs[i]);
        init_sensor_state(i + 1, &g_sensor_states[i]);
        ESP_LOGI(TAG, "Initialized sensor %d configuration", i + 1);
    }

    // Inicializar estadísticas
    memset(&g_statistics, 0, sizeof(soil_moisture_statistics_t));

    // Inicializar ADC
    esp_err_t ret = init_adc_unit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC unit");
        return ret;
    }

    g_driver_initialized = true;
    ESP_LOGI(TAG, "Soil moisture driver initialized successfully");

    return ESP_OK;
}

esp_err_t soil_moisture_driver_deinit(void) {
    if (!g_driver_initialized) {
        return ESP_OK; // Ya desinicializado
    }

    ESP_LOGI(TAG, "Deinitializing soil moisture driver");

    // Liberar recursos ADC
    if (g_adc1_cali_handle) {
        esp_err_t ret = adc_cali_delete_scheme_curve_fitting(g_adc1_cali_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to delete ADC calibration: %s", esp_err_to_name(ret));
        }
        g_adc1_cali_handle = NULL;
    }

    if (g_adc1_handle) {
        esp_err_t ret = adc_oneshot_del_unit(g_adc1_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to delete ADC unit: %s", esp_err_to_name(ret));
        }
        g_adc1_handle = NULL;
    }

    g_driver_initialized = false;
    ESP_LOGI(TAG, "Soil moisture driver deinitialized");

    return ESP_OK;
}

/**
 * @brief Leer valor raw ADC de un sensor específico
 */
static esp_err_t read_sensor_raw_adc(uint8_t sensor_number, int* raw_value) {
    if (!g_driver_initialized || !g_adc1_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    if (sensor_number < 1 || sensor_number > g_num_sensors) {
        ESP_LOGE(TAG, "Invalid sensor number: %d", sensor_number);
        return ESP_ERR_INVALID_ARG;
    }

    adc_channel_t channel = g_sensor_configs[sensor_number - 1].adc_channel;

    esp_err_t ret = adc_oneshot_read(g_adc1_handle, channel, raw_value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ADC channel %d: %s", channel, esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

/**
 * @brief Leer múltiples valores ADC para filtrado
 */
static esp_err_t read_multiple_samples(uint8_t sensor_number, int* samples, size_t num_samples) {
    for (size_t i = 0; i < num_samples; i++) {
        esp_err_t ret = read_sensor_raw_adc(sensor_number, &samples[i]);
        if (ret != ESP_OK) {
            return ret;
        }

        // Pequeña pausa entre muestras para estabilidad
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_OK;
}

/**
 * @brief Aplicar filtro mediano a las muestras
 */
static int apply_median_filter(int* samples, size_t num_samples) {
    // Crear copia temporal para no modificar original
    int temp_samples[num_samples];
    memcpy(temp_samples, samples, num_samples * sizeof(int));

    // Ordenamiento burbuja simple para arrays pequeños
    for (size_t i = 0; i < num_samples - 1; i++) {
        for (size_t j = 0; j < num_samples - i - 1; j++) {
            if (temp_samples[j] > temp_samples[j + 1]) {
                int temp = temp_samples[j];
                temp_samples[j] = temp_samples[j + 1];
                temp_samples[j + 1] = temp;
            }
        }
    }

    // Retornar valor mediano
    return temp_samples[num_samples / 2];
}

/**
 * @brief Detectar y remover outliers de las muestras
 */
static size_t remove_outliers(int* samples, size_t num_samples, int* filtered_samples) {
    if (num_samples < 3) {
        memcpy(filtered_samples, samples, num_samples * sizeof(int));
        return num_samples;
    }

    // Calcular media y desviación estándar simple
    long sum = 0;
    for (size_t i = 0; i < num_samples; i++) {
        sum += samples[i];
    }
    double mean = (double)sum / num_samples;

    double variance = 0;
    for (size_t i = 0; i < num_samples; i++) {
        double diff = samples[i] - mean;
        variance += diff * diff;
    }
    double std_dev = sqrt(variance / num_samples);

    // Filtrar valores que están más de 2 desviaciones estándar de la media
    size_t filtered_count = 0;
    double threshold = 2.0 * std_dev;

    for (size_t i = 0; i < num_samples; i++) {
        double diff = fabs(samples[i] - mean);
        if (diff <= threshold) {
            filtered_samples[filtered_count++] = samples[i];
        }
    }

    // Si quedan muy pocas muestras, usar todas
    if (filtered_count < num_samples / 2) {
        memcpy(filtered_samples, samples, num_samples * sizeof(int));
        return num_samples;
    }

    return filtered_count;
}

/**
 * @brief Calcular promedio de muestras
 */
static int calculate_average(int* samples, size_t num_samples) {
    if (num_samples == 0) return 0;

    long sum = 0;
    for (size_t i = 0; i < num_samples; i++) {
        sum += samples[i];
    }

    return (int)(sum / num_samples);
}

/**
 * @brief Convertir valor ADC raw a voltaje
 */
static esp_err_t convert_raw_to_voltage(int raw_value, int* voltage_mv) {
    if (!g_adc1_cali_handle) {
        // Sin calibración, usar conversión lineal simple
        *voltage_mv = (raw_value * 3300) / 4095;
        return ESP_OK;
    }

    return adc_cali_raw_to_voltage(g_adc1_cali_handle, raw_value, voltage_mv);
}

/**
 * @brief Convertir valor ADC a porcentaje de humedad
 */
static float convert_adc_to_percentage(int adc_value, uint8_t sensor_number) {
    soil_moisture_sensor_config_t* config = &g_sensor_configs[sensor_number - 1];

    // Mapear valor ADC usando calibración específica del sensor
    float percentage = map_value(
        (float)adc_value,
        (float)config->air_dry_value,      // Valor seco (0%)
        (float)config->water_saturated_value, // Valor húmedo (100%)
        0.0,                               // 0%
        100.0                              // 100%
    );

    // Limitar resultado a rango válido
    if (percentage < 0.0) percentage = 0.0;
    if (percentage > 100.0) percentage = 100.0;

    return percentage;
}

/**
 * @brief Aplicar compensación por temperatura
 */
static float apply_temperature_compensation(float humidity_percent, float temperature_celsius, uint8_t sensor_number) {
    soil_moisture_sensor_config_t* config = &g_sensor_configs[sensor_number - 1];

    if (!config->enable_temperature_compensation) {
        return humidity_percent;
    }

    // Factor de compensación basado en desviación de temperatura de referencia (25°C)
    float temp_deviation = temperature_celsius - 25.0;
    float compensation_factor = temp_deviation * config->temperature_coefficient;

    // Aplicar compensación
    float compensated_humidity = humidity_percent + compensation_factor;

    // Limitar a rango válido
    if (compensated_humidity < 0.0) compensated_humidity = 0.0;
    if (compensated_humidity > 100.0) compensated_humidity = 100.0;

    ESP_LOGD(TAG, "Sensor %d: Temp compensation: %.1f°C → %.1f%% → %.1f%%",
             sensor_number, temperature_celsius, humidity_percent, compensated_humidity);

    return compensated_humidity;
}

/**
 * @brief Leer un sensor individual con filtrado completo
 */
esp_err_t soil_moisture_driver_read_sensor(uint8_t sensor_number, float* humidity_percent, float temperature_celsius) {
    if (!g_driver_initialized) {
        ESP_LOGE(TAG, "Driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (sensor_number < 1 || sensor_number > g_num_sensors) {
        ESP_LOGE(TAG, "Invalid sensor number: %d", sensor_number);
        return ESP_ERR_INVALID_ARG;
    }

    if (!humidity_percent) {
        ESP_LOGE(TAG, "Null humidity_percent pointer");
        return ESP_ERR_INVALID_ARG;
    }

    soil_moisture_sensor_config_t* config = &g_sensor_configs[sensor_number - 1];
    soil_moisture_sensor_state_t* state = &g_sensor_states[sensor_number - 1];

    // Leer múltiples muestras para filtrado
    int raw_samples[MAX_SAMPLES_PER_READING];
    size_t num_samples = config->samples_per_reading;

    esp_err_t ret = read_multiple_samples(sensor_number, raw_samples, num_samples);
    if (ret != ESP_OK) {
        state->last_error = ret;
        state->consecutive_errors++;
        ESP_LOGE(TAG, "Failed to read sensor %d samples", sensor_number);
        return ret;
    }

    // Aplicar filtros según configuración
    int final_adc_value;

    if (config->enable_outlier_detection) {
        // Remover outliers
        int filtered_samples[MAX_SAMPLES_PER_READING];
        size_t filtered_count = remove_outliers(raw_samples, num_samples, filtered_samples);

        if (config->filter_type == SOIL_MOISTURE_FILTER_MEDIAN) {
            final_adc_value = apply_median_filter(filtered_samples, filtered_count);
        } else {
            final_adc_value = calculate_average(filtered_samples, filtered_count);
        }
    } else {
        // Sin detección de outliers
        if (config->filter_type == SOIL_MOISTURE_FILTER_MEDIAN) {
            final_adc_value = apply_median_filter(raw_samples, num_samples);
        } else {
            final_adc_value = calculate_average(raw_samples, num_samples);
        }
    }

    // Convertir ADC a porcentaje
    float humidity = convert_adc_to_percentage(final_adc_value, sensor_number);

    // Aplicar compensación por temperatura si está disponible
    if (temperature_celsius > -100.0) { // Verificar que la temperatura sea válida
        humidity = apply_temperature_compensation(humidity, temperature_celsius, sensor_number);
    }

    // Actualizar estado del sensor
    state->last_adc_value = final_adc_value;
    state->last_humidity_percent = humidity;
    state->last_reading_timestamp = esp_timer_get_time() / 1000; // ms
    state->consecutive_errors = 0;
    state->last_error = ESP_OK;
    state->total_readings++;

    // Convertir para estadísticas de voltaje
    int voltage_mv;
    convert_raw_to_voltage(final_adc_value, &voltage_mv);
    state->last_voltage_mv = voltage_mv;

    // Actualizar estadísticas globales
    g_statistics.total_readings++;

    *humidity_percent = humidity;

    ESP_LOGD(TAG, "Sensor %d: ADC=%d, Voltage=%dmV, Humidity=%.1f%%",
             sensor_number, final_adc_value, voltage_mv, humidity);

    return ESP_OK;
}

/**
 * @brief Leer todos los sensores configurados
 */
esp_err_t soil_moisture_driver_read_all_sensors(soil_sensor_data_t* sensor_data, float temperature_celsius) {
    if (!g_driver_initialized) {
        ESP_LOGE(TAG, "Driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!sensor_data) {
        ESP_LOGE(TAG, "Null sensor_data pointer");
        return ESP_ERR_INVALID_ARG;
    }

    // Inicializar estructura de salida
    memset(sensor_data, 0, sizeof(soil_sensor_data_t));
    sensor_data->timestamp = esp_timer_get_time() / 1000000; // segundos desde boot

    esp_err_t overall_result = ESP_OK;
    bool any_sensor_read = false;

    // Leer cada sensor configurado
    for (uint8_t i = 0; i < g_num_sensors; i++) {
        uint8_t sensor_number = i + 1;
        float humidity = 0.0;

        esp_err_t ret = soil_moisture_driver_read_sensor(sensor_number, &humidity, temperature_celsius);

        if (ret == ESP_OK) {
            any_sensor_read = true;

            // Asignar a los campos correspondientes según el número de sensor
            switch (sensor_number) {
                case 1:
                    sensor_data->soil_humidity_1 = humidity;
                    break;
                case 2:
                    sensor_data->soil_humidity_2 = humidity;
                    break;
                case 3:
                    sensor_data->soil_humidity_3 = humidity;
                    break;
            }
        } else {
            ESP_LOGW(TAG, "Failed to read sensor %d: %s", sensor_number, esp_err_to_name(ret));
            overall_result = ret; // Mantener último error
        }
    }

    if (!any_sensor_read) {
        ESP_LOGE(TAG, "Failed to read any soil moisture sensor");
        g_statistics.total_failed_readings++;
        return overall_result;
    }

    g_statistics.successful_readings++;

    ESP_LOGI(TAG, "Soil sensors read: 1=%.1f%% 2=%.1f%% 3=%.1f%%",
             sensor_data->soil_humidity_1,
             sensor_data->soil_humidity_2,
             sensor_data->soil_humidity_3);

    return ESP_OK;
}

/**
 * @brief Calibrar sensor en condición de aire seco (0% humedad)
 */
esp_err_t soil_moisture_driver_calibrate_air_dry(uint8_t sensor_number, int* calibrated_value) {
    if (!g_driver_initialized) {
        ESP_LOGE(TAG, "Driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (sensor_number < 1 || sensor_number > g_num_sensors) {
        ESP_LOGE(TAG, "Invalid sensor number: %d", sensor_number);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting air-dry calibration for sensor %d", sensor_number);
    ESP_LOGI(TAG, "Please ensure sensor %d is completely dry and press continue...", sensor_number);

    // Leer múltiples muestras para calibración precisa
    int samples[20];
    esp_err_t ret = read_multiple_samples(sensor_number, samples, 20);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read calibration samples");
        return ret;
    }

    // Usar filtro mediano para calibración robusta
    int air_dry_value = apply_median_filter(samples, 20);

    // Actualizar configuración del sensor
    g_sensor_configs[sensor_number - 1].air_dry_value = air_dry_value;

    // Convertir a voltaje para referencia
    int voltage_mv;
    convert_raw_to_voltage(air_dry_value, &voltage_mv);

    ESP_LOGI(TAG, "Sensor %d air-dry calibration completed: ADC=%d, Voltage=%dmV",
             sensor_number, air_dry_value, voltage_mv);

    if (calibrated_value) {
        *calibrated_value = air_dry_value;
    }

    return ESP_OK;
}

/**
 * @brief Calibrar sensor en condición saturada de agua (100% humedad)
 */
esp_err_t soil_moisture_driver_calibrate_water_saturated(uint8_t sensor_number, int* calibrated_value) {
    if (!g_driver_initialized) {
        ESP_LOGE(TAG, "Driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (sensor_number < 1 || sensor_number > g_num_sensors) {
        ESP_LOGE(TAG, "Invalid sensor number: %d", sensor_number);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting water-saturated calibration for sensor %d", sensor_number);
    ESP_LOGI(TAG, "Please immerse sensor %d in water and press continue...", sensor_number);

    // Leer múltiples muestras para calibración precisa
    int samples[20];
    esp_err_t ret = read_multiple_samples(sensor_number, samples, 20);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read calibration samples");
        return ret;
    }

    // Usar filtro mediano para calibración robusta
    int water_saturated_value = apply_median_filter(samples, 20);

    // Actualizar configuración del sensor
    g_sensor_configs[sensor_number - 1].water_saturated_value = water_saturated_value;

    // Convertir a voltaje para referencia
    int voltage_mv;
    convert_raw_to_voltage(water_saturated_value, &voltage_mv);

    ESP_LOGI(TAG, "Sensor %d water-saturated calibration completed: ADC=%d, Voltage=%dmV",
             sensor_number, water_saturated_value, voltage_mv);

    if (calibrated_value) {
        *calibrated_value = water_saturated_value;
    }

    return ESP_OK;
}

/**
 * @brief Obtener configuración de un sensor
 */
esp_err_t soil_moisture_driver_get_sensor_config(uint8_t sensor_number, soil_moisture_sensor_config_t* config) {
    if (!g_driver_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (sensor_number < 1 || sensor_number > g_num_sensors || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(config, &g_sensor_configs[sensor_number - 1], sizeof(soil_moisture_sensor_config_t));
    return ESP_OK;
}

/**
 * @brief Establecer configuración de un sensor
 */
esp_err_t soil_moisture_driver_set_sensor_config(uint8_t sensor_number, const soil_moisture_sensor_config_t* config) {
    if (!g_driver_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (sensor_number < 1 || sensor_number > g_num_sensors || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    // Validar configuración
    if (config->samples_per_reading < 1 || config->samples_per_reading > MAX_SAMPLES_PER_READING) {
        ESP_LOGE(TAG, "Invalid samples_per_reading: %d", config->samples_per_reading);
        return ESP_ERR_INVALID_ARG;
    }

    if (config->filter_type >= SOIL_MOISTURE_FILTER_MAX) {
        ESP_LOGE(TAG, "Invalid filter_type: %d", config->filter_type);
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&g_sensor_configs[sensor_number - 1], config, sizeof(soil_moisture_sensor_config_t));

    ESP_LOGI(TAG, "Sensor %d configuration updated", sensor_number);
    return ESP_OK;
}

/**
 * @brief Obtener estado de un sensor
 */
esp_err_t soil_moisture_driver_get_sensor_state(uint8_t sensor_number, soil_moisture_sensor_state_t* state) {
    if (!g_driver_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (sensor_number < 1 || sensor_number > g_num_sensors || !state) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(state, &g_sensor_states[sensor_number - 1], sizeof(soil_moisture_sensor_state_t));
    return ESP_OK;
}

/**
 * @brief Obtener estadísticas del driver
 */
esp_err_t soil_moisture_driver_get_statistics(soil_moisture_statistics_t* stats) {
    if (!g_driver_initialized || !stats) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(stats, &g_statistics, sizeof(soil_moisture_statistics_t));
    return ESP_OK;
}

/**
 * @brief Resetear estadísticas del driver
 */
esp_err_t soil_moisture_driver_reset_statistics(void) {
    if (!g_driver_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&g_statistics, 0, sizeof(soil_moisture_statistics_t));
    ESP_LOGI(TAG, "Driver statistics reset");
    return ESP_OK;
}

/**
 * @brief Obtener información de salud de un sensor
 */
bool soil_moisture_driver_is_sensor_healthy(uint8_t sensor_number) {
    if (!g_driver_initialized || sensor_number < 1 || sensor_number > g_num_sensors) {
        return false;
    }

    soil_moisture_sensor_state_t* state = &g_sensor_states[sensor_number - 1];

    // Considerar sensor saludable si:
    // 1. No tiene errores consecutivos excesivos
    // 2. Ha tenido lecturas exitosas recientemente
    // 3. Los valores están en rango razonable

    if (state->consecutive_errors > 5) {
        return false;
    }

    if (state->total_readings == 0) {
        return false;
    }

    // Verificar que la última lectura sea reciente (últimos 5 minutos)
    uint64_t current_time = esp_timer_get_time() / 1000; // ms
    if (current_time - state->last_reading_timestamp > 300000) { // 5 min = 300000 ms
        return false;
    }

    return true;
}

/**
 * @brief Verificar si todos los sensores están saludables
 */
bool soil_moisture_driver_are_all_sensors_healthy(void) {
    if (!g_driver_initialized) {
        return false;
    }

    for (uint8_t i = 0; i < g_num_sensors; i++) {
        if (!soil_moisture_driver_is_sensor_healthy(i + 1)) {
            return false;
        }
    }

    return true;
}

/**
 * @brief Obtener configuración global del driver
 */
esp_err_t soil_moisture_driver_get_global_config(soil_moisture_driver_config_t* config) {
    if (!g_driver_initialized || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(config, &g_global_config, sizeof(soil_moisture_driver_config_t));
    return ESP_OK;
}

/**
 * @brief Establecer configuración global del driver
 */
esp_err_t soil_moisture_driver_set_global_config(const soil_moisture_driver_config_t* config) {
    if (!g_driver_initialized || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&g_global_config, config, sizeof(soil_moisture_driver_config_t));
    ESP_LOGI(TAG, "Global configuration updated");
    return ESP_OK;
}

/**
 * @brief Obtener número de sensores configurados
 */
uint8_t soil_moisture_driver_get_sensor_count(void) {
    return g_driver_initialized ? g_num_sensors : 0;
}

/**
 * @brief Verificar si el driver está inicializado
 */
bool soil_moisture_driver_is_initialized(void) {
    return g_driver_initialized;
}

/**
 * @brief Convertir tipo de filtro a string para debugging
 */
const char* soil_moisture_driver_filter_type_to_string(soil_moisture_filter_type_t filter_type) {
    switch (filter_type) {
        case SOIL_MOISTURE_FILTER_NONE:
            return "NONE";
        case SOIL_MOISTURE_FILTER_AVERAGE:
            return "AVERAGE";
        case SOIL_MOISTURE_FILTER_MEDIAN:
            return "MEDIAN";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Imprimir información de debug de un sensor
 */
void soil_moisture_driver_print_sensor_info(uint8_t sensor_number) {
    if (!g_driver_initialized || sensor_number < 1 || sensor_number > g_num_sensors) {
        ESP_LOGW(TAG, "Cannot print info for invalid sensor %d", sensor_number);
        return;
    }

    soil_moisture_sensor_config_t* config = &g_sensor_configs[sensor_number - 1];
    soil_moisture_sensor_state_t* state = &g_sensor_states[sensor_number - 1];

    ESP_LOGI(TAG, "=== SENSOR %d INFO ===", sensor_number);
    ESP_LOGI(TAG, "ADC Channel: %d", config->adc_channel);
    ESP_LOGI(TAG, "Air-dry value: %d", config->air_dry_value);
    ESP_LOGI(TAG, "Water-sat value: %d", config->water_saturated_value);
    ESP_LOGI(TAG, "Samples per reading: %d", config->samples_per_reading);
    ESP_LOGI(TAG, "Filter type: %s", soil_moisture_driver_filter_type_to_string(config->filter_type));
    ESP_LOGI(TAG, "Outlier detection: %s", config->enable_outlier_detection ? "ON" : "OFF");
    ESP_LOGI(TAG, "Temp compensation: %s", config->enable_temperature_compensation ? "ON" : "OFF");
    ESP_LOGI(TAG, "Temp coefficient: %.4f", config->temperature_coefficient);
    ESP_LOGI(TAG, "Last ADC value: %d", state->last_adc_value);
    ESP_LOGI(TAG, "Last humidity: %.1f%%", state->last_humidity_percent);
    ESP_LOGI(TAG, "Last voltage: %dmV", state->last_voltage_mv);
    ESP_LOGI(TAG, "Total readings: %" PRIu32, state->total_readings);
    ESP_LOGI(TAG, "Consecutive errors: %d", state->consecutive_errors);
    ESP_LOGI(TAG, "Healthy: %s", soil_moisture_driver_is_sensor_healthy(sensor_number) ? "YES" : "NO");
    ESP_LOGI(TAG, "===================");
}

/**
 * @brief Imprimir estadísticas generales del driver
 */
void soil_moisture_driver_print_statistics(void) {
    if (!g_driver_initialized) {
        ESP_LOGW(TAG, "Driver not initialized");
        return;
    }

    ESP_LOGI(TAG, "=== DRIVER STATISTICS ===");
    ESP_LOGI(TAG, "Total readings: %" PRIu32, g_statistics.total_readings);
    ESP_LOGI(TAG, "Successful readings: %" PRIu32, g_statistics.successful_readings);
    ESP_LOGI(TAG, "Failed readings: %" PRIu32, g_statistics.total_failed_readings);
    ESP_LOGI(TAG, "Success rate: %.1f%%",
             g_statistics.total_readings > 0 ?
             (float)g_statistics.successful_readings / g_statistics.total_readings * 100.0 : 0.0);
    ESP_LOGI(TAG, "Configured sensors: %d", g_num_sensors);
    ESP_LOGI(TAG, "All sensors healthy: %s", soil_moisture_driver_are_all_sensors_healthy() ? "YES" : "NO");
    ESP_LOGI(TAG, "========================");
}