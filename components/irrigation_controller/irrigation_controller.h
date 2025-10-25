/**
 * @file irrigation_controller.h
 * @brief Irrigation Controller Component - Smart Irrigation Logic
 *
 * Complete irrigation control system with safety features, offline mode,
 * and adaptive logic based on Colombian agricultural dataset.
 *
 * Component Responsibilities:
 * - Valve control (2 relay channels)
 * - Automatic irrigation evaluation
 * - Safety limits and thermal protection
 * - MQTT command execution
 * - Offline adaptive mode (4 levels)
 * - Session management and history
 *
 * Safety Features:
 * - Max 2h per session, 4h minimum between sessions
 * - Thermal protection (>40°C auto-stop)
 * - Over-irrigation protection (>80% humidity auto-stop)
 * - Emergency stop capability
 *
 * Migration from hexagonal: NEW component - implements irrigation business logic
 *
 * @author Liwaisi Tech
 * @date 2025-10-01
 * @version 2.0.0 - Component-Based Architecture
 */

#ifndef IRRIGATION_CONTROLLER_H
#define IRRIGATION_CONTROLLER_H

#include "esp_err.h"
#include "esp_event.h"
#include "common_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ TYPES AND ENUMS ============================ */

/**
 * @brief Irrigation decision result
 */
typedef enum {
    IRRIGATION_DECISION_NO_ACTION = 0,  ///< No irrigation needed
    IRRIGATION_DECISION_START,          ///< Start irrigation
    IRRIGATION_DECISION_CONTINUE,       ///< Continue current irrigation
    IRRIGATION_DECISION_STOP,           ///< Stop irrigation (target reached)
    IRRIGATION_DECISION_EMERGENCY_STOP, ///< Emergency stop (safety)
    IRRIGATION_DECISION_THERMAL_STOP    ///< Thermal protection stop
} irrigation_decision_t;

/**
 * @brief Irrigation evaluation result
 */
typedef struct {
    irrigation_decision_t decision;     ///< Decision result
    uint16_t duration_minutes;          ///< Recommended duration (for START)
    float soil_avg_humidity;            ///< Average soil humidity evaluated
    float ambient_temperature;          ///< Current temperature
    char reason[64];                    ///< Human-readable reason
} irrigation_evaluation_t;

/**
 * @brief Irrigation session record
 */
typedef struct {
    uint32_t start_time;                ///< Session start timestamp
    uint32_t end_time;                  ///< Session end timestamp
    uint16_t duration_seconds;          ///< Actual duration
    float soil_humidity_before;         ///< Soil humidity before irrigation
    float soil_humidity_after;          ///< Soil humidity after irrigation
    uint8_t valve_used;                 ///< Valve number used (1-2)
    irrigation_command_t stop_reason;   ///< Stop reason
} irrigation_session_t;

/**
 * @brief Irrigation statistics
 */
typedef struct {
    uint32_t total_sessions;            ///< Total irrigation sessions
    uint32_t total_runtime_seconds;     ///< Total irrigation time
    uint32_t today_runtime_seconds;     ///< Today's irrigation time
    uint32_t emergency_stops;           ///< Emergency stop count
    uint32_t thermal_stops;             ///< Thermal protection triggers
    uint32_t last_session_time;         ///< Last session timestamp
    irrigation_session_t last_session;  ///< Last session details
} irrigation_stats_t;

/**
 * @brief Irrigation controller configuration
 */
typedef struct {
    // Thresholds (Colombian dataset optimized)
    float soil_threshold_critical;      ///< Critical soil humidity (30%)
    float soil_threshold_optimal;       ///< Optimal soil humidity (75%)
    float soil_threshold_max;           ///< Maximum soil humidity (80%)
    float temp_critical;                ///< Critical temperature (32°C)
    float temp_thermal_stop;            ///< Thermal protection (40°C)

    // Safety limits
    uint16_t max_duration_minutes;      ///< Max session duration (120 min)
    uint16_t min_interval_minutes;      ///< Min interval between sessions (240 min)
    uint16_t max_daily_minutes;         ///< Max daily irrigation (360 min)

    // Valve configuration
    uint8_t valve_gpio_1;               ///< Valve 1 GPIO (relay IN1)
    uint8_t valve_gpio_2;               ///< Valve 2 GPIO (relay IN2)
    uint8_t primary_valve;              ///< Primary valve to use (1 or 2)

    // Offline mode
    bool enable_offline_mode;           ///< Enable offline irrigation
    uint32_t offline_timeout_seconds;   ///< Timeout to activate offline (300s)

    // Adaptive mode thresholds
    float offline_warning_threshold;    ///< Warning threshold (50%)
    float offline_critical_threshold;   ///< Critical threshold (40%)
    float offline_emergency_threshold;  ///< Emergency threshold (30%)
} irrigation_controller_config_t;

