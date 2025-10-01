/*
 * Copyright (c) 2016 Jonathan Hartsuiker <https://github.com/jsuiker>
 * Copyright (c) 2018 Ruslan V. Uss <unclerus@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of itscontributors
 *    may be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file dht.h
 * @defgroup dht dht
 * @{
 *
 * ESP-IDF driver for DHT11, AM2301 (DHT21, DHT22, AM2302, AM2321), Itead Si7021
 *
 * Ported from esp-open-rtos
 *
 * Copyright (c) 2016 Jonathan Hartsuiker <https://github.com/jsuiker>\n
 * Copyright (c) 2018 Ruslan V. Uss <unclerus@gmail.com>\n
 *
 * BSD Licensed as described in the file LICENSE
 *
 * @note A suitable pull-up resistor should be connected to the selected GPIO line
 *
 */
#ifndef __DHT_H__
#define __DHT_H__

#include <driver/gpio.h>
#include <esp_err.h>
#include "common_types.h"  // For ambient_data_t (Component-Based Architecture)

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ RETRY CONFIGURATION ============================ */

/**
 * @brief Maximum number of retry attempts for DHT22 sensor reading
 *
 * DHT22 sensors can fail up to 50% of reads in rural conditions with
 * electromagnetic interference. Retry logic significantly improves reliability.
 */
#define DHT_MAX_RETRIES         3

/**
 * @brief Delay between retry attempts in milliseconds
 *
 * DHT22 datasheet requires minimum 2 seconds between consecutive reads.
 * 2.5 seconds provides safe margin for reliable operation.
 */
#define DHT_RETRY_DELAY_MS      2500

/**
 * Sensor type
 */
typedef enum
{
    DHT_TYPE_DHT11 = 0,   //!< DHT11
    DHT_TYPE_AM2301,      //!< AM2301 (DHT21, DHT22, AM2302, AM2321)
    DHT_TYPE_SI7021       //!< Itead Si7021
} dht_sensor_type_t;

/**
 * @brief Read integer data from sensor on specified pin
 *
 * Humidity and temperature are returned as integers.
 * For example: humidity=625 is 62.5 %, temperature=244 is 24.4 degrees Celsius
 *
 * @param sensor_type DHT11 or DHT22
 * @param pin GPIO pin connected to sensor OUT
 * @param[out] humidity Humidity, percents * 10, nullable
 * @param[out] temperature Temperature, degrees Celsius * 10, nullable
 * @return `ESP_OK` on success
 */
esp_err_t dht_read_data(dht_sensor_type_t sensor_type, gpio_num_t pin,
        int16_t *humidity, int16_t *temperature);

/**
 * @brief Read float data from sensor on specified pin
 *
 * Humidity and temperature are returned as floats.
 *
 * @param sensor_type DHT11 or DHT22
 * @param pin GPIO pin connected to sensor OUT
 * @param[out] humidity Humidity, percents, nullable
 * @param[out] temperature Temperature, degrees Celsius, nullable
 * @return `ESP_OK` on success
 */
esp_err_t dht_read_float_data(dht_sensor_type_t sensor_type, gpio_num_t pin,
        float *humidity, float *temperature);

/**
 * @brief Read ambient data with automatic retry logic (Component-Based wrapper)
 *
 * This function is the recommended API for the Component-Based Architecture.
 * It provides:
 * - Automatic retry with exponential backoff (configurable attempts)
 * - Returns data in ambient_data_t format (compatible with common_types.h)
 * - Automatic timestamp generation
 * - Enhanced error logging for field diagnostics
 *
 * Typical usage for Smart Irrigation System:
 * @code
 * ambient_data_t ambient;
 * esp_err_t ret = dht_read_ambient_data(DHT_TYPE_AM2301, GPIO_NUM_18,
 *                                       &ambient, DHT_MAX_RETRIES);
 * if (ret == ESP_OK) {
 *     ESP_LOGI(TAG, "Temp: %.1f°C, Humidity: %.1f%%",
 *              ambient.temperature, ambient.humidity);
 * }
 * @endcode
 *
 * @param sensor_type Sensor type (use DHT_TYPE_AM2301 for DHT22)
 * @param pin GPIO pin connected to sensor OUT (GPIO_NUM_18 in this project)
 * @param[out] data Pointer to ambient_data_t structure to fill
 * @param max_retries Maximum retry attempts (0 = use DHT_MAX_RETRIES default)
 *
 * @return
 *     - ESP_OK: Success, data is valid
 *     - ESP_ERR_TIMEOUT: All retry attempts failed (sensor not responding)
 *     - ESP_ERR_INVALID_CRC: Checksum validation failed on all attempts
 *     - ESP_ERR_INVALID_ARG: Invalid parameters (data is NULL)
 *
 * @note This function blocks for up to (max_retries * DHT_RETRY_DELAY_MS) milliseconds
 *       For 3 retries: maximum ~7.5 seconds blocking time
 */
esp_err_t dht_read_ambient_data(dht_sensor_type_t sensor_type, gpio_num_t pin,
                                ambient_data_t *data, uint8_t max_retries);

#ifdef __cplusplus
}
#endif

/**@}*/

#endif  // __DHT_H__
