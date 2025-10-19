/**
 * @file mqtt_adapter.c
 * @brief MQTT Client Component Implementation - Component-Based Architecture
 *
 * Consolidates MQTT functionality from hexagonal architecture:
 * - mqtt_adapter.c (connection management, events, publishing)
 * - mqtt_client_manager.c (utilities)
 * - publish_sensor_data.c (use case logic)
 * - json_device_serializer.c (JSON serialization)
 *
 * Migration: Hexagonal Architecture → Component-Based Architecture
 * @author Liwaisi Tech
 * @date 2025-10-03
 * @version 2.0.0
 */

// ESP-IDF includes (MUST be first to define esp_mqtt_event_t)
#include "mqtt_client.h"  // ESP-IDF MQTT client header from ESP-IDF framework
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Component headers (after ESP-IDF to avoid conflicts)
#include "mqtt_client_manager.h"  // Local component header
#include "sensor_reader.h"
#include "device_config.h"
#include "wifi_manager.h"

#include "sdkconfig.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

/* ========================== CONSTANTS AND MACROS ========================== */

static const char *TAG = "mqtt_client";

// Use menuconfig-provided broker URI
#define MQTT_DEFAULT_BROKER_URI CONFIG_MQTT_BROKER_URI

#define MQTT_DEFAULT_PORT               8083
#define MQTT_DEFAULT_KEEPALIVE_SEC      60
#define MQTT_DEFAULT_USE_WEBSOCKETS     true
#define MQTT_DEFAULT_ENABLE_SSL         true
#define MQTT_CLIENT_ID_PREFIX           "ESP32"

// Reconnection configuration (exponential backoff)
#define MQTT_RECONNECT_INITIAL_DELAY_MS 10000   // 10 seconds
#define MQTT_RECONNECT_MAX_DELAY_MS     3600000 // 1 hour

// Buffer sizes
#define MQTT_BUFFER_SIZE                4096
#define MQTT_TASK_STACK_SIZE            6144

// Firmware version
#define FIRMWARE_VERSION                "v1.2.0"

/* ========================== TYPES AND STRUCTURES ========================== */

/**
 * @brief MQTT client internal context
 *
 * Encapsulates complete state of MQTT component.
 * Replaces mqtt_connection_t entity from hexagonal architecture.
 */
typedef struct {
    // ESP-IDF MQTT client handle
    esp_mqtt_client_handle_t client;

    // Component state
    mqtt_state_t state;
    bool initialized;

    // Configuration
    mqtt_config_params_t config;

    // Status and statistics
    mqtt_status_t status;

    // Reconnection management
    esp_timer_handle_t reconnect_timer;
    uint32_t current_retry_delay_ms;

    // Irrigation command callback
    mqtt_irrigation_command_cb_t cmd_callback;
    void* cmd_callback_user_data;

} mqtt_client_context_t;

/* ========================== GLOBAL STATE ========================== */

static mqtt_client_context_t s_mqtt_ctx = {0};

// Event base declaration
ESP_EVENT_DEFINE_BASE(MQTT_CLIENT_EVENTS);

/* ========================== FORWARD DECLARATIONS ========================== */

// Event handlers
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data);
static void mqtt_wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data);
static void mqtt_reconnect_timer_callback(void* arg);

// Internal helpers
static esp_err_t mqtt_generate_client_id(char* client_id, size_t size);
static esp_err_t mqtt_configure_client(void);
static esp_err_t mqtt_start_reconnect_timer(uint32_t delay_ms);
static void mqtt_handle_irrigation_command(esp_mqtt_event_t* event);
static esp_err_t mqtt_build_device_json(cJSON** json_out);
static esp_err_t mqtt_build_sensor_data_json(const sensor_reading_t* reading,
                                             cJSON** json_out);

/* ========================== INITIALIZATION ========================== */

/**
 * @brief Generate MQTT client ID based on MAC address
 */
static esp_err_t mqtt_generate_client_id(char* client_id, size_t size)
{
    uint8_t mac[6];
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MAC address: %s", esp_err_to_name(ret));
        return ret;
    }

    snprintf(client_id, size, "%s_%02X%02X%02X",
             MQTT_CLIENT_ID_PREFIX, mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "Generated MQTT client ID: %s", client_id);
    return ESP_OK;
}

/**
 * @brief Configure ESP-MQTT client with WebSocket transport
 */
