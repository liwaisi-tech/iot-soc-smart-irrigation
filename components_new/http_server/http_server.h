/**
 * @file http_server.h
 * @brief HTTP Server Component - REST API and Device Information
 *
 * Simple HTTP server for device information and sensor data.
 * Provides REST endpoints for monitoring and diagnostics.
 *
 * Component Responsibilities:
 * - HTTP server management
 * - REST API endpoints (/whoami, /sensors, /ping, /status)
 * - JSON response formatting
 * - Request logging
 * - CORS support
 *
 * Migration from hexagonal: Consolidates http_adapter + endpoints + middleware
 *
 * @author Liwaisi Tech
 * @date 2025-10-01
 * @version 2.0.0 - Component-Based Architecture
 */

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "esp_err.h"
#include "esp_http_server.h"
#include "common_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ TYPES AND ENUMS ============================ */

/**
 * @brief HTTP server state
 */
typedef enum {
    HTTP_STATE_UNINITIALIZED = 0,   ///< Not initialized
    HTTP_STATE_STOPPED,             ///< Initialized but stopped
    HTTP_STATE_RUNNING,             ///< Server running
    HTTP_STATE_ERROR                ///< Error state
} http_server_state_t;

/**
 * @brief HTTP endpoint
 */
typedef enum {
    HTTP_ENDPOINT_WHOAMI = 0,       ///< /whoami - Device information
    HTTP_ENDPOINT_SENSORS,          ///< /sensors - Current sensor readings
    HTTP_ENDPOINT_PING,             ///< /ping - Health check
    HTTP_ENDPOINT_STATUS,           ///< /status - System status
    HTTP_ENDPOINT_IRRIGATION,       ///< /irrigation - Irrigation status
    HTTP_ENDPOINT_CONFIG,           ///< /config - Device configuration
    HTTP_ENDPOINT_COUNT             ///< Total endpoint count
} http_endpoint_t;

/**
 * @brief HTTP server configuration
 */
typedef struct {
    uint16_t port;                  ///< Server port (default: 80)
    uint16_t max_uri_handlers;      ///< Max URI handlers (default: 10)
    uint16_t max_resp_headers;      ///< Max response headers (default: 8)
    size_t stack_size;              ///< Server task stack size (default: 4096)
    uint8_t priority;               ///< Server task priority (default: 5)
    bool enable_logging;            ///< Enable request logging
    bool enable_cors;               ///< Enable CORS headers
} http_server_config_t;

/**
 * @brief HTTP server status
 */
typedef struct {
    http_server_state_t state;      ///< Server state
    uint16_t port;                  ///< Listening port
    uint32_t total_requests;        ///< Total requests served
    uint32_t error_count;           ///< Total errors
    uint32_t last_request_time;     ///< Last request timestamp
    httpd_handle_t handle;          ///< Server handle (internal use)
} http_server_status_t;

/**
 * @brief HTTP request statistics
 */
typedef struct {
    uint32_t requests[HTTP_ENDPOINT_COUNT]; ///< Requests per endpoint
    uint32_t total_requests;        ///< Total requests
    uint32_t total_errors;          ///< Total errors
    uint32_t avg_response_time_ms;  ///< Average response time
} http_request_stats_t;

/* ============================ PUBLIC API ============================ */

/**
 * @brief Initialize HTTP server component
 *
 * Initializes HTTP server with configuration.
 * Requires WiFi connection with IP address.
 *
 * @param config Server configuration (NULL for default)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t http_server_init(const http_server_config_t* config);

/**
 * @brief Start HTTP server
 *
 * Starts HTTP server and begins accepting requests.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t http_server_start(void);

/**
 * @brief Stop HTTP server
 *
 * Stops server gracefully.
 *
 * @return ESP_OK on success
 */
esp_err_t http_server_stop(void);

/**
 * @brief Deinitialize HTTP server
 *
 * Stops server and cleans up resources.
 *
 * @return ESP_OK on success
 */
esp_err_t http_server_deinit(void);

/**
 * @brief Get server status
 *
 * @param[out] status Pointer to status structure to fill
 * @return ESP_OK on success
 */
esp_err_t http_server_get_status(http_server_status_t* status);

/**
 * @brief Get request statistics
 *
 * @param[out] stats Pointer to stats structure to fill
 * @return ESP_OK on success
 */
esp_err_t http_server_get_stats(http_request_stats_t* stats);

/**
 * @brief Check if server is running
 *
 * @return true if server is accepting requests
 */
bool http_server_is_running(void);

/**
 * @brief Reset request statistics
 *
 * @return ESP_OK on success
 */
esp_err_t http_server_reset_stats(void);

/* ============================ ENDPOINT RESPONSES ============================ */

