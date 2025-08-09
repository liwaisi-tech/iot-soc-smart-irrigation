#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handler for GET /ping endpoint
 * 
 * Returns a simple "pong" text response to check server connectivity.
 * 
 * @param req HTTP request handle
 * @return ESP_OK on success, error code on failure
 */
esp_err_t ping_get_handler(httpd_req_t *req);

/**
 * @brief Register the ping endpoint with the HTTP server
 * 
 * @param server HTTP server handle
 * @return ESP_OK on success, error code on failure
 */
esp_err_t ping_register_endpoints(httpd_handle_t server);

#ifdef __cplusplus
}
#endif