/**
 * @file safety_watchdog.h
 * @brief Safety Watchdog - Monitoring and protection logic
 *
 * Monitors irrigation session safety conditions:
 * - Session duration limits (max 120 minutes)
 * - Valve operation time (alert if > 40 minutes)
 * - Temperature protection (auto-stop if > 40°C)
 * - Over-moisture protection (auto-stop if >= 80%)
 * - MQTT override timeout (max 30 minutes)
 *
 * Returns alerts for safety violations that trigger immediate action.
 *
 * @author Liwaisi Tech
 * @date 2025-10-21
 * @version 1.0.0 - Phase 5
 */

#ifndef SAFETY_WATCHDOG_H
#define SAFETY_WATCHDOG_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ WATCHDOG INPUTS ============================ */

/**
 * @brief Input parameters for safety watchdog check
 *
 * Contains current system state parameters to evaluate
 * against safety thresholds.
 */
typedef struct {
    uint32_t session_duration_ms;       ///< Current session duration (milliseconds)
    uint32_t valve_open_time_ms;        ///< Time valve has been open (milliseconds)
    uint32_t mqtt_override_idle_ms;     ///< Time since last MQTT command (milliseconds)
    float current_temperature;          ///< Current ambient temperature (°C)
    float current_soil_humidity_avg;    ///< Average soil humidity (%)
    bool is_raining;                    ///< Rain detection flag (future)
} watchdog_inputs_t;

/* ============================ WATCHDOG ALERTS ============================ */

/**
 * @brief Safety alerts from watchdog evaluation
 *
 * Boolean flags indicate which safety thresholds have been exceeded.
 * Multiple alerts can be triggered simultaneously.
 */
typedef struct {
    bool session_timeout_exceeded;      ///< Session > 120 minutes
    bool valve_timeout_exceeded;        ///< Valve open > 40 minutes (alert to N8N)
    bool mqtt_override_timeout;         ///< MQTT override > 30 minutes (auto-timeout)
    bool temperature_critical;          ///< Temperature > 40°C (thermal protection)
    bool overmoisture_detected;         ///< Any soil sensor >= 80%
    bool rain_detected;                 ///< Rain detected (Phase 6 feature)
} watchdog_alerts_t;

/* ============================ WATCHDOG API ============================ */

/**
 * @brief Initialize safety watchdog
 *
 * Sets up watchdog state and resets session timer.
 * Must be called before evaluating safety conditions.
 *
 * @return ESP_OK on success
 */
esp_err_t safety_watchdog_init(void);

/**
 * @brief Evaluate safety conditions
 *
 * Checks current system state against all safety thresholds.
 * Returns structure with boolean flags for each alert condition.
 *
 * Usage:
 * ```c
 * watchdog_inputs_t inputs = {
 *     .session_duration_ms = elapsed_ms,
 *     .valve_open_time_ms = valve_elapsed_ms,
 *     .current_temperature = reading.ambient.temperature,
 *     .current_soil_humidity_avg = (soil1 + soil2 + soil3) / 3.0
 * };
 * watchdog_alerts_t alerts = safety_watchdog_check(&inputs);
 *
 * if (alerts.temperature_critical) {
 *     // Stop irrigation immediately
 * }
 * ```
 *
 * @param inputs Pointer to watchdog input parameters
 * @return watchdog_alerts_t structure with alert flags set
 */
watchdog_alerts_t safety_watchdog_check(const watchdog_inputs_t* inputs);

/**
 * @brief Reset session timer
 *
 * Called when starting new irrigation session.
 * Resets session_start_time for duration tracking.
 *
 * @return ESP_OK on success
 */
esp_err_t safety_watchdog_reset_session(void);

/**
 * @brief Reset valve timer
 *
 * Called when opening valve.
 * Resets valve_open_time for timeout tracking.
 *
 * @return ESP_OK on success
 */
esp_err_t safety_watchdog_reset_valve_timer(void);

/**
 * @brief Reset MQTT override timer
 *
 * Called when receiving MQTT command.
 * Resets mqtt_override_idle_time for timeout tracking.
 *
 * @return ESP_OK on success
 */
esp_err_t safety_watchdog_reset_mqtt_timer(void);

/**
 * @brief Get watchdog statistics (debugging)
 *
 * Returns current watchdog state for monitoring/debugging.
 *
 * @param[out] inputs Pointer to store current watchdog inputs
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if inputs is NULL
 */
esp_err_t safety_watchdog_get_current_inputs(watchdog_inputs_t* inputs);

/* ============================ SAFETY THRESHOLDS ============================ */

/**
 * @brief Session duration limit (milliseconds)
 *
 * Maximum continuous irrigation time: 120 minutes
 * Triggers session_timeout_exceeded alert
 */
#define WATCHDOG_SESSION_TIMEOUT_MS (120 * 60 * 1000)  // 120 minutes

/**
 * @brief Valve operation timeout (milliseconds)
 *
 * Maximum valve open time before alert to N8N: 40 minutes
 * Does NOT stop irrigation - sends notification only
 */
#define WATCHDOG_VALVE_TIMEOUT_MS (40 * 60 * 1000)     // 40 minutes

/**
 * @brief MQTT override timeout (milliseconds)
 *
 * Maximum MQTT override active time: 30 minutes
 * After timeout without new command, returns to auto mode
 */
#define WATCHDOG_MQTT_TIMEOUT_MS (30 * 60 * 1000)      // 30 minutes

/**
 * @brief Critical temperature threshold (°C)
 *
 * Auto-stop irrigation if temperature exceeds this
 * Triggers temperature_critical alert
 */
#define WATCHDOG_TEMP_CRITICAL_CELSIUS 40.0f

/**
 * @brief Over-moisture threshold (%)
 *
 * Auto-stop if ANY soil sensor reaches this level
 * Triggers overmoisture_detected alert
 */
#define WATCHDOG_OVERMOISTURE_PERCENT 80.0f

#ifdef __cplusplus
}
#endif

#endif // SAFETY_WATCHDOG_H