/**
 * @brief Offline mode level
 */
typedef enum {
    OFFLINE_LEVEL_NORMAL = 0,           ///< Normal: 2h intervals
    OFFLINE_LEVEL_WARNING,              ///< Warning: 1h intervals
    OFFLINE_LEVEL_CRITICAL,             ///< Critical: 30min intervals
    OFFLINE_LEVEL_EMERGENCY             ///< Emergency: 15min intervals
} offline_level_t;

/**
 * @brief Complete irrigation controller status
 */
typedef struct {
    irrigation_state_t state;           ///< Current state
    irrigation_mode_t mode;             ///< Current mode (online/offline)
    offline_level_t offline_level;      ///< Offline mode level

    // Current session
    bool is_irrigating;                 ///< Irrigation active
    uint32_t session_start;             ///< Current session start
    uint32_t session_elapsed_sec;       ///< Current session elapsed time
    uint8_t active_valve;               ///< Active valve (1-2, 0=none)

    // Safety status
    bool safety_lock;                   ///< Safety lock active
    bool thermal_protection_active;     ///< Thermal protection triggered
    uint32_t next_allowed_session;      ///< Next allowed session timestamp

    // Statistics
    irrigation_stats_t stats;           ///< Session statistics

    // Last evaluation
    irrigation_evaluation_t last_eval;  ///< Last evaluation result
    uint32_t last_eval_time;            ///< Last evaluation timestamp
} irrigation_controller_status_t;

/* ============================ PUBLIC API ============================ */

/**
 * @brief Initialize irrigation controller component
 *
 * Initializes GPIO for valves, configures safety limits, and prepares
 * irrigation logic.
 *
 * @param config Configuration (NULL for default)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t irrigation_controller_init(const irrigation_controller_config_t* config);

/**
 * @brief Deinitialize irrigation controller
 *
 * Stops any active irrigation, releases GPIO, and cleans up resources.
 *
 * @return ESP_OK on success
 */
esp_err_t irrigation_controller_deinit(void);

/**
 * @brief Execute irrigation command
 *
 * Executes MQTT or manual irrigation command with safety checks.
 *
 * @param command Command type (START/STOP/EMERGENCY_STOP)
 * @param duration_minutes Duration for START command (0 for default)
 * @return ESP_OK if command executed, error code if safety check failed
 */
esp_err_t irrigation_controller_execute_command(irrigation_command_t command,
                                                uint16_t duration_minutes);

/**
 * @brief Evaluate soil conditions and decide irrigation action
 *
 * Evaluates current soil moisture and environmental conditions.
 * Applies Colombian dataset thresholds and safety logic.
 * In online mode: provides recommendation
 * In offline mode: auto-executes irrigation if needed
 *
 * @param soil_data Current soil moisture data
 * @param ambient_data Current ambient environmental data
 * @param[out] evaluation Evaluation result (can be NULL)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t irrigation_controller_evaluate_and_act(const soil_data_t* soil_data,
                                                 const ambient_data_t* ambient_data,
                                                 irrigation_evaluation_t* evaluation);

/**
 * @brief Manual start irrigation
 *
 * Manually starts irrigation with specified duration.
 * Performs all safety checks.
 *
 * @param duration_minutes Irrigation duration (0 for default 15min)
 * @param valve_number Valve to use (1-2, 0 for primary)
 * @return ESP_OK if started, error code if safety check failed
 */
esp_err_t irrigation_controller_start(uint16_t duration_minutes, uint8_t valve_number);

/**
 * @brief Manual stop irrigation
 *
 * Manually stops current irrigation session.
 *
 * @return ESP_OK on success
 */
esp_err_t irrigation_controller_stop(void);