static esp_err_t mqtt_configure_client(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = s_mqtt_ctx.config.broker_uri,
        .credentials.client_id = s_mqtt_ctx.config.client_id,
        .session.keepalive = s_mqtt_ctx.config.keepalive_sec,
        .session.disable_clean_session = false,  // Always clean session
        .buffer.size = MQTT_BUFFER_SIZE,
        .task.stack_size = MQTT_TASK_STACK_SIZE,
    };

    // Add username/password if provided
    if (strlen(s_mqtt_ctx.config.username) > 0) {
        mqtt_cfg.credentials.username = s_mqtt_ctx.config.username;
    }
    if (strlen(s_mqtt_ctx.config.password) > 0) {
        mqtt_cfg.credentials.authentication.password = s_mqtt_ctx.config.password;
    }

    s_mqtt_ctx.client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_ctx.client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }

    esp_err_t ret = esp_mqtt_client_register_event(s_mqtt_ctx.client,
                                                   ESP_EVENT_ANY_ID,
                                                   mqtt_event_handler,
                                                   NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "MQTT client configured successfully");
    ESP_LOGI(TAG, "  Broker URI: %s", s_mqtt_ctx.config.broker_uri);
    ESP_LOGI(TAG, "  Client ID: %s", s_mqtt_ctx.config.client_id);
    ESP_LOGI(TAG, "  Keepalive: %d seconds", s_mqtt_ctx.config.keepalive_sec);

    return ESP_OK;
}

