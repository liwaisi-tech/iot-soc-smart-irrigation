/**
 * @file system_monitor.h
 * @brief System Monitor Component - Health Checks and Connectivity Status
 *
 * Monitors system health, connectivity, and component status.
 * Provides diagnostic information and health checks.
 *
 * Component Responsibilities:
 * - Component health monitoring
 * - Connectivity status tracking
 * - Memory usage monitoring
 * - System uptime tracking
 * - Error logging and reporting
 *
 * Migration from hexagonal: NEW component - system-wide monitoring
 *
 * @author Liwaisi Tech
 * @date 2025-10-01
 * @version 2.0.0 - Component-Based Architecture
 */

#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#include "esp_err.h"
#include "esp_event.h"
#include "common_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ TYPES AND ENUMS ============================ */

/**
 * @brief System monitor state
 */
typedef enum {
    MONITOR_STATE_UNINITIALIZED = 0,    ///< Not initialized
    MONITOR_STATE_RUNNING,              ///< Monitoring active
    MONITOR_STATE_PAUSED,               ///< Monitoring paused
    MONITOR_STATE_ERROR                 ///< Error state
} monitor_state_t;

/**
 * @brief Component identifier
 */
typedef enum {
    COMPONENT_WIFI_MANAGER = 0,     ///< WiFi manager component
    COMPONENT_MQTT_CLIENT,          ///< MQTT client component
    COMPONENT_SENSOR_READER,        ///< Sensor reader component
    COMPONENT_IRRIGATION_CONTROLLER,///< Irrigation controller component
    COMPONENT_HTTP_SERVER,          ///< HTTP server component
    COMPONENT_DEVICE_CONFIG,        ///< Device config component
    COMPONENT_SYSTEM_MONITOR,       ///< System monitor (self)
    COMPONENT_COUNT                 ///< Total component count
} component_id_t;

/**
 * @brief Memory statistics
 */
typedef struct {
    uint32_t free_heap;             ///< Current free heap (bytes)
    uint32_t min_free_heap;         ///< Minimum free heap since boot (bytes)
    uint32_t max_alloc_heap;        ///< Largest allocatable block (bytes)
    uint32_t total_heap;            ///< Total heap size (bytes)
    uint8_t heap_usage_percent;     ///< Heap usage percentage
} memory_stats_t;

/**
 * @brief Connectivity detailed status
 */
typedef struct {
    connectivity_status_t status;   ///< Overall connectivity status
    bool wifi_connected;            ///< WiFi connected
    bool wifi_has_ip;               ///< WiFi has IP address
    bool mqtt_connected;            ///< MQTT connected
    int8_t wifi_rssi;               ///< WiFi signal strength (dBm)
    uint32_t wifi_uptime_sec;       ///< WiFi connection uptime
    uint32_t mqtt_uptime_sec;       ///< MQTT connection uptime
    uint32_t total_disconnects;     ///< Total disconnect events
} connectivity_details_t;

/**
 * @brief System monitor configuration
 */
typedef struct {
    uint16_t health_check_interval_sec; ///< Health check interval (default: 60s)
    uint16_t memory_check_interval_sec; ///< Memory check interval (default: 30s)
    uint32_t memory_warning_threshold;  ///< Memory warning threshold (bytes)
    uint32_t memory_critical_threshold; ///< Memory critical threshold (bytes)
    bool enable_auto_recovery;          ///< Enable auto-recovery attempts
    bool enable_event_logging;          ///< Enable event logging
} system_monitor_config_t;

/**
 * @brief Complete system monitor status
 */
typedef struct {
    monitor_state_t state;          ///< Monitor state
    system_status_t system;         ///< System status (from common_types.h)
    memory_stats_t memory;          ///< Memory statistics
    connectivity_details_t connectivity; ///< Connectivity details
    uint32_t last_health_check;     ///< Last health check timestamp
    uint32_t health_check_count;    ///< Total health checks performed
} system_monitor_status_t;

/**
 * @brief System event log entry
 */
typedef struct {
    uint32_t timestamp;             ///< Event timestamp
    component_id_t component;       ///< Component that triggered event
    health_status_t severity;       ///< Event severity
    char message[64];               ///< Event message
} system_event_t;

/* ============================ PUBLIC API ============================ */

/**
 * @brief Initialize system monitor component
 *
 * Starts health monitoring task and initializes tracking.
 *
 * @param config Monitor configuration (NULL for default)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t system_monitor_init(const system_monitor_config_t* config);

/**
 * @brief Deinitialize system monitor
 *
 * Stops monitoring task and cleans up resources.
 *
 * @return ESP_OK on success
 */
esp_err_t system_monitor_deinit(void);

/**
 * @brief Start health monitoring
 *
 * Begins periodic health checks of all components.
 *
 * @return ESP_OK on success
 */
esp_err_t system_monitor_start(void);

/**
 * @brief Stop health monitoring
 *
 * Pauses periodic health checks.
 *
 * @return ESP_OK on success
 */
esp_err_t system_monitor_stop(void);

/**
 * @brief Perform immediate health check
 *
 * Checks health of all components immediately.
 *
 * @return ESP_OK if all components healthy, ESP_FAIL if issues detected
 */
esp_err_t system_monitor_check_health(void);

/**
 * @brief Get complete system status
 *
 * @param[out] status Pointer to status structure to fill
 * @return ESP_OK on success
 */
esp_err_t system_monitor_get_status(system_monitor_status_t* status);

/**
 * @brief Get overall system health
 *
 * @return Overall health status (healthy/warning/critical/failure)
 */
health_status_t system_monitor_get_health(void);

