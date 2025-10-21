/**
 * @file safety_watchdog.c
 * @brief Safety Watchdog Implementation - Monitoring and protection logic
 *
 * Evaluates system state against safety thresholds.
 * Tracks timers for session, valve operation, and MQTT override.
 *
 * @author Liwaisi Tech
 * @date 2025-10-21
 * @version 1.0.0 - Phase 5 (STUBS - implementation framework)
 */

#include "safety_watchdog.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <time.h>
#include <inttypes.h>

/* ============================ PRIVATE STATE ============================ */

static const char *TAG = "safety_watchdog";

/**
 * @brief Session timing state
 */
typedef struct {
    time_t session_start_time;      ///< Session start timestamp
    time_t valve_open_time;         ///< Valve open start time
    time_t mqtt_override_start_time;///< MQTT override start time
} watchdog_timers_t;

/**
 * @brief Global watchdog state
 */
static watchdog_timers_t s_timers = {
    .session_start_time = 0,
    .valve_open_time = 0,
    .mqtt_override_start_time = 0
};

/**
 * @brief Last evaluation inputs (for debugging)
 */
static watchdog_inputs_t s_last_inputs = {0};

/**
 * @brief Spinlock for thread-safe timer access
 */
static portMUX_TYPE watchdog_spinlock = portMUX_INITIALIZER_UNLOCKED;

/**
 * @brief Initialization flag
 */
static bool is_initialized = false;

/* ============================ PUBLIC API ============================ */

