/**
 * @file http_server.c
 * @brief HTTP Server Component Implementation - Component-Based Architecture
 *
 * Consolidates HTTP functionality from hexagonal architecture:
 * - http_adapter.c (server wrapper)
 * - server.c (core HTTP server)
 * - whoami.c, temperature_humidity.c, ping.c (endpoints)
 * - logging_middleware.c, middleware_manager.c (middleware)
 *
 * Migration: Hexagonal Architecture → Component-Based Architecture
 * @author Liwaisi Tech
 * @date 2025-10-03
 * @version 2.0.0
 */

#include "http_server.h"
#include "sensor_reader.h"
#include "device_config.h"
#include "wifi_manager.h"

// ESP-IDF includes
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <stdio.h>

/* ========================== CONSTANTS AND MACROS ========================== */

static const char *TAG = "http_server";

// Firmware version
#define FIRMWARE_VERSION                "v2.0.0"

// Response buffer sizes
#define HTTP_RESPONSE_BUFFER_SIZE       1024
#define HTTP_ERROR_BUFFER_SIZE          512

/* ========================== TYPES AND STRUCTURES ========================== */

/**
 * @brief HTTP server internal context
 *
 * Encapsulates complete state of HTTP server component.
 */
typedef struct {
    // ESP-IDF HTTP server handle
    httpd_handle_t server;

    // Component state
    http_server_state_t state;
    bool initialized;

    // Configuration
    http_server_config_t config;

    // Statistics
    http_request_stats_t stats;

} http_server_context_t;

/* ========================== GLOBAL STATE ========================== */

static http_server_context_t s_http_ctx = {0};

/* ========================== FORWARD DECLARATIONS ========================== */

// Endpoint handlers
static esp_err_t whoami_handler(httpd_req_t *req);
static esp_err_t sensors_handler(httpd_req_t *req);
static esp_err_t ping_handler(httpd_req_t *req);
static esp_err_t status_handler(httpd_req_t *req);

// Error handlers
static esp_err_t default_404_handler(httpd_req_t *req, httpd_err_code_t err);

// Internal helpers
static esp_err_t register_all_endpoints(void);
static void add_cors_headers(httpd_req_t *req);
static void log_request(const char* method, const char* uri, int status_code);
static const char* get_method_string(httpd_method_t method);

/* ========================== INITIALIZATION ========================== */

