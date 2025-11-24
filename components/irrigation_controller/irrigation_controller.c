/**
 * @file irrigation_controller.c
 * @brief Irrigation Controller Implementation - Main logic and state machine
 *
 * Core irrigation control system with state machine, safety checks,
 * and integration with other components.
 *
 * Phase 5 STUBS: Framework for implementation
 * Provides structure, logging, and basic state management
 * Ready for Phase 2 implementation of state handlers
 *
 * @author Liwaisi Tech
 * @date 2025-10-21
 * @version 1.0.0 - Phase 5 (Structure + Stubs)
 */

#include "irrigation_controller.h"
#include "notification_service.h"
#include "drivers/valve_driver/valve_driver.h"
#include "drivers/safety_watchdog/safety_watchdog.h"
#include "drivers/offline_mode/offline_mode_driver.h"
#include "sensor_reader.h"
#include "wifi_manager.h"
#include "device_config.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include <time.h>
#include <string.h>
#include <inttypes.h>

/* ============================ PRIVATE TYPES ============================ */

/**
 * @brief Internal irrigation controller context
 *
 * Maintains complete state of irrigation system
 */
typedef struct {
    // Configuration
    irrigation_controller_config_t config;

    // Current session state
    irrigation_state_t current_state;
    irrigation_mode_t current_mode;
    time_t session_start_time;
    time_t last_session_end_time;
    uint16_t current_session_duration_min;
    bool is_valve_open;
    uint8_t active_valve_num;

    // MQTT override
    bool mqtt_override_active;
    char mqtt_override_command[20];
    time_t mqtt_override_start_time;

    // Statistics
    uint32_t session_count;
    uint32_t total_runtime_today_sec;

    // Online/offline detection
    bool is_online;
    time_t last_online_check;

    // Safety
    bool safety_lock;
    bool thermal_protection_active;
    time_t next_allowed_session;

    // Last evaluation
    irrigation_evaluation_t last_evaluation;
    time_t last_eval_time;

    // Startup stabilization
    uint8_t startup_cycles_remaining;  ///< Remaining startup cycles (10 cycles at 60s for stabilization)
} irrigation_controller_context_t;

/* ============================ PRIVATE STATE ============================ */

static const char *TAG = "irrigation_controller";

/**
 * @brief Global irrigation controller context
 */
static irrigation_controller_context_t s_irrig_ctx = {
    .current_state = IRRIGATION_IDLE,
    .current_mode = IRRIGATION_MODE_ONLINE,
    .is_valve_open = false,
    .active_valve_num = 0,
    .mqtt_override_active = false,
    .session_count = 0,
    .total_runtime_today_sec = 0,
    .is_online = false,
    .safety_lock = false,
    .thermal_protection_active = false,
    .startup_cycles_remaining = 10  // First 10 cycles at 60s for stabilization
};

/**
 * @brief Task handle for irrigation evaluation task
 */
static TaskHandle_t s_irrigation_task_handle = NULL;

/**
 * @brief Spinlock for thread-safe state access
 */
static portMUX_TYPE s_irrigation_spinlock = portMUX_INITIALIZER_UNLOCKED;

/**
 * @brief Initialization flag
 */
static bool is_initialized = false;

/* ============================ PRIVATE FUNCTIONS ============================ */

// Forward declarations for state handlers
static void irrigation_state_idle_handler(const sensor_reading_t* reading);
static void irrigation_state_running_handler(const sensor_reading_t* reading);
static void irrigation_state_error_handler(const sensor_reading_t* reading);
static void irrigation_state_thermal_stop_handler(const sensor_reading_t* reading);

/**
 * @brief Irrigation evaluation task
 *
 * Main task loop for periodic irrigation evaluation.
 * Interval logic:
 * - Online mode: 60 seconds (always)
 * - Offline mode: 60 seconds for first 10 cycles (startup stabilization), then 2 hours
 * Implements complete state machine with sensor integration.
 */
