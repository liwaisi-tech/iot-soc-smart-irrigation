#include "temperature_humidity.h"
#include "../server.h"
#include "../middleware/logging_middleware.h"
#include "shared_resource_manager.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "TEMP_HUMIDITY_ENDPOINT";

esp_err_t temperature_humidity_get_handler(httpd_req_t *req)
{
    // Initialize request context for enhanced logging
    http_request_context_t log_context = http_middleware_init_request_context(req);
    http_middleware_log_request_start(req, &log_context, &HTTP_LOGGING_CONFIG_DEFAULT);
    
    ESP_LOGI(TAG, "Processing temperature and humidity request");
    
    // Get current timestamp in milliseconds since epoch
    int64_t timestamp_us = esp_timer_get_time();
    int64_t timestamp_ms = timestamp_us / 1000;
    
    // Read sensor data using thread-safe function
    ambient_sensor_data_t sensor_data;
    esp_err_t ret = sensor_read_safe(&sensor_data);
    
    char response_buffer[256];
    int written;
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read sensor data: %s", esp_err_to_name(ret));
        
        // Return error response with appropriate HTTP status
        const char* error_message;
        const char* error_description;
        
        switch (ret) {
            case ESP_ERR_TIMEOUT:
                error_message = "Sensor Timeout";
                error_description = "Sensor reading timed out after 5 seconds";
                break;
            case ESP_ERR_INVALID_STATE:
                error_message = "Sensor Not Ready";
                error_description = "Sensor or resource manager not initialized";
                break;
            case ESP_ERR_INVALID_CRC:
                error_message = "Sensor Data Error";
                error_description = "Invalid sensor data received (checksum failed)";
                break;
            default:
                error_message = "Sensor Error";
                error_description = "Failed to read temperature and humidity sensor";
                break;
        }
        
        // Create error JSON response
        written = snprintf(response_buffer, sizeof(response_buffer),
            "{"
            "\"timestamp\":%lld,"
            "\"status\":\"error\","
            "\"error\":{"
            "\"message\":\"%s\","
            "\"description\":\"%s\","
            "\"code\":\"%s\""
            "}"
            "}",
            timestamp_ms,
            error_message,
            error_description,
            esp_err_to_name(ret)
        );
        
        if (written < 0 || written >= sizeof(response_buffer)) {
            ESP_LOGE(TAG, "Error JSON response buffer overflow");
            http_middleware_log_request_end(req, &log_context, 500, &HTTP_LOGGING_CONFIG_DEFAULT);
            return http_response_send_error(req, 500, "Internal Server Error", "Response buffer overflow");
        }
        
        // Send 500 Internal Server Error with JSON error details
        httpd_resp_set_status(req, "500 Internal Server Error");
        esp_err_t response_ret = http_response_send_json(req, response_buffer);
        http_middleware_log_request_end(req, &log_context, 500, &HTTP_LOGGING_CONFIG_DEFAULT);
        
        return response_ret;
    }
    
    // Create successful JSON response
    written = snprintf(response_buffer, sizeof(response_buffer),
        "{"
        "\"timestamp\":%lld,"
        "\"temperature\":%.1f,"
        "\"humidity\":%.1f,"
        "\"status\":\"success\""
        "}",
        timestamp_ms,
        sensor_data.ambient_temperature,
        sensor_data.ambient_humidity
    );
    
    if (written < 0 || written >= sizeof(response_buffer)) {
        ESP_LOGE(TAG, "JSON response buffer overflow");
        http_middleware_log_request_end(req, &log_context, 500, &HTTP_LOGGING_CONFIG_DEFAULT);
        return http_response_send_error(req, 500, "Internal Server Error", "Response buffer overflow");
    }
    
    ESP_LOGI(TAG, "Sensor reading successful: T=%.1f°C, H=%.1f%%", 
             sensor_data.ambient_temperature, sensor_data.ambient_humidity);
    
    esp_err_t response_ret = http_response_send_json(req, response_buffer);
    http_middleware_log_request_end(req, &log_context, (response_ret == ESP_OK ? 200 : 500), &HTTP_LOGGING_CONFIG_DEFAULT);
    
    return response_ret;
}

esp_err_t temperature_humidity_register_endpoints(httpd_handle_t server)
{
    if (server == NULL) {
        ESP_LOGE(TAG, "Server handle is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    httpd_uri_t temp_humidity_uri = {
        .uri = "/temperature-and-humidity",
        .method = HTTP_GET,
        .handler = temperature_humidity_get_handler,
        .user_ctx = NULL
    };
    
    esp_err_t ret = httpd_register_uri_handler(server, &temp_humidity_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error registering /temperature-and-humidity handler: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Registered endpoint: GET /temperature-and-humidity");
    return ESP_OK;
}