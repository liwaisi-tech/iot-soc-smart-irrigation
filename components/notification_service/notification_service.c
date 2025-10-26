/**
 * @file notification_service.c
 * @brief Notification Service Implementation - Webhook transmission
 *
 * Sends irrigation events to N8N webhook with JSON payload.
 * Thread-safe implementation with WiFi awareness.
 *
 * Extracted from irrigation_controller to follow Component-Based Architecture
 * principles (Single Responsibility Component).
 *
 * @author Liwaisi Tech
 * @date 2025-10-26
 * @version 1.0.0
 */

#include "notification_service.h"
#include "wifi_manager.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "sdkconfig.h"
#include <string.h>
#include <stdlib.h>

/* ============================ PRIVATE TYPES ============================ */

/**
 * @brief Internal notification service context
 */
typedef struct {
    bool is_initialized;
    uint32_t send_count;
    uint32_t error_count;
    bool last_send_ok;
} notification_service_context_t;

/* ============================ PRIVATE STATE ============================ */

static const char *TAG = "notification_service";

/**
 * @brief Global notification service context
 */
static notification_service_context_t s_notif_ctx = {
    .is_initialized = false,
    .send_count = 0,
    .error_count = 0,
    .last_send_ok = false
};

/**
 * @brief Spinlock for thread-safe state access
 */
static portMUX_TYPE s_notification_spinlock = portMUX_INITIALIZER_UNLOCKED;

/* ============================ PRIVATE FUNCTIONS ============================ */

/**
 * @brief Send HTTP POST to N8N webhook
 *
 * Internal function that performs the actual HTTP request.
 * Constructs JSON payload and sends to configured N8N URL.
 *
 * @param event_type Event type string
 * @param soil_moisture Soil moisture value
 * @param humidity Ambient humidity
 * @param temperature Ambient temperature
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t _send_webhook_post(
    const char* event_type,
    float soil_moisture,
    float humidity,
    float temperature)
{
    if (event_type == NULL) {
        ESP_LOGE(TAG, "Event type cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Get N8N webhook URL from Kconfig
    #ifdef CONFIG_NOTIFICATION_N8N_WEBHOOK_URL
        #define N8N_WEBHOOK_URL CONFIG_NOTIFICATION_N8N_WEBHOOK_URL
    #else
        #define N8N_WEBHOOK_URL "http://192.168.1.177:5678/webhook/irrigation-events"
    #endif

    // Get API key from Kconfig
    #ifdef CONFIG_NOTIFICATION_N8N_API_KEY
        #define N8N_API_KEY CONFIG_NOTIFICATION_N8N_API_KEY
    #else
        #define N8N_API_KEY "d1b4873f5144c6db6c87f03109010c314a7fb9edf3052f4f478bb2d967181427"
    #endif

    // Build JSON payload
    cJSON* root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "event_type", event_type);
    cJSON_AddStringToObject(root, "device_id", "ESP32_HUERTA_001");

    cJSON* sensor_data = cJSON_CreateObject();
    if (sensor_data == NULL) {
        ESP_LOGE(TAG, "Failed to create sensor_data object");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddNumberToObject(sensor_data, "soil_moisture_prom", soil_moisture);
    cJSON_AddNumberToObject(sensor_data, "humidity", humidity);
    cJSON_AddNumberToObject(sensor_data, "temperature", temperature);
    cJSON_AddItemToObject(root, "sensor_data", sensor_data);

    char* json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to serialize JSON for N8N webhook");
        return ESP_ERR_NO_MEM;
    }

    // Create HTTP client
    esp_http_client_config_t config = {
        .url = N8N_WEBHOOK_URL,
        .method = HTTP_METHOD_POST,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to create HTTP client for N8N webhook");
        free(json_str);
        return ESP_ERR_NO_MEM;
    }

    // Set headers
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "X-API-Key", N8N_API_KEY);

    // Set request body
    esp_http_client_set_post_field(client, json_str, strlen(json_str));

    // Perform request
    esp_err_t ret = esp_http_client_perform(client);

    if (ret == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "N8N webhook sent: %s (HTTP %d)", event_type, status_code);

        // Update stats
        portENTER_CRITICAL(&s_notification_spinlock);
        {
            s_notif_ctx.send_count++;
            s_notif_ctx.last_send_ok = true;
        }
        portEXIT_CRITICAL(&s_notification_spinlock);
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(ret));

        // Update error stats
        portENTER_CRITICAL(&s_notification_spinlock);
        {
            s_notif_ctx.error_count++;
            s_notif_ctx.last_send_ok = false;
        }
        portEXIT_CRITICAL(&s_notification_spinlock);
    }

    esp_http_client_cleanup(client);
    free(json_str);

    return ret;
}

/* ============================ PUBLIC API ============================ */