static void irrigation_evaluation_task(void *param)
{
    ESP_LOGI(TAG, "Irrigation evaluation task started");
    uint32_t cycle_count = 0;

    while (1) {
        cycle_count++;
        ESP_LOGI(TAG, "=== Irrigation evaluation cycle #%" PRIu32 " ===", cycle_count);
        
        // 1. Detect connectivity status
        bool is_online = wifi_manager_is_connected();

        portENTER_CRITICAL(&s_irrigation_spinlock);
        {
            s_irrig_ctx.is_online = is_online;
        }
        portEXIT_CRITICAL(&s_irrigation_spinlock);

        // 2. Read sensors
        sensor_reading_t reading;
        esp_err_t sensor_ret = sensor_reader_get_all(&reading);

        if (sensor_ret != ESP_OK) {
            ESP_LOGE(TAG, "Sensor read failed: %s", esp_err_to_name(sensor_ret));
            portENTER_CRITICAL(&s_irrigation_spinlock);
            {
                s_irrig_ctx.current_state = IRRIGATION_ERROR;
            }
            portEXIT_CRITICAL(&s_irrigation_spinlock);
            irrigation_state_error_handler(NULL);
        } else {
            // 3. Execute state machine
            irrigation_state_t current_state;
            portENTER_CRITICAL(&s_irrigation_spinlock);
            {
                current_state = s_irrig_ctx.current_state;
            }
            portEXIT_CRITICAL(&s_irrigation_spinlock);

            switch (current_state) {
                case IRRIGATION_IDLE:
                    irrigation_state_idle_handler(&reading);
                    break;

                case IRRIGATION_ACTIVE:
                    irrigation_state_running_handler(&reading);
                    break;

                case IRRIGATION_ERROR:
                    irrigation_state_error_handler(&reading);
                    break;

                case IRRIGATION_THERMAL_PROTECTION:
                    irrigation_state_thermal_stop_handler(&reading);
                    break;

                case IRRIGATION_EMERGENCY_STOP:
                    // Emergency stop requires manual unlock
                    ESP_LOGW(TAG, "EMERGENCY_STOP state - waiting for manual unlock");
                    break;

                default:
                    ESP_LOGW(TAG, "Unknown state: %d", current_state);
                    break;
            }
        }

        // 4. Determine evaluation interval based on connectivity and startup state
        uint32_t eval_interval_ms;
        uint8_t startup_cycles;
        
        portENTER_CRITICAL(&s_irrigation_spinlock);
        {
            startup_cycles = s_irrig_ctx.startup_cycles_remaining;
        }
        portEXIT_CRITICAL(&s_irrigation_spinlock);
        
        if (is_online) {
            eval_interval_ms = 60000;  // 60 seconds online
            ESP_LOGD(TAG, "Online mode - next evaluation in 60s");
        } else {
            // Offline mode: Use 60s for first 10 cycles (startup stabilization)
            // Then switch to 2h intervals
            if (startup_cycles > 0) {
                eval_interval_ms = 60000;  // 60 seconds during startup
                ESP_LOGI(TAG, "Offline mode - STARTUP: cycle %d/10, next evaluation in 60s", 
                         11 - startup_cycles);
                
                // Decrement startup counter (only when offline)
                portENTER_CRITICAL(&s_irrigation_spinlock);
                {
                    s_irrig_ctx.startup_cycles_remaining--;
                }
                portEXIT_CRITICAL(&s_irrigation_spinlock);
            } else {
                eval_interval_ms = 7200000;  // 2 hours offline (normal operation)
                ESP_LOGD(TAG, "Offline mode - normal operation, next evaluation in 2h");
            }
        }

        // 5. Log summary (INFO level for visibility)
        irrigation_state_t current_state_log;
        bool is_valve_open_log;
        portENTER_CRITICAL(&s_irrigation_spinlock);
        {
            current_state_log = s_irrig_ctx.current_state;
            is_valve_open_log = s_irrig_ctx.is_valve_open;
        }
        portEXIT_CRITICAL(&s_irrigation_spinlock);
        
        ESP_LOGI(TAG, "Evaluation cycle: State=%d, Valve=%s, Online=%d, StartupCycles=%d, NextWait=%" PRIu32 "ms",
                 current_state_log,
                 is_valve_open_log ? "OPEN" : "CLOSED",
                 is_online,
                 startup_cycles,
                 eval_interval_ms);

        // 6. Wait for next evaluation with periodic WiFi check
        // If offline, check WiFi every 10 seconds to detect reconnection quickly
        if (!is_online) {
            // Break long delay into smaller chunks to check WiFi status
            uint32_t check_interval_ms = 10000;  // Check every 10 seconds
            uint32_t remaining_ms = eval_interval_ms;
            
            while (remaining_ms > 0 && !wifi_manager_is_connected()) {
                uint32_t delay_ms = (remaining_ms > check_interval_ms) ? check_interval_ms : remaining_ms;
                vTaskDelay(pdMS_TO_TICKS(delay_ms));
                remaining_ms -= delay_ms;
            }
            
            // If WiFi reconnected during wait, log it
            if (wifi_manager_is_connected()) {
                ESP_LOGI(TAG, "WiFi reconnected during wait - switching to online mode");
            }
        } else {
            // Online mode: simple delay
            vTaskDelay(pdMS_TO_TICKS(eval_interval_ms));
        }
    }
}

/**
 * @brief Get default configuration
 *
 * Returns default configuration using Kconfig values (from menuconfig/sdkconfig)
 * with fallbacks to sensible defaults if not configured.
 *
 * Configuration hierarchy:
 * 1. CONFIG_IRRIGATION_* from sdkconfig.h (menuconfig values)
 * 2. Fallback values if not defined
 */
