#include "logging_middleware.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char* TAG = "HTTP_MIDDLEWARE";

// Default logging configuration
const http_logging_config_t HTTP_LOGGING_CONFIG_DEFAULT = {
    .log_timing = true,
    .log_status = true,
    .log_client_info = false,  // Disabled by default for simplicity
    .log_headers = false,      // Disabled by default for simplicity
    .log_tag = "HTTP"
};

http_request_context_t http_middleware_init_request_context(httpd_req_t *req)
{
    http_request_context_t context = {0};
    
    if (req == NULL) {
        return context;
    }
    
    context.start_time = xTaskGetTickCount();
    context.status_code = 0;  // Will be set later
    context.method_str = http_middleware_get_method_string(req->method);
    context.uri = req->uri;
    context.timing_enabled = true;
    
    return context;
}

void http_middleware_log_request(httpd_req_t *req)
{
    // Maintain backward compatibility with existing implementation
    if (req == NULL) {
        return;
    }
    
    const char* method_str = http_middleware_get_method_string(req->method);
    ESP_LOGI("HTTP", "%s %s", method_str, req->uri);
}

void http_middleware_log_request_start(httpd_req_t *req, http_request_context_t *context, const http_logging_config_t *config)
{
    if (req == NULL || context == NULL) {
        return;
    }
    
    const http_logging_config_t *log_config = config ? config : &HTTP_LOGGING_CONFIG_DEFAULT;
    const char* log_tag = log_config->log_tag ? log_config->log_tag : "HTTP";
    
    // Basic request information
    ESP_LOGI(log_tag, "→ %s %s", context->method_str, context->uri);
    
    // Additional information based on configuration
    if (log_config->log_client_info) {
        // Note: ESP-IDF HTTP server doesn't easily provide client IP
        // This would require additional socket-level access
        ESP_LOGD(log_tag, "  Client info logging not implemented");
    }
    
    if (log_config->log_headers) {
        // Basic header logging - could be extended for specific headers
        size_t content_len = httpd_req_get_hdr_value_len(req, "Content-Length");
        if (content_len > 0) {
            ESP_LOGD(log_tag, "  Content-Length header detected (%zu bytes)", content_len);
        }
    }
}

void http_middleware_log_request_end(httpd_req_t *req, http_request_context_t *context, int status_code, const http_logging_config_t *config)
{
    if (req == NULL || context == NULL) {
        return;
    }
    
    const http_logging_config_t *log_config = config ? config : &HTTP_LOGGING_CONFIG_DEFAULT;
    const char* log_tag = log_config->log_tag ? log_config->log_tag : "HTTP";
    
    context->status_code = status_code;
    
    // Build log message with timing and status information
    if (log_config->log_timing && context->timing_enabled) {
        uint32_t processing_time = http_middleware_get_processing_time_ms(context->start_time);
        
        if (log_config->log_status) {
            ESP_LOGI(log_tag, "← %s %s [%d] (%lu ms)", 
                    context->method_str, context->uri, status_code, processing_time);
        } else {
            ESP_LOGI(log_tag, "← %s %s (%lu ms)", 
                    context->method_str, context->uri, processing_time);
        }
    } else if (log_config->log_status) {
        ESP_LOGI(log_tag, "← %s %s [%d]", 
                context->method_str, context->uri, status_code);
    } else {
        ESP_LOGI(log_tag, "← %s %s", context->method_str, context->uri);
    }
}

const char* http_middleware_get_method_string(httpd_method_t method)
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

uint32_t http_middleware_get_processing_time_ms(TickType_t start_time)
{
    TickType_t end_time = xTaskGetTickCount();
    TickType_t elapsed_ticks = end_time - start_time;
    
    // Convert ticks to milliseconds
    return (uint32_t)(elapsed_ticks * portTICK_PERIOD_MS);
}