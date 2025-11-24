/**
 * @file offline_mode_driver.c
 * @brief Offline Mode Driver Implementation
 *
 * Adaptive offline irrigation logic with 4 levels based on soil humidity.
 * Automatically adjusts evaluation interval when WiFi/MQTT unavailable.
 *
 * @author Liwaisi Tech
 * @date 2025-10-25
 * @version 1.0.0 - Phase 5
 */

#include "offline_mode_driver.h"
#include "esp_log.h"
#include "freertos/portmacro.h"
#include <time.h>
#include <string.h>

/* ============================ PRIVATE CONSTANTS ============================ */

static const char *TAG = "offline_mode";

/**
 * @brief Offline level configuration
 *
 * Maps levels to thresholds and intervals.
 */
typedef struct {
    offline_level_t level;
    float threshold_low;              ///< Soil humidity lower bound
    float threshold_high;             ///< Soil humidity upper bound
    uint32_t interval_ms;             ///< Evaluation interval
    const char* name;                 ///< Human-readable name
} offline_level_config_t;

static const offline_level_config_t s_level_config[] = {
    {
        .level = OFFLINE_LEVEL_NORMAL,
        .threshold_low = 45.0f,
        .threshold_high = 100.0f,
        .interval_ms = OFFLINE_INTERVAL_NORMAL_MS,
        .name = "NORMAL (2h)"
    },
    {
        .level = OFFLINE_LEVEL_WARNING,
        .threshold_low = 40.0f,
        .threshold_high = 45.0f,
        .interval_ms = OFFLINE_INTERVAL_WARNING_MS,
        .name = "WARNING (1h)"
    },
    {
        .level = OFFLINE_LEVEL_CRITICAL,
        .threshold_low = 30.0f,
        .threshold_high = 40.0f,
        .interval_ms = OFFLINE_INTERVAL_CRITICAL_MS,
        .name = "CRITICAL (30m)"
    },
    {
        .level = OFFLINE_LEVEL_EMERGENCY,
        .threshold_low = 0.0f,
        .threshold_high = 30.0f,
        .interval_ms = OFFLINE_INTERVAL_EMERGENCY_MS,
        .name = "EMERGENCY (15m)"
    }
};

#define OFFLINE_LEVEL_COUNT (sizeof(s_level_config) / sizeof(s_level_config[0]))

/* ============================ PRIVATE STATE ============================ */

/**
 * @brief Offline mode context
 */
typedef struct {
    bool is_initialized;
    bool is_active;
    offline_level_t current_level;
    float last_soil_humidity;
    time_t last_evaluation_time;
    uint32_t level_change_count;
    portMUX_TYPE spinlock;
} offline_mode_context_t;

static offline_mode_context_t s_offline_ctx = {
    .is_initialized = false,
    .is_active = false,
    .current_level = OFFLINE_LEVEL_NORMAL,
    .last_soil_humidity = 50.0f,
    .last_evaluation_time = 0,
    .level_change_count = 0,
    .spinlock = portMUX_INITIALIZER_UNLOCKED
};

/* ============================ PRIVATE FUNCTIONS ============================ */

/**
 * @brief Get level configuration by level type
 */
static const offline_level_config_t* _get_level_config(offline_level_t level)
{
    for (size_t i = 0; i < OFFLINE_LEVEL_COUNT; i++) {
        if (s_level_config[i].level == level) {
            return &s_level_config[i];
        }
    }
    // Default to NORMAL if not found
    return &s_level_config[0];
}

/**
 * @brief Determine level from soil humidity
 *
 * Uses hysteresis to avoid rapid switching.
 */
static offline_level_t _evaluate_level(float soil_humidity_avg, offline_level_t current_level)
{
    // Check each level in reverse order (from most critical)
    for (int i = OFFLINE_LEVEL_COUNT - 1; i >= 0; i--) {
        const offline_level_config_t* config = &s_level_config[i];

        if (soil_humidity_avg < config->threshold_high) {
            // Apply hysteresis only if changing level
            if (config->level != current_level) {
                // Require crossing by hysteresis amount
                float hysteresis_threshold = config->threshold_high - OFFLINE_LEVEL_HYSTERESIS;
                if (soil_humidity_avg < hysteresis_threshold) {
                    return config->level;
                }
                // Return current level if within hysteresis zone
                return current_level;
            } else {
                // Already in this level
                return config->level;
            }
        }
    }

    // Default to NORMAL
    return OFFLINE_LEVEL_NORMAL;
}

/**
 * @brief Format level name for logging
 */
static const char* _get_level_name(offline_level_t level)
{
    const offline_level_config_t* config = _get_level_config(level);
    return config->name;
}

/* ============================ PUBLIC API IMPLEMENTATION ============================ */