/**
 * @brief Execute irrigation command via MQTT
 *
 * Executes START, STOP, or EMERGENCY_STOP command with full safety validation.
 * Applies all security checks (safety lock, timing, daily limits).
 *
 * START command:
 * - Validates safety_lock (if active, rejects)
 * - Validates minimum interval between sessions (4h default)
 * - Validates max daily irrigation time (360 min default)
 * - Opens valve and changes state to ACTIVE
 *
 * STOP command:
 * - Closes valve
 * - Changes state to IDLE
 * - Accumulates runtime to daily counter
 *
 * EMERGENCY_STOP command:
 * - Closes ALL valves immediately
 * - Activates safety_lock (prevents any irrigation until manually unlocked)
 * - Changes state to EMERGENCY_STOP
 *
 * @param command Command type (START/STOP/EMERGENCY_STOP)
 * @param duration_minutes Duration for START command (0 = default 15 min)
 * @return ESP_OK if executed, ESP_ERR_INVALID_STATE if safety checks fail,
 *         ESP_ERR_INVALID_ARG for invalid command
 */
esp_err_t irrigation_controller_execute_command(irrigation_command_t command,
                                                uint16_t duration_minutes);

/**
 * @brief Evaluate soil/environment conditions and decide irrigation action
 *
 * Evaluates sensor data and decides irrigation action based on mode (ONLINE/OFFLINE).
 *
 * ONLINE MODE (WiFi/MQTT connected):
 * - Evaluates thresholds
 * - Only RECOMMENDS actions (saves in last_evaluation)
 * - Does NOT execute (waits for MQTT command)
 * - Useful for cloud-based decisions
 *
 * OFFLINE MODE (no WiFi/MQTT):
 * - Evaluates thresholds
 * - Uses offline_mode_driver to determine level (NORMAL/WARNING/CRITICAL/EMERGENCY)
 * - AUTO-EXECUTES irrigation if needed (CRITICAL or EMERGENCY levels)
 * - Provides autonomous operation when disconnected
 *
 * Evaluation Thresholds:
 * - soil < critical (30%): START irrigation
 * - soil >= optimal (75%): STOP irrigation
 * - soil >= max (80%): EMERGENCY STOP (over-moisture)
 * - temperature > 40°C: THERMAL PROTECTION
 *
 * @param soil_data Soil moisture data from 3 sensors
 * @param ambient_data Temperature and humidity data
 * @param[out] evaluation Optional pointer to store evaluation result
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not initialized,
 *         ESP_ERR_INVALID_ARG if parameters invalid
 */
esp_err_t irrigation_controller_evaluate_and_act(const soil_data_t* soil_data,
                                                 const ambient_data_t* ambient_data,
                                                 irrigation_evaluation_t* evaluation);

/**
 * @brief Emergency stop
 *
 * Immediately stops irrigation and activates safety lock.
 * Requires manual unlock.
 *
 * @return ESP_OK on success
 */
esp_err_t irrigation_controller_emergency_stop(void);

/**
 * @brief Unlock safety lock
 *
 * Unlocks safety lock after emergency stop.
 *
 * @return ESP_OK on success
 */
esp_err_t irrigation_controller_unlock_safety(void);

/**
 * @brief Set offline mode
 *
 * Enables or disables offline mode.
 * In offline mode, controller auto-executes irrigation based on sensor data.
 *
 * @param enable true to enable offline mode
 * @return ESP_OK on success
 */
esp_err_t irrigation_controller_set_offline_mode(bool enable);

/**
 * @brief Check if offline mode is active
 *
 * @return true if offline mode active
 */
bool irrigation_controller_is_offline_mode(void);

/**
 * @brief Get offline mode level
 *
 * @return Current offline level
 */
offline_level_t irrigation_controller_get_offline_level(void);

/**
 * @brief Get controller status
 *
 * @param[out] status Pointer to status structure to fill
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if status is NULL
 */
esp_err_t irrigation_controller_get_status(irrigation_controller_status_t* status);

/**
 * @brief Check if irrigation is currently active
 *
 * @return true if irrigating
 */
bool irrigation_controller_is_irrigating(void);

/**
 * @brief Get current irrigation state
 *
 * @return Current state
 */
irrigation_state_t irrigation_controller_get_state(void);

/**
 * @brief Get irrigation statistics
 *
 * @param[out] stats Pointer to statistics structure to fill
 * @return ESP_OK on success
 */