esp_err_t http_server_init(const http_server_config_t* config)
{
    if (s_http_ctx.initialized) {
        ESP_LOGW(TAG, "HTTP server already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing HTTP server component...");

    // Initialize context
    memset(&s_http_ctx, 0, sizeof(http_server_context_t));
    s_http_ctx.state = HTTP_STATE_UNINITIALIZED;

    // Load configuration (use defaults if NULL)
    if (config != NULL) {
        memcpy(&s_http_ctx.config, config, sizeof(http_server_config_t));
    } else {
        // Use default configuration
        s_http_ctx.config = (http_server_config_t)HTTP_SERVER_DEFAULT_CONFIG();
    }

    ESP_LOGI(TAG, "HTTP server configuration:");
    ESP_LOGI(TAG, "  Port: %d", s_http_ctx.config.port);
    ESP_LOGI(TAG, "  Stack size: %zu bytes", s_http_ctx.config.stack_size);
    ESP_LOGI(TAG, "  Logging: %s", s_http_ctx.config.enable_logging ? "enabled" : "disabled");
    ESP_LOGI(TAG, "  CORS: %s", s_http_ctx.config.enable_cors ? "enabled" : "disabled");

    s_http_ctx.state = HTTP_STATE_STOPPED;
    s_http_ctx.initialized = true;

    ESP_LOGI(TAG, "HTTP server component initialized successfully");
    return ESP_OK;
}

esp_err_t http_server_start(void)
{
    if (!s_http_ctx.initialized) {
        ESP_LOGE(TAG, "HTTP server not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_http_ctx.state == HTTP_STATE_RUNNING) {
        ESP_LOGW(TAG, "HTTP server already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting HTTP server on port %d...", s_http_ctx.config.port);

    // Configure ESP-IDF HTTP server
    httpd_config_t httpd_cfg = HTTPD_DEFAULT_CONFIG();
    httpd_cfg.server_port = s_http_ctx.config.port;
    httpd_cfg.max_uri_handlers = s_http_ctx.config.max_uri_handlers;
    httpd_cfg.max_resp_headers = s_http_ctx.config.max_resp_headers;
    httpd_cfg.stack_size = s_http_ctx.config.stack_size;
    httpd_cfg.task_priority = s_http_ctx.config.priority;
    httpd_cfg.lru_purge_enable = true;

    // Start HTTP server
    esp_err_t ret = httpd_start(&s_http_ctx.server, &httpd_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        s_http_ctx.state = HTTP_STATE_ERROR;
        return ret;
    }

    // Register 404 error handler
    httpd_register_err_handler(s_http_ctx.server, HTTPD_404_NOT_FOUND, default_404_handler);

    // Register all endpoints
    ret = register_all_endpoints();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register endpoints: %s", esp_err_to_name(ret));
        httpd_stop(s_http_ctx.server);
        s_http_ctx.server = NULL;
        s_http_ctx.state = HTTP_STATE_ERROR;
        return ret;
    }

    s_http_ctx.state = HTTP_STATE_RUNNING;
    ESP_LOGI(TAG, "HTTP server started successfully on port %d", s_http_ctx.config.port);

    return ESP_OK;
}

esp_err_t http_server_stop(void)
{
    if (!s_http_ctx.initialized) {
        ESP_LOGW(TAG, "HTTP server not initialized");
        return ESP_OK;
    }

    if (s_http_ctx.server == NULL) {
        ESP_LOGW(TAG, "HTTP server not running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping HTTP server...");

    esp_err_t ret = httpd_stop(s_http_ctx.server);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    s_http_ctx.server = NULL;
    s_http_ctx.state = HTTP_STATE_STOPPED;

    ESP_LOGI(TAG, "HTTP server stopped");
    return ESP_OK;
}

esp_err_t http_server_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing HTTP server...");

    // Stop the server first
    http_server_stop();

    // Reset statistics
    memset(&s_http_ctx.stats, 0, sizeof(http_request_stats_t));

    // Reset context
    memset(&s_http_ctx, 0, sizeof(http_server_context_t));

    ESP_LOGI(TAG, "HTTP server deinitialized");
    return ESP_OK;
}

/* ========================== ENDPOINT HANDLERS ========================== */

/**
 * @brief GET /whoami - Device information and available endpoints
 */
static esp_err_t whoami_handler(httpd_req_t *req)
{
    // Update statistics
    s_http_ctx.stats.requests[HTTP_ENDPOINT_WHOAMI]++;
    s_http_ctx.stats.total_requests++;
    // Note: last_request_time tracking removed (context doesn't have status field)

    // Log request start
    if (s_http_ctx.config.enable_logging) {
        log_request("GET", HTTP_URI_WHOAMI, 0);
    }

    // Get MAC address
    uint8_t mac[6];
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MAC address: %s", esp_err_to_name(ret));
        memset(mac, 0, 6);
    }

    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Get IP address (placeholder - will be implemented when wifi_manager provides it)
    char ip_str[16] = "0.0.0.0";
    // TODO: wifi_manager_get_ip_string(ip_str, sizeof(ip_str));

    // Get device configuration
    char device_name[32] = "Smart Irrigation Device";
    char crop_name[16] = "Unknown";
    // TODO: device_config_get_device_name(device_name, sizeof(device_name));
    // TODO: device_config_get_crop_name(crop_name, sizeof(crop_name));

    // Get uptime
    uint64_t uptime_us = esp_timer_get_time();
    uint32_t uptime_sec = (uint32_t)(uptime_us / 1000000);

    // Create JSON response
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        s_http_ctx.stats.total_errors++;
        return httpd_resp_send_500(req);
    }

    // Device object
    cJSON *device = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "device", device);
    cJSON_AddStringToObject(device, "name", device_name);
    cJSON_AddStringToObject(device, "mac_address", mac_str);
    cJSON_AddStringToObject(device, "ip_address", ip_str);
    cJSON_AddStringToObject(device, "crop_name", crop_name);
    cJSON_AddStringToObject(device, "firmware_version", FIRMWARE_VERSION);
    cJSON_AddNumberToObject(device, "uptime_seconds", uptime_sec);

    // Endpoints array
    cJSON *endpoints = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "endpoints", endpoints);

    cJSON *ep1 = cJSON_CreateObject();
    cJSON_AddStringToObject(ep1, "path", HTTP_URI_WHOAMI);
    cJSON_AddStringToObject(ep1, "method", "GET");
    cJSON_AddStringToObject(ep1, "description", "Device information and available endpoints");
    cJSON_AddItemToArray(endpoints, ep1);

    cJSON *ep2 = cJSON_CreateObject();
    cJSON_AddStringToObject(ep2, "path", HTTP_URI_SENSORS);
    cJSON_AddStringToObject(ep2, "method", "GET");
    cJSON_AddStringToObject(ep2, "description", "Current sensor readings (immediate read)");
    cJSON_AddItemToArray(endpoints, ep2);

    cJSON *ep3 = cJSON_CreateObject();
    cJSON_AddStringToObject(ep3, "path", HTTP_URI_PING);
    cJSON_AddStringToObject(ep3, "method", "GET");
    cJSON_AddStringToObject(ep3, "description", "Health check endpoint");
    cJSON_AddItemToArray(endpoints, ep3);

    cJSON *ep4 = cJSON_CreateObject();
    cJSON_AddStringToObject(ep4, "path", HTTP_URI_STATUS);
    cJSON_AddStringToObject(ep4, "method", "GET");
    cJSON_AddStringToObject(ep4, "description", "System status (available in Phase 2)");
    cJSON_AddItemToArray(endpoints, ep4);

    // Serialize to string
    char *json_string = cJSON_Print(root);
    if (json_string == NULL) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        cJSON_Delete(root);
        s_http_ctx.stats.total_errors++;
        return httpd_resp_send_500(req);
    }

    // Add CORS headers if enabled
    add_cors_headers(req);

    // Send response
    httpd_resp_set_type(req, HTTP_CONTENT_TYPE_JSON);
    esp_err_t send_ret = httpd_resp_send(req, json_string, strlen(json_string));

    // Cleanup
    free(json_string);
    cJSON_Delete(root);

    // Log request end
    if (s_http_ctx.config.enable_logging) {
        log_request("GET", HTTP_URI_WHOAMI, (send_ret == ESP_OK ? 200 : 500));
    }

    return send_ret;
}