esp_err_t offline_mode_init(void)
{
    if (s_offline_ctx.is_initialized) {
        ESP_LOGW(TAG, "Offline mode already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing offline mode driver");

    // Initialize with defaults
    portENTER_CRITICAL(&s_offline_ctx.spinlock);
    {
        s_offline_ctx.is_active = false;
        s_offline_ctx.current_level = OFFLINE_LEVEL_NORMAL;
        s_offline_ctx.last_soil_humidity = 50.0f;
        s_offline_ctx.last_evaluation_time = time(NULL);
        s_offline_ctx.level_change_count = 0;
        s_offline_ctx.is_initialized = true;
    }
    portEXIT_CRITICAL(&s_offline_ctx.spinlock);

    ESP_LOGI(TAG, "Offline mode initialized");
    ESP_LOGI(TAG, "  Level thresholds: NORMAL(45 percent), WARNING(40 percent), CRITICAL(30 percent), EMERGENCY(<30 percent)");
    ESP_LOGI(TAG, "  Hysteresis: %.1f percent", OFFLINE_LEVEL_HYSTERESIS);

    return ESP_OK;
}

esp_err_t offline_mode_deinit(void)
{
    if (!s_offline_ctx.is_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing offline mode driver");

    portENTER_CRITICAL(&s_offline_ctx.spinlock);
    {
        s_offline_ctx.is_initialized = false;
        s_offline_ctx.is_active = false;
    }
    portEXIT_CRITICAL(&s_offline_ctx.spinlock);

    return ESP_OK;
}

offline_evaluation_t offline_mode_evaluate(float soil_humidity_avg)
{
    offline_evaluation_t result = {0};

    if (!s_offline_ctx.is_initialized) {
        ESP_LOGW(TAG, "Offline mode not initialized");
        result.level = OFFLINE_LEVEL_NORMAL;
        result.interval_ms = OFFLINE_INTERVAL_NORMAL_MS;
        result.reason = "not_initialized";
        return result;
    }

    // Validate input
    if (soil_humidity_avg < 0.0f || soil_humidity_avg > 100.0f) {
        ESP_LOGW(TAG, "Invalid soil humidity: %.1f%%", soil_humidity_avg);
        soil_humidity_avg = 50.0f;  // Default to middle
    }

    // Get current level
    offline_level_t current_level;
    portENTER_CRITICAL(&s_offline_ctx.spinlock);
    {
        current_level = s_offline_ctx.current_level;
    }
    portEXIT_CRITICAL(&s_offline_ctx.spinlock);

    // Evaluate new level with hysteresis
    offline_level_t new_level = _evaluate_level(soil_humidity_avg, current_level);

    // Get configuration for new level
    const offline_level_config_t* config = _get_level_config(new_level);

    // Fill result
    result.level = new_level;
    result.interval_ms = config->interval_ms;
    result.soil_humidity_avg = soil_humidity_avg;
    result.reason = config->name;

    // Update context if level changed
    if (new_level != current_level) {
        portENTER_CRITICAL(&s_offline_ctx.spinlock);
        {
            s_offline_ctx.current_level = new_level;
            s_offline_ctx.last_soil_humidity = soil_humidity_avg;
            s_offline_ctx.last_evaluation_time = time(NULL);
            s_offline_ctx.level_change_count++;
        }
        portEXIT_CRITICAL(&s_offline_ctx.spinlock);

        ESP_LOGI(TAG, "Offline level changed: %s → %s (soil=%.1f%%, interval=%lu ms)",
                 _get_level_name(current_level),
                 _get_level_name(new_level),
                 soil_humidity_avg,
                 result.interval_ms);
    } else {
        // Log at debug level if no change
        ESP_LOGD(TAG, "Offline level maintained: %s (soil=%.1f%%, interval=%lu ms)",
                 _get_level_name(current_level),
                 soil_humidity_avg,
                 result.interval_ms);

        // Still update last soil humidity for trend tracking
        portENTER_CRITICAL(&s_offline_ctx.spinlock);
        {
            s_offline_ctx.last_soil_humidity = soil_humidity_avg;
        }
        portEXIT_CRITICAL(&s_offline_ctx.spinlock);
    }

    return result;
}

uint32_t offline_mode_get_interval_ms(offline_level_t level)
{
    const offline_level_config_t* config = _get_level_config(level);
    return config->interval_ms;
}

offline_level_t offline_mode_get_current_level(void)
{
    offline_level_t level;

    portENTER_CRITICAL(&s_offline_ctx.spinlock);
    {
        if (!s_offline_ctx.is_initialized) {
            level = OFFLINE_LEVEL_NORMAL;
        } else {
            level = s_offline_ctx.current_level;
        }
    }
    portEXIT_CRITICAL(&s_offline_ctx.spinlock);

    return level;
}

esp_err_t offline_mode_get_status(offline_mode_status_t* status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_offline_ctx.spinlock);
    {
        status->is_active = s_offline_ctx.is_active;
        status->current_level = s_offline_ctx.current_level;
        status->next_evaluation_time = s_offline_ctx.last_evaluation_time +
                                      (offline_mode_get_interval_ms(s_offline_ctx.current_level) / 1000);
        status->last_soil_humidity = s_offline_ctx.last_soil_humidity;
        status->level_change_count = s_offline_ctx.level_change_count;
    }
    portEXIT_CRITICAL(&s_offline_ctx.spinlock);

    return ESP_OK;
}
