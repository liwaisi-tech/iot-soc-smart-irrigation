#include "ping.h"
#include "../server.h"
#include "../middleware/logging_middleware.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "PING_ENDPOINT";

esp_err_t ping_get_handler(httpd_req_t *req)
{
    // Initialize request context for enhanced logging
    http_request_context_t log_context = http_middleware_init_request_context(req);
    http_middleware_log_request_start(req, &log_context, &HTTP_LOGGING_CONFIG_DEFAULT);
    
    ESP_LOGI(TAG, "Processing ping request");
    
    const char* response_text = "pong";
    
    // Set content type to plain text
    httpd_resp_set_type(req, "text/plain");
    
    // Send the plain text response
    esp_err_t ret = httpd_resp_send(req, response_text, strlen(response_text));
    
    // Log the response
    http_middleware_log_request_end(req, &log_context, (ret == ESP_OK ? 200 : 500), &HTTP_LOGGING_CONFIG_DEFAULT);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send ping response: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Ping response sent successfully");
    }
    
    return ret;
}

esp_err_t ping_register_endpoints(httpd_handle_t server)
{
    if (server == NULL) {
        ESP_LOGE(TAG, "Server handle is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    httpd_uri_t ping_uri = {
        .uri = "/ping",
        .method = HTTP_GET,
        .handler = ping_get_handler,
        .user_ctx = NULL
    };
    
    esp_err_t ret = httpd_register_uri_handler(server, &ping_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error registering /ping handler: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Registered endpoint: GET /ping");
    return ESP_OK;
}