/**
 * @brief GET /sensors - Current sensor readings (immediate read)
 */
static esp_err_t sensors_handler(httpd_req_t *req)
{
    // Update statistics
    s_http_ctx.stats.requests[HTTP_ENDPOINT_SENSORS]++;
    s_http_ctx.stats.total_requests++;
    // Note: last_request_time tracking removed (context doesn't have status field)

    // Log request start
    if (s_http_ctx.config.enable_logging) {
        log_request("GET", HTTP_URI_SENSORS, 0);
    }

    // Read sensors IMMEDIATELY (not cached)
    sensor_reading_t reading;
    esp_err_t ret = sensor_reader_get_all(&reading);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        s_http_ctx.stats.total_errors++;
        return httpd_resp_send_500(req);
    }

    int status_code;

    if (ret != ESP_OK) {
        // Sensor read failed - return error response
        ESP_LOGE(TAG, "Failed to read sensors: %s", esp_err_to_name(ret));

        cJSON_AddStringToObject(root, "status", "error");

        cJSON *error = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "error", error);

        const char *error_msg = "Sensor read failed";
        const char *error_desc = "Failed to read sensor data";

        switch (ret) {
            case ESP_ERR_TIMEOUT:
                error_msg = "Sensor Timeout";
                error_desc = "Sensor reading timed out";
                break;
            case ESP_ERR_INVALID_STATE:
                error_msg = "Sensor Not Ready";
                error_desc = "Sensor not initialized or not healthy";
                break;
            default:
                break;
        }

        cJSON_AddStringToObject(error, "message", error_msg);
        cJSON_AddStringToObject(error, "description", error_desc);
        cJSON_AddStringToObject(error, "code", esp_err_to_name(ret));

        status_code = 500;
        s_http_ctx.stats.total_errors++;

    } else {
        // Success - return sensor data
        cJSON_AddStringToObject(root, "status", "success");
        cJSON_AddNumberToObject(root, "ambient_temperature", reading.ambient.temperature);
        cJSON_AddNumberToObject(root, "ambient_humidity", reading.ambient.humidity);
        cJSON_AddNumberToObject(root, "soil_humidity_1", reading.soil.soil_humidity[0]);
        cJSON_AddNumberToObject(root, "soil_humidity_2", reading.soil.soil_humidity[1]);
        cJSON_AddNumberToObject(root, "soil_humidity_3", reading.soil.soil_humidity[2]);
        // Note: timestamp removed - sensor_reading_t doesn't have timestamp field

        status_code = 200;
    }

    // Serialize to string
    char *json_string = cJSON_Print(root);
    if (json_string == NULL) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        cJSON_Delete(root);
        s_http_ctx.stats.total_errors++;
        return httpd_resp_send_500(req);
    }

    // Add CORS headers if enabled
    add_cors_headers(req);

    // Send response
    httpd_resp_set_type(req, HTTP_CONTENT_TYPE_JSON);
    if (status_code == 500) {
        httpd_resp_set_status(req, "500 Internal Server Error");
    }
    esp_err_t send_ret = httpd_resp_send(req, json_string, strlen(json_string));

    // Cleanup
    free(json_string);
    cJSON_Delete(root);

    // Log request end
    if (s_http_ctx.config.enable_logging) {
        log_request("GET", HTTP_URI_SENSORS, (send_ret == ESP_OK ? status_code : 500));
    }

    return send_ret;
}

