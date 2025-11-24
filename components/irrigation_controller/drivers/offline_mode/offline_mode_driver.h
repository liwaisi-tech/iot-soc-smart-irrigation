/**
 * @file offline_mode_driver.h
 * @brief Offline Mode Driver - Adaptive irrigation evaluation when disconnected
 *
 * Provides adaptive offline irrigation logic with 4 levels based on soil humidity.
 * Automatically adjusts evaluation interval when WiFi/MQTT unavailable.
 *
 * Levels:
 * - NORMAL (2h):     Soil 45-75% - normal conditions
 * - WARNING (1h):    Soil 40-45% - low humidity warning
 * - CRITICAL (30m):  Soil 30-40% - critical conditions
 * - EMERGENCY (15m): Soil <30%   - emergency conditions
 *
 * Phase 5: Fixed intervals per level
 * Phase 6: Adaptive learning from sensor trends
 *
 * @author Liwaisi Tech
 * @date 2025-10-25
 * @version 1.0.0 - Phase 5 (Adaptive Levels)
 */

#ifndef OFFLINE_MODE_DRIVER_H
#define OFFLINE_MODE_DRIVER_H

#include "esp_err.h"
#include "irrigation_controller.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ TYPES AND ENUMS ============================ */

// Note: offline_level_t is defined in irrigation_controller.h
// Determines evaluation interval based on soil humidity
// - NORMAL (0): 2h intervals (45-75% soil)
// - WARNING (1): 1h intervals (40-45% soil)
// - CRITICAL (2): 30min intervals (30-40% soil)
// - EMERGENCY (3): 15min intervals (<30% soil)

/**
 * @brief Offline mode evaluation result
 */
typedef struct {
    offline_level_t level;           ///< Current offline level
    uint32_t interval_ms;            ///< Evaluation interval in milliseconds
    float soil_humidity_avg;          ///< Soil humidity that triggered this level
    const char* reason;              ///< Human-readable reason
} offline_evaluation_t;

/**
 * @brief Offline mode status
 */
typedef struct {
    bool is_active;                  ///< Offline mode active
    offline_level_t current_level;   ///< Current level
    uint32_t next_evaluation_time;   ///< Next evaluation timestamp
    float last_soil_humidity;        ///< Last soil humidity reading
    uint32_t level_change_count;     ///< Total level changes
} offline_mode_status_t;

/* ============================ PUBLIC API ============================ */

/**
 * @brief Initialize offline mode driver
 *
 * Sets up offline mode evaluation system.
 * Must be called before using other offline_mode functions.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t offline_mode_init(void);

/**
 * @brief Deinitialize offline mode driver
 *
 * Cleans up resources.
 *
 * @return ESP_OK on success
 */
esp_err_t offline_mode_deinit(void);

/**
 * @brief Evaluate offline level based on soil humidity
 *
 * Determines which offline level to use based on current soil humidity.
 * Uses hysteresis to avoid rapid level switching.
 *
 * Decision Table:
 * - Soil 45-75%: NORMAL   (interval 2h)
 * - Soil 40-45%: WARNING  (interval 1h)
 * - Soil 30-40%: CRITICAL (interval 30min)
 * - Soil <30%:   EMERGENCY (interval 15min)
 *
 * @param soil_humidity_avg Average soil humidity percentage (0-100%)
 * @return offline_evaluation_t with level and interval
 */
offline_evaluation_t offline_mode_evaluate(float soil_humidity_avg);

/**
 * @brief Get evaluation interval for a specific level
 *
 * Returns the evaluation interval in milliseconds for the given level.
 *
 * @param level Offline level
 * @return Interval in milliseconds (7200000=2h, 3600000=1h, 1800000=30m, 900000=15m)
 */
uint32_t offline_mode_get_interval_ms(offline_level_t level);

/**
 * @brief Get current offline level
 *
 * Returns the most recently evaluated offline level.
 *
 * @return Current offline level
 */
offline_level_t offline_mode_get_current_level(void);

/**
 * @brief Get offline mode status
 *
 * @param[out] status Pointer to status structure to fill
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if status is NULL
 */
esp_err_t offline_mode_get_status(offline_mode_status_t* status);

/* ============================ CONFIGURATION ============================ */

/**
 * @brief Offline level interval definitions (milliseconds)
 */
#define OFFLINE_INTERVAL_NORMAL_MS      (2 * 60 * 60 * 1000)    ///< 2 hours
#define OFFLINE_INTERVAL_WARNING_MS     (1 * 60 * 60 * 1000)    ///< 1 hour
#define OFFLINE_INTERVAL_CRITICAL_MS    (30 * 60 * 1000)        ///< 30 minutes
#define OFFLINE_INTERVAL_EMERGENCY_MS   (15 * 60 * 1000)        ///< 15 minutes

/**
 * @brief Offline level thresholds (soil humidity percentage)
 *
 * Note: These can be overridden by Kconfig settings
 */
#define OFFLINE_THRESHOLD_NORMAL_LOW    45.0f   ///< Below: WARNING level
#define OFFLINE_THRESHOLD_WARNING_LOW   40.0f   ///< Below: CRITICAL level
#define OFFLINE_THRESHOLD_CRITICAL_LOW  30.0f   ///< Below: EMERGENCY level

/**
 * @brief Hysteresis for level changes (percentage points)
 *
 * Prevents rapid switching between levels due to sensor noise.
 * Level only changes when humidity crosses threshold by this amount.
 */
#define OFFLINE_LEVEL_HYSTERESIS        2.0f    ///< 2% hysteresis

#ifdef __cplusplus
}
#endif

#endif // OFFLINE_MODE_DRIVER_H
