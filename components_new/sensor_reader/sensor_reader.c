/**
 * @file sensor_reader.c
 * @brief Sensor Reader Component - Unified Sensor Interface Implementation
 *
 * Integrates DHT22 environmental sensor and soil moisture sensors
 * with health monitoring and error tracking.
 *
 * @author Liwaisi Tech
 * @date 2025-10-01
 * @version 2.0.0 - Component-Based Architecture
 */

#include "sensor_reader.h"
#include "dht.h"                    // Driver DHT22
#include "moisture_sensor.h"        // Driver sensores suelo
#include "esp_log.h"
#include "esp_mac.h"                // Para MAC address
#include "esp_netif.h"              // Para IP address
#include <string.h>
#include <time.h>

static const char *TAG = "sensor_reader";

/* ============================ ESTADO INTERNO ============================ */

static bool s_initialized = false;
static sensor_config_t s_config;
static sensor_health_t s_sensor_health[SENSOR_TYPE_COUNT];
static uint32_t s_total_readings = 0;
static uint32_t s_reading_id = 0;

/* ============================ CONSTANTES ============================ */

// Canales ADC para sensores de suelo (common_types.h línea 226-228)
static const adc_channel_t SOIL_ADC_CHANNELS[3] = {
    ADC_CHANNEL_0,  // GPIO 36 (ADC_SOIL_SENSOR_1)
    ADC_CHANNEL_3,  // GPIO 39 (ADC_SOIL_SENSOR_2)
    ADC_CHANNEL_6   // GPIO 34 (ADC_SOIL_SENSOR_3)
};

/* ============================ IMPLEMENTACIÓN API PÚBLICA ============================ */

esp_err_t sensor_reader_init(const sensor_config_t* config)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Sensor reader already initialized");
        return ESP_OK;
    }

    // Usar configuración por defecto si es NULL
    if (config == NULL) {
        s_config = (sensor_config_t)SENSOR_READER_DEFAULT_CONFIG();
    } else {
        memcpy(&s_config, config, sizeof(sensor_config_t));
    }

    ESP_LOGI(TAG, "Initializing sensor reader component...");

    // Inicializar health tracking para todos los sensores
    for (int i = 0; i < SENSOR_TYPE_COUNT; i++) {
        s_sensor_health[i].type = (sensor_type_t)i;
        s_sensor_health[i].is_healthy = true;
        s_sensor_health[i].error_count = 0;
        s_sensor_health[i].total_reads = 0;
        s_sensor_health[i].successful_reads = 0;
        s_sensor_health[i].last_read_time = 0;
        s_sensor_health[i].last_value = 0.0f;
    }

    // Inicializar sensores de humedad del suelo
    ESP_LOGI(TAG, "Initializing %d soil moisture sensors...", s_config.soil_sensor_count);
    for (uint8_t i = 0; i < s_config.soil_sensor_count && i < 3; i++) {
        moisture_sensor_config_t soil_cfg = {
            .channel = SOIL_ADC_CHANNELS[i],
            .bitwidth = ADC_BITWIDTH_12,
            .atten = s_config.adc_attenuation,
            .unit = ADC_UNIT_1,
            .read_interval_ms = 1000,
            .sensor_type = TYPE_CAP  // Sensor capacitivo
        };

        esp_err_t ret = moisture_sensor_init(&soil_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init soil sensor %d on ADC channel %d: %s",
                     i, SOIL_ADC_CHANNELS[i], esp_err_to_name(ret));
            s_sensor_health[SENSOR_TYPE_SOIL_1 + i].is_healthy = false;
        } else {
            ESP_LOGI(TAG, "Soil sensor %d initialized successfully (ADC channel %d)",
                     i, SOIL_ADC_CHANNELS[i]);
        }
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Sensor reader initialized: DHT22 GPIO=%d, Soil sensors=%d",
             s_config.dht22_gpio, s_config.soil_sensor_count);

    return ESP_OK;
}

