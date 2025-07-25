#ifndef SHARED_RESOURCE_MANAGER_H
#define SHARED_RESOURCE_MANAGER_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Shared resource types for synchronization
 */
typedef enum {
    SHARED_RESOURCE_NVS,        // NVS access synchronization
    SHARED_RESOURCE_NETWORK,    // Network operations synchronization
    SHARED_RESOURCE_JSON,       // JSON serialization synchronization
    SHARED_RESOURCE_MAX
} shared_resource_type_t;

/**
 * @brief Initialize shared resource manager
 * 
 * Creates semaphores for different resource types to ensure
 * thread-safe access across HTTP and MQTT operations.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t shared_resource_manager_init(void);

/**
 * @brief Deinitialize shared resource manager
 * 
 * Cleans up semaphores and resources.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t shared_resource_manager_deinit(void);

/**
 * @brief Take (lock) a shared resource semaphore
 * 
 * @param resource_type Type of resource to lock
 * @param timeout_ms Timeout in milliseconds (portMAX_DELAY for no timeout)
 * @return ESP_OK if semaphore taken, ESP_ERR_TIMEOUT if timeout, ESP_FAIL otherwise
 */
esp_err_t shared_resource_take(shared_resource_type_t resource_type, uint32_t timeout_ms);

/**
 * @brief Give (unlock) a shared resource semaphore
 * 
 * @param resource_type Type of resource to unlock
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t shared_resource_give(shared_resource_type_t resource_type);

/**
 * @brief Check if shared resource manager is initialized
 * 
 * @return true if initialized, false otherwise
 */
bool shared_resource_manager_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* SHARED_RESOURCE_MANAGER_H */