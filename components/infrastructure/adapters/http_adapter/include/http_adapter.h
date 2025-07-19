#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the HTTP adapter
 * 
 * This function initializes the HTTP server adapter following the hexagonal
 * architecture pattern. It acts as a port interface for the HTTP server
 * infrastructure.
 * 
 * @return ESP_OK on success, ESP_FAIL or other error codes on failure
 */
esp_err_t http_adapter_init(void);

/**
 * @brief Stop the HTTP adapter
 * 
 * This function stops the HTTP server and cleans up resources.
 * 
 * @return ESP_OK on success, ESP_FAIL or other error codes on failure
 */
esp_err_t http_adapter_stop(void);

/**
 * @brief Check if HTTP adapter is running
 * 
 * @return true if HTTP server is running, false otherwise
 */
bool http_adapter_is_running(void);

#ifdef __cplusplus
}
#endif