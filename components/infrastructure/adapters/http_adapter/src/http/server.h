#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the internal HTTP server
 * 
 * This function creates and starts the ESP-IDF HTTP server instance
 * on port 80 with default configuration.
 * 
 * @return ESP_OK on success, ESP_FAIL or other error codes on failure
 */
esp_err_t http_server_start(void);

/**
 * @brief Stop the internal HTTP server
 * 
 * This function stops the HTTP server and frees resources.
 * 
 * @return ESP_OK on success, ESP_FAIL or other error codes on failure
 */
esp_err_t http_server_stop(void);

/**
 * @brief Get the HTTP server handle
 * 
 * @return HTTP server handle if running, NULL otherwise
 */
httpd_handle_t http_server_get_handle(void);

/**
 * @brief Register a URI handler for the HTTP server
 * 
 * @param uri URI pattern to match
 * @param method HTTP method (GET, POST, etc.)
 * @param handler Handler function
 * @return ESP_OK on success, error code on failure
 */
esp_err_t http_server_register_handler(const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *req));

/**
 * @brief Send JSON response with appropriate headers
 * 
 * @param req HTTP request handle
 * @param json_string JSON string to send
 * @return ESP_OK on success, error code on failure
 */
esp_err_t http_response_send_json(httpd_req_t *req, const char* json_string);

/**
 * @brief Send JSON error response
 * 
 * @param req HTTP request handle
 * @param status_code HTTP status code
 * @param message Error message
 * @param description Error description
 * @return ESP_OK on success, error code on failure
 */
esp_err_t http_response_send_error(httpd_req_t *req, int status_code, const char* message, const char* description);


#ifdef __cplusplus
}
#endif