/**
 * @brief GET /ping - Health check endpoint
 */
static esp_err_t ping_handler(httpd_req_t *req)
{
    // Update statistics
    s_http_ctx.stats.requests[HTTP_ENDPOINT_PING]++;
    s_http_ctx.stats.total_requests++;
    // Note: last_request_time tracking removed (context doesn't have status field)

    // Log request start
    if (s_http_ctx.config.enable_logging) {
        log_request("GET", HTTP_URI_PING, 0);
    }

    // Get uptime and free heap
    uint64_t uptime_us = esp_timer_get_time();
    uint32_t uptime_sec = (uint32_t)(uptime_us / 1000000);
    uint32_t free_heap = esp_get_free_heap_size();

    // Create JSON response
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        s_http_ctx.stats.total_errors++;
        return httpd_resp_send_500(req);
    }

    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddNumberToObject(root, "uptime_seconds", uptime_sec);
    cJSON_AddNumberToObject(root, "free_heap", free_heap);

    // Serialize to string
    char *json_string = cJSON_Print(root);
    if (json_string == NULL) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        cJSON_Delete(root);
        s_http_ctx.stats.total_errors++;
        return httpd_resp_send_500(req);
    }

    // Add CORS headers if enabled
    add_cors_headers(req);

    // Send response
    httpd_resp_set_type(req, HTTP_CONTENT_TYPE_JSON);
    esp_err_t send_ret = httpd_resp_send(req, json_string, strlen(json_string));

    // Cleanup
    free(json_string);
    cJSON_Delete(root);

    // Log request end
    if (s_http_ctx.config.enable_logging) {
        log_request("GET", HTTP_URI_PING, (send_ret == ESP_OK ? 200 : 500));
    }

    return send_ret;
}

/**
 * @brief GET /status - System status (Phase 2)
 */
static esp_err_t status_handler(httpd_req_t *req)
{
    // Update statistics
    s_http_ctx.stats.requests[HTTP_ENDPOINT_STATUS]++;
    s_http_ctx.stats.total_requests++;

    // Log request start
    if (s_http_ctx.config.enable_logging) {
        log_request("GET", HTTP_URI_STATUS, 0);
    }

    // Placeholder response for Phase 2
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        s_http_ctx.stats.total_errors++;
        return httpd_resp_send_500(req);
    }

    cJSON_AddStringToObject(root, "status", "not_implemented");
    cJSON_AddStringToObject(root, "message", "Status endpoint available in Phase 2 (irrigation_controller)");

    // Serialize to string
    char *json_string = cJSON_Print(root);
    if (json_string == NULL) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        cJSON_Delete(root);
        s_http_ctx.stats.total_errors++;
        return httpd_resp_send_500(req);
    }

    // Add CORS headers if enabled
    add_cors_headers(req);

    // Send response
    httpd_resp_set_type(req, HTTP_CONTENT_TYPE_JSON);
    httpd_resp_set_status(req, "501 Not Implemented");
    esp_err_t send_ret = httpd_resp_send(req, json_string, strlen(json_string));

    // Cleanup
    free(json_string);
    cJSON_Delete(root);

    // Log request end
    if (s_http_ctx.config.enable_logging) {
        log_request("GET", HTTP_URI_STATUS, 501);
    }

    return send_ret;
}

/* ========================== ERROR HANDLERS ========================== */

/**
 * @brief Default 404 handler
 */
static esp_err_t default_404_handler(httpd_req_t *req, httpd_err_code_t err)
{
    // Update error statistics
    s_http_ctx.stats.total_errors++;

    // Log request
    if (s_http_ctx.config.enable_logging) {
        ESP_LOGW(TAG, "404 Not Found: %s %s", get_method_string(req->method), req->uri);
    }

    // Create JSON error response
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return httpd_resp_send_500(req);
    }

    cJSON *error = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "error", error);
    cJSON_AddNumberToObject(error, "code", 404);
    cJSON_AddStringToObject(error, "message", "Not Found");
    cJSON_AddStringToObject(error, "description", "The requested endpoint does not exist");

    // Serialize to string
    char *json_string = cJSON_Print(root);
    if (json_string == NULL) {
        cJSON_Delete(root);
        return httpd_resp_send_500(req);
    }

    // Add CORS headers if enabled
    add_cors_headers(req);

    // Send response
    httpd_resp_set_type(req, HTTP_CONTENT_TYPE_JSON);
    httpd_resp_set_status(req, "404 Not Found");
    esp_err_t send_ret = httpd_resp_send(req, json_string, strlen(json_string));

    // Cleanup
    free(json_string);
    cJSON_Delete(root);

    return send_ret;
}

