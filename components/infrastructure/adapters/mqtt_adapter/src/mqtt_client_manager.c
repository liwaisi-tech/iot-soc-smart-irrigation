#include "mqtt_client_manager.h"
#include "esp_log.h"
#include <string.h>
#include <inttypes.h>

static const char *TAG = "mqtt_client_manager";

// This file provides additional MQTT client management utilities
// Currently, most functionality is handled directly in mqtt_adapter.c
// This file is reserved for future extensions like:
// - Advanced connection pooling
// - Message queuing during disconnections
// - Client configuration management
// - Connection health monitoring

/**
 * @brief Get string representation of MQTT connection state
 */
const char* mqtt_client_manager_state_to_string(mqtt_adapter_state_t state)
{
    switch (state) {
        case MQTT_ADAPTER_STATE_UNINITIALIZED: return "UNINITIALIZED";
        case MQTT_ADAPTER_STATE_INITIALIZED: return "INITIALIZED";
        case MQTT_ADAPTER_STATE_CONNECTING: return "CONNECTING";
        case MQTT_ADAPTER_STATE_CONNECTED: return "CONNECTED";
        case MQTT_ADAPTER_STATE_DISCONNECTED: return "DISCONNECTED";
        case MQTT_ADAPTER_STATE_RECONNECTING: return "RECONNECTING";
        case MQTT_ADAPTER_STATE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Log MQTT connection statistics
 */
void mqtt_client_manager_log_stats(const mqtt_adapter_status_t *status)
{
    if (status == NULL) {
        ESP_LOGE(TAG, "Invalid status pointer");
        return;
    }
    
    ESP_LOGI(TAG, "MQTT Statistics:");
    ESP_LOGI(TAG, "  State: %s", mqtt_client_manager_state_to_string(status->state));
    ESP_LOGI(TAG, "  Connected: %s", status->connected ? "Yes" : "No");
    ESP_LOGI(TAG, "  Device Registered: %s", status->device_registered ? "Yes" : "No");
    ESP_LOGI(TAG, "  Client ID: %s", status->client_id);
    ESP_LOGI(TAG, "  Reconnect Count: %" PRIu32, status->reconnect_count);
    ESP_LOGI(TAG, "  Message Count: %" PRIu32, status->message_count);
}