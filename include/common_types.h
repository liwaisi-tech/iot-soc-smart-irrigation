/**
 * @file common_types.h
 * @brief Common data types for Smart Irrigation System - Component-Based Architecture
 *
 * Optimized types for ESP32 with FPU support.
 * Target: <16 bytes per reading, aligned for FPU operations
 *
 * @author Liwaisi Tech
 * @date 2025-10-01
 * @version 2.0.0 - Component-Based Architecture
 */

#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ SENSOR DATA TYPES ============================ */

/**
 * @brief Ambient environmental data (temperature and humidity)
 *
 * Size: 12 bytes (3 x 4 bytes float)
 * FPU optimized: All floats aligned
 */
typedef struct {
    float temperature;      ///< Temperature in °C (-40 to 80°C for DHT22)
    float humidity;         ///< Relative humidity in % (0-100%)
    uint32_t timestamp;     ///< Unix timestamp of reading
} ambient_data_t;

/**
 * @brief Soil moisture sensor data
 *
 * Size: 16 bytes (4 x 4 bytes)
 * Supports 1-3 capacitive soil moisture sensors
 */
typedef struct {
    float soil_humidity[3]; ///< Soil moisture % (0-100%) for up to 3 sensors
    uint8_t sensor_count;   ///< Number of active sensors (1-3)
    uint8_t reserved[3];    ///< Reserved for alignment
    uint32_t timestamp;     ///< Unix timestamp of reading
} soil_data_t;

/**
 * @brief Complete sensor reading package
 *
 * Size: 76 bytes total
 * Combines ambient and soil data with device info
 */
typedef struct {
    ambient_data_t ambient; ///< Ambient temperature and humidity
    soil_data_t soil;       ///< Soil moisture readings
    char device_mac[18];    ///< MAC address (XX:XX:XX:XX:XX:XX format)
    char device_ip[16];     ///< IP address (XXX.XXX.XXX.XXX format)
    uint32_t reading_id;    ///< Sequential reading counter
} sensor_reading_t;

/* ============================ IRRIGATION TYPES ============================ */

/**
 * @brief Irrigation system state
 */
typedef enum {
    IRRIGATION_IDLE = 0,            ///< System idle, no irrigation
    IRRIGATION_ACTIVE,              ///< Irrigation in progress
    IRRIGATION_PAUSED,              ///< Irrigation paused (waiting condition)
    IRRIGATION_ERROR,               ///< System error detected
    IRRIGATION_EMERGENCY_STOP,      ///< Emergency stop activated
    IRRIGATION_THERMAL_PROTECTION   ///< Thermal protection triggered (>40°C)
} irrigation_state_t;

/**
 * @brief Irrigation mode (online/offline)
 */
typedef enum {
    IRRIGATION_MODE_ONLINE = 0,     ///< Online mode (WiFi/MQTT active)
    IRRIGATION_MODE_OFFLINE_NORMAL, ///< Offline mode - normal (2h intervals)
    IRRIGATION_MODE_OFFLINE_WARNING,///< Offline mode - warning (1h intervals)
    IRRIGATION_MODE_OFFLINE_CRITICAL,///< Offline mode - critical (30min intervals)
    IRRIGATION_MODE_OFFLINE_EMERGENCY///< Offline mode - emergency (15min intervals)
} irrigation_mode_t;

/**
 * @brief Irrigation command types
 */
typedef enum {
    IRRIGATION_CMD_START = 0,       ///< Start irrigation
    IRRIGATION_CMD_STOP,            ///< Stop irrigation normally
    IRRIGATION_CMD_EMERGENCY_STOP,  ///< Emergency stop
    IRRIGATION_CMD_PAUSE,           ///< Pause irrigation
    IRRIGATION_CMD_RESUME           ///< Resume paused irrigation
} irrigation_command_t;

/**
 * @brief Irrigation session status
 *
 * Tracks current irrigation session with safety limits
 */
typedef struct {
    irrigation_state_t state;       ///< Current irrigation state
    irrigation_mode_t mode;         ///< Current operating mode
    uint32_t session_start_time;    ///< Session start timestamp
    uint32_t session_duration_sec;  ///< Current session duration in seconds
    uint32_t total_runtime_today;   ///< Total runtime today in seconds
    uint16_t valve_number;          ///< Active valve number (1-2)
    bool safety_lock;               ///< Safety lock status
    float last_soil_avg;            ///< Last average soil humidity %
} irrigation_status_t;

/* ============================ DEVICE TYPES ============================ */

/**
 * @brief System configuration stored in NVS (Component-Based Architecture)
 *
 * RENAMED from device_config_t to system_config_t to avoid conflict
 * with hexagonal architecture's device_config_t (in device_config_service.h)
 *
 * Size: ~300 bytes
 */
typedef struct {
    // WiFi Configuration
    char wifi_ssid[32];             ///< WiFi SSID
    char wifi_password[64];         ///< WiFi password

    // MQTT Configuration
    char mqtt_broker[128];          ///< MQTT broker URL
    uint16_t mqtt_port;             ///< MQTT broker port
    char mqtt_client_id[32];        ///< MQTT client ID

    // Device Information
    char device_name[32];           ///< Device friendly name
    char crop_name[16];             ///< Crop type being irrigated
    char firmware_version[16];      ///< Firmware version

    // Irrigation Configuration
    float soil_threshold_critical;  ///< Critical soil humidity (30%)
    float soil_threshold_optimal;   ///< Optimal soil humidity (75%)
    float soil_threshold_max;       ///< Maximum soil humidity (80%)
    uint16_t max_duration_minutes;  ///< Max irrigation duration (120 min)
    uint16_t min_interval_minutes;  ///< Min interval between sessions (240 min)

    // Sensor Configuration
    uint8_t soil_sensor_count;      ///< Number of soil sensors (1-3)
    uint16_t reading_interval_sec;  ///< Sensor reading interval (60s online)

    // Safety Configuration
    float thermal_protection_temp;  ///< Temperature limit for auto-stop (40°C)
    bool enable_offline_mode;       ///< Enable offline irrigation mode
} system_config_t;

