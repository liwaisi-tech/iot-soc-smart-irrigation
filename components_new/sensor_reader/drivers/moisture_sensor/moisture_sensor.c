#include "moisture_sensor.h"
#include "esp_log.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "Moisture sensor";
static adc_oneshot_unit_handle_t adc_handle;
static bool adc_initialized = false;


#define VALUE_WHEN_DRY_CAP 2865  // Valor cuando el sensor Capacitio está seco
#define VALUE_WHEN_WET_CAP 1220 // Valor cuando el sensor capacitivo está en agua
#define VALUE_WHEN_DRY_YL 3096  // Valor cuando el sensor YL está seco
#define VALUE_WHEN_WET_YL 800 // Valor cuando el sensor YL está en agua
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