esp_err_t notification_service_init(void)
{
    portENTER_CRITICAL(&s_notification_spinlock);
    {
        if (s_notif_ctx.is_initialized) {
            portEXIT_CRITICAL(&s_notification_spinlock);
            ESP_LOGW(TAG, "Notification service already initialized");
            return ESP_OK;
        }

        s_notif_ctx.is_initialized = true;
        s_notif_ctx.send_count = 0;
        s_notif_ctx.error_count = 0;
        s_notif_ctx.last_send_ok = false;
    }
    portEXIT_CRITICAL(&s_notification_spinlock);

    ESP_LOGI(TAG, "Notification service initialized");

    // Log configuration
    #ifdef CONFIG_NOTIFICATION_VERIFY_WIFI
        ESP_LOGI(TAG, "  WiFi verification: ENABLED");
    #else
        ESP_LOGI(TAG, "  WiFi verification: DISABLED");
    #endif

    #ifdef CONFIG_NOTIFICATION_N8N_WEBHOOK_URL
        ESP_LOGI(TAG, "  N8N URL: %s", CONFIG_NOTIFICATION_N8N_WEBHOOK_URL);
    #else
        ESP_LOGI(TAG, "  N8N URL: (default)");
    #endif

    return ESP_OK;
}

esp_err_t notification_service_deinit(void)
{
    portENTER_CRITICAL(&s_notification_spinlock);
    {
        s_notif_ctx.is_initialized = false;
    }
    portEXIT_CRITICAL(&s_notification_spinlock);

    ESP_LOGI(TAG, "Notification service deinitialized");
    return ESP_OK;
}

esp_err_t notification_send_irrigation_event(
    const char* event_type,
    float soil_moisture,
    float humidity,
    float temperature)
{
    if (event_type == NULL) {
        ESP_LOGE(TAG, "Event type cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Check if initialized
    bool is_initialized;
    portENTER_CRITICAL(&s_notification_spinlock);
    {
        is_initialized = s_notif_ctx.is_initialized;
    }
    portEXIT_CRITICAL(&s_notification_spinlock);

    if (!is_initialized) {
        ESP_LOGW(TAG, "Notification service not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Verify WiFi connectivity if configured
    #ifdef CONFIG_NOTIFICATION_VERIFY_WIFI
        if (!wifi_manager_is_connected()) {
            ESP_LOGD(TAG, "WiFi offline, skipping notification: %s", event_type);
            return ESP_ERR_INVALID_STATE;
        }
    #endif

    ESP_LOGD(TAG, "Sending notification: %s (soil=%.1f%%, humidity=%.1f%%, temp=%.1f°C)",
             event_type, soil_moisture, humidity, temperature);

    // Send webhook
    return _send_webhook_post(event_type, soil_moisture, humidity, temperature);
}

bool notification_service_is_initialized(void)
{
    bool is_initialized;
    portENTER_CRITICAL(&s_notification_spinlock);
    {
        is_initialized = s_notif_ctx.is_initialized;
    }
    portEXIT_CRITICAL(&s_notification_spinlock);
    return is_initialized;
}

bool notification_service_is_last_send_ok(void)
{
    bool last_ok;
    portENTER_CRITICAL(&s_notification_spinlock);
    {
        last_ok = s_notif_ctx.last_send_ok;
    }
    portEXIT_CRITICAL(&s_notification_spinlock);
    return last_ok;
}

uint32_t notification_service_get_send_count(void)
{
    uint32_t count;
    portENTER_CRITICAL(&s_notification_spinlock);
    {
        count = s_notif_ctx.send_count;
    }
    portEXIT_CRITICAL(&s_notification_spinlock);
    return count;
}

uint32_t notification_service_get_error_count(void)
{
    uint32_t count;
    portENTER_CRITICAL(&s_notification_spinlock);
    {
        count = s_notif_ctx.error_count;
    }
    portEXIT_CRITICAL(&s_notification_spinlock);
    return count;
}

esp_err_t notification_service_reset_stats(void)
{
    portENTER_CRITICAL(&s_notification_spinlock);
    {
        s_notif_ctx.send_count = 0;
        s_notif_ctx.error_count = 0;
        s_notif_ctx.last_send_ok = false;
    }
    portEXIT_CRITICAL(&s_notification_spinlock);

    ESP_LOGI(TAG, "Notification statistics reset");
    return ESP_OK;
}
