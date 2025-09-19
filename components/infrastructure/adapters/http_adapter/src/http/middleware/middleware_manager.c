#include "middleware_manager.h"
#include "esp_log.h"

static const char* TAG = "HTTP_MW";

// Static middleware arrays (no dynamic allocation)
#define MAX_MIDDLEWARE_COUNT 4
static http_middleware_func_t pre_middleware[MAX_MIDDLEWARE_COUNT];
static http_middleware_func_t post_middleware[MAX_MIDDLEWARE_COUNT];
static int pre_count = 0;
static int post_count = 0;
static bool manager_initialized = false;

esp_err_t http_middleware_manager_init(void)
{
    if (manager_initialized) {
        return ESP_OK;
    }

    pre_count = post_count = 0;
    manager_initialized = true;
    return ESP_OK;
}

esp_err_t http_middleware_manager_cleanup(void)
{
    pre_count = post_count = 0;
    manager_initialized = false;
    return ESP_OK;
}

esp_err_t http_middleware_register(http_middleware_func_t func,
                                  http_middleware_phase_t phase,
                                  int priority,
                                  const char *name,
                                  void *context)
{
    if (!manager_initialized || !func) {
        return ESP_ERR_INVALID_ARG;
    }

    if (phase == HTTP_MIDDLEWARE_PHASE_PRE_HANDLER) {
        if (pre_count >= MAX_MIDDLEWARE_COUNT) return ESP_ERR_NO_MEM;
        pre_middleware[pre_count++] = func;
    } else if (phase == HTTP_MIDDLEWARE_PHASE_POST_HANDLER) {
        if (post_count >= MAX_MIDDLEWARE_COUNT) return ESP_ERR_NO_MEM;
        post_middleware[post_count++] = func;
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

esp_err_t http_middleware_execute_pre_handler(httpd_req_t *req)
{
    if (!manager_initialized || !req) {
        return ESP_OK;
    }

    for (int i = 0; i < pre_count; i++) {
        esp_err_t ret = pre_middleware[i](req, NULL);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    return ESP_OK;
}

esp_err_t http_middleware_execute_post_handler(httpd_req_t *req, esp_err_t handler_status)
{
    if (!manager_initialized || !req) {
        return ESP_OK;
    }

    for (int i = 0; i < post_count; i++) {
        post_middleware[i](req, NULL);  // Continue even if one fails
    }

    return ESP_OK;
}

esp_err_t http_middleware_unregister(const char *name)
{
    // Simplified unregister - clear all middleware
    pre_count = post_count = 0;
    return ESP_OK;
}

int http_middleware_get_count(http_middleware_phase_t phase)
{
    if (!manager_initialized) return 0;

    return (phase == HTTP_MIDDLEWARE_PHASE_PRE_HANDLER) ? pre_count : post_count;
}