/*
 * Componente de ESP32 
 * Driver Lectura sensor de humedad del suelo YL-69, HL-69 o sensor capacitivo
 * Copyright 2024, Briggitte Castañeda <btatacc@gmail.com>
 */
#ifndef MOISTURE_SENSOR_H
#define MOISTURE_SENSOR_H
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

typedef enum
{
    TYPE_YL69 = 0,   //!< YL69
    TYPE_CAP,      //!< Capacitivo
} groud_sensor_type_t;

typedef struct {
    adc_channel_t channel;
    adc_bitwidth_t bitwidth;
    adc_atten_t atten;
    adc_unit_t unit; 
    uint32_t read_interval_ms;  
    groud_sensor_type_t sensor_type;
} moisture_sensor_config_t;

#define SENSOR_DEFAULT_CONFIG { \
    .channel = ADC_CHANNEL_6,        \
    .bitwidth = ADC_BITWIDTH_12,     \
    .atten = ADC_ATTEN_DB_12,        \
    .unit = ADC_UNIT_1,              \
    .read_interval_ms = 5000,        \
     .sensor_type = TYPE_YL69,  \
}

esp_err_t moisture_sensor_init(moisture_sensor_config_t *config);
int sensor_read_raw(adc_channel_t channel);
void sensor_read_percentage(adc_channel_t channel, int *humidity, groud_sensor_type_t sensor_type);

/**
 * @brief Read soil moisture with RAW ADC value
 *
 * Returns both the calculated humidity percentage and the raw ADC value
 * for calibration purposes. This function is useful for determining
 * calibration values in field conditions.
 *
 * @param channel ADC channel to read
 * @param[out] humidity Calculated humidity percentage (0-100%)
 * @param[out] raw_adc Raw ADC value (0-4095)
 * @param sensor_type Sensor type (TYPE_CAP or TYPE_YL69)
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t sensor_read_with_raw(
    adc_channel_t channel,
    int *humidity,
    int *raw_adc,
    groud_sensor_type_t sensor_type
);

#endif // MOISTURE_SENSOR_H