#include "logging_middleware.h"
#include "esp_log.h"

static const char* TAG = "HTTP";

// Simplified logging configuration
const http_logging_config_t HTTP_LOGGING_CONFIG_DEFAULT = {
    .log_timing = false,   // Disabled for efficiency
    .log_status = true,
    .log_client_info = false,
    .log_headers = false,
    .log_tag = "HTTP"
};

http_request_context_t http_middleware_init_request_context(httpd_req_t *req)
{
    http_request_context_t context = {0};
    if (req) {
        context.method_str = http_middleware_get_method_string(req->method);
        context.uri = req->uri;
    }
    return context;
}

void http_middleware_log_request(httpd_req_t *req)
{
    if (req) {
        ESP_LOGD(TAG, "%s %s", http_middleware_get_method_string(req->method), req->uri);
    }
}

void http_middleware_log_request_start(httpd_req_t *req, http_request_context_t *context, const http_logging_config_t *config)
{
    if (req && context) {
        ESP_LOGD(TAG, "→ %s %s", context->method_str, context->uri);
    }
}

void http_middleware_log_request_end(httpd_req_t *req, http_request_context_t *context, int status_code, const http_logging_config_t *config)
{
    if (req && context) {
        ESP_LOGD(TAG, "← %s %s [%d]", context->method_str, context->uri, status_code);
    }
}

const char* http_middleware_get_method_string(httpd_method_t method)
{
    static const char* methods[] = {"GET", "POST", "PUT", "DELETE", "HEAD", "PATCH", "OPTIONS"};
    return (method < 7) ? methods[method] : "UNKNOWN";
}

uint32_t http_middleware_get_processing_time_ms(TickType_t start_time)
{
    return (xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS;
}