esp_err_t mqtt_client_init(void)
{
    if (s_mqtt_ctx.initialized) {
        ESP_LOGW(TAG, "MQTT client already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing MQTT client component...");

    // Initialize context
    memset(&s_mqtt_ctx, 0, sizeof(mqtt_client_context_t));
    s_mqtt_ctx.state = MQTT_STATE_UNINITIALIZED;
    s_mqtt_ctx.current_retry_delay_ms = MQTT_RECONNECT_INITIAL_DELAY_MS;

    // Load configuration from NVS (device_config component)
    // Use defaults if NVS values not found
    strncpy(s_mqtt_ctx.config.broker_uri, MQTT_DEFAULT_BROKER_URI,
            sizeof(s_mqtt_ctx.config.broker_uri) - 1);
    s_mqtt_ctx.config.broker_port = MQTT_DEFAULT_PORT;
    s_mqtt_ctx.config.keepalive_sec = MQTT_DEFAULT_KEEPALIVE_SEC;
    s_mqtt_ctx.config.use_websockets = MQTT_DEFAULT_USE_WEBSOCKETS;
    s_mqtt_ctx.config.enable_ssl = MQTT_DEFAULT_ENABLE_SSL;

    // TODO: Load from NVS when device_config component is migrated
    // device_config_get_mqtt_broker(s_mqtt_ctx.config.broker_uri, sizeof(...));
    // device_config_get_mqtt_port(&s_mqtt_ctx.config.broker_port);

    // Generate client ID
    esp_err_t ret = mqtt_generate_client_id(s_mqtt_ctx.config.client_id,
                                            sizeof(s_mqtt_ctx.config.client_id));
    if (ret != ESP_OK) {
        return ret;
    }

    // Configure MQTT client
    ret = mqtt_configure_client();
    if (ret != ESP_OK) {
        return ret;
    }

    // Register WiFi event handler (auto-start on IP obtained)
    ret = esp_event_handler_register(WIFI_MANAGER_EVENTS, ESP_EVENT_ANY_ID,
                                     &mqtt_wifi_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WiFi event handler: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    // Update status
    s_mqtt_ctx.state = MQTT_STATE_UNINITIALIZED;  // Initialized but not connected yet
    s_mqtt_ctx.initialized = true;
    strncpy(s_mqtt_ctx.status.client_id, s_mqtt_ctx.config.client_id,
            sizeof(s_mqtt_ctx.status.client_id) - 1);
    strncpy(s_mqtt_ctx.status.broker_uri, s_mqtt_ctx.config.broker_uri,
            sizeof(s_mqtt_ctx.status.broker_uri) - 1);
    s_mqtt_ctx.status.broker_port = s_mqtt_ctx.config.broker_port;

    // Post initialization event
    esp_event_post(MQTT_CLIENT_EVENTS, MQTT_CLIENT_EVENT_INITIALIZED,
                   NULL, 0, portMAX_DELAY);

    ESP_LOGI(TAG, "MQTT client component initialized successfully");
    return ESP_OK;
}

/* ========================== CONNECTION MANAGEMENT ========================== */

esp_err_t mqtt_client_start(void)
{
    if (!s_mqtt_ctx.initialized) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_mqtt_ctx.state == MQTT_STATE_CONNECTED) {
        ESP_LOGW(TAG, "MQTT client already connected");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting MQTT client...");

    s_mqtt_ctx.state = MQTT_STATE_CONNECTING;
    esp_event_post(MQTT_CLIENT_EVENTS, MQTT_CLIENT_EVENT_CONNECTING,
                   NULL, 0, portMAX_DELAY);

    esp_err_t ret = esp_mqtt_client_start(s_mqtt_ctx.client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(ret));
        s_mqtt_ctx.state = MQTT_STATE_ERROR;
        return ret;
    }

    return ESP_OK;
}

esp_err_t mqtt_client_stop(void)
{
    if (!s_mqtt_ctx.initialized) {
        ESP_LOGW(TAG, "MQTT client not initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping MQTT client...");

    // Stop reconnection timer
    if (s_mqtt_ctx.reconnect_timer != NULL) {
        esp_timer_stop(s_mqtt_ctx.reconnect_timer);
    }

    if (s_mqtt_ctx.client != NULL) {
        esp_err_t ret = esp_mqtt_client_stop(s_mqtt_ctx.client);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop MQTT client: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    s_mqtt_ctx.state = MQTT_STATE_DISCONNECTED;
    ESP_LOGI(TAG, "MQTT client stopped");

    return ESP_OK;
}

esp_err_t mqtt_client_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing MQTT client...");

    // Stop the client first
    mqtt_client_stop();

    // Clean up reconnection timer
    if (s_mqtt_ctx.reconnect_timer != NULL) {
        esp_timer_delete(s_mqtt_ctx.reconnect_timer);
        s_mqtt_ctx.reconnect_timer = NULL;
    }

    // Destroy MQTT client
    if (s_mqtt_ctx.client != NULL) {
        esp_mqtt_client_destroy(s_mqtt_ctx.client);
        s_mqtt_ctx.client = NULL;
    }

    // Unregister event handlers
    esp_event_handler_unregister(WIFI_MANAGER_EVENTS, ESP_EVENT_ANY_ID,
                                  &mqtt_wifi_event_handler);

    // Reset state
    memset(&s_mqtt_ctx, 0, sizeof(mqtt_client_context_t));

    ESP_LOGI(TAG, "MQTT client deinitialized");
    return ESP_OK;
}

esp_err_t mqtt_client_reconnect(void)
{
    if (!s_mqtt_ctx.initialized) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_mqtt_ctx.state == MQTT_STATE_CONNECTED) {
        ESP_LOGW(TAG, "MQTT client already connected");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Force reconnection requested");

    // Stop any running reconnection timer
    if (s_mqtt_ctx.reconnect_timer != NULL) {
        esp_timer_stop(s_mqtt_ctx.reconnect_timer);
    }

    // Reset retry delay
    s_mqtt_ctx.current_retry_delay_ms = MQTT_RECONNECT_INITIAL_DELAY_MS;

    // Start reconnection immediately
    mqtt_reconnect_timer_callback(NULL);

    return ESP_OK;
}

/* ========================== RECONNECTION TIMER ========================== */

/**
 * @brief Reconnection timer callback (exponential backoff)
 */
static void mqtt_reconnect_timer_callback(void* arg)
{
    ESP_LOGI(TAG, "Reconnection timer triggered - attempting MQTT reconnection");

    if (s_mqtt_ctx.state == MQTT_STATE_DISCONNECTED ||
        s_mqtt_ctx.state == MQTT_STATE_ERROR) {

        s_mqtt_ctx.state = MQTT_STATE_CONNECTING;
        esp_event_post(MQTT_CLIENT_EVENTS, MQTT_CLIENT_EVENT_CONNECTING,
                       NULL, 0, portMAX_DELAY);

        esp_err_t ret = esp_mqtt_client_start(s_mqtt_ctx.client);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start MQTT client during reconnection: %s",
                     esp_err_to_name(ret));

            // Increase retry delay with exponential backoff
            s_mqtt_ctx.current_retry_delay_ms *= 2;
            if (s_mqtt_ctx.current_retry_delay_ms > MQTT_RECONNECT_MAX_DELAY_MS) {
                s_mqtt_ctx.current_retry_delay_ms = MQTT_RECONNECT_MAX_DELAY_MS;
            }

            ESP_LOGW(TAG, "Next reconnection attempt in %lu ms",
                     s_mqtt_ctx.current_retry_delay_ms);

            // Schedule next retry
            mqtt_start_reconnect_timer(s_mqtt_ctx.current_retry_delay_ms);
        }
    }
}