static irrigation_controller_config_t _get_default_config(void)
{
    return (irrigation_controller_config_t){
        // Soil moisture thresholds - from Kconfig (menuconfig)
        #ifdef CONFIG_IRRIGATION_SOIL_THRESHOLD_START
            .soil_threshold_critical = (float)CONFIG_IRRIGATION_SOIL_THRESHOLD_START,
        #else
            .soil_threshold_critical = 51.0f,  // Fallback: Colombian dataset default
        #endif

        #ifdef CONFIG_IRRIGATION_SOIL_THRESHOLD_STOP
            .soil_threshold_optimal = (float)CONFIG_IRRIGATION_SOIL_THRESHOLD_STOP,
        #else
            .soil_threshold_optimal = 70.0f,  // Fallback: Healthy soil level
        #endif

        #ifdef CONFIG_IRRIGATION_SOIL_THRESHOLD_DANGER
            .soil_threshold_max = (float)CONFIG_IRRIGATION_SOIL_THRESHOLD_DANGER,
        #else
            .soil_threshold_max = 80.0f,  // Fallback: Over-irrigation protection
        #endif

        // Temperature thresholds - from Kconfig
        #ifdef CONFIG_IRRIGATION_TEMP_CRITICAL_CELSIUS
            .temp_critical = (float)CONFIG_IRRIGATION_TEMP_CRITICAL_CELSIUS,
        #else
            .temp_critical = 32.0f,  // Fallback: Warning level
        #endif

        #ifdef CONFIG_IRRIGATION_TEMP_CRITICAL_CELSIUS
            .temp_thermal_stop = (float)CONFIG_IRRIGATION_TEMP_CRITICAL_CELSIUS,
        #else
            .temp_thermal_stop = 40.0f,  // Fallback: Thermal protection
        #endif

        // Safety limits - from Kconfig
        #ifdef CONFIG_IRRIGATION_MAX_DURATION_MINUTES
            .max_duration_minutes = CONFIG_IRRIGATION_MAX_DURATION_MINUTES,
        #else
            .max_duration_minutes = 120,  // Fallback: 2 hours
        #endif

        #ifdef CONFIG_IRRIGATION_MIN_INTERVAL_MINUTES
            .min_interval_minutes = CONFIG_IRRIGATION_MIN_INTERVAL_MINUTES,
        #else
            .min_interval_minutes = 240,  // Fallback: 4 hours
        #endif

        // Max daily irrigation (no Kconfig yet, using hardcoded)
        .max_daily_minutes = MAX_DAILY_IRRIGATION_MIN,  // 360 minutes (6 hours)

        // Valve configuration
        .valve_gpio_1 = GPIO_VALVE_1,
        .valve_gpio_2 = GPIO_VALVE_2,
        .primary_valve = 1,

        // Offline mode configuration - from Kconfig
        #ifdef CONFIG_IRRIGATION_ENABLE_OFFLINE_MODE
            .enable_offline_mode = (bool)CONFIG_IRRIGATION_ENABLE_OFFLINE_MODE,
        #else
            .enable_offline_mode = true,
        #endif

        #ifdef CONFIG_IRRIGATION_OFFLINE_TIMEOUT_SECONDS
            .offline_timeout_seconds = CONFIG_IRRIGATION_OFFLINE_TIMEOUT_SECONDS,
        #else
            .offline_timeout_seconds = 300,  // Fallback: 5 minutes
        #endif

        // Offline adaptive thresholds (from common_types as fallback)
        .offline_warning_threshold = 50.0f,
        .offline_critical_threshold = 40.0f,
        .offline_emergency_threshold = 30.0f
    };
}


/**
 * @brief State handler: IDLE
 *
 * Evaluates if riego should start based on soil moisture
 */
static void irrigation_state_idle_handler(const sensor_reading_t* reading)
{
    if (reading == NULL) {
        return;
    }

    // Calculate average soil moisture from 3 sensors
    float soil_avg = (reading->soil.soil_humidity[0] +
                      reading->soil.soil_humidity[1] +
                      reading->soil.soil_humidity[2]) / 3.0f;

    // Log at INFO level for visibility
    ESP_LOGI(TAG, "IDLE state: soil_avg=%.1f%% (sensors: %.1f%%, %.1f%%, %.1f%%) - threshold=%.1f%%",
             soil_avg, 
             reading->soil.soil_humidity[0],
             reading->soil.soil_humidity[1],
             reading->soil.soil_humidity[2],
             s_irrig_ctx.config.soil_threshold_critical);

    // Check if should start irrigation (<= to include threshold value)
    if (soil_avg <= s_irrig_ctx.config.soil_threshold_critical) {
        ESP_LOGI(TAG, "Soil too dry (%.1f%% <= %.1f%%) - starting irrigation",
                 soil_avg, s_irrig_ctx.config.soil_threshold_critical);

        // Open valve
        valve_driver_open(s_irrig_ctx.config.primary_valve);

        // Update state
        portENTER_CRITICAL(&s_irrigation_spinlock);
        {
            s_irrig_ctx.is_valve_open = true;
            s_irrig_ctx.active_valve_num = s_irrig_ctx.config.primary_valve;
            s_irrig_ctx.session_start_time = time(NULL);
            s_irrig_ctx.current_state = IRRIGATION_ACTIVE;
            s_irrig_ctx.session_count++;
        }
        portEXIT_CRITICAL(&s_irrigation_spinlock);

        // Reset watchdog timers
        safety_watchdog_reset_session();
        safety_watchdog_reset_valve_timer();

        // Send notification
        notification_send_irrigation_event("irrigation_on", soil_avg,
                                          reading->ambient.humidity,
                                          reading->ambient.temperature);
    }
}

/**
 * @brief State handler: ACTIVE (running irrigation)
 *
 * Monitors running irrigation and decides when to stop
 */
