#include "boot_counter.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "soc/rtc.h"

static const char *TAG = "boot_counter";

RTC_DATA_ATTR static boot_counter_t s_boot_counter = {0};

esp_err_t boot_counter_init(void)
{
    ESP_LOGI(TAG, "Initializing boot counter");
    
    if (s_boot_counter.magic_number != BOOT_COUNTER_MAGIC_NUMBER) {
        ESP_LOGI(TAG, "Boot counter not initialized, creating new counter");
        s_boot_counter.magic_number = BOOT_COUNTER_MAGIC_NUMBER;
        s_boot_counter.boot_count = 0;
        s_boot_counter.last_boot_time = 0;
        s_boot_counter.first_boot_time = 0;
    }
    
    ESP_LOGI(TAG, "Boot counter initialized - current count: %lu", s_boot_counter.boot_count);
    return ESP_OK;
}

esp_err_t boot_counter_increment(void)
{
    uint32_t current_time = (uint32_t)(esp_timer_get_time() / 1000);
    
    if (s_boot_counter.boot_count == 0) {
        s_boot_counter.first_boot_time = current_time;
        s_boot_counter.boot_count = 1;
        ESP_LOGI(TAG, "First boot detected, count: %lu", s_boot_counter.boot_count);
    } else {
        uint32_t time_since_first = current_time - s_boot_counter.first_boot_time;
        
        if (time_since_first > BOOT_COUNTER_TIME_WINDOW_MS) {
            ESP_LOGI(TAG, "Time window expired (%lu ms), resetting counter", time_since_first);
            s_boot_counter.boot_count = 1;
            s_boot_counter.first_boot_time = current_time;
        } else {
            s_boot_counter.boot_count++;
            ESP_LOGI(TAG, "Incremented boot count: %lu (within time window)", s_boot_counter.boot_count);
        }
    }
    
    s_boot_counter.last_boot_time = current_time;
    
    ESP_LOGI(TAG, "Boot counter: %lu, time since first: %lu ms", 
             s_boot_counter.boot_count, 
             current_time - s_boot_counter.first_boot_time);
    
    return ESP_OK;
}

esp_err_t boot_counter_clear(void)
{
    ESP_LOGI(TAG, "Clearing boot counter");
    s_boot_counter.boot_count = 0;
    s_boot_counter.last_boot_time = 0;
    s_boot_counter.first_boot_time = 0;
    return ESP_OK;
}

esp_err_t boot_counter_check_reset_pattern(bool *should_provision)
{
    if (should_provision == NULL) {
        ESP_LOGE(TAG, "Invalid parameter: should_provision is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    *should_provision = false;
    
    if (s_boot_counter.magic_number != BOOT_COUNTER_MAGIC_NUMBER) {
        ESP_LOGW(TAG, "Boot counter not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_boot_counter.boot_count >= BOOT_COUNTER_RESET_THRESHOLD) {
        if (boot_counter_is_in_time_window()) {
            *should_provision = true;
            ESP_LOGW(TAG, "Reset pattern detected! Boot count: %lu (threshold: %d)", 
                     s_boot_counter.boot_count, BOOT_COUNTER_RESET_THRESHOLD);
        } else {
            ESP_LOGI(TAG, "Boot count exceeded threshold but outside time window");
        }
    }
    
    return ESP_OK;
}

esp_err_t boot_counter_get_count(uint32_t *count)
{
    if (count == NULL) {
        ESP_LOGE(TAG, "Invalid parameter: count is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    *count = s_boot_counter.boot_count;
    return ESP_OK;
}

esp_err_t boot_counter_set_normal_operation(void)
{
    ESP_LOGI(TAG, "Setting normal operation mode - clearing boot counter");
    return boot_counter_clear();
}

bool boot_counter_is_in_time_window(void)
{
    if (s_boot_counter.boot_count == 0) {
        return false;
    }
    
    uint32_t current_time = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t time_since_first = current_time - s_boot_counter.first_boot_time;
    
    bool in_window = time_since_first <= BOOT_COUNTER_TIME_WINDOW_MS;
    
    ESP_LOGD(TAG, "Time window check: %lu ms elapsed, window: %d ms, in_window: %s",
             time_since_first, BOOT_COUNTER_TIME_WINDOW_MS, in_window ? "true" : "false");
    
    return in_window;
}