/**
 * @brief Start reconnection timer with specified delay
 */
static esp_err_t mqtt_start_reconnect_timer(uint32_t delay_ms)
{
    if (s_mqtt_ctx.reconnect_timer == NULL) {
        esp_timer_create_args_t timer_args = {
            .callback = mqtt_reconnect_timer_callback,
            .arg = NULL,
            .name = "mqtt_reconnect"
        };

        esp_err_t ret = esp_timer_create(&timer_args, &s_mqtt_ctx.reconnect_timer);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create reconnect timer: %s",
                     esp_err_to_name(ret));
            return ret;
        }
    }

    ESP_LOGI(TAG, "Scheduling MQTT reconnection in %lu ms", delay_ms);
    return esp_timer_start_once(s_mqtt_ctx.reconnect_timer, delay_ms * 1000);
}

/* ========================== EVENT HANDLERS ========================== */

/**
 * @brief Main MQTT event handler
 *
 * Handles all ESP-MQTT client events (CONNECTED, DISCONNECTED, DATA, ERROR).
 * Consolidated from mqtt_adapter.c:mqtt_event_handler()
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_t* event = (esp_mqtt_event_t*)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

            s_mqtt_ctx.state = MQTT_STATE_CONNECTED;
            s_mqtt_ctx.status.connected = true;
            s_mqtt_ctx.current_retry_delay_ms = MQTT_RECONNECT_INITIAL_DELAY_MS;
            s_mqtt_ctx.status.reconnect_count++;

            // Stop reconnection timer
            if (s_mqtt_ctx.reconnect_timer != NULL) {
                esp_timer_stop(s_mqtt_ctx.reconnect_timer);
            }

            // Post connected event
            esp_event_post(MQTT_CLIENT_EVENTS, MQTT_CLIENT_EVENT_CONNECTED,
                           NULL, 0, portMAX_DELAY);

            // Publish device registration
            esp_err_t ret = mqtt_client_publish_registration();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to publish device registration");
            }

            // Auto-subscribe to irrigation commands if callback registered
            if (s_mqtt_ctx.cmd_callback != NULL) {
                mqtt_client_subscribe_irrigation_commands();
            }

            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");

            s_mqtt_ctx.state = MQTT_STATE_DISCONNECTED;
            s_mqtt_ctx.status.connected = false;
            s_mqtt_ctx.status.device_registered = false;

            // Post disconnected event
            esp_event_post(MQTT_CLIENT_EVENTS, MQTT_CLIENT_EVENT_DISCONNECTED,
                           NULL, 0, portMAX_DELAY);

            // Start reconnection timer with exponential backoff
            mqtt_start_reconnect_timer(s_mqtt_ctx.current_retry_delay_ms);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);

            s_mqtt_ctx.status.message_count++;
            s_mqtt_ctx.status.last_publish_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

            // Post published event
            esp_event_post(MQTT_CLIENT_EVENTS, MQTT_CLIENT_EVENT_PUBLISHED,
                           &event->msg_id, sizeof(int), portMAX_DELAY);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGD(TAG, "MQTT_EVENT_DATA");
            ESP_LOGD(TAG, "  Topic: %.*s", event->topic_len, event->topic);
            ESP_LOGD(TAG, "  Data length: %d", event->data_len);

            // Handle irrigation commands
            mqtt_handle_irrigation_command(event);

            // Post data received event
            esp_event_post(MQTT_CLIENT_EVENTS, MQTT_CLIENT_EVENT_DATA_RECEIVED,
                           NULL, 0, portMAX_DELAY);
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");

            s_mqtt_ctx.state = MQTT_STATE_ERROR;

            // Post error event
            esp_event_post(MQTT_CLIENT_EVENTS, MQTT_CLIENT_EVENT_ERROR,
                           NULL, 0, portMAX_DELAY);

            // Start reconnection timer on error
            mqtt_start_reconnect_timer(s_mqtt_ctx.current_retry_delay_ms);
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);

            s_mqtt_ctx.state = MQTT_STATE_SUBSCRIBED;

            // Post subscribed event
            esp_event_post(MQTT_CLIENT_EVENTS, MQTT_CLIENT_EVENT_SUBSCRIBED,
                           &event->msg_id, sizeof(int), portMAX_DELAY);
            break;

        default:
            ESP_LOGD(TAG, "MQTT event: %ld", event_id);
            break;
    }
}

/**
 * @brief WiFi event handler for MQTT component
 *
 * Auto-starts MQTT client when WiFi IP is obtained.
 * Consolidated from mqtt_adapter.c:mqtt_adapter_wifi_event_handler()
 */