static void irrigation_state_running_handler(const sensor_reading_t* reading)
{
    if (reading == NULL) {
        return;
    }

    // Calculate soil average
    float soil_avg = (reading->soil.soil_humidity[0] +
                      reading->soil.soil_humidity[1] +
                      reading->soil.soil_humidity[2]) / 3.0f;

    // Find maximum (any sensor too wet?)
    float soil_max = reading->soil.soil_humidity[0];
    if (reading->soil.soil_humidity[1] > soil_max) soil_max = reading->soil.soil_humidity[1];
    if (reading->soil.soil_humidity[2] > soil_max) soil_max = reading->soil.soil_humidity[2];

    ESP_LOGD(TAG, "ACTIVE state: soil_avg=%.1f%%, soil_max=%.1f%% (stop=%.1f%%, danger=%.1f%%)",
             soil_avg, soil_max,
             s_irrig_ctx.config.soil_threshold_optimal,
             s_irrig_ctx.config.soil_threshold_max);

    // Calculate elapsed time
    time_t elapsed = time(NULL) - s_irrig_ctx.session_start_time;

    // Check safety watchdog
    watchdog_inputs_t watchdog_inputs = {
        .session_duration_ms = elapsed * 1000,
        .valve_open_time_ms = elapsed * 1000,
        .mqtt_override_idle_ms = 0,
        .current_temperature = reading->ambient.temperature,
        .current_soil_humidity_avg = soil_avg
    };

    watchdog_alerts_t alerts = safety_watchdog_check(&watchdog_inputs);

    // Decide if should stop
    bool should_stop = false;
    const char* stop_reason = NULL;

    // Check conditions in priority order
    if (soil_max >= s_irrig_ctx.config.soil_threshold_max) {
        should_stop = true;
        stop_reason = "over_moisture";
        ESP_LOGW(TAG, "Over-moisture detected (%.1f%% >= %.1f%%)",
                 soil_max, s_irrig_ctx.config.soil_threshold_max);
    }
    else if (alerts.temperature_critical) {
        should_stop = true;
        stop_reason = "temperature_critical";
        portENTER_CRITICAL(&s_irrigation_spinlock);
        {
            s_irrig_ctx.thermal_protection_active = true;
            s_irrig_ctx.current_state = IRRIGATION_THERMAL_PROTECTION;
        }
        portEXIT_CRITICAL(&s_irrigation_spinlock);
        ESP_LOGE(TAG, "THERMAL PROTECTION: T°=%.1f°C > %.1f°C",
                 reading->ambient.temperature, s_irrig_ctx.config.temp_thermal_stop);
    }
    else if (alerts.session_timeout_exceeded) {
        should_stop = true;
        stop_reason = "session_timeout";
        ESP_LOGW(TAG, "Session timeout: %lld sec > %d min",
                 (long long)elapsed, s_irrig_ctx.config.max_duration_minutes * 60);
    }
    else if (soil_avg >= s_irrig_ctx.config.soil_threshold_optimal) {
        should_stop = true;
        stop_reason = "target_reached";
        ESP_LOGI(TAG, "Target soil moisture reached (%.1f%% >= %.1f%%)",
                 soil_avg, s_irrig_ctx.config.soil_threshold_optimal);
    }

    // Execute stop if needed
    if (should_stop) {
        valve_driver_close(s_irrig_ctx.config.primary_valve);

        portENTER_CRITICAL(&s_irrigation_spinlock);
        {
            s_irrig_ctx.is_valve_open = false;
            s_irrig_ctx.last_session_end_time = time(NULL);
            s_irrig_ctx.total_runtime_today_sec += elapsed;

            if (s_irrig_ctx.current_state != IRRIGATION_THERMAL_PROTECTION) {
                s_irrig_ctx.current_state = IRRIGATION_IDLE;
            }
        }
        portEXIT_CRITICAL(&s_irrigation_spinlock);

        ESP_LOGI(TAG, "Irrigation stopped: %s (duration %.1f min)", stop_reason, elapsed / 60.0f);

        // Send notification
        notification_send_irrigation_event("irrigation_off", soil_avg,
                                          reading->ambient.humidity,
                                          reading->ambient.temperature);
    }

    // Alert if valve timeout (40 min)
    if (alerts.valve_timeout_exceeded) {
        ESP_LOGW(TAG, "Valve timeout alert: open for %lld sec", (long long)elapsed);
    }
}

/**
 * @brief State handler: ERROR
 *
 * Error state - sensors failed or safety triggered
 */
static void irrigation_state_error_handler(const sensor_reading_t* reading)
{
    ESP_LOGE(TAG, "ERROR state handler");

    // Close all valves for safety
    valve_driver_close(1);
    valve_driver_close(2);

    portENTER_CRITICAL(&s_irrigation_spinlock);
    {
        s_irrig_ctx.is_valve_open = false;
    }
    portEXIT_CRITICAL(&s_irrigation_spinlock);

    if (reading != NULL) {
        notification_send_irrigation_event("sensor_error",
                        (reading->soil.soil_humidity[0] +
                         reading->soil.soil_humidity[1] +
                         reading->soil.soil_humidity[2]) / 3.0f,
                        reading->ambient.humidity,
                        reading->ambient.temperature);
    }
}

/**
 * @brief State handler: THERMAL_PROTECTION
 *
 * Temperature too high - wait for cooling
 */
static void irrigation_state_thermal_stop_handler(const sensor_reading_t* reading)
{
    if (reading == NULL) {
        return;
    }

    ESP_LOGW(TAG, "THERMAL_PROTECTION: T°=%.1f°C (waiting for < %.1f°C)",
             reading->ambient.temperature,
             s_irrig_ctx.config.temp_critical);

    // Check if cooled down enough to resume
    if (reading->ambient.temperature < s_irrig_ctx.config.temp_critical) {
        portENTER_CRITICAL(&s_irrigation_spinlock);
        {
            s_irrig_ctx.thermal_protection_active = false;
            s_irrig_ctx.current_state = IRRIGATION_IDLE;
        }
        portEXIT_CRITICAL(&s_irrigation_spinlock);

        ESP_LOGI(TAG, "Temperature normalized, returning to IDLE");
    }

    notification_send_irrigation_event("temperature_critical",
                    (reading->soil.soil_humidity[0] +
                     reading->soil.soil_humidity[1] +
                     reading->soil.soil_humidity[2]) / 3.0f,
                    reading->ambient.humidity,
                    reading->ambient.temperature);
}

