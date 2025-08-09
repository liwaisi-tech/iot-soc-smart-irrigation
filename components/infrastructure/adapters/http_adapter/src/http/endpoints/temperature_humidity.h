#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handler for GET /temperature-and-humidity endpoint
 * 
 * Returns current temperature and humidity sensor readings in JSON format.
 * Uses thread-safe sensor reading with 5-second timeout.
 * 
 * @param req HTTP request handle
 * @return ESP_OK on success, error code on failure
 */
esp_err_t temperature_humidity_get_handler(httpd_req_t *req);

/**
 * @brief Register the temperature-humidity endpoint with the HTTP server
 * 
 * @param server HTTP server handle
 * @return ESP_OK on success, error code on failure
 */
esp_err_t temperature_humidity_register_endpoints(httpd_handle_t server);

#ifdef __cplusplus
}
#endif