esp_err_t irrigation_controller_get_stats(irrigation_stats_t* stats);

/**
 * @brief Reset daily statistics
 *
 * Resets today's runtime counter (called automatically at midnight).
 *
 * @return ESP_OK on success
 */
esp_err_t irrigation_controller_reset_daily_stats(void);

/**
 * @brief Check if irrigation is allowed (safety checks)
 *
 * Checks all safety conditions without starting irrigation.
 *
 * @param[out] reason Buffer to store reason if not allowed (can be NULL)
 * @param reason_len Size of reason buffer
 * @return true if irrigation allowed, false otherwise
 */
bool irrigation_controller_is_allowed(char* reason, size_t reason_len);

/* ============================ EVENT DEFINITIONS ============================ */

/**
 * @brief Irrigation controller event base
 */
ESP_EVENT_DECLARE_BASE(IRRIGATION_CONTROLLER_EVENTS);

/**
 * @brief Irrigation controller event IDs
 */
typedef enum {
    IRRIGATION_EVENT_INITIALIZED,       ///< Component initialized
    IRRIGATION_EVENT_STARTED,           ///< Irrigation started
    IRRIGATION_EVENT_STOPPED,           ///< Irrigation stopped normally
    IRRIGATION_EVENT_EMERGENCY_STOP,    ///< Emergency stop triggered
    IRRIGATION_EVENT_THERMAL_STOP,      ///< Thermal protection triggered
    IRRIGATION_EVENT_DURATION_REACHED,  ///< Max duration reached
    IRRIGATION_EVENT_TARGET_REACHED,    ///< Target humidity reached
    IRRIGATION_EVENT_EVALUATION_DONE,   ///< Evaluation completed
    IRRIGATION_EVENT_OFFLINE_MODE_CHANGED, ///< Offline mode changed
    IRRIGATION_EVENT_SAFETY_LOCK        ///< Safety lock activated
} irrigation_controller_event_id_t;

/* ============================ CONFIGURATION ============================ */

/**
 * @brief Default irrigation controller configuration (Colombian dataset)
 */
#define IRRIGATION_CONTROLLER_DEFAULT_CONFIG() {    \
    .soil_threshold_critical = THRESHOLD_SOIL_CRITICAL,     \
    .soil_threshold_optimal = THRESHOLD_SOIL_OPTIMAL,       \
    .soil_threshold_max = THRESHOLD_SOIL_MAX,               \
    .temp_critical = THRESHOLD_TEMP_CRITICAL,               \
    .temp_thermal_stop = THRESHOLD_TEMP_THERMAL_STOP,       \
    .max_duration_minutes = MAX_IRRIGATION_DURATION_MIN,    \
    .min_interval_minutes = MIN_IRRIGATION_INTERVAL_MIN,    \
    .max_daily_minutes = MAX_DAILY_IRRIGATION_MIN,          \
    .valve_gpio_1 = GPIO_VALVE_1,                           \
    .valve_gpio_2 = GPIO_VALVE_2,                           \
    .primary_valve = 1,                                     \
    .enable_offline_mode = true,                            \
    .offline_timeout_seconds = 300,                         \
    .offline_warning_threshold = 50.0f,                     \
    .offline_critical_threshold = 40.0f,                    \
    .offline_emergency_threshold = 30.0f                    \
}

/**
 * @brief Default irrigation duration (minutes)
 */
#define IRRIGATION_DEFAULT_DURATION_MIN     15

/**
 * @brief Irrigation controller NVS namespace
 */
#define IRRIGATION_CONTROLLER_NVS_NAMESPACE "irrig_cfg"

/**
 * @brief Irrigation configuration NVS keys
 */
#define IRRIGATION_NVS_KEY_THRESHOLD        "threshold"
#define IRRIGATION_NVS_KEY_MAX_DURATION     "max_dur"
#define IRRIGATION_NVS_KEY_PRIMARY_VALVE    "prim_valve"

/**
 * @brief Irrigation statistics NVS keys
 */
#define IRRIGATION_NVS_KEY_TOTAL_SESSIONS   "total_sess"
#define IRRIGATION_NVS_KEY_TOTAL_RUNTIME    "total_run"

#ifdef __cplusplus
}
#endif

#endif // IRRIGATION_CONTROLLER_H
