/**
 * @file mqtt_client_manager.h
 * @brief MQTT Client Component - Communication and Data Publishing
 *
 * Simple MQTT client for sensor data publishing and irrigation commands.
 * Supports WebSockets for compatibility with cloud brokers.
 *
 * Component Responsibilities:
 * - MQTT connection management
 * - Device registration publishing
 * - Sensor data publishing (JSON format)
 * - Irrigation command subscription
 * - Automatic reconnection
 *
 * Migration from hexagonal: Consolidates mqtt_adapter + mqtt_client_manager + publish_sensor_data use case
 *
 * @author Liwaisi Tech
 * @date 2025-10-01
 * @version 2.0.0 - Component-Based Architecture
 */

#ifndef MQTT_CLIENT_MANAGER_H
#define MQTT_CLIENT_MANAGER_H

#include "esp_err.h"
#include "esp_event.h"
#include "common_types.h"
#include <stdbool.h>
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ TYPES AND ENUMS ============================ */

/**
 * @brief MQTT connection state
 */
typedef enum {
    MQTT_STATE_UNINITIALIZED = 0,   ///< Component not initialized
    MQTT_STATE_DISCONNECTED,        ///< Initialized but disconnected
    MQTT_STATE_CONNECTING,          ///< Connection attempt in progress
    MQTT_STATE_CONNECTED,           ///< Connected to broker
    MQTT_STATE_SUBSCRIBED,          ///< Connected and subscribed to topics
    MQTT_STATE_ERROR                ///< Error state
} mqtt_state_t;

/**
 * @brief MQTT client status
 */
typedef struct {
    mqtt_state_t state;             ///< Current MQTT state
    bool connected;                 ///< Connection status
    bool device_registered;         ///< Device registration sent
    char client_id[32];             ///< MQTT client ID
    char broker_uri[128];           ///< Broker URI
    uint16_t broker_port;           ///< Broker port
    uint32_t message_count;         ///< Total messages published
    uint32_t reconnect_count;       ///< Reconnection attempts
    uint32_t last_publish_time;     ///< Last successful publish timestamp
} mqtt_status_t;

/**
 * @brief MQTT configuration
 */
typedef struct {
    char broker_uri[128];           ///< MQTT broker URI (wss://broker.com/mqtt)
    uint16_t broker_port;           ///< Broker port (1883 for TCP, 8083 for WSS)
    char client_id[32];             ///< Client ID (auto-generated if empty)
    char username[32];              ///< MQTT username (optional)
    char password[64];              ///< MQTT password (optional)
    uint16_t keepalive_sec;         ///< Keepalive interval (default: 60s)
    bool use_websockets;            ///< Enable WebSocket transport
    bool enable_ssl;                ///< Enable SSL/TLS
} mqtt_config_params_t;

/**
 * @brief Irrigation command callback function
 *
 * Called when irrigation command is received via MQTT.
 *
 * @param command Command type (start/stop/emergency_stop)
 * @param duration_minutes Duration in minutes (for start command)
 * @param user_data User data passed during registration
 */
typedef void (*mqtt_irrigation_command_cb_t)(irrigation_command_t command,
                                             uint16_t duration_minutes,
                                             void* user_data);

/* ============================ PUBLIC API ============================ */

/**
 * @brief Initialize MQTT client component
 *
 * Initializes MQTT client with configuration from NVS or default.
 * Requires WiFi to be connected for successful operation.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_client_init(void);

/**
 * @brief Start MQTT client connection
 *
 * Initiates connection to broker. Will auto-reconnect on failures.
 *
 * @return ESP_OK if connection started, error code otherwise
 */
esp_err_t mqtt_client_start(void);

/**
 * @brief Stop MQTT client
 *
 * Disconnects from broker gracefully.
 *
 * @return ESP_OK on success
 */
esp_err_t mqtt_client_stop(void);

/**
 * @brief Deinitialize MQTT client
 *
 * Stops client and cleans up resources.
 *
 * @return ESP_OK on success
 */
esp_err_t mqtt_client_deinit(void);

/**
 * @brief Publish device registration message
 *
 * Sends device registration to "irrigation/register" topic.
 * JSON format: {event_type, mac_address, ip_address, device_name, crop_name, firmware_version}
 *
 * @return ESP_OK if published successfully, error code otherwise
 */
esp_err_t mqtt_client_publish_registration(void);

/**
 * @brief Publish sensor data
 *
 * Publishes sensor reading to "irrigation/data/{crop_name}/{mac_address}" topic.
 * JSON format: {event_type, mac_address, ip_address, ambient_temperature,
 *               ambient_humidity, soil_humidity_1, soil_humidity_2, soil_humidity_3}
 *
 * @param reading Sensor reading data
 * @return ESP_OK if published successfully, error code otherwise
 */
esp_err_t mqtt_client_publish_sensor_data(const sensor_reading_t* reading);

/**
 * @brief Publish irrigation status
 *
 * Publishes current irrigation status to "irrigation/status/{mac_address}" topic.
 * JSON format: {state, mode, session_duration, valve_number, last_soil_avg}
 *
 * @param status Irrigation status data
 * @return ESP_OK if published successfully, error code otherwise
 */
esp_err_t mqtt_client_publish_irrigation_status(const irrigation_status_t* status);

/**
 * @brief Subscribe to irrigation control commands
 *
 * Subscribes to "irrigation/control/{mac_address}" topic.
 * Received commands trigger the registered callback.
 *
 * @return ESP_OK if subscribed successfully, error code otherwise
 */
