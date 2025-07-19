#pragma once

#include "esp_err.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Request context structure for enhanced logging
 * 
 * Contains timing and request metadata for comprehensive logging
 */
typedef struct {
    TickType_t start_time;      // Request start time
    int status_code;            // Response status code
    const char* method_str;     // HTTP method as string
    const char* uri;            // Request URI
    bool timing_enabled;        // Whether to log timing information
} http_request_context_t;

/**
 * @brief Logging configuration structure
 * 
 * Allows configuring what information to log
 */
typedef struct {
    bool log_timing;            // Log request processing time
    bool log_status;            // Log response status codes
    bool log_client_info;       // Log client IP (if available)
    bool log_headers;           // Log request headers
    const char* log_tag;        // ESP-IDF log tag to use
} http_logging_config_t;

/**
 * @brief Default logging configuration
 */
extern const http_logging_config_t HTTP_LOGGING_CONFIG_DEFAULT;

/**
 * @brief Initialize request context for logging
 * 
 * Should be called at the beginning of request processing
 * 
 * @param req HTTP request handle
 * @return Initialized request context
 */
http_request_context_t http_middleware_init_request_context(httpd_req_t *req);

/**
 * @brief Log request start with basic information
 * 
 * Logs HTTP method and URI. Compatible with existing implementation.
 * 
 * @param req HTTP request handle
 */
void http_middleware_log_request(httpd_req_t *req);

/**
 * @brief Log request start with context
 * 
 * Enhanced version that uses request context for additional information
 * 
 * @param req HTTP request handle
 * @param context Request context
 * @param config Logging configuration
 */
void http_middleware_log_request_start(httpd_req_t *req, http_request_context_t *context, const http_logging_config_t *config);

/**
 * @brief Log request completion
 * 
 * Logs request completion with status code and timing information
 * 
 * @param req HTTP request handle
 * @param context Request context
 * @param status_code HTTP response status code
 * @param config Logging configuration
 */
void http_middleware_log_request_end(httpd_req_t *req, http_request_context_t *context, int status_code, const http_logging_config_t *config);

/**
 * @brief Get HTTP method as string
 * 
 * Helper function to convert HTTP method enum to string
 * 
 * @param method HTTP method enum
 * @return Method as string
 */
const char* http_middleware_get_method_string(httpd_method_t method);

/**
 * @brief Calculate request processing time
 * 
 * @param start_time Request start time
 * @return Processing time in milliseconds
 */
uint32_t http_middleware_get_processing_time_ms(TickType_t start_time);

#ifdef __cplusplus
}
#endif