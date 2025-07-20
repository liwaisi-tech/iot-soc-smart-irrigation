#include "shared_resource_manager.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "shared_resource_manager";

static SemaphoreHandle_t s_resource_semaphores[SHARED_RESOURCE_MAX] = {0};
static bool s_manager_initialized = false;

static const char *s_resource_names[] = {
    "NVS",
    "NETWORK", 
    "JSON"
};

esp_err_t shared_resource_manager_init(void)
{
    if (s_manager_initialized) {
        ESP_LOGW(TAG, "Shared resource manager already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing shared resource manager");
    
    // Create semaphores for each resource type
    for (int i = 0; i < SHARED_RESOURCE_MAX; i++) {
        s_resource_semaphores[i] = xSemaphoreCreateMutex();
        if (s_resource_semaphores[i] == NULL) {
            ESP_LOGE(TAG, "Failed to create semaphore for resource: %s", s_resource_names[i]);
            
            // Cleanup previously created semaphores
            for (int j = 0; j < i; j++) {
                if (s_resource_semaphores[j] != NULL) {
                    vSemaphoreDelete(s_resource_semaphores[j]);
                    s_resource_semaphores[j] = NULL;
                }
            }
            return ESP_ERR_NO_MEM;
        }
        
        ESP_LOGD(TAG, "Created semaphore for resource: %s", s_resource_names[i]);
    }
    
    s_manager_initialized = true;
    ESP_LOGI(TAG, "Shared resource manager initialized successfully");
    
    return ESP_OK;
}

esp_err_t shared_resource_manager_deinit(void)
{
    if (!s_manager_initialized) {
        ESP_LOGW(TAG, "Shared resource manager not initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Deinitializing shared resource manager");
    
    // Delete all semaphores
    for (int i = 0; i < SHARED_RESOURCE_MAX; i++) {
        if (s_resource_semaphores[i] != NULL) {
            vSemaphoreDelete(s_resource_semaphores[i]);
            s_resource_semaphores[i] = NULL;
            ESP_LOGD(TAG, "Deleted semaphore for resource: %s", s_resource_names[i]);
        }
    }
    
    s_manager_initialized = false;
    ESP_LOGI(TAG, "Shared resource manager deinitialized");
    
    return ESP_OK;
}

esp_err_t shared_resource_take(shared_resource_type_t resource_type, uint32_t timeout_ms)
{
    if (!s_manager_initialized) {
        ESP_LOGE(TAG, "Shared resource manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (resource_type >= SHARED_RESOURCE_MAX) {
        ESP_LOGE(TAG, "Invalid resource type: %d", resource_type);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_resource_semaphores[resource_type] == NULL) {
        ESP_LOGE(TAG, "Semaphore for resource %s is NULL", s_resource_names[resource_type]);
        return ESP_FAIL;
    }
    
    TickType_t timeout_ticks = (timeout_ms == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    
    ESP_LOGV(TAG, "Taking semaphore for resource: %s (timeout: %lu ms)", 
             s_resource_names[resource_type], timeout_ms);
    
    if (xSemaphoreTake(s_resource_semaphores[resource_type], timeout_ticks) == pdTRUE) {
        ESP_LOGV(TAG, "Successfully took semaphore for resource: %s", s_resource_names[resource_type]);
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Failed to take semaphore for resource: %s (timeout)", s_resource_names[resource_type]);
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t shared_resource_give(shared_resource_type_t resource_type)
{
    if (!s_manager_initialized) {
        ESP_LOGE(TAG, "Shared resource manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (resource_type >= SHARED_RESOURCE_MAX) {
        ESP_LOGE(TAG, "Invalid resource type: %d", resource_type);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_resource_semaphores[resource_type] == NULL) {
        ESP_LOGE(TAG, "Semaphore for resource %s is NULL", s_resource_names[resource_type]);
        return ESP_FAIL;
    }
    
    ESP_LOGV(TAG, "Giving semaphore for resource: %s", s_resource_names[resource_type]);
    
    if (xSemaphoreGive(s_resource_semaphores[resource_type]) == pdTRUE) {
        ESP_LOGV(TAG, "Successfully gave semaphore for resource: %s", s_resource_names[resource_type]);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to give semaphore for resource: %s", s_resource_names[resource_type]);
        return ESP_FAIL;
    }
}

bool shared_resource_manager_is_initialized(void)
{
    return s_manager_initialized;
}