esp_err_t mqtt_client_subscribe_irrigation_commands(void);

/**
 * @brief Register irrigation command callback
 *
 * Sets the callback function for irrigation commands received via MQTT.
 *
 * @param callback Callback function
 * @param user_data User data passed to callback (can be NULL)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if callback is NULL
 */
esp_err_t mqtt_client_register_command_callback(mqtt_irrigation_command_cb_t callback,
                                                void* user_data);

/**
 * @brief Get current MQTT status
 *
 * @param[out] status Pointer to status structure to fill
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if status is NULL
 */
esp_err_t mqtt_client_get_status(mqtt_status_t* status);

/**
 * @brief Check if MQTT is connected
 *
 * @return true if connected to broker, false otherwise
 */
bool mqtt_client_is_connected(void);

/**
 * @brief Force MQTT reconnection
 *
 * Disconnects and reconnects to broker.
 *
 * @return ESP_OK if reconnection initiated
 */
esp_err_t mqtt_client_reconnect(void);

/* ============================ EVENT DEFINITIONS ============================ */

/**
 * @brief MQTT client event base
 */
ESP_EVENT_DECLARE_BASE(MQTT_CLIENT_EVENTS);

/**
 * @brief MQTT client event IDs
 */
typedef enum {
    MQTT_CLIENT_EVENT_INITIALIZED,      ///< Component initialized
    MQTT_CLIENT_EVENT_CONNECTING,       ///< Connecting to broker
    MQTT_CLIENT_EVENT_CONNECTED,        ///< Connected to broker
    MQTT_CLIENT_EVENT_DISCONNECTED,     ///< Disconnected from broker
    MQTT_CLIENT_EVENT_SUBSCRIBED,       ///< Subscribed to topics
    MQTT_CLIENT_EVENT_PUBLISHED,        ///< Message published successfully
    MQTT_CLIENT_EVENT_DATA_RECEIVED,    ///< Data received from subscribed topic
    MQTT_CLIENT_EVENT_ERROR             ///< Error occurred
} mqtt_client_event_id_t;

/* ============================ MQTT TOPICS ============================ */

/**
 * @brief MQTT topic definitions
 */
#define MQTT_TOPIC_REGISTER             "irrigation/register"
#define MQTT_TOPIC_DATA_PREFIX          "irrigation/data"
#define MQTT_TOPIC_CONTROL_PREFIX       "irrigation/control"
#define MQTT_TOPIC_STATUS_PREFIX        "irrigation/status"

/**
 * @brief Build data topic for specific device
 * Format: irrigation/data/{crop_name}/{mac_address}
 */
#define MQTT_BUILD_DATA_TOPIC(buf, crop, mac) \
    snprintf(buf, sizeof(buf), "%s/%s/%s", MQTT_TOPIC_DATA_PREFIX, crop, mac)

/**
 * @brief Build control topic for specific device
 * Format: irrigation/control/{mac_address}
 */
#define MQTT_BUILD_CONTROL_TOPIC(buf, mac) \
    snprintf(buf, sizeof(buf), "%s/%s", MQTT_TOPIC_CONTROL_PREFIX, mac)

/**
 * @brief Build status topic for specific device
 * Format: irrigation/status/{mac_address}
 */
#define MQTT_BUILD_STATUS_TOPIC(buf, mac) \
    snprintf(buf, sizeof(buf), "%s/%s", MQTT_TOPIC_STATUS_PREFIX, mac)

/* ============================ CONFIGURATION ============================ */

/**
 * @brief Default MQTT configuration
 */
#ifndef CONFIG_MQTT_BROKER_URI
#define CONFIG_MQTT_BROKER_URI "mqtt://mqtt.liwaisi.tech/mqtt"
#endif

#define MQTT_CLIENT_DEFAULT_CONFIG() {      \
    .broker_uri = CONFIG_MQTT_BROKER_URI,  \
    .broker_port = 8083,                   \
    .client_id = "",                      \
    .username = "",                       \
    .password = "",                       \
    .keepalive_sec = 60,                   \
    .use_websockets = true,                \
    .enable_ssl = true                     \
}

/**
 * @brief MQTT client NVS namespace
 */
#define MQTT_CLIENT_NVS_NAMESPACE "mqtt_cfg"

/**
 * @brief MQTT client NVS keys
 */
#define MQTT_CLIENT_NVS_KEY_BROKER_URI  "broker_uri"
#define MQTT_CLIENT_NVS_KEY_PORT        "port"
#define MQTT_CLIENT_NVS_KEY_USERNAME    "username"
#define MQTT_CLIENT_NVS_KEY_PASSWORD    "password"

/**
 * @brief MQTT QoS levels
 */
#define MQTT_QOS_0  0   ///< At most once delivery
#define MQTT_QOS_1  1   ///< At least once delivery (default)
#define MQTT_QOS_2  2   ///< Exactly once delivery

/**
 * @brief Default QoS for sensor data and commands
 */
#define MQTT_DEFAULT_QOS    MQTT_QOS_1

/**
 * @brief MQTT message buffer sizes
 */
#define MQTT_MAX_TOPIC_LENGTH       128
#define MQTT_MAX_PAYLOAD_LENGTH     512

#ifdef __cplusplus
}
#endif

#endif // MQTT_CLIENT_MANAGER_H