/* ============================ PUBLIC API ============================ */

esp_err_t irrigation_controller_init(const irrigation_controller_config_t* config)
{
    ESP_LOGI(TAG, "=== irrigation_controller_init() CALLED ===");
    
    if (is_initialized) {
        ESP_LOGW(TAG, "Irrigation controller already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing irrigation controller");

    // Initialize valve driver
    ESP_LOGI(TAG, "Step 1: Initializing valve driver...");
    esp_err_t ret = valve_driver_init();
    ESP_LOGI(TAG, "valve_driver_init() returned: %s (code: %d)", esp_err_to_name(ret), ret);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize valve driver: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Step 1: Valve driver initialized OK");

    // Initialize safety watchdog
    ESP_LOGI(TAG, "Step 2: Initializing safety watchdog...");
    ret = safety_watchdog_init();
    ESP_LOGI(TAG, "safety_watchdog_init() returned: %s (code: %d)", esp_err_to_name(ret), ret);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize safety watchdog: %s", esp_err_to_name(ret));
        valve_driver_deinit();
        return ret;
    }
    ESP_LOGI(TAG, "Step 2: Safety watchdog initialized OK");

    // Initialize offline mode driver
    ESP_LOGI(TAG, "Step 3: Initializing offline mode driver...");
    ret = offline_mode_init();
    ESP_LOGI(TAG, "offline_mode_init() returned: %s (code: %d)", esp_err_to_name(ret), ret);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize offline mode driver: %s", esp_err_to_name(ret));
        valve_driver_deinit();
        return ret;
    }
    ESP_LOGI(TAG, "Step 3: Offline mode driver initialized OK");

    // Set configuration
    if (config != NULL) {
        portENTER_CRITICAL(&s_irrigation_spinlock);
        {
            s_irrig_ctx.config = *config;
        }
        portEXIT_CRITICAL(&s_irrigation_spinlock);
    } else {
        portENTER_CRITICAL(&s_irrigation_spinlock);
        {
            s_irrig_ctx.config = _get_default_config();
        }
        portEXIT_CRITICAL(&s_irrigation_spinlock);
    }

    // Initialize startup cycles counter (10 cycles at 60s for stabilization when offline)
    portENTER_CRITICAL(&s_irrigation_spinlock);
    {
        s_irrig_ctx.startup_cycles_remaining = 10;
    }
    portEXIT_CRITICAL(&s_irrigation_spinlock);
    ESP_LOGI(TAG, "Startup stabilization: 10 cycles at 60s when offline");

    is_initialized = true;

    // Create evaluation task
    ESP_LOGI(TAG, "Step 4: Creating irrigation evaluation task...");
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        irrigation_evaluation_task,
        "irrigation_task",
        4096,
        NULL,
        4,  // Priority 4 (below wifi/mqtt at ~5, above idle at ~1)
        &s_irrigation_task_handle,
        1   // Core 1
    );
    ESP_LOGI(TAG, "xTaskCreatePinnedToCore() returned: %s (handle: %p)", 
             (task_ret == pdPASS) ? "pdPASS" : "pdFAIL", s_irrigation_task_handle);

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create irrigation evaluation task");
        valve_driver_deinit();
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Step 4: Irrigation evaluation task created successfully");

    ESP_LOGI(TAG, "Irrigation controller initialized");
    ESP_LOGI(TAG, "  Soil threshold: %.1f%% start, %.1f%% stop",
             s_irrig_ctx.config.soil_threshold_critical,
             s_irrig_ctx.config.soil_threshold_optimal);
    ESP_LOGI(TAG, "  Max duration: %d minutes", s_irrig_ctx.config.max_duration_minutes);
    ESP_LOGI(TAG, "  Primary valve: %d", s_irrig_ctx.config.primary_valve);
    ESP_LOGI(TAG, "  Evaluation task created (priority 4, stack 4KB)");

    return ESP_OK;
}