static void mqtt_wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_base == WIFI_MANAGER_EVENTS) {
        switch (event_id) {
            case WIFI_MANAGER_EVENT_IP_OBTAINED:
                ESP_LOGI(TAG, "WiFi IP obtained - starting MQTT client");

                if (s_mqtt_ctx.initialized && s_mqtt_ctx.state != MQTT_STATE_CONNECTED) {
                    esp_err_t ret = mqtt_client_start();
                    if (ret != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to start MQTT client after WiFi IP obtained");
                    }
                }
                break;

            case WIFI_MANAGER_EVENT_DISCONNECTED:
                ESP_LOGW(TAG, "WiFi disconnected - MQTT will attempt reconnection when WiFi recovers");
                break;

            default:
                break;
        }
    }
}

/* ========================== JSON SERIALIZATION (INLINE) ========================== */

/**
 * @brief Build device registration JSON
 *
 * Consolidated from json_device_serializer.c:serialize_device_registration()
 * Creates JSON: {event_type, mac_address, ip_address, device_name, crop_name, firmware_version}
 */
static esp_err_t mqtt_build_device_json(cJSON** json_out)
{
    if (json_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Get MAC address
    uint8_t mac[6];
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MAC address: %s", esp_err_to_name(ret));
        return ret;
    }

    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Get IP address from WiFi manager
    char ip_str[16] = "0.0.0.0";
    // TODO: Implement when wifi_manager provides get_ip_string()
    // wifi_manager_get_ip_string(ip_str, sizeof(ip_str));

    // Get device name and crop name from device_config
    char device_name[32] = "Smart Irrigation Device";
    char crop_name[16] = "Unknown";
    // TODO: Load from device_config when migrated
    // device_config_get_device_name(device_name, sizeof(device_name));
    // device_config_get_crop_name(crop_name, sizeof(crop_name));

    // Create JSON object
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(json, "event_type", "device_registration");
    cJSON_AddStringToObject(json, "mac_address", mac_str);
    cJSON_AddStringToObject(json, "ip_address", ip_str);
    cJSON_AddStringToObject(json, "device_name", device_name);
    cJSON_AddStringToObject(json, "crop_name", crop_name);
    cJSON_AddStringToObject(json, "firmware_version", FIRMWARE_VERSION);

    *json_out = json;
    return ESP_OK;
}

/**
 * @brief Build sensor data JSON
 *
 * Consolidated from json_device_serializer.c:serialize_complete_sensor_data()
 * Creates JSON: {event_type, mac_address, ip_address, ambient_*, soil_humidity_*}
 */
static esp_err_t mqtt_build_sensor_data_json(const sensor_reading_t* reading,
                                             cJSON** json_out)
{
    if (reading == NULL || json_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Get MAC address
    uint8_t mac[6];
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MAC address: %s", esp_err_to_name(ret));
        return ret;
    }

    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Get IP address (use from reading structure)

    // Create JSON object
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(json, "event_type", "sensor_data");
    cJSON_AddStringToObject(json, "mac_address", mac_str);
    cJSON_AddStringToObject(json, "ip_address", reading->device_ip);
    cJSON_AddNumberToObject(json, "ambient_temperature", reading->ambient.temperature);
    cJSON_AddNumberToObject(json, "ambient_humidity", reading->ambient.humidity);
    cJSON_AddNumberToObject(json, "soil_humidity_1", reading->soil.soil_humidity[0]);
    cJSON_AddNumberToObject(json, "soil_humidity_2", reading->soil.soil_humidity[1]);
    cJSON_AddNumberToObject(json, "soil_humidity_3", reading->soil.soil_humidity[2]);

    *json_out = json;
    return ESP_OK;
}

/* ========================== PUBLISHING ========================== */

