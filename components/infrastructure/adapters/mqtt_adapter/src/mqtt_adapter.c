#include "mqtt_adapter.h"
#include "mqtt_connection.h"
#include "device_registration_message.h"
#include "device_config_service.h"
#include "wifi_adapter.h"
#include "shared_resource_manager.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

static const char *TAG = "mqtt_adapter";

ESP_EVENT_DEFINE_BASE(MQTT_ADAPTER_EVENTS);

// Global MQTT connection entity
static mqtt_connection_t s_mqtt_connection = {0};
static bool s_adapter_initialized = false;
static esp_timer_handle_t s_reconnect_timer = NULL;

// Forward declarations
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static esp_err_t mqtt_adapter_configure_client(void);
static esp_err_t mqtt_adapter_generate_client_id(void);
static void mqtt_adapter_wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void mqtt_reconnect_timer_callback(void* arg);
static esp_err_t mqtt_adapter_start_reconnect_timer(uint32_t delay_ms);

/**
 * @brief MQTT event handler for ESP-MQTT client events
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT client connected to broker");
            s_mqtt_connection.state = MQTT_CONNECTION_STATE_CONNECTED;
            s_mqtt_connection.stats.connect_count++;
            s_mqtt_connection.stats.last_connect_time = esp_timer_get_time() / 1000000; // Convert to seconds
            s_mqtt_connection.stats.current_retry_delay_ms = 30000; // Reset retry delay to 30 seconds
            
            // Stop reconnection timer if running
            if (s_reconnect_timer != NULL) {
                esp_timer_stop(s_reconnect_timer);
            }
            
            esp_event_post(MQTT_ADAPTER_EVENTS, MQTT_ADAPTER_EVENT_CONNECTED, NULL, 0, portMAX_DELAY);
            
            // Automatically publish device registration after connection
            esp_err_t ret = mqtt_adapter_publish_device_registration();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to publish device registration: %s", esp_err_to_name(ret));
            }
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT client disconnected from broker");
            s_mqtt_connection.state = MQTT_CONNECTION_STATE_DISCONNECTED;
            s_mqtt_connection.stats.disconnect_count++;
            s_mqtt_connection.stats.last_disconnect_time = esp_timer_get_time() / 1000000;
            s_mqtt_connection.device_registered = false;
            
            esp_event_post(MQTT_ADAPTER_EVENTS, MQTT_ADAPTER_EVENT_DISCONNECTED, NULL, 0, portMAX_DELAY);
            
            // Start reconnection timer with exponential backoff
            mqtt_adapter_start_reconnect_timer(s_mqtt_connection.stats.current_retry_delay_ms);
            break;
            
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT message published, msg_id=%d", event->msg_id);
            s_mqtt_connection.stats.publish_count++;
            esp_event_post(MQTT_ADAPTER_EVENTS, MQTT_ADAPTER_EVENT_PUBLISHED, &event->msg_id, sizeof(int), portMAX_DELAY);
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT client error occurred");
            s_mqtt_connection.state = MQTT_CONNECTION_STATE_ERROR;
            esp_event_post(MQTT_ADAPTER_EVENTS, MQTT_ADAPTER_EVENT_ERROR, NULL, 0, portMAX_DELAY);
            
            // Start reconnection timer on error
            mqtt_adapter_start_reconnect_timer(s_mqtt_connection.stats.current_retry_delay_ms);
            break;
            
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT message received on topic: %.*s", event->topic_len, event->topic);
            // For future implementation of command subscriptions
            break;
            
        default:
            ESP_LOGD(TAG, "MQTT event: %" PRId32, event_id);
            break;
    }
}

/**
 * @brief WiFi event handler for MQTT adapter
 */