esp_err_t sensor_reader_deinit(void)
{
    if (!s_initialized) {
        ESP_LOGW(TAG, "Sensor reader not initialized");
        return ESP_OK;
    }

    s_initialized = false;
    s_total_readings = 0;
    s_reading_id = 0;

    ESP_LOGI(TAG, "Sensor reader deinitialized");
    return ESP_OK;
}

esp_err_t sensor_reader_get_ambient(ambient_data_t* data)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Sensor reader not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (data == NULL) {
        ESP_LOGE(TAG, "ambient_data_t pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Actualizar estadísticas de lectura
    s_sensor_health[SENSOR_TYPE_DHT22].total_reads++;

    // Llamar driver DHT22 con retries automáticos
    // IMPORTANTE: dht_read_ambient_data() ya retorna ambient_data_t directamente
    esp_err_t ret = dht_read_ambient_data(
        DHT_TYPE_AM2301,                    // DHT22 sensor type
        (gpio_num_t)s_config.dht22_gpio,    // GPIO configurado
        data,                                // Puntero a ambient_data_t
        0                                    // 0 = usar DHT_MAX_RETRIES (3 intentos)
    );

    if (ret == ESP_OK) {
        // Lectura exitosa - actualizar health tracking
        s_sensor_health[SENSOR_TYPE_DHT22].successful_reads++;
        s_sensor_health[SENSOR_TYPE_DHT22].error_count = 0;
        s_sensor_health[SENSOR_TYPE_DHT22].is_healthy = true;
        s_sensor_health[SENSOR_TYPE_DHT22].last_read_time = data->timestamp;
        s_sensor_health[SENSOR_TYPE_DHT22].last_value = data->temperature;

        ESP_LOGD(TAG, "DHT22 reading: T=%.1f°C, H=%.1f%%",
                 data->temperature, data->humidity);
    } else {
        // Lectura falló - incrementar contador de errores
        s_sensor_health[SENSOR_TYPE_DHT22].error_count++;

        // Marcar como no saludable si supera el límite de errores
        if (s_sensor_health[SENSOR_TYPE_DHT22].error_count >= s_config.max_consecutive_errors) {
            s_sensor_health[SENSOR_TYPE_DHT22].is_healthy = false;
            ESP_LOGE(TAG, "DHT22 marked unhealthy after %d consecutive errors",
                     s_sensor_health[SENSOR_TYPE_DHT22].error_count);
        }

        ESP_LOGW(TAG, "DHT22 read failed: %s (error count: %d)",
                 esp_err_to_name(ret),
                 s_sensor_health[SENSOR_TYPE_DHT22].error_count);
    }

    return ret;
}

esp_err_t sensor_reader_get_soil(soil_data_t* data)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Sensor reader not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (data == NULL) {
        ESP_LOGE(TAG, "soil_data_t pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Inicializar estructura de salida
    memset(data, 0, sizeof(soil_data_t));
    data->sensor_count = 0;
    data->timestamp = (uint32_t)time(NULL);

    uint8_t successful_reads = 0;

    // Buffers para logs compactos (DEBUG level)
    int raw_values[3] = {0};
    int humidity_values[3] = {0};

    // Leer cada sensor de suelo configurado
    for (uint8_t i = 0; i < s_config.soil_sensor_count && i < 3; i++) {
        sensor_type_t sensor_type = (sensor_type_t)(SENSOR_TYPE_SOIL_1 + i);
        s_sensor_health[sensor_type].total_reads++;

        int humidity_percent = 0;
        int raw_adc = 0;

        // Llamar nueva API que retorna RAW + porcentaje
        esp_err_t ret = sensor_read_with_raw(
            SOIL_ADC_CHANNELS[i],
            &humidity_percent,
            &raw_adc,
            TYPE_CAP
        );

        // Guardar valores para log compacto
        raw_values[i] = raw_adc;
        humidity_values[i] = humidity_percent;

        // Validar resultado
        if (ret == ESP_OK && humidity_percent >= 0 && humidity_percent <= 100) {
            // Lectura válida
            data->soil_humidity[i] = (float)humidity_percent;
            successful_reads++;

            // Actualizar health tracking
            s_sensor_health[sensor_type].successful_reads++;
            s_sensor_health[sensor_type].error_count = 0;
            s_sensor_health[sensor_type].is_healthy = true;
            s_sensor_health[sensor_type].last_value = (float)humidity_percent;
            s_sensor_health[sensor_type].last_read_time = data->timestamp;
        } else {
            // Lectura inválida
            data->soil_humidity[i] = 0.0f;
            s_sensor_health[sensor_type].error_count++;

            // Marcar como no saludable si supera el límite
            if (s_sensor_health[sensor_type].error_count >= s_config.max_consecutive_errors) {
                s_sensor_health[sensor_type].is_healthy = false;
                ESP_LOGE(TAG, "Soil sensor %d marked unhealthy after %d errors",
                         i, s_sensor_health[sensor_type].error_count);
            }

            ESP_LOGW(TAG, "Soil sensor %d invalid reading: RAW=%d, %%=%d (error count: %d)",
                     i, raw_adc, humidity_percent, s_sensor_health[sensor_type].error_count);
        }
    }

    data->sensor_count = successful_reads;

    // ============================================================================
    // DEBUG LOG - Formato Compacto (solo visible con nivel DEBUG activo)
    // ============================================================================
    // Mostrar valores RAW para calibración manual
    // Para activar: idf.py menuconfig → Component config → Log output → Debug
    ESP_LOGD(TAG, "Soil sensors: [RAW: %d/%d/%d] [%%: %d/%d/%d]",
             raw_values[0], raw_values[1], raw_values[2],
             humidity_values[0], humidity_values[1], humidity_values[2]);

    // Retornar error solo si TODOS los sensores fallaron
    if (successful_reads == 0) {
        ESP_LOGE(TAG, "All soil sensors failed to read");
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGD(TAG, "Soil sensors read: %d/%d successful",
             successful_reads, s_config.soil_sensor_count);

    return ESP_OK;
}

esp_err_t sensor_reader_get_all(sensor_reading_t* reading)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Sensor reader not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (reading == NULL) {
        ESP_LOGE(TAG, "sensor_reading_t pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Inicializar estructura completa
    memset(reading, 0, sizeof(sensor_reading_t));

    // 1. Leer sensor ambiental (DHT22)
    esp_err_t ambient_ret = sensor_reader_get_ambient(&reading->ambient);

    // 2. Leer sensores de suelo
    esp_err_t soil_ret = sensor_reader_get_soil(&reading->soil);

    // 3. Obtener MAC address del dispositivo
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(reading->device_mac, sizeof(reading->device_mac),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // 4. Obtener IP address (si está conectado a WiFi)
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif != NULL) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            snprintf(reading->device_ip, sizeof(reading->device_ip),
                     IPSTR, IP2STR(&ip_info.ip));
        } else {
            strcpy(reading->device_ip, "0.0.0.0");
        }
    } else {
        strcpy(reading->device_ip, "0.0.0.0");
    }

    // 5. Asignar ID de lectura secuencial
    reading->reading_id = ++s_reading_id;

    // 6. Incrementar contador total de lecturas
    s_total_readings++;

    // Retornar éxito si AL MENOS UNO de los sensores funcionó
    if (ambient_ret == ESP_OK || soil_ret == ESP_OK) {
        ESP_LOGD(TAG, "Reading #%u complete (ambient:%s, soil:%s)",
                 reading->reading_id,
                 ambient_ret == ESP_OK ? "OK" : "FAIL",
                 soil_ret == ESP_OK ? "OK" : "FAIL");
        return ESP_OK;
    }

    // Ambos sensores fallaron
    ESP_LOGE(TAG, "Complete sensor reading failed - all sensors returned errors");
    return ESP_ERR_INVALID_RESPONSE;
}

esp_err_t sensor_reader_get_status(sensor_status_t* status)
{
    if (status == NULL) {
        ESP_LOGE(TAG, "sensor_status_t pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Llenar estructura de status
    status->state = s_initialized ? SENSOR_STATE_READY : SENSOR_STATE_UNINITIALIZED;
    status->total_readings = s_total_readings;
    status->last_reading_time = s_sensor_health[SENSOR_TYPE_DHT22].last_read_time;
    status->all_sensors_healthy = sensor_reader_is_healthy();

    // Copiar estado de salud de todos los sensores
    memcpy(status->sensors, s_sensor_health, sizeof(s_sensor_health));

    return ESP_OK;
}

bool sensor_reader_is_healthy(void)
{
    if (!s_initialized) {
        return false;
    }

    // Criterio de salud: al menos el DHT22 O un sensor de suelo debe estar saludable
    bool dht22_healthy = s_sensor_health[SENSOR_TYPE_DHT22].is_healthy;
    bool any_soil_healthy = false;

    for (uint8_t i = 0; i < s_config.soil_sensor_count && i < 3; i++) {
        if (s_sensor_health[SENSOR_TYPE_SOIL_1 + i].is_healthy) {
            any_soil_healthy = true;
            break;
        }
    }

    return dht22_healthy || any_soil_healthy;
}

esp_err_t sensor_reader_get_sensor_health(sensor_type_t type, sensor_health_t* health)
{
    if (health == NULL) {
        ESP_LOGE(TAG, "sensor_health_t pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (type >= SENSOR_TYPE_COUNT) {
        ESP_LOGE(TAG, "Invalid sensor type: %d", type);
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(health, &s_sensor_health[type], sizeof(sensor_health_t));
    return ESP_OK;
}

esp_err_t sensor_reader_reset_errors(sensor_type_t type)
{
    if (type == SENSOR_TYPE_COUNT) {
        // Resetear todos los sensores
        for (int i = 0; i < SENSOR_TYPE_COUNT; i++) {
            s_sensor_health[i].error_count = 0;
            s_sensor_health[i].is_healthy = true;
        }
        ESP_LOGI(TAG, "Error counters reset for all sensors");
    } else if (type < SENSOR_TYPE_COUNT) {
        // Resetear sensor específico
        s_sensor_health[type].error_count = 0;
        s_sensor_health[type].is_healthy = true;
        ESP_LOGI(TAG, "Error counter reset for sensor type %d", type);
    } else {
        ESP_LOGE(TAG, "Invalid sensor type: %d", type);
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

esp_err_t sensor_reader_calibrate_soil(uint8_t sensor_index,
                                       uint16_t dry_mv,
                                       uint16_t wet_mv)
{
    if (sensor_index >= 3) {
        ESP_LOGE(TAG, "Invalid sensor index: %d (max 2)", sensor_index);
        return ESP_ERR_INVALID_ARG;
    }

    if (dry_mv <= wet_mv) {
        ESP_LOGE(TAG, "Invalid calibration: dry_mv (%d) must be > wet_mv (%d)",
                 dry_mv, wet_mv);
        return ESP_ERR_INVALID_ARG;
    }

    s_config.soil_cal_dry_mv[sensor_index] = dry_mv;
    s_config.soil_cal_wet_mv[sensor_index] = wet_mv;

    ESP_LOGI(TAG, "Soil sensor %d calibrated: dry=%d mV, wet=%d mV",
             sensor_index, dry_mv, wet_mv);

    return ESP_OK;
}

esp_err_t sensor_reader_get_soil_calibration(uint8_t sensor_index,
                                             uint16_t* dry_mv,
                                             uint16_t* wet_mv)
{
    if (sensor_index >= 3) {
        ESP_LOGE(TAG, "Invalid sensor index: %d (max 2)", sensor_index);
        return ESP_ERR_INVALID_ARG;
    }

    if (dry_mv == NULL || wet_mv == NULL) {
        ESP_LOGE(TAG, "Calibration pointers are NULL");
        return ESP_ERR_INVALID_ARG;
    }

    *dry_mv = s_config.soil_cal_dry_mv[sensor_index];
    *wet_mv = s_config.soil_cal_wet_mv[sensor_index];

    return ESP_OK;
}