esp_err_t mqtt_client_publish_registration(void)
{
    if (!s_mqtt_ctx.initialized) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_mqtt_ctx.state != MQTT_STATE_CONNECTED) {
        ESP_LOGE(TAG, "MQTT client not connected");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Publishing device registration...");

    // Build JSON
    cJSON *json = NULL;
    esp_err_t ret = mqtt_build_device_json(&json);
    if (ret != ESP_OK) {
        return ret;
    }

    // Serialize to string
    char *json_string = cJSON_Print(json);
    if (json_string == NULL) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        cJSON_Delete(json);
        return ESP_ERR_NO_MEM;
    }

    // Publish to topic
    const char *topic = MQTT_TOPIC_REGISTER;
    int msg_id = esp_mqtt_client_publish(s_mqtt_ctx.client,
                                         topic,
                                         json_string,
                                         0,  // Let ESP-MQTT calculate length
                                         MQTT_DEFAULT_QOS,
                                         0); // Don't retain

    if (msg_id == -1) {
        ESP_LOGE(TAG, "Failed to publish device registration message");
        ret = ESP_FAIL;
    } else {
        ESP_LOGI(TAG, "Device registration published successfully");
        ESP_LOGI(TAG, "  Topic: %s", topic);
        ESP_LOGI(TAG, "  Message ID: %d", msg_id);
        ESP_LOGD(TAG, "  Payload: %s", json_string);

        s_mqtt_ctx.status.device_registered = true;
        ret = ESP_OK;
    }

    // Cleanup
    free(json_string);
    cJSON_Delete(json);

    return ret;
}

esp_err_t mqtt_client_publish_sensor_data(const sensor_reading_t* reading)
{
    if (!s_mqtt_ctx.initialized) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (reading == NULL) {
        ESP_LOGE(TAG, "Sensor reading is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_mqtt_ctx.state != MQTT_STATE_CONNECTED) {
        ESP_LOGW(TAG, "MQTT client not connected, skipping sensor data publishing");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Publishing sensor data...");

    // Build JSON
    cJSON *json = NULL;
    esp_err_t ret = mqtt_build_sensor_data_json(reading, &json);
    if (ret != ESP_OK) {
        return ret;
    }

    // Serialize to string
    char *json_string = cJSON_Print(json);
    if (json_string == NULL) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        cJSON_Delete(json);
        return ESP_ERR_NO_MEM;
    }

    // Build dynamic topic: irrigation/data/{crop_name}/{mac_address}
    char topic[MQTT_MAX_TOPIC_LENGTH];
    char crop_name[16] = "Unknown";
    // TODO: Load from device_config when migrated
    // device_config_get_crop_name(crop_name, sizeof(crop_name));

    MQTT_BUILD_DATA_TOPIC(topic, crop_name, reading->device_mac);

    // Publish to topic
    int msg_id = esp_mqtt_client_publish(s_mqtt_ctx.client,
                                         topic,
                                         json_string,
                                         0,  // Let ESP-MQTT calculate length
                                         MQTT_DEFAULT_QOS,
                                         0); // Don't retain

    if (msg_id == -1) {
        ESP_LOGE(TAG, "Failed to publish sensor data message");
        ret = ESP_FAIL;
    } else {
        ESP_LOGD(TAG, "Sensor data published successfully");
        ESP_LOGD(TAG, "  Topic: %s", topic);
        ESP_LOGD(TAG, "  Message ID: %d", msg_id);
        ESP_LOGV(TAG, "  Payload: %s", json_string);
        ret = ESP_OK;
    }

    // Cleanup
    free(json_string);
    cJSON_Delete(json);

    return ret;
}

esp_err_t mqtt_client_publish_irrigation_status(const irrigation_status_t* status)
{
    // TODO: Implement when irrigation_controller component is migrated (Phase 2)
    ESP_LOGW(TAG, "mqtt_client_publish_irrigation_status() not yet implemented (Phase 2)");
    return ESP_ERR_NOT_SUPPORTED;
}

/* ========================== SUBSCRIPTION ========================== */

esp_err_t mqtt_client_subscribe_irrigation_commands(void)
{
    if (!s_mqtt_ctx.initialized) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_mqtt_ctx.state != MQTT_STATE_CONNECTED) {
        ESP_LOGE(TAG, "MQTT client not connected");
        return ESP_ERR_INVALID_STATE;
    }

    // Build topic: irrigation/control/{mac_address}
    uint8_t mac[6];
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MAC address: %s", esp_err_to_name(ret));
        return ret;
    }

    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    char topic[MQTT_MAX_TOPIC_LENGTH];
    MQTT_BUILD_CONTROL_TOPIC(topic, mac_str);

    ESP_LOGI(TAG, "Subscribing to irrigation commands: %s", topic);

    int msg_id = esp_mqtt_client_subscribe(s_mqtt_ctx.client, topic, MQTT_DEFAULT_QOS);
    if (msg_id == -1) {
        ESP_LOGE(TAG, "Failed to subscribe to irrigation commands");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Subscribed to irrigation commands, msg_id=%d", msg_id);
    return ESP_OK;
}

