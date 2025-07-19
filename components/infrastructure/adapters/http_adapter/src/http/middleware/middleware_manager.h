#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Middleware execution phase
 */
typedef enum {
    HTTP_MIDDLEWARE_PHASE_PRE_HANDLER,   // Before handler execution
    HTTP_MIDDLEWARE_PHASE_POST_HANDLER   // After handler execution
} http_middleware_phase_t;

/**
 * @brief Middleware function signature
 * 
 * @param req HTTP request handle
 * @param context User-defined context data
 * @return ESP_OK to continue processing, ESP_FAIL to abort
 */
typedef esp_err_t (*http_middleware_func_t)(httpd_req_t *req, void *context);

/**
 * @brief Middleware registration structure
 */
typedef struct http_middleware_registration {
    http_middleware_func_t func;           // Middleware function
    http_middleware_phase_t phase;         // Execution phase
    void *context;                         // User context data
    int priority;                          // Execution priority (lower = earlier)
    const char *name;                      // Middleware name for debugging
    struct http_middleware_registration *next;  // Linked list next pointer
} http_middleware_registration_t;

/**
 * @brief Initialize the middleware manager
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t http_middleware_manager_init(void);

/**
 * @brief Cleanup the middleware manager
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t http_middleware_manager_cleanup(void);

/**
 * @brief Register a middleware function
 * 
 * @param func Middleware function
 * @param phase Execution phase
 * @param priority Execution priority (lower number = higher priority)
 * @param name Middleware name for debugging
 * @param context User context data
 * @return ESP_OK on success, error code on failure
 */
esp_err_t http_middleware_register(http_middleware_func_t func, 
                                  http_middleware_phase_t phase,
                                  int priority,
                                  const char *name,
                                  void *context);

/**
 * @brief Execute all registered pre-handler middleware
 * 
 * @param req HTTP request handle
 * @return ESP_OK if all middleware succeeded, ESP_FAIL if any middleware failed
 */
esp_err_t http_middleware_execute_pre_handler(httpd_req_t *req);

/**
 * @brief Execute all registered post-handler middleware
 * 
 * @param req HTTP request handle
 * @param handler_status Status code returned by the handler
 * @return ESP_OK if all middleware succeeded, ESP_FAIL if any middleware failed
 */
esp_err_t http_middleware_execute_post_handler(httpd_req_t *req, esp_err_t handler_status);

/**
 * @brief Unregister a middleware by name
 * 
 * @param name Middleware name
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not found
 */
esp_err_t http_middleware_unregister(const char *name);

/**
 * @brief Get count of registered middleware for a phase
 * 
 * @param phase Middleware phase
 * @return Number of registered middleware for the phase
 */
int http_middleware_get_count(http_middleware_phase_t phase);

/**
 * @brief Helper macro to create a wrapped handler with middleware support
 * 
 * This macro creates a wrapper around your actual handler function that
 * automatically executes pre and post middleware.
 */
#define HTTP_HANDLER_WITH_MIDDLEWARE(handler_name, original_handler) \
    static esp_err_t handler_name(httpd_req_t *req) { \
        esp_err_t ret = http_middleware_execute_pre_handler(req); \
        if (ret != ESP_OK) { \
            return ret; \
        } \
        esp_err_t handler_ret = original_handler(req); \
        http_middleware_execute_post_handler(req, handler_ret); \
        return handler_ret; \
    }

#ifdef __cplusplus
}
#endif