/**
 * @file notification_service.h
 * @brief Notification Service Component - Webhook and Event Notifications
 *
 * Handles sending notifications and webhook events to external services (N8N).
 * Manages HTTP communication for irrigation events with WiFi awareness.
 *
 * Component Responsibilities:
 * - N8N webhook integration for irrigation events
 * - HTTP client management for outbound notifications
 * - WiFi connectivity verification before sending
 * - JSON payload construction for events
 *
 * Thread-Safety:
 * - All state access protected by spinlock (portMUX_TYPE)
 * - Safe for concurrent calls from multiple tasks
 *
 * Configuration:
 * - N8N webhook URL and API key via Kconfig
 * - WiFi verification before sending (optional)
 *
 * @author Liwaisi Tech
 * @date 2025-10-26
 * @version 1.0.0 - Extracted from irrigation_controller
 */

#ifndef NOTIFICATION_SERVICE_H
#define NOTIFICATION_SERVICE_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ PUBLIC API ============================ */

/**
 * @brief Initialize notification service component
 *
 * Initializes notification service and prepares for webhook transmission.
 * Must be called before sending any notifications.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t notification_service_init(void);

/**
 * @brief Deinitialize notification service
 *
 * Cleans up resources and stops notification service.
 *
 * @return ESP_OK on success
 */
esp_err_t notification_service_deinit(void);

/**
 * @brief Send irrigation event notification via N8N webhook
 *
 * Sends HTTP POST to N8N webhook with irrigation event data.
 * Verifies WiFi connectivity before sending (if CONFIG_NOTIFICATION_VERIFY_WIFI=true).
 *
 * Supported Event Types:
 * - "irrigation_on" - Irrigation started
 * - "irrigation_off" - Irrigation stopped
 * - "sensor_error" - Sensor reading failed
 * - "temperature_critical" - Temperature threshold exceeded
 * - "emergency_stop" - Emergency stop triggered
 *
 * @param event_type Type of irrigation event (required)
 * @param soil_moisture Average soil moisture percentage (0-100%)
 * @param humidity Ambient humidity percentage (0-100%)
 * @param temperature Ambient temperature in °C
 *
 * @return ESP_OK if sent/queued successfully, error code otherwise
 * @return ESP_ERR_INVALID_ARG if event_type is NULL
 * @return ESP_ERR_INVALID_STATE if WiFi is offline (when verification enabled)
 * @return ESP_ERR_NO_MEM if JSON serialization failed
 *
 * Thread-Safe: Yes
 */
esp_err_t notification_send_irrigation_event(
    const char* event_type,
    float soil_moisture,
    float humidity,
    float temperature);

/**
 * @brief Check if notification service is initialized
 *
 * @return true if initialized and ready to send notifications
 */
bool notification_service_is_initialized(void);

/**
 * @brief Check if last notification was sent successfully
 *
 * @return true if last attempted notification was successful
 */
bool notification_service_is_last_send_ok(void);

/**
 * @brief Get count of notifications sent since boot
 *
 * @return Number of successfully sent notifications
 */
uint32_t notification_service_get_send_count(void);

/**
 * @brief Get count of failed notification attempts
 *
 * @return Number of failed notification attempts
 */
uint32_t notification_service_get_error_count(void);

/**
 * @brief Reset notification statistics
 *
 * Resets send and error counters.
 *
 * @return ESP_OK on success
 */
esp_err_t notification_service_reset_stats(void);

/* ============================ CONFIGURATION ============================ */

/**
 * @brief Notification service NVS namespace
 */
#define NOTIFICATION_SERVICE_NVS_NAMESPACE "notif_svc"

/**
 * @brief Notification statistics NVS keys
 */
#define NOTIFICATION_NVS_KEY_SEND_COUNT    "send_cnt"
#define NOTIFICATION_NVS_KEY_ERROR_COUNT   "err_cnt"
#define NOTIFICATION_NVS_KEY_LAST_SEND_OK  "last_ok"

#ifdef __cplusplus
}
#endif

#endif // NOTIFICATION_SERVICE_H