esp_err_t mqtt_client_register_command_callback(mqtt_irrigation_command_cb_t callback,
                                                void* user_data)
{
    if (callback == NULL) {
        ESP_LOGE(TAG, "Callback is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    s_mqtt_ctx.cmd_callback = callback;
    s_mqtt_ctx.cmd_callback_user_data = user_data;

    ESP_LOGI(TAG, "Irrigation command callback registered");

    // Auto-subscribe if already connected
    if (s_mqtt_ctx.state == MQTT_STATE_CONNECTED) {
        return mqtt_client_subscribe_irrigation_commands();
    }

    return ESP_OK;
}

/**
 * @brief Handle irrigation command received via MQTT
 *
 * Parses JSON payload: {"command": "start|stop|emergency_stop", "duration_minutes": 15}
 * Calls registered callback if command is valid.
 */
static void mqtt_handle_irrigation_command(esp_mqtt_event_t* event)
{
    // Verify topic matches irrigation/control/<MAC>
    if (event->topic_len < 20 || strncmp(event->topic, MQTT_TOPIC_CONTROL_PREFIX,
                                         strlen(MQTT_TOPIC_CONTROL_PREFIX)) != 0) {
        return;  // Not an irrigation command topic
    }

    ESP_LOGI(TAG, "Received irrigation command");

    // Check if callback is registered
    if (s_mqtt_ctx.cmd_callback == NULL) {
        ESP_LOGW(TAG, "No irrigation command callback registered, ignoring command");
        return;
    }

    // Parse JSON payload
    cJSON *json = cJSON_ParseWithLength(event->data, event->data_len);
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to parse irrigation command JSON");
        return;
    }

    // Extract command
    cJSON *cmd_item = cJSON_GetObjectItem(json, "command");
    if (!cJSON_IsString(cmd_item)) {
        ESP_LOGE(TAG, "Invalid command format");
        cJSON_Delete(json);
        return;
    }

    const char *cmd_str = cJSON_GetStringValue(cmd_item);
    irrigation_command_t command;

    if (strcmp(cmd_str, "start") == 0) {
        command = IRRIGATION_CMD_START;
    } else if (strcmp(cmd_str, "stop") == 0) {
        command = IRRIGATION_CMD_STOP;
    } else if (strcmp(cmd_str, "emergency_stop") == 0) {
        command = IRRIGATION_CMD_EMERGENCY_STOP;
    } else {
        ESP_LOGE(TAG, "Unknown irrigation command: %s", cmd_str);
        cJSON_Delete(json);
        return;
    }

    // Extract duration (optional, defaults to 0 for stop commands)
    uint16_t duration_minutes = 0;
    cJSON *duration_item = cJSON_GetObjectItem(json, "duration_minutes");
    if (cJSON_IsNumber(duration_item)) {
        duration_minutes = (uint16_t)cJSON_GetNumberValue(duration_item);
    }

    ESP_LOGI(TAG, "Irrigation command parsed: cmd=%s, duration=%d min",
             cmd_str, duration_minutes);

    // Call registered callback
    s_mqtt_ctx.cmd_callback(command, duration_minutes, s_mqtt_ctx.cmd_callback_user_data);

    cJSON_Delete(json);
}

/* ========================== STATUS AND HELPERS ========================== */

esp_err_t mqtt_client_get_status(mqtt_status_t* status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(status, &s_mqtt_ctx.status, sizeof(mqtt_status_t));
    status->state = s_mqtt_ctx.state;
    status->connected = (s_mqtt_ctx.state == MQTT_STATE_CONNECTED);

    return ESP_OK;
}

bool mqtt_client_is_connected(void)
{
    return (s_mqtt_ctx.state == MQTT_STATE_CONNECTED);
}
