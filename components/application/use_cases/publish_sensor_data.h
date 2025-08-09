#ifndef APPLICATION_USE_CASES_PUBLISH_SENSOR_DATA_H
#define APPLICATION_USE_CASES_PUBLISH_SENSOR_DATA_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Publish sensor data use case
 * 
 * This use case orchestrates the process of reading ambient sensor data
 * (temperature and humidity) and publishing it via MQTT. It follows the
 * application layer pattern in hexagonal architecture.
 * 
 * Workflow:
 * 1. Read sensor data using the DHT sensor driver
 * 2. Validate sensor readings
 * 3. Publish data via MQTT adapter
 * 4. Handle failures gracefully (log warnings but don't crash)
 * 
 * @return ESP_OK on success, ESP_ERR_* on failure
 *         Note: Sensor reading failures are logged as warnings and return ESP_OK
 *               to allow the publishing task to continue running
 */
esp_err_t publish_sensor_data_use_case(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_USE_CASES_PUBLISH_SENSOR_DATA_H */