/**
 * @brief Check specific component health
 *
 * @param component Component to check
 * @param[out] health Pointer to health structure to fill
 * @return ESP_OK on success
 */
esp_err_t system_monitor_check_component(component_id_t component,
                                         component_health_t* health);

/**
 * @brief Get memory statistics
 *
 * @param[out] stats Pointer to memory stats structure to fill
 * @return ESP_OK on success
 */
esp_err_t system_monitor_get_memory_stats(memory_stats_t* stats);

/**
 * @brief Get connectivity status
 *
 * @param[out] connectivity Pointer to connectivity structure to fill
 * @return ESP_OK on success
 */
esp_err_t system_monitor_get_connectivity(connectivity_details_t* connectivity);

/**
 * @brief Check if system is healthy
 *
 * @return true if all critical components healthy
 */
bool system_monitor_is_healthy(void);

/**
 * @brief Check if WiFi is connected
 *
 * @return true if WiFi connected with IP
 */
bool system_monitor_is_wifi_connected(void);

/**
 * @brief Check if MQTT is connected
 *
 * @return true if MQTT connected
 */
bool system_monitor_is_mqtt_connected(void);

/**
 * @brief Check if system has full connectivity
 *
 * @return true if WiFi and MQTT both connected
 */
bool system_monitor_has_full_connectivity(void);

/**
 * @brief Get system uptime
 *
 * @return System uptime in seconds
 */
uint32_t system_monitor_get_uptime(void);

/**
 * @brief Reset component error counters
 *
 * @param component Component to reset (or COMPONENT_COUNT for all)
 * @return ESP_OK on success
 */
esp_err_t system_monitor_reset_errors(component_id_t component);

/**
 * @brief Log system event
 *
 * Logs an event for monitoring and diagnostics.
 *
 * @param component Component triggering event
 * @param severity Event severity
 * @param message Event message (max 63 chars)
 * @return ESP_OK on success
 */
esp_err_t system_monitor_log_event(component_id_t component,
                                   health_status_t severity,
                                   const char* message);

/**
 * @brief Get recent system events
 *
 * Retrieves recent event log entries.
 *
 * @param[out] events Array to store events
 * @param max_events Maximum number of events to retrieve
 * @param[out] count Actual number of events retrieved
 * @return ESP_OK on success
 */
esp_err_t system_monitor_get_events(system_event_t* events,
                                    uint8_t max_events,
                                    uint8_t* count);

/**
 * @brief Clear event log
 *
 * @return ESP_OK on success
 */
esp_err_t system_monitor_clear_events(void);

/**
 * @brief Trigger system recovery
 *
 * Attempts to recover from unhealthy state by restarting components.
 *
 * @param component Component to recover (or COMPONENT_COUNT for auto-detect)
 * @return ESP_OK if recovery initiated
 */
esp_err_t system_monitor_trigger_recovery(component_id_t component);

/* ============================ EVENT DEFINITIONS ============================ */

/**
 * @brief System monitor event base
 */
ESP_EVENT_DECLARE_BASE(SYSTEM_MONITOR_EVENTS);

/**
 * @brief System monitor event IDs
 */
typedef enum {
    SYSTEM_MONITOR_EVENT_INITIALIZED,       ///< Monitor initialized
    SYSTEM_MONITOR_EVENT_HEALTH_CHECK_OK,   ///< Health check passed
    SYSTEM_MONITOR_EVENT_HEALTH_WARNING,    ///< Health warning detected
    SYSTEM_MONITOR_EVENT_HEALTH_CRITICAL,   ///< Critical health issue
    SYSTEM_MONITOR_EVENT_COMPONENT_FAILED,  ///< Component failure detected
    SYSTEM_MONITOR_EVENT_MEMORY_WARNING,    ///< Low memory warning
    SYSTEM_MONITOR_EVENT_MEMORY_CRITICAL,   ///< Critical memory state
    SYSTEM_MONITOR_EVENT_CONNECTIVITY_LOST, ///< Connectivity lost
    SYSTEM_MONITOR_EVENT_CONNECTIVITY_RESTORED, ///< Connectivity restored
    SYSTEM_MONITOR_EVENT_RECOVERY_STARTED,  ///< Recovery attempt started
    SYSTEM_MONITOR_EVENT_RECOVERY_COMPLETED ///< Recovery completed
} system_monitor_event_id_t;

/* ============================ CONFIGURATION ============================ */

/**
 * @brief Default system monitor configuration
 */
#define SYSTEM_MONITOR_DEFAULT_CONFIG() {       \
    .health_check_interval_sec = 60,            \
    .memory_check_interval_sec = 30,            \
    .memory_warning_threshold = 50000,          \
    .memory_critical_threshold = 30000,         \
    .enable_auto_recovery = true,               \
    .enable_event_logging = true                \
}

/**
 * @brief Event log size (number of events to keep)
 */
#define SYSTEM_MONITOR_EVENT_LOG_SIZE   20

/**
 * @brief Component name strings (for logging)
 */
#define COMPONENT_NAME_WIFI_MANAGER     "WiFi Manager"
#define COMPONENT_NAME_MQTT_CLIENT      "MQTT Client"
#define COMPONENT_NAME_SENSOR_READER    "Sensor Reader"
#define COMPONENT_NAME_IRRIGATION       "Irrigation Ctrl"
#define COMPONENT_NAME_HTTP_SERVER      "HTTP Server"
#define COMPONENT_NAME_DEVICE_CONFIG    "Device Config"
#define COMPONENT_NAME_SYSTEM_MONITOR   "System Monitor"

/**
 * @brief Get component name string
 */
const char* system_monitor_get_component_name(component_id_t component);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_MONITOR_H
