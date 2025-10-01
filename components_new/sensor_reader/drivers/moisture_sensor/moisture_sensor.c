#include "moisture_sensor.h"
#include "esp_log.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "Moisture sensor";
static adc_oneshot_unit_handle_t adc_handle;
static bool adc_initialized = false;

// ============================================================================
// CALIBRATION VALUES - Manual Configuration
// ============================================================================
// These values should be updated based on field measurements using DEBUG logs.
//
// HOW TO CALIBRATE:
// 1. Enable DEBUG logs: idf.py menuconfig → Component config → Log output → Debug
// 2. Install sensor in field and monitor logs for 5 minutes
// 3. Note RAW_ADC values in dry conditions → Update VALUE_WHEN_DRY_CAP
// 4. Water soil heavily and wait 30 minutes
// 5. Note RAW_ADC values in saturated conditions → Update VALUE_WHEN_WET_CAP
// 6. Recompile and flash firmware
//
// FUTURE PHASE 2: These will be configurable via WiFi Provisioning UI and Raspberry API
//                 Values will be stored in NVS for persistence across reboots
// ============================================================================

#define VALUE_WHEN_DRY_CAP 2865  // Capacitive sensor in dry soil (air reading)
#define VALUE_WHEN_WET_CAP 1220  // Capacitive sensor in wet soil (water reading)
#define VALUE_WHEN_DRY_YL 3096   // YL-69 sensor in dry soil
#define VALUE_WHEN_WET_YL 800    // YL-69 sensor in wet soil
#define HUMIDITY_MAX 100
#define HUMIDITY_MIN 0

static int map_value(int value, int value_when_dry, int value_when_wet) {

    return (value - value_when_dry) * (HUMIDITY_MAX - HUMIDITY_MIN) / 
           (value_when_wet - value_when_dry) + HUMIDITY_MIN;
}

esp_err_t moisture_sensor_init(moisture_sensor_config_t *config) {
    if (!adc_initialized) {
        // Inicializar ADC solo una vez
        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = config->unit,
            .ulp_mode = false
        };
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));
        adc_initialized = true;
    }
    
    // Configurar el canal específico
    adc_oneshot_chan_cfg_t chan_config = {
        .atten = config->atten,
        .bitwidth = config->bitwidth
    };
    
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, config->channel, &chan_config));
    return ESP_OK;
}

int sensor_read_raw(adc_channel_t channel) {
    if (!adc_initialized) {
        ESP_LOGE(TAG, "YL69 no inicializado");
        return -1;
    }

    int raw_value;
    esp_err_t ret = adc_oneshot_read(adc_handle, channel, &raw_value);
    // Manejo de errores
    if (ret == ESP_ERR_TIMEOUT) {
        ESP_LOGE(TAG, "Error de tiempo de espera al leer el ADC en el canal %d", channel);
        return -1; // O manejar el error de otra manera
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al leer el ADC: %d", ret);
        return -1; // O manejar el error de otra manera
    }

    return raw_value;
}

void sensor_read_percentage(adc_channel_t channel, int *humidity, groud_sensor_type_t sensor_type) {

    int value_when_dry;
    int value_when_wet;
    if (sensor_type==TYPE_YL69){
        value_when_dry = VALUE_WHEN_DRY_YL;
        value_when_wet = VALUE_WHEN_WET_YL;
    }
    else{
        value_when_dry = VALUE_WHEN_DRY_CAP;
        value_when_wet = VALUE_WHEN_WET_CAP;
    }
    *humidity = map_value(sensor_read_raw(channel),value_when_dry,value_when_wet);
}

esp_err_t sensor_read_with_raw(
    adc_channel_t channel,
    int *humidity,
    int *raw_adc,
    groud_sensor_type_t sensor_type)
{
    if (humidity == NULL || raw_adc == NULL) {
        ESP_LOGE(TAG, "NULL pointer in sensor_read_with_raw");
        return ESP_ERR_INVALID_ARG;
    }

    // Leer valor RAW del ADC
    int raw_value = sensor_read_raw(channel);
    if (raw_value < 0) {
        ESP_LOGE(TAG, "Failed to read ADC channel %d", channel);
        return ESP_FAIL;
    }

    *raw_adc = raw_value;

    // Seleccionar valores de calibración según tipo de sensor
    int value_when_dry;
    int value_when_wet;

    if (sensor_type == TYPE_YL69) {
        value_when_dry = VALUE_WHEN_DRY_YL;
        value_when_wet = VALUE_WHEN_WET_YL;
    } else {
        value_when_dry = VALUE_WHEN_DRY_CAP;
        value_when_wet = VALUE_WHEN_WET_CAP;
    }

    // Calcular porcentaje con mapeo lineal
    *humidity = map_value(raw_value, value_when_dry, value_when_wet);

    // Clamp al rango válido (0-100%)
    if (*humidity < 0) *humidity = 0;
    if (*humidity > 100) *humidity = 100;

    return ESP_OK;
}