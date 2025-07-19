#include "middleware_manager.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char* TAG = "MIDDLEWARE_MGR";

// Linked lists for middleware registrations
static http_middleware_registration_t *pre_handler_middleware = NULL;
static http_middleware_registration_t *post_handler_middleware = NULL;
static bool manager_initialized = false;

// Forward declarations
static http_middleware_registration_t** get_middleware_list(http_middleware_phase_t phase);
static esp_err_t insert_middleware_sorted(http_middleware_registration_t **list, http_middleware_registration_t *middleware);
static void free_middleware_list(http_middleware_registration_t **list);

esp_err_t http_middleware_manager_init(void)
{
    if (manager_initialized) {
        ESP_LOGW(TAG, "Middleware manager already initialized");
        return ESP_OK;
    }
    
    pre_handler_middleware = NULL;
    post_handler_middleware = NULL;
    manager_initialized = true;
    
    ESP_LOGI(TAG, "Middleware manager initialized");
    return ESP_OK;
}

esp_err_t http_middleware_manager_cleanup(void)
{
    if (!manager_initialized) {
        return ESP_OK;
    }
    
    free_middleware_list(&pre_handler_middleware);
    free_middleware_list(&post_handler_middleware);
    
    manager_initialized = false;
    ESP_LOGI(TAG, "Middleware manager cleaned up");
    return ESP_OK;
}

esp_err_t http_middleware_register(http_middleware_func_t func, 
                                  http_middleware_phase_t phase,
                                  int priority,
                                  const char *name,
                                  void *context)
{
    if (!manager_initialized) {
        ESP_LOGE(TAG, "Middleware manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (func == NULL || name == NULL) {
        ESP_LOGE(TAG, "Invalid parameters: func and name cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Allocate new middleware registration
    http_middleware_registration_t *middleware = malloc(sizeof(http_middleware_registration_t));
    if (middleware == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for middleware registration");
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize middleware registration
    middleware->func = func;
    middleware->phase = phase;
    middleware->context = context;
    middleware->priority = priority;
    middleware->name = strdup(name);  // Create a copy of the name
    middleware->next = NULL;
    
    if (middleware->name == NULL) {
        free(middleware);
        ESP_LOGE(TAG, "Failed to allocate memory for middleware name");
        return ESP_ERR_NO_MEM;
    }
    
    // Get the appropriate middleware list
    http_middleware_registration_t **list = get_middleware_list(phase);
    if (list == NULL) {
        free((void*)middleware->name);
        free(middleware);
        ESP_LOGE(TAG, "Invalid middleware phase");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Insert middleware in sorted order by priority
    esp_err_t ret = insert_middleware_sorted(list, middleware);
    if (ret != ESP_OK) {
        free((void*)middleware->name);
        free(middleware);
        return ret;
    }
    
    ESP_LOGI(TAG, "Registered middleware '%s' with priority %d in %s phase", 
             name, priority, (phase == HTTP_MIDDLEWARE_PHASE_PRE_HANDLER) ? "PRE" : "POST");
    
    return ESP_OK;
}

esp_err_t http_middleware_execute_pre_handler(httpd_req_t *req)
{
    if (!manager_initialized) {
        return ESP_OK;  // No middleware to execute
    }
    
    http_middleware_registration_t *current = pre_handler_middleware;
    while (current != NULL) {
        ESP_LOGD(TAG, "Executing pre-handler middleware: %s", current->name);
        
        esp_err_t ret = current->func(req, current->context);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Pre-handler middleware '%s' failed with error: %s", 
                     current->name, esp_err_to_name(ret));
            return ret;
        }
        
        current = current->next;
    }
    
    return ESP_OK;
}

esp_err_t http_middleware_execute_post_handler(httpd_req_t *req, esp_err_t handler_status)
{
    if (!manager_initialized) {
        return ESP_OK;  // No middleware to execute
    }
    
    http_middleware_registration_t *current = post_handler_middleware;
    while (current != NULL) {
        ESP_LOGD(TAG, "Executing post-handler middleware: %s", current->name);
        
        esp_err_t ret = current->func(req, current->context);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Post-handler middleware '%s' failed with error: %s", 
                     current->name, esp_err_to_name(ret));
            // Continue executing other post-handler middleware even if one fails
        }
        
        current = current->next;
    }
    
    return ESP_OK;
}

esp_err_t http_middleware_unregister(const char *name)
{
    if (!manager_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Search in both middleware lists
    http_middleware_registration_t **lists[] = {&pre_handler_middleware, &post_handler_middleware};
    const char *phase_names[] = {"PRE", "POST"};
    
    for (int i = 0; i < 2; i++) {
        http_middleware_registration_t **list = lists[i];
        http_middleware_registration_t *current = *list;
        http_middleware_registration_t *prev = NULL;
        
        while (current != NULL) {
            if (strcmp(current->name, name) == 0) {
                // Found the middleware to remove
                if (prev == NULL) {
                    // First item in list
                    *list = current->next;
                } else {
                    prev->next = current->next;
                }
                
                free((void*)current->name);
                free(current);
                
                ESP_LOGI(TAG, "Unregistered middleware '%s' from %s phase", name, phase_names[i]);
                return ESP_OK;
            }
            
            prev = current;
            current = current->next;
        }
    }
    
    ESP_LOGW(TAG, "Middleware '%s' not found for unregistration", name);
    return ESP_ERR_NOT_FOUND;
}

int http_middleware_get_count(http_middleware_phase_t phase)
{
    if (!manager_initialized) {
        return 0;
    }
    
    http_middleware_registration_t **list = get_middleware_list(phase);
    if (list == NULL) {
        return 0;
    }
    
    int count = 0;
    http_middleware_registration_t *current = *list;
    while (current != NULL) {
        count++;
        current = current->next;
    }
    
    return count;
}

// Helper functions
static http_middleware_registration_t** get_middleware_list(http_middleware_phase_t phase)
{
    switch (phase) {
        case HTTP_MIDDLEWARE_PHASE_PRE_HANDLER:
            return &pre_handler_middleware;
        case HTTP_MIDDLEWARE_PHASE_POST_HANDLER:
            return &post_handler_middleware;
        default:
            return NULL;
    }
}

static esp_err_t insert_middleware_sorted(http_middleware_registration_t **list, http_middleware_registration_t *middleware)
{
    if (list == NULL || middleware == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // If list is empty or new middleware has higher priority than first item
    if (*list == NULL || middleware->priority < (*list)->priority) {
        middleware->next = *list;
        *list = middleware;
        return ESP_OK;
    }
    
    // Find the correct position to insert
    http_middleware_registration_t *current = *list;
    while (current->next != NULL && current->next->priority <= middleware->priority) {
        current = current->next;
    }
    
    middleware->next = current->next;
    current->next = middleware;
    
    return ESP_OK;
}

static void free_middleware_list(http_middleware_registration_t **list)
{
    if (list == NULL) {
        return;
    }
    
    http_middleware_registration_t *current = *list;
    while (current != NULL) {
        http_middleware_registration_t *next = current->next;
        free((void*)current->name);
        free(current);
        current = next;
    }
    
    *list = NULL;
}