/* ========================== ENDPOINT REGISTRATION ========================== */

/**
 * @brief Register all HTTP endpoints
 */
static esp_err_t register_all_endpoints(void)
{
    esp_err_t ret;

    // Register /whoami endpoint
    httpd_uri_t whoami_uri = {
        .uri = HTTP_URI_WHOAMI,
        .method = HTTP_GET,
        .handler = whoami_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(s_http_ctx.server, &whoami_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /whoami endpoint: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Registered endpoint: GET %s", HTTP_URI_WHOAMI);

    // Register /sensors endpoint
    httpd_uri_t sensors_uri = {
        .uri = HTTP_URI_SENSORS,
        .method = HTTP_GET,
        .handler = sensors_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(s_http_ctx.server, &sensors_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /sensors endpoint: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Registered endpoint: GET %s", HTTP_URI_SENSORS);

    // Register /ping endpoint
    httpd_uri_t ping_uri = {
        .uri = HTTP_URI_PING,
        .method = HTTP_GET,
        .handler = ping_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(s_http_ctx.server, &ping_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /ping endpoint: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Registered endpoint: GET %s", HTTP_URI_PING);

    // Register /status endpoint (placeholder for Phase 2)
    httpd_uri_t status_uri = {
        .uri = HTTP_URI_STATUS,
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(s_http_ctx.server, &status_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /status endpoint: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Registered endpoint: GET %s (placeholder)", HTTP_URI_STATUS);

    return ESP_OK;
}

/* ========================== HELPER FUNCTIONS ========================== */

/**
 * @brief Add CORS headers to response
 */
static void add_cors_headers(httpd_req_t *req)
{
    if (s_http_ctx.config.enable_cors) {
        httpd_resp_set_hdr(req, HTTP_HEADER_CORS_ORIGIN, HTTP_CORS_ORIGIN_VALUE);
        httpd_resp_set_hdr(req, HTTP_HEADER_CORS_METHODS, HTTP_CORS_METHODS_VALUE);
        httpd_resp_set_hdr(req, HTTP_HEADER_CORS_HEADERS, HTTP_CORS_HEADERS_VALUE);
    }
}

/**
 * @brief Log HTTP request (simplified inline logging)
 */
static void log_request(const char* method, const char* uri, int status_code)
{
    if (status_code == 0) {
        // Request start
        ESP_LOGD(TAG, "→ %s %s", method, uri);
    } else {
        // Request end
        ESP_LOGD(TAG, "← %s %s [%d]", method, uri, status_code);
    }
}

/**
 * @brief Get HTTP method as string
 */
static const char* get_method_string(httpd_method_t method)
{
    switch (method) {
        case HTTP_GET:     return "GET";
        case HTTP_POST:    return "POST";
        case HTTP_PUT:     return "PUT";
        case HTTP_DELETE:  return "DELETE";
        case HTTP_HEAD:    return "HEAD";
        case HTTP_PATCH:   return "PATCH";
        case HTTP_OPTIONS: return "OPTIONS";
        default:           return "UNKNOWN";
    }
}

/* ========================== STATUS AND STATISTICS ========================== */

esp_err_t http_server_get_status(http_server_status_t* status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    status->state = s_http_ctx.state;
    status->port = s_http_ctx.config.port;
    status->total_requests = s_http_ctx.stats.total_requests;
    status->error_count = s_http_ctx.stats.total_errors;
    status->last_request_time = 0;  // Not tracked in context
    status->handle = s_http_ctx.server;

    return ESP_OK;
}

esp_err_t http_server_get_stats(http_request_stats_t* stats)
{
    if (stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(stats, &s_http_ctx.stats, sizeof(http_request_stats_t));
    return ESP_OK;
}

esp_err_t http_server_reset_stats(void)
{
    memset(&s_http_ctx.stats, 0, sizeof(http_request_stats_t));
    ESP_LOGI(TAG, "Statistics reset");
    return ESP_OK;
}

bool http_server_is_running(void)
{
    return (s_http_ctx.state == HTTP_STATE_RUNNING);
}