esp_err_t safety_watchdog_init(void)
{
    if (is_initialized) {
        ESP_LOGW(TAG, "Safety watchdog already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing safety watchdog");

    portENTER_CRITICAL(&watchdog_spinlock);
    {
        s_timers.session_start_time = 0;
        s_timers.valve_open_time = 0;
        s_timers.mqtt_override_start_time = 0;
    }
    portEXIT_CRITICAL(&watchdog_spinlock);

    is_initialized = true;

    // Log safety thresholds for reference
    ESP_LOGI(TAG, "Safety watchdog initialized");
    ESP_LOGI(TAG, "  Session timeout: %u ms (%.0f min)",
             WATCHDOG_SESSION_TIMEOUT_MS,
             WATCHDOG_SESSION_TIMEOUT_MS / (60.0 * 1000));
    ESP_LOGI(TAG, "  Valve timeout: %u ms (%.0f min)",
             WATCHDOG_VALVE_TIMEOUT_MS,
             WATCHDOG_VALVE_TIMEOUT_MS / (60.0 * 1000));
    ESP_LOGI(TAG, "  MQTT timeout: %u ms (%.0f min)",
             WATCHDOG_MQTT_TIMEOUT_MS,
             WATCHDOG_MQTT_TIMEOUT_MS / (60.0 * 1000));
    ESP_LOGI(TAG, "  Thermal protection: %.1f°C", WATCHDOG_TEMP_CRITICAL_CELSIUS);
    ESP_LOGI(TAG, "  Over-moisture: %.1f%%", WATCHDOG_OVERMOISTURE_PERCENT);

    return ESP_OK;
}

watchdog_alerts_t safety_watchdog_check(const watchdog_inputs_t* inputs)
{
    watchdog_alerts_t alerts = {
        .session_timeout_exceeded = false,
        .valve_timeout_exceeded = false,
        .mqtt_override_timeout = false,
        .temperature_critical = false,
        .overmoisture_detected = false,
        .rain_detected = false
    };

    if (!is_initialized || inputs == NULL) {
        return alerts;
    }

    // Store last inputs for debugging
    portENTER_CRITICAL(&watchdog_spinlock);
    {
        s_last_inputs = *inputs;
    }
    portEXIT_CRITICAL(&watchdog_spinlock);

    /* ==================== SESSION TIMEOUT CHECK ==================== */
    if (inputs->session_duration_ms >= WATCHDOG_SESSION_TIMEOUT_MS) {
        alerts.session_timeout_exceeded = true;
        ESP_LOGW(TAG, "Session timeout exceeded: %" PRIu32 " ms", inputs->session_duration_ms);
    }

    /* ==================== VALVE TIMEOUT CHECK ==================== */
    if (inputs->valve_open_time_ms >= WATCHDOG_VALVE_TIMEOUT_MS) {
        alerts.valve_timeout_exceeded = true;
        ESP_LOGW(TAG, "Valve timeout alert: %" PRIu32 " ms (%.0f min)",
                 inputs->valve_open_time_ms,
                 inputs->valve_open_time_ms / (60.0 * 1000));
    }

    /* ==================== MQTT OVERRIDE TIMEOUT CHECK ==================== */
    if (inputs->mqtt_override_idle_ms >= WATCHDOG_MQTT_TIMEOUT_MS) {
        alerts.mqtt_override_timeout = true;
        ESP_LOGI(TAG, "MQTT override timeout: %" PRIu32 " ms", inputs->mqtt_override_idle_ms);
    }

    /* ==================== THERMAL PROTECTION CHECK ==================== */
    if (inputs->current_temperature >= WATCHDOG_TEMP_CRITICAL_CELSIUS) {
        alerts.temperature_critical = true;
        ESP_LOGE(TAG, "THERMAL PROTECTION: Temperature %.1f°C >= %.1f°C threshold",
                 inputs->current_temperature, WATCHDOG_TEMP_CRITICAL_CELSIUS);
    }

    /* ==================== OVER-MOISTURE CHECK ==================== */
    if (inputs->current_soil_humidity_avg >= WATCHDOG_OVERMOISTURE_PERCENT) {
        alerts.overmoisture_detected = true;
        ESP_LOGW(TAG, "Over-moisture detected: %.1f%% >= %.1f%% threshold",
                 inputs->current_soil_humidity_avg, WATCHDOG_OVERMOISTURE_PERCENT);
    }

    /* ==================== RAIN DETECTION CHECK ==================== */
    // Phase 6 feature - reserved for future implementation
    alerts.rain_detected = false;

    // Log summary if any alerts
    if (alerts.session_timeout_exceeded || alerts.valve_timeout_exceeded ||
        alerts.mqtt_override_timeout || alerts.temperature_critical ||
        alerts.overmoisture_detected) {
        ESP_LOGW(TAG, "Safety alerts: session=%d valve=%d mqtt=%d temp=%d moisture=%d",
                 alerts.session_timeout_exceeded, alerts.valve_timeout_exceeded,
                 alerts.mqtt_override_timeout, alerts.temperature_critical,
                 alerts.overmoisture_detected);
    }

    return alerts;
}

esp_err_t safety_watchdog_reset_session(void)
{
    if (!is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&watchdog_spinlock);
    {
        s_timers.session_start_time = time(NULL);
        ESP_LOGD(TAG, "Session timer reset to %" PRId64, (int64_t)s_timers.session_start_time);
    }
    portEXIT_CRITICAL(&watchdog_spinlock);

    return ESP_OK;
}

esp_err_t safety_watchdog_reset_valve_timer(void)
{
    if (!is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&watchdog_spinlock);
    {
        s_timers.valve_open_time = time(NULL);
        ESP_LOGD(TAG, "Valve timer reset to %" PRId64, (int64_t)s_timers.valve_open_time);
    }
    portEXIT_CRITICAL(&watchdog_spinlock);

    return ESP_OK;
}

esp_err_t safety_watchdog_reset_mqtt_timer(void)
{
    if (!is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&watchdog_spinlock);
    {
        s_timers.mqtt_override_start_time = time(NULL);
        ESP_LOGD(TAG, "MQTT override timer reset to %" PRId64, (int64_t)s_timers.mqtt_override_start_time);
    }
    portEXIT_CRITICAL(&watchdog_spinlock);

    return ESP_OK;
}

esp_err_t safety_watchdog_get_current_inputs(watchdog_inputs_t* inputs)
{
    if (!is_initialized || inputs == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&watchdog_spinlock);
    {
        *inputs = s_last_inputs;
    }
    portEXIT_CRITICAL(&watchdog_spinlock);

    return ESP_OK;
}