static void mqtt_adapter_wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_ADAPTER_EVENTS) {
        switch (event_id) {
            case WIFI_ADAPTER_EVENT_IP_OBTAINED:
                ESP_LOGI(TAG, "WiFi IP obtained - starting MQTT client");
                if (s_adapter_initialized && s_mqtt_connection.state == MQTT_CONNECTION_STATE_INITIALIZED) {
                    esp_err_t ret = mqtt_adapter_start();
                    if (ret != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to start MQTT client after WiFi IP obtained: %s", esp_err_to_name(ret));
                    }
                }
                break;
                
            case WIFI_ADAPTER_EVENT_DISCONNECTED:
                ESP_LOGW(TAG, "WiFi disconnected - MQTT will attempt reconnection when WiFi recovers");
                break;
                
            default:
                break;
        }
    }
}

/**
 * @brief Generate MQTT client ID based on MAC address
 */
static esp_err_t mqtt_adapter_generate_client_id(void)
{
    uint8_t mac[6];
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MAC address: %s", esp_err_to_name(ret));
        return ret;
    }
    
    snprintf(s_mqtt_connection.config.client_id, sizeof(s_mqtt_connection.config.client_id),
             "%s_%02X%02X%02X", "liwaisi_sis", mac[3], mac[4], mac[5]);
    
    ESP_LOGI(TAG, "Generated MQTT client ID: %s", s_mqtt_connection.config.client_id);
    return ESP_OK;
}

/**
 * @brief Configure ESP-MQTT client with WebSocket transport
 */
static esp_err_t mqtt_adapter_configure_client(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = s_mqtt_connection.config.broker_uri,
        .credentials.client_id = s_mqtt_connection.config.client_id,
        .session.keepalive = s_mqtt_connection.config.keepalive_seconds,
        .session.disable_clean_session = !s_mqtt_connection.config.clean_session,
        .buffer.size = 2048,
        .task.stack_size = 8192,
    };
    
    s_mqtt_connection.mqtt_client_handle = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_connection.mqtt_client_handle == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }
    
    esp_err_t ret = esp_mqtt_client_register_event(s_mqtt_connection.mqtt_client_handle, 
                                                   ESP_EVENT_ANY_ID, 
                                                   mqtt_event_handler, 
                                                   NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "MQTT client configured successfully");
    return ESP_OK;
}

/**
 * @brief Reconnection timer callback
 */
static void mqtt_reconnect_timer_callback(void* arg)
{
    ESP_LOGI(TAG, "Reconnection timer triggered - attempting MQTT reconnection");
    
    if (s_mqtt_connection.state == MQTT_CONNECTION_STATE_DISCONNECTED || 
        s_mqtt_connection.state == MQTT_CONNECTION_STATE_ERROR) {
        
        s_mqtt_connection.state = MQTT_CONNECTION_STATE_RECONNECTING;
        esp_event_post(MQTT_ADAPTER_EVENTS, MQTT_ADAPTER_EVENT_CONNECTING, NULL, 0, portMAX_DELAY);
        
        esp_err_t ret = esp_mqtt_client_start(s_mqtt_connection.mqtt_client_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start MQTT client during reconnection: %s", esp_err_to_name(ret));
            
            // Increase retry delay with exponential backoff
            s_mqtt_connection.stats.current_retry_delay_ms *= 2;
            if (s_mqtt_connection.stats.current_retry_delay_ms > 3600000) { // 1 hour max
                s_mqtt_connection.stats.current_retry_delay_ms = 3600000;
            }
            
            // Schedule next retry
            mqtt_adapter_start_reconnect_timer(s_mqtt_connection.stats.current_retry_delay_ms);
        }
    }
}

/**
 * @brief Start reconnection timer with specified delay
 */