/**
 * @brief Endpoint: GET /whoami
 *
 * Returns device information including name, crop, MAC, IP, firmware version,
 * and available API endpoints.
 *
 * Response (JSON):
 * {
 *   "device_name": "ESP32_Riego_01",
 *   "crop_name": "tomates",
 *   "mac_address": "XX:XX:XX:XX:XX:XX",
 *   "ip_address": "192.168.1.100",
 *   "firmware_version": "2.0.0",
 *   "uptime_seconds": 12345,
 *   "endpoints": ["/whoami", "/sensors", "/ping", "/status", "/irrigation"]
 * }
 */

/**
 * @brief Endpoint: GET /sensors
 *
 * Returns current sensor readings (immediate read, not cached).
 *
 * Response (JSON):
 * {
 *   "ambient_temperature": 25.6,
 *   "ambient_humidity": 65.2,
 *   "soil_humidity_1": 45.8,
 *   "soil_humidity_2": 42.1,
 *   "soil_humidity_3": 48.3,
 *   "timestamp": 1640995200
 * }
 */

/**
 * @brief Endpoint: GET /ping
 *
 * Health check endpoint.
 *
 * Response (JSON):
 * {
 *   "status": "ok",
 *   "uptime_seconds": 12345,
 *   "free_heap": 180000
 * }
 */

/**
 * @brief Endpoint: GET /status
 *
 * Returns complete system status including connectivity, components health,
 * and memory usage.
 *
 * Response (JSON):
 * {
 *   "system_health": "healthy",
 *   "connectivity": "full",
 *   "wifi_connected": true,
 *   "mqtt_connected": true,
 *   "irrigation_state": "idle",
 *   "free_heap": 180000,
 *   "uptime_seconds": 12345
 * }
 */

/**
 * @brief Endpoint: GET /irrigation
 *
 * Returns irrigation controller status.
 *
 * Response (JSON):
 * {
 *   "state": "idle",
 *   "mode": "online",
 *   "is_irrigating": false,
 *   "session_elapsed_sec": 0,
 *   "total_sessions": 15,
 *   "today_runtime_sec": 900
 * }
 */

/**
 * @brief Endpoint: GET /config
 *
 * Returns device configuration (non-sensitive data only).
 *
 * Response (JSON):
 * {
 *   "device_name": "ESP32_Riego_01",
 *   "crop_name": "tomates",
 *   "soil_threshold": 45.0,
 *   "max_irrigation_duration_min": 120,
 *   "sensor_count": 3,
 *   "reading_interval_sec": 60
 * }
 */

/* ============================ CONFIGURATION ============================ */

/**
 * @brief Default HTTP server configuration
 */
#define HTTP_SERVER_DEFAULT_CONFIG() {      \
    .port = 80,                             \
    .max_uri_handlers = 10,                 \
    .max_resp_headers = 8,                  \
    .stack_size = 4096,                     \
    .priority = 5,                          \
    .enable_logging = true,                 \
    .enable_cors = true                     \
}

/**
 * @brief HTTP server port
 */
#define HTTP_SERVER_DEFAULT_PORT    80

/**
 * @brief Maximum response buffer size
 */
#define HTTP_MAX_RESPONSE_SIZE      1024

/**
 * @brief Request timeout (seconds)
 */
#define HTTP_REQUEST_TIMEOUT_SEC    10

/**
 * @brief API version
 */
#define HTTP_API_VERSION            "2.0"

/**
 * @brief CORS headers
 */
#define HTTP_HEADER_CORS_ORIGIN         "Access-Control-Allow-Origin"
#define HTTP_HEADER_CORS_METHODS        "Access-Control-Allow-Methods"
#define HTTP_HEADER_CORS_HEADERS        "Access-Control-Allow-Headers"
#define HTTP_CORS_ORIGIN_VALUE          "*"
#define HTTP_CORS_METHODS_VALUE         "GET, POST, OPTIONS"
#define HTTP_CORS_HEADERS_VALUE         "Content-Type"

/**
 * @brief Content type headers
 */
#define HTTP_CONTENT_TYPE_JSON      "application/json"
#define HTTP_CONTENT_TYPE_TEXT      "text/plain"

/**
 * @brief HTTP status codes (commonly used)
 */
#define HTTP_STATUS_OK              200
#define HTTP_STATUS_BAD_REQUEST     400
#define HTTP_STATUS_NOT_FOUND       404
#define HTTP_STATUS_SERVER_ERROR    500

/* ============================ URI PATHS ============================ */

#define HTTP_URI_WHOAMI             "/whoami"
#define HTTP_URI_SENSORS            "/sensors"
#define HTTP_URI_PING               "/ping"
#define HTTP_URI_STATUS             "/status"
#define HTTP_URI_IRRIGATION         "/irrigation"
#define HTTP_URI_CONFIG             "/config"

#ifdef __cplusplus
}
#endif

#endif // HTTP_SERVER_H