/* ============================ SYSTEM MONITORING TYPES ============================ */

/**
 * @brief System health status
 */
typedef enum {
    HEALTH_STATUS_HEALTHY = 0,      ///< All systems operational
    HEALTH_STATUS_WARNING,          ///< Non-critical issues detected
    HEALTH_STATUS_CRITICAL,         ///< Critical issues detected
    HEALTH_STATUS_FAILURE           ///< System failure
} health_status_t;

/**
 * @brief Connectivity status
 */
typedef enum {
    CONNECTIVITY_DISCONNECTED = 0,  ///< No connectivity
    CONNECTIVITY_WIFI_ONLY,         ///< WiFi connected, no MQTT
    CONNECTIVITY_FULL              ///< WiFi + MQTT connected
} connectivity_status_t;

/**
 * @brief Component health check result
 */
typedef struct {
    char component_name[16];        ///< Component name
    health_status_t status;         ///< Health status
    uint32_t error_count;           ///< Error count since last reset
    uint32_t last_check_time;       ///< Last health check timestamp
} component_health_t;

/**
 * @brief System monitor status
 */
typedef struct {
    // Memory
    uint32_t free_heap;             ///< Free heap memory in bytes
    uint32_t min_free_heap;         ///< Minimum free heap since boot

    // Connectivity
    connectivity_status_t connectivity; ///< Connectivity status
    uint32_t wifi_reconnect_count;  ///< WiFi reconnection count
    uint32_t mqtt_reconnect_count;  ///< MQTT reconnection count

    // Component Health
    component_health_t wifi_health;     ///< WiFi component health
    component_health_t mqtt_health;     ///< MQTT component health
    component_health_t sensor_health;   ///< Sensor reader health
    component_health_t irrigation_health; ///< Irrigation controller health

    // System
    uint32_t uptime_seconds;        ///< System uptime in seconds
    health_status_t overall_status; ///< Overall system health
} system_status_t;

/* ============================ GPIO DEFINITIONS ============================ */

/**
 * @brief GPIO pin assignments (avoiding boot-strapping pins)
 *
 * Safe pins that don't affect ESP32 boot:
 * - Avoid: GPIO 0, 2, 5, 12, 15 (boot mode pins)
 * - Use: GPIO 18, 21, 22, 23 (safe digital)
 * - ADC: GPIO 36, 39, 34 (input only, safe)
 */
#define GPIO_VALVE_1            21  ///< Relay IN1 for valve 1
#define GPIO_VALVE_2            22  ///< Relay IN2 for valve 2
#define GPIO_DHT22              18  ///< DHT22 data pin
#define GPIO_STATUS_LED         23  ///< Status LED

// ADC Channels for soil moisture sensors (input-only pins)
#define ADC_SOIL_SENSOR_1       36  ///< ADC1_CH0 - Soil sensor 1
#define ADC_SOIL_SENSOR_2       39  ///< ADC1_CH3 - Soil sensor 2
#define ADC_SOIL_SENSOR_3       34  ///< ADC1_CH6 - Soil sensor 3

/* ============================ CONSTANTS ============================ */

// Irrigation thresholds are now configured via Kconfig (menuconfig)
// See: components/irrigation_controller/Kconfig
// Previous hardcoded values moved to irrigation_controller via _get_default_config()
#define THRESHOLD_HUMIDITY_OPTIMAL      60.0f   ///< Optimal ambient humidity (%)

// Safety limits
#define MAX_IRRIGATION_DURATION_MIN     120     ///< Maximum 2 hours per session
#define MIN_IRRIGATION_INTERVAL_MIN     240     ///< Minimum 4 hours between sessions
#define MAX_DAILY_IRRIGATION_MIN        360     ///< Maximum 6 hours total per day

// Timing constants
#define SENSOR_READING_INTERVAL_ONLINE  60      ///< 60 seconds online mode
#define SENSOR_READING_INTERVAL_OFFLINE_NORMAL   7200  ///< 2 hours normal offline
#define SENSOR_READING_INTERVAL_OFFLINE_WARNING  3600  ///< 1 hour warning offline
#define SENSOR_READING_INTERVAL_OFFLINE_CRITICAL 1800  ///< 30 min critical offline
#define SENSOR_READING_INTERVAL_OFFLINE_EMERGENCY 900  ///< 15 min emergency offline

// Task priorities
#define TASK_PRIORITY_SAFETY_WATCHDOG   5       ///< Highest - safety critical
#define TASK_PRIORITY_IRRIGATION        4       ///< High - irrigation control
#define TASK_PRIORITY_SENSOR            3       ///< Medium - sensor reading
#define TASK_PRIORITY_MQTT              3       ///< Medium - communication
#define TASK_PRIORITY_HEALTH            2       ///< Low - monitoring

#ifdef __cplusplus
}
#endif

#endif // COMMON_TYPES_H