static esp_err_t mqtt_adapter_start_reconnect_timer(uint32_t delay_ms)
{
    if (s_reconnect_timer == NULL) {
        esp_timer_create_args_t timer_args = {
            .callback = mqtt_reconnect_timer_callback,
            .arg = NULL,
            .name = "mqtt_reconnect"
        };
        
        esp_err_t ret = esp_timer_create(&timer_args, &s_reconnect_timer);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create reconnect timer: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    
    ESP_LOGI(TAG, "Scheduling MQTT reconnection in %" PRIu32 " ms", delay_ms);
    return esp_timer_start_once(s_reconnect_timer, delay_ms * 1000); // Convert to microseconds
}

esp_err_t mqtt_adapter_init(void)
{
    if (s_adapter_initialized) {
        ESP_LOGW(TAG, "MQTT adapter already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing MQTT adapter");
    
    // Initialize connection entity
    memset(&s_mqtt_connection, 0, sizeof(mqtt_connection_t));
    s_mqtt_connection.state = MQTT_CONNECTION_STATE_UNINITIALIZED;
    
    // Configure connection parameters
    strncpy(s_mqtt_connection.config.broker_uri, "mqtt://192.168.1.198:1883", 
            sizeof(s_mqtt_connection.config.broker_uri) - 1);
    s_mqtt_connection.config.keepalive_seconds = 60;
    s_mqtt_connection.config.qos_level = 1;
    s_mqtt_connection.config.clean_session = true;
    s_mqtt_connection.stats.current_retry_delay_ms = 30000; // 30 seconds initial delay
    
    // Generate client ID
    esp_err_t ret = mqtt_adapter_generate_client_id();
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Configure MQTT client
    ret = mqtt_adapter_configure_client();
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Register WiFi event handler
    ret = esp_event_handler_register(WIFI_ADAPTER_EVENTS, ESP_EVENT_ANY_ID, 
                                     &mqtt_adapter_wifi_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WiFi event handler: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_mqtt_connection.state = MQTT_CONNECTION_STATE_INITIALIZED;
    s_adapter_initialized = true;
    
    ESP_LOGI(TAG, "MQTT adapter initialized successfully");
    ESP_LOGI(TAG, "Broker URI: %s", s_mqtt_connection.config.broker_uri);
    ESP_LOGI(TAG, "Client ID: %s", s_mqtt_connection.config.client_id);
    
    esp_event_post(MQTT_ADAPTER_EVENTS, MQTT_ADAPTER_EVENT_INIT_COMPLETE, NULL, 0, portMAX_DELAY);
    
    return ESP_OK;
}

esp_err_t mqtt_adapter_start(void)
{
    if (!s_adapter_initialized) {
        ESP_LOGE(TAG, "MQTT adapter not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_mqtt_connection.state == MQTT_CONNECTION_STATE_CONNECTED) {
        ESP_LOGW(TAG, "MQTT client already connected");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting MQTT client");
    s_mqtt_connection.state = MQTT_CONNECTION_STATE_CONNECTING;
    esp_event_post(MQTT_ADAPTER_EVENTS, MQTT_ADAPTER_EVENT_CONNECTING, NULL, 0, portMAX_DELAY);
    
    esp_err_t ret = esp_mqtt_client_start(s_mqtt_connection.mqtt_client_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(ret));
        s_mqtt_connection.state = MQTT_CONNECTION_STATE_ERROR;
        return ret;
    }
    
    return ESP_OK;
}

esp_err_t mqtt_adapter_stop(void)
{
    if (!s_adapter_initialized) {
        ESP_LOGW(TAG, "MQTT adapter not initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping MQTT client");
    
    // Stop reconnection timer
    if (s_reconnect_timer != NULL) {
        esp_timer_stop(s_reconnect_timer);
    }
    
    if (s_mqtt_connection.mqtt_client_handle != NULL) {
        esp_err_t ret = esp_mqtt_client_stop(s_mqtt_connection.mqtt_client_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop MQTT client: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    
    s_mqtt_connection.state = MQTT_CONNECTION_STATE_DISCONNECTED;
    return ESP_OK;
}

esp_err_t mqtt_adapter_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing MQTT adapter");
    
    // Stop the client first
    mqtt_adapter_stop();
    
    // Clean up reconnection timer
    if (s_reconnect_timer != NULL) {
        esp_timer_delete(s_reconnect_timer);
        s_reconnect_timer = NULL;
    }
    
    // Destroy MQTT client
    if (s_mqtt_connection.mqtt_client_handle != NULL) {
        esp_mqtt_client_destroy(s_mqtt_connection.mqtt_client_handle);
        s_mqtt_connection.mqtt_client_handle = NULL;
    }
    
    // Unregister event handlers
    esp_event_handler_unregister(WIFI_ADAPTER_EVENTS, ESP_EVENT_ANY_ID, &mqtt_adapter_wifi_event_handler);
    
    // Reset state
    memset(&s_mqtt_connection, 0, sizeof(mqtt_connection_t));
    s_adapter_initialized = false;
    
    ESP_LOGI(TAG, "MQTT adapter deinitialized");
    return ESP_OK;
}

esp_err_t mqtt_adapter_get_status(mqtt_adapter_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    status->state = s_mqtt_connection.state;
    status->connected = (s_mqtt_connection.state == MQTT_CONNECTION_STATE_CONNECTED);
    status->device_registered = s_mqtt_connection.device_registered;
    status->reconnect_count = s_mqtt_connection.stats.disconnect_count;
    status->message_count = s_mqtt_connection.stats.publish_count;
    strncpy(status->client_id, s_mqtt_connection.config.client_id, sizeof(status->client_id) - 1);
    status->client_id[sizeof(status->client_id) - 1] = '\0';
    
    return ESP_OK;
}

bool mqtt_adapter_is_connected(void)
{
    return (s_mqtt_connection.state == MQTT_CONNECTION_STATE_CONNECTED);
}

esp_err_t mqtt_adapter_reconnect(void)
{
    if (!s_adapter_initialized) {
        ESP_LOGE(TAG, "MQTT adapter not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_mqtt_connection.state == MQTT_CONNECTION_STATE_CONNECTED) {
        ESP_LOGW(TAG, "MQTT client already connected");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Force reconnection requested");
    
    // Stop any running reconnection timer
    if (s_reconnect_timer != NULL) {
        esp_timer_stop(s_reconnect_timer);
    }
    
    // Reset retry delay
    s_mqtt_connection.stats.current_retry_delay_ms = 30000; // 30 seconds initial delay
    
    // Start reconnection immediately
    mqtt_reconnect_timer_callback(NULL);
    
    return ESP_OK;
}

esp_err_t mqtt_adapter_publish_device_registration(void)
{
    if (!s_adapter_initialized) {
        ESP_LOGE(TAG, "MQTT adapter not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_mqtt_connection.state != MQTT_CONNECTION_STATE_CONNECTED) {
        ESP_LOGE(TAG, "MQTT client not connected");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Publishing device registration message");
    
    // Create device registration message
    device_registration_message_t reg_message = {0};
    
    // Set event type
    strncpy(reg_message.event_type, "register", sizeof(reg_message.event_type) - 1);
    
    // Get MAC address
    uint8_t mac[6];
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MAC address: %s", esp_err_to_name(ret));
        return ret;
    }
    snprintf(reg_message.mac_address, sizeof(reg_message.mac_address),
             "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    // Get device information from NVS (with semaphore protection)
    esp_err_t nvs_ret = shared_resource_take(SHARED_RESOURCE_NVS, 5000); // 5 second timeout
    if (nvs_ret == ESP_OK) {
        // Get device name from NVS
        ret = device_config_service_get_name(reg_message.device_name, sizeof(reg_message.device_name));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to get device name from NVS, using default: %s", esp_err_to_name(ret));
            strncpy(reg_message.device_name, "Smart Irrigation Device", sizeof(reg_message.device_name) - 1);
        }
        
        // Get location description from NVS
        ret = device_config_service_get_location(reg_message.location_description, sizeof(reg_message.location_description));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to get location from NVS, using default: %s", esp_err_to_name(ret));
            strncpy(reg_message.location_description, "Unknown Location", sizeof(reg_message.location_description) - 1);
        }
        
        shared_resource_give(SHARED_RESOURCE_NVS);
    } else {
        ESP_LOGW(TAG, "Failed to take NVS semaphore, using default values");
        strncpy(reg_message.device_name, "Smart Irrigation Device", sizeof(reg_message.device_name) - 1);
        strncpy(reg_message.location_description, "Unknown Location", sizeof(reg_message.location_description) - 1);
    }
    
    // Get current IP address
    esp_ip4_addr_t ip;
    ret = wifi_adapter_get_ip(&ip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get IP address: %s", esp_err_to_name(ret));
        return ret;
    }
    snprintf(reg_message.ip_address, sizeof(reg_message.ip_address),
             IPSTR, IP2STR(&ip));
    
    // Create JSON payload (with semaphore protection)
    esp_err_t json_ret = shared_resource_take(SHARED_RESOURCE_JSON, 3000); // 3 second timeout
    if (json_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to take JSON semaphore: %s", esp_err_to_name(json_ret));
        return json_ret;
    }
    
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        shared_resource_give(SHARED_RESOURCE_JSON);
        return ESP_ERR_NO_MEM;
    }
    
    cJSON *event_type = cJSON_CreateString(reg_message.event_type);
    cJSON *mac_address = cJSON_CreateString(reg_message.mac_address);
    cJSON *device_name = cJSON_CreateString(reg_message.device_name);
    cJSON *ip_address = cJSON_CreateString(reg_message.ip_address);
    cJSON *location_description = cJSON_CreateString(reg_message.location_description);
    
    if (event_type == NULL || mac_address == NULL || device_name == NULL || 
        ip_address == NULL || location_description == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON strings");
        cJSON_Delete(json);
        shared_resource_give(SHARED_RESOURCE_JSON);
        return ESP_ERR_NO_MEM;
    }
    
    cJSON_AddItemToObject(json, "event_type", event_type);
    cJSON_AddItemToObject(json, "mac_address", mac_address);
    cJSON_AddItemToObject(json, "device_name", device_name);
    cJSON_AddItemToObject(json, "ip_address", ip_address);
    cJSON_AddItemToObject(json, "location_description", location_description);
    
    char *json_string = cJSON_Print(json);
    if (json_string == NULL) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        cJSON_Delete(json);
        shared_resource_give(SHARED_RESOURCE_JSON);
        return ESP_ERR_NO_MEM;
    }
    
    // JSON operations complete, release semaphore
    shared_resource_give(SHARED_RESOURCE_JSON);
    
    // Publish to MQTT topic
    const char *topic = "/liwaisi/iot/smart-irrigation/device/registration";
    int msg_id = esp_mqtt_client_publish(s_mqtt_connection.mqtt_client_handle,
                                         topic,
                                         json_string,
                                         0, // Let ESP-MQTT calculate length
                                         s_mqtt_connection.config.qos_level,
                                         0); // Don't retain
    
    if (msg_id == -1) {
        ESP_LOGE(TAG, "Failed to publish device registration message");
        ret = ESP_FAIL;
    } else {
        ESP_LOGI(TAG, "Device registration message published successfully");
        ESP_LOGI(TAG, "Topic: %s", topic);
        ESP_LOGI(TAG, "Message ID: %d", msg_id);
        ESP_LOGI(TAG, "Payload: %s", json_string);
        
        s_mqtt_connection.device_registered = true;
        esp_event_post(MQTT_ADAPTER_EVENTS, MQTT_ADAPTER_EVENT_DEVICE_REGISTERED, &msg_id, sizeof(int), portMAX_DELAY);
        ret = ESP_OK;
    }
    
    // Cleanup
    free(json_string);
    cJSON_Delete(json);
    
    return ret;
}