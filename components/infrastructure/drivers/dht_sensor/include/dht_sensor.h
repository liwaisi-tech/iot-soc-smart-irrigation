#ifndef DHT_SENSOR_H
#define DHT_SENSOR_H

#include "esp_err.h"
#include "driver/gpio.h"
#include "ambient_sensor_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize DHT sensor on specified GPIO pin
 * 
 * Configures the GPIO pin for DHT22/AM2301 sensor communication.
 * Must be called before attempting to read sensor data.
 * 
 * @param pin GPIO pin number connected to the DHT sensor data line
 * @return ESP_OK on success, error code on failure
 */
esp_err_t dht_sensor_init(gpio_num_t pin);

/**
 * @brief Read temperature and humidity from DHT sensor
 * 
 * Performs a blocking read operation on the DHT22/AM2301 sensor
 * and fills the provided ambient_sensor_data_t structure with the results.
 * This function includes timing-critical operations and should not be
 * called from ISR context.
 * 
 * @param data Pointer to ambient_sensor_data_t structure to fill with readings
 * @return ESP_OK on success, ESP_ERR_TIMEOUT on communication timeout,
 *         ESP_ERR_INVALID_CRC on checksum failure, other error codes on failure
 */
esp_err_t dht_sensor_read(ambient_sensor_data_t* data);

/**
 * @brief Get the currently configured GPIO pin for DHT sensor
 * 
 * @return GPIO pin number, or GPIO_NUM_NC if not initialized
 */
gpio_num_t dht_sensor_get_pin(void);

/**
 * @brief Check if DHT sensor is initialized
 * 
 * @return true if sensor is initialized, false otherwise
 */
bool dht_sensor_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* DHT_SENSOR_H */