esp_err_t irrigation_controller_start(uint16_t duration_minutes, uint8_t valve_number)
{
    if (!is_initialized) {
        ESP_LOGE(TAG, "Irrigation controller not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (valve_number < 1 || valve_number > 2) {
        ESP_LOGE(TAG, "Invalid valve number: %d", valve_number);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting irrigation: valve %d, duration %d min", valve_number, duration_minutes);

    // Open valve
    esp_err_t ret = valve_driver_open(valve_number);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open valve");
        return ret;
    }

    portENTER_CRITICAL(&s_irrigation_spinlock);
    {
        s_irrig_ctx.is_valve_open = true;
        s_irrig_ctx.active_valve_num = valve_number;
        s_irrig_ctx.session_start_time = time(NULL);
        s_irrig_ctx.current_state = IRRIGATION_ACTIVE;
        s_irrig_ctx.session_count++;
    }
    portEXIT_CRITICAL(&s_irrigation_spinlock);

    // Reset watchdog timers
    safety_watchdog_reset_session();
    safety_watchdog_reset_valve_timer();

    return ESP_OK;
}

esp_err_t irrigation_controller_stop(void)
{
    if (!is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Stopping irrigation controller");

    // Close all valves immediately
    valve_driver_close(1);
    valve_driver_close(2);

    portENTER_CRITICAL(&s_irrigation_spinlock);
    {
        s_irrig_ctx.is_valve_open = false;
        s_irrig_ctx.current_state = IRRIGATION_IDLE;
    }
    portEXIT_CRITICAL(&s_irrigation_spinlock);

    // Delete task if running
    if (s_irrigation_task_handle != NULL) {
        vTaskDelete(s_irrigation_task_handle);
        s_irrigation_task_handle = NULL;
    }

    return ESP_OK;
}

esp_err_t irrigation_controller_deinit(void)
{
    if (!is_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing irrigation controller");

    // Stop main task
    irrigation_controller_stop();

    // Deinitialize drivers
    valve_driver_deinit();

    is_initialized = false;

    return ESP_OK;
}

irrigation_state_t irrigation_controller_get_state(void)
{
    irrigation_state_t state;

    portENTER_CRITICAL(&s_irrigation_spinlock);
    {
        state = s_irrig_ctx.current_state;
    }
    portEXIT_CRITICAL(&s_irrigation_spinlock);

    return state;
}

esp_err_t irrigation_controller_get_status(irrigation_controller_status_t* status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_irrigation_spinlock);
    {
        status->state = s_irrig_ctx.current_state;
        status->mode = s_irrig_ctx.current_mode;
        status->is_irrigating = s_irrig_ctx.is_valve_open;
        status->active_valve = s_irrig_ctx.active_valve_num;
        status->safety_lock = s_irrig_ctx.safety_lock;
        status->thermal_protection_active = s_irrig_ctx.thermal_protection_active;

        // Calculate session elapsed time
        if (s_irrig_ctx.is_valve_open && s_irrig_ctx.session_start_time > 0) {
            status->session_elapsed_sec = time(NULL) - s_irrig_ctx.session_start_time;
        } else {
            status->session_elapsed_sec = 0;
        }

        // Statistics
        status->stats.total_sessions = s_irrig_ctx.session_count;
        status->stats.total_runtime_seconds = 0;  // TODO: accumulate from NVS
        status->stats.today_runtime_seconds = s_irrig_ctx.total_runtime_today_sec;
        status->stats.last_session_time = s_irrig_ctx.last_session_end_time;

        // Last evaluation
        status->last_eval = s_irrig_ctx.last_evaluation;
        status->last_eval_time = s_irrig_ctx.last_eval_time;
    }
    portEXIT_CRITICAL(&s_irrigation_spinlock);

    return ESP_OK;
}

bool irrigation_controller_is_initialized(void)
{
    return is_initialized;
}

bool irrigation_controller_is_online(void)
{
    bool is_online;

    portENTER_CRITICAL(&s_irrigation_spinlock);
    {
        is_online = s_irrig_ctx.is_online;
    }
    portEXIT_CRITICAL(&s_irrigation_spinlock);

    return is_online;
}

esp_err_t irrigation_controller_get_config(irrigation_controller_config_t* config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_irrigation_spinlock);
    {
        *config = s_irrig_ctx.config;
    }
    portEXIT_CRITICAL(&s_irrigation_spinlock);

    return ESP_OK;
}

/* ============================ MQTT COMMAND EXECUTION ============================ */

esp_err_t irrigation_controller_execute_command(irrigation_command_t command,
                                                uint16_t duration_minutes)
{
    if (!is_initialized) {
        ESP_LOGE(TAG, "Irrigation controller not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Execute command: %d, duration: %d min", command, duration_minutes);

    // Get current state
    bool safety_lock;
    time_t next_allowed;
    uint32_t daily_runtime;

    portENTER_CRITICAL(&s_irrigation_spinlock);
    {
        safety_lock = s_irrig_ctx.safety_lock;
        next_allowed = s_irrig_ctx.next_allowed_session;
        daily_runtime = s_irrig_ctx.total_runtime_today_sec;
    }
    portEXIT_CRITICAL(&s_irrigation_spinlock);

    // Handle EMERGENCY_STOP
    if (command == IRRIGATION_CMD_EMERGENCY_STOP) {
        ESP_LOGE(TAG, "EMERGENCY STOP executed via MQTT command");

        // Close all valves immediately
        valve_driver_emergency_close_all();

        // Activate safety lock
        portENTER_CRITICAL(&s_irrigation_spinlock);
        {
            s_irrig_ctx.safety_lock = true;
            s_irrig_ctx.is_valve_open = false;
            s_irrig_ctx.current_state = IRRIGATION_EMERGENCY_STOP;
        }
        portEXIT_CRITICAL(&s_irrigation_spinlock);

        // Send notification
        notification_send_irrigation_event("emergency_stop", 0.0f, 0.0f, 0.0f);

        return ESP_OK;
    }

    // Handle STOP
    if (command == IRRIGATION_CMD_STOP) {
        ESP_LOGI(TAG, "Stop irrigation via MQTT command");

        // Close valves
        valve_driver_close(s_irrig_ctx.config.primary_valve);

        portENTER_CRITICAL(&s_irrigation_spinlock);
        {
            if (s_irrig_ctx.is_valve_open) {
                s_irrig_ctx.is_valve_open = false;
                s_irrig_ctx.last_session_end_time = time(NULL);
                if (s_irrig_ctx.session_start_time > 0) {
                    time_t duration = time(NULL) - s_irrig_ctx.session_start_time;
                    s_irrig_ctx.total_runtime_today_sec += duration;
                }
            }
            s_irrig_ctx.current_state = IRRIGATION_IDLE;
            s_irrig_ctx.mqtt_override_active = false;
        }
        portEXIT_CRITICAL(&s_irrigation_spinlock);

        return ESP_OK;
    }

    // Handle START
    if (command == IRRIGATION_CMD_START) {
        // Check safety lock
        if (safety_lock) {
            ESP_LOGW(TAG, "Cannot START: safety lock is active (requires manual unlock)");
            return ESP_ERR_INVALID_STATE;
        }

        // Check minimum interval between sessions
        time_t now = time(NULL);
        if (now < next_allowed) {
            uint32_t wait_minutes = (next_allowed - now) / 60;
            ESP_LOGW(TAG, "Cannot START: must wait %lu more minutes (min interval: %d minutes)",
                     wait_minutes, s_irrig_ctx.config.min_interval_minutes);
            return ESP_ERR_INVALID_STATE;
        }

        // Check max daily duration
        if (daily_runtime >= (s_irrig_ctx.config.max_daily_minutes * 60)) {
            ESP_LOGW(TAG, "Cannot START: max daily duration reached (%d minutes)",
                     s_irrig_ctx.config.max_daily_minutes);
            return ESP_ERR_INVALID_STATE;
        }

        // If duration not specified, use default
        if (duration_minutes == 0) {
            duration_minutes = 15;  // Default 15 minutes
        }

        // Validate duration against max
        if (duration_minutes > s_irrig_ctx.config.max_duration_minutes) {
            ESP_LOGW(TAG, "Duration %d exceeds max %d, clamping",
                     duration_minutes, s_irrig_ctx.config.max_duration_minutes);
            duration_minutes = s_irrig_ctx.config.max_duration_minutes;
        }

        // Open valve
        esp_err_t ret = valve_driver_open(s_irrig_ctx.config.primary_valve);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open valve: %s", esp_err_to_name(ret));
            return ret;
        }

        // Update state
        portENTER_CRITICAL(&s_irrigation_spinlock);
        {
            s_irrig_ctx.is_valve_open = true;
            s_irrig_ctx.active_valve_num = s_irrig_ctx.config.primary_valve;
            s_irrig_ctx.session_start_time = time(NULL);
            s_irrig_ctx.current_session_duration_min = duration_minutes;
            s_irrig_ctx.current_state = IRRIGATION_ACTIVE;
            s_irrig_ctx.session_count++;
            s_irrig_ctx.mqtt_override_active = true;
        }
        portEXIT_CRITICAL(&s_irrigation_spinlock);

        // Reset watchdog timers
        safety_watchdog_reset_session();
        safety_watchdog_reset_valve_timer();

        ESP_LOGI(TAG, "Irrigation started via MQTT: valve %d, duration %d min",
                 s_irrig_ctx.config.primary_valve, duration_minutes);

        // Send notification
        notification_send_irrigation_event("irrigation_on", 0.0f, 0.0f, 0.0f);

        return ESP_OK;
    }

    ESP_LOGW(TAG, "Unknown command: %d", command);
    return ESP_ERR_INVALID_ARG;
}

/* ============================ EVALUATION AND DECISION ============================ */

esp_err_t irrigation_controller_evaluate_and_act(const soil_data_t* soil_data,
                                                 const ambient_data_t* ambient_data,
                                                 irrigation_evaluation_t* evaluation)
{
    if (!is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // If both parameters NULL, cannot evaluate
    if (soil_data == NULL || ambient_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Calculate average soil humidity from 3 sensors
    float soil_avg = (soil_data->soil_humidity[0] +
                      soil_data->soil_humidity[1] +
                      soil_data->soil_humidity[2]) / 3.0f;

    float soil_max = soil_data->soil_humidity[0];
    if (soil_data->soil_humidity[1] > soil_max) soil_max = soil_data->soil_humidity[1];
    if (soil_data->soil_humidity[2] > soil_max) soil_max = soil_data->soil_humidity[2];

    // Get current state and mode
    irrigation_state_t current_state;
    bool is_online;

    portENTER_CRITICAL(&s_irrigation_spinlock);
    {
        current_state = s_irrig_ctx.current_state;
        is_online = s_irrig_ctx.is_online;
    }
    portEXIT_CRITICAL(&s_irrigation_spinlock);

    // Initialize evaluation result
    irrigation_evaluation_t eval = {
        .decision = IRRIGATION_DECISION_NO_ACTION,
        .soil_avg_humidity = soil_avg,
        .ambient_temperature = ambient_data->temperature,
        .duration_minutes = 0
    };

    // Evaluate based on mode
    if (is_online) {
        // ONLINE MODE: Only recommend, don't execute
        ESP_LOGD(TAG, "ONLINE mode evaluation: soil=%.1f%%, temp=%.1f°C, state=%d",
                 soil_avg, ambient_data->temperature, current_state);

        if (current_state == IRRIGATION_IDLE) {
            // Check if should start
            if (soil_avg < s_irrig_ctx.config.soil_threshold_critical) {
                eval.decision = IRRIGATION_DECISION_START;
                eval.duration_minutes = 15;
                snprintf(eval.reason, sizeof(eval.reason),
                        "Dry soil (%.1f%% < %.1f%%), recommendation only",
                        soil_avg, s_irrig_ctx.config.soil_threshold_critical);
            } else if (soil_avg < s_irrig_ctx.config.soil_threshold_optimal) {
                eval.decision = IRRIGATION_DECISION_NO_ACTION;
                snprintf(eval.reason, sizeof(eval.reason),
                        "Soil moisture adequate (%.1f%%)", soil_avg);
            } else {
                eval.decision = IRRIGATION_DECISION_NO_ACTION;
                snprintf(eval.reason, sizeof(eval.reason),
                        "Soil moisture sufficient (%.1f%%)", soil_avg);
            }
        } else if (current_state == IRRIGATION_ACTIVE) {
            // Monitoring running irrigation
            if (soil_max >= s_irrig_ctx.config.soil_threshold_max) {
                eval.decision = IRRIGATION_DECISION_STOP;
                snprintf(eval.reason, sizeof(eval.reason),
                        "Over-moisture detected (%.1f%% >= %.1f%%)",
                        soil_max, s_irrig_ctx.config.soil_threshold_max);
            } else if (soil_avg >= s_irrig_ctx.config.soil_threshold_optimal) {
                eval.decision = IRRIGATION_DECISION_STOP;
                snprintf(eval.reason, sizeof(eval.reason),
                        "Target soil moisture reached (%.1f%% >= %.1f%%)",
                        soil_avg, s_irrig_ctx.config.soil_threshold_optimal);
            } else {
                eval.decision = IRRIGATION_DECISION_CONTINUE;
                snprintf(eval.reason, sizeof(eval.reason),
                        "Continue irrigation (soil %.1f%% < target %.1f%%)",
                        soil_avg, s_irrig_ctx.config.soil_threshold_optimal);
            }
        }
    } else {
        // OFFLINE MODE: Evaluate and execute automatically
        offline_evaluation_t offline_eval = offline_mode_evaluate(soil_avg);

        ESP_LOGD(TAG, "OFFLINE mode: level=%d, interval=%lu ms, soil=%.1f%%",
                 offline_eval.level, offline_eval.interval_ms, soil_avg);

        if (current_state == IRRIGATION_IDLE) {
            // Check offline level
            if (offline_eval.level >= OFFLINE_LEVEL_CRITICAL) {
                // Critical or emergency - start automatically
                ESP_LOGI(TAG, "OFFLINE: Starting automatic irrigation (level=%d, soil=%.1f%%)",
                        offline_eval.level, soil_avg);

                // Execute start
                esp_err_t ret = valve_driver_open(s_irrig_ctx.config.primary_valve);
                if (ret == ESP_OK) {
                    portENTER_CRITICAL(&s_irrigation_spinlock);
                    {
                        s_irrig_ctx.is_valve_open = true;
                        s_irrig_ctx.active_valve_num = s_irrig_ctx.config.primary_valve;
                        s_irrig_ctx.session_start_time = time(NULL);
                        s_irrig_ctx.current_state = IRRIGATION_ACTIVE;
                        s_irrig_ctx.session_count++;
                    }
                    portEXIT_CRITICAL(&s_irrigation_spinlock);

                    safety_watchdog_reset_session();
                    safety_watchdog_reset_valve_timer();

                    eval.decision = IRRIGATION_DECISION_START;
                    eval.duration_minutes = 20;
                    snprintf(eval.reason, sizeof(eval.reason),
                            "Auto-start OFFLINE (level=%s, soil=%.1f%%)",
                            offline_eval.reason, soil_avg);
                } else {
                    eval.decision = IRRIGATION_DECISION_NO_ACTION;
                    snprintf(eval.reason, sizeof(eval.reason), "Failed to open valve");
                }
            } else {
                eval.decision = IRRIGATION_DECISION_NO_ACTION;
                snprintf(eval.reason, sizeof(eval.reason),
                        "OFFLINE: Normal level, no action (soil=%.1f%%)", soil_avg);
            }
        }
    }

    // Save evaluation
    portENTER_CRITICAL(&s_irrigation_spinlock);
    {
        s_irrig_ctx.last_evaluation = eval;
        s_irrig_ctx.last_eval_time = time(NULL);
    }
    portEXIT_CRITICAL(&s_irrigation_spinlock);

    // Return result if requested
    if (evaluation != NULL) {
        *evaluation = eval;
    }

    return ESP_OK;
}

/* ============================ STATISTICS AND PERSISTENCE ============================ */

esp_err_t irrigation_controller_get_stats(irrigation_stats_t* stats)
{
    if (stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_irrigation_spinlock);
    {
        stats->total_sessions = s_irrig_ctx.session_count;
        stats->total_runtime_seconds = 0;  // TODO: read from NVS
        stats->today_runtime_seconds = s_irrig_ctx.total_runtime_today_sec;
        stats->emergency_stops = 0;        // TODO: increment in emergency_stop
        stats->thermal_stops = 0;          // TODO: increment in thermal protection
        stats->last_session_time = s_irrig_ctx.last_session_end_time;

        // Memset to avoid uninitialized data
        memset(&stats->last_session, 0, sizeof(irrigation_session_t));
    }
    portEXIT_CRITICAL(&s_irrigation_spinlock);

    return ESP_OK;
}

esp_err_t irrigation_controller_reset_daily_stats(void)
{
    if (!is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Resetting daily statistics");

    portENTER_CRITICAL(&s_irrigation_spinlock);
    {
        s_irrig_ctx.total_runtime_today_sec = 0;
    }
    portEXIT_CRITICAL(&s_irrigation_spinlock);

    // TODO: Persist to NVS for recovery after reboot
    // device_config_set_u32("irrig_cfg", "today_run", 0);

    return ESP_OK;
}
