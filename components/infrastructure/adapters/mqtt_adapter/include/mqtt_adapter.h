#ifndef MQTT_ADAPTER_H
#define MQTT_ADAPTER_H

#include "esp_err.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "ambient_sensor_data.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(MQTT_ADAPTER_EVENTS);

typedef enum {
    MQTT_ADAPTER_EVENT_INIT_COMPLETE,
    MQTT_ADAPTER_EVENT_CONNECTING,
    MQTT_ADAPTER_EVENT_CONNECTED,
    MQTT_ADAPTER_EVENT_DISCONNECTED,
    MQTT_ADAPTER_EVENT_PUBLISHED,
    MQTT_ADAPTER_EVENT_DEVICE_REGISTERED,
    MQTT_ADAPTER_EVENT_CONNECTION_FAILED,
    MQTT_ADAPTER_EVENT_ERROR
} mqtt_adapter_event_id_t;

typedef enum {
    MQTT_ADAPTER_STATE_UNINITIALIZED,
    MQTT_ADAPTER_STATE_INITIALIZED,
    MQTT_ADAPTER_STATE_CONNECTING,
    MQTT_ADAPTER_STATE_CONNECTED,
    MQTT_ADAPTER_STATE_DISCONNECTED,
    MQTT_ADAPTER_STATE_RECONNECTING,
    MQTT_ADAPTER_STATE_ERROR
} mqtt_adapter_state_t;

typedef struct {
    mqtt_adapter_state_t state;
    bool connected;
    bool device_registered;
    uint32_t reconnect_count;
    uint32_t message_count;
    char client_id[32];
} mqtt_adapter_status_t;

/**
 * @brief Initialize MQTT adapter
 * 
 * Sets up MQTT client configuration and prepares for connection.
 * Should be called after WiFi connection is established.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_adapter_init(void);

/**
 * @brief Start MQTT adapter
 * 
 * Initiates connection to MQTT broker and begins automatic reconnection.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_adapter_start(void);

/**
 * @brief Stop MQTT adapter
 * 
 * Disconnects from MQTT broker and stops reconnection attempts.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_adapter_stop(void);

/**
 * @brief Deinitialize MQTT adapter
 * 
 * Cleans up MQTT client resources and resets state.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_adapter_deinit(void);

/**
 * @brief Get current MQTT adapter status
 * 
 * @param[out] status Pointer to status structure to fill
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_adapter_get_status(mqtt_adapter_status_t *status);

/**
 * @brief Check if MQTT client is connected
 * 
 * @return true if connected, false otherwise
 */
bool mqtt_adapter_is_connected(void);

/**
 * @brief Publish device registration message
 * 
 * Sends device registration information to the registration topic.
 * This function retrieves device information from NVS and current IP.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_adapter_publish_device_registration(void);

/**
 * @brief Publish sensor data message
 * 
 * Sends ambient sensor data (temperature and humidity) to the sensor data topic.
 * Creates JSON payload with MAC address, temperature, and humidity values.
 * 
 * @param sensor_data Pointer to ambient sensor data structure containing readings
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_adapter_publish_sensor_data(const ambient_sensor_data_t *sensor_data);

/**
 * @brief Force reconnection attempt
 *
 * Triggers immediate reconnection attempt if not already connected.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_adapter_reconnect(void);

/**
 * @brief Test MQTT broker connectivity diagnostics
 *
 * Performs comprehensive connectivity tests including DNS resolution,
 * TCP connection, and MQTT protocol validation. Used for debugging
 * connection issues in rural deployment scenarios.
 *
 * @return ESP_OK if broker is reachable, error code otherwise
 */
esp_err_t mqtt_adapter_test_broker_connectivity(void);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_ADAPTER_H */