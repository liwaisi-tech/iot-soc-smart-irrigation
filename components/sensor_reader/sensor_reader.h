/**
 * @file sensor_reader.h
 * @brief Sensor Reader Component - Unified Sensor Interface
 *
 * Manages all sensor readings: environmental (DHT22) and soil moisture (ADC).
 * Provides filtered, validated sensor data with error detection.
 *
 * Component Responsibilities:
 * - DHT22 temperature/humidity reading
 * - 3x capacitive soil moisture sensor reading (ADC)
 * - Data validation and filtering
 * - Sensor health monitoring
 * - Calibration management
 *
 * Migration from hexagonal: Consolidates dht_sensor driver + IMPORTS external soil sensor drivers
 *
 * Hardware:
 * - DHT22: GPIO 18 (one-wire protocol)
 * - Soil 1: ADC1_CH0 (GPIO 36) - input only
 * - Soil 2: ADC1_CH3 (GPIO 39) - input only
 * - Soil 3: ADC1_CH6 (GPIO 34) - input only
 *
 * @author Liwaisi Tech
 * @date 2025-10-01
 * @version 2.0.0 - Component-Based Architecture
 */

#ifndef SENSOR_READER_H
#define SENSOR_READER_H

#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"
#include "common_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ TYPES AND ENUMS ============================ */

/**
 * @brief Sensor reader state
 */
typedef enum {
    SENSOR_STATE_UNINITIALIZED = 0, ///< Component not initialized
    SENSOR_STATE_READY,             ///< Ready to read sensors
    SENSOR_STATE_READING,           ///< Reading in progress
    SENSOR_STATE_ERROR              ///< Error state
} sensor_state_t;

/**
 * @brief Sensor type enumeration
 */
typedef enum {
    SENSOR_TYPE_DHT22 = 0,          ///< DHT22 environmental sensor
    SENSOR_TYPE_SOIL_1,             ///< Soil moisture sensor 1
    SENSOR_TYPE_SOIL_2,             ///< Soil moisture sensor 2
    SENSOR_TYPE_SOIL_3,             ///< Soil moisture sensor 3
    SENSOR_TYPE_COUNT               ///< Total sensor types
} sensor_type_t;

/**
 * @brief Individual sensor health status
 */
typedef struct {
    sensor_type_t type;             ///< Sensor type
    bool is_healthy;                ///< Health status
    uint32_t error_count;           ///< Consecutive error count
    uint32_t total_reads;           ///< Total read attempts
    uint32_t successful_reads;      ///< Successful read count
    uint32_t last_read_time;        ///< Last successful read timestamp
    float last_value;               ///< Last valid reading
} sensor_health_t;

/**
 * @brief Sensor reader configuration
 */
typedef struct {
    // Soil sensors
    uint8_t soil_sensor_count;      ///< Number of soil sensors (1-3)
    bool enable_soil_filtering;     ///< Enable moving average filter
    uint8_t filter_window_size;     ///< Filter window size (default: 5)

    // DHT22
    uint8_t dht22_gpio;             ///< DHT22 GPIO pin (default: 18)
    uint16_t dht22_read_timeout_ms; ///< DHT22 read timeout (default: 2000ms)

    // ADC configuration
    adc_atten_t adc_attenuation;    ///< ADC attenuation (default: ADC_ATTEN_DB_11)

    // Calibration (soil moisture sensors) - [PHASE 2 FEATURE]
    // Currently not used - manual calibration via #define in moisture_sensor.c
    // Will be loaded from NVS and configurable via WiFi Provisioning UI
    uint16_t soil_cal_dry_mv[3];    ///< Calibration: dry soil voltage (mV) - Future use
    uint16_t soil_cal_wet_mv[3];    ///< Calibration: wet soil voltage (mV) - Future use

    // Error handling
    uint8_t max_consecutive_errors; ///< Max errors before marking unhealthy
} sensor_config_t;

/**
 * @brief Sensor reader status
 */
typedef struct {
    sensor_state_t state;           ///< Current state
    sensor_health_t sensors[SENSOR_TYPE_COUNT]; ///< Health per sensor
    uint32_t total_readings;        ///< Total readings since init
    uint32_t last_reading_time;     ///< Last reading timestamp
    bool all_sensors_healthy;       ///< True if all sensors operational
} sensor_status_t;

/* ============================ PUBLIC API ============================ */

/**
 * @brief Initialize sensor reader component
 *
 * Initializes DHT22 driver and ADC for soil moisture sensors.
 * Configures GPIO and ADC channels according to configuration.
 *
 * @param config Sensor configuration (NULL for default)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sensor_reader_init(const sensor_config_t* config);

/**
 * @brief Deinitialize sensor reader
 *
 * Cleans up GPIO and ADC resources.
 *
 * @return ESP_OK on success
 */
esp_err_t sensor_reader_deinit(void);

/**
 * @brief Read ambient environmental data
 *
 * Reads temperature and humidity from DHT22 sensor.
 * Applies validation and error checking.
 *
 * @param[out] data Pointer to ambient data structure to fill
 * @return ESP_OK if read successful, error code otherwise
 */
esp_err_t sensor_reader_get_ambient(ambient_data_t* data);

/**
 * @brief Read soil moisture data
 *
 * Reads humidity from all configured soil sensors.
 * Applies calibration, filtering, and validation.
 *
 * @param[out] data Pointer to soil data structure to fill
 * @return ESP_OK if at least one sensor read successfully, error code otherwise
 */
esp_err_t sensor_reader_get_soil(soil_data_t* data);

/**
 * @brief Read all sensors (ambient + soil)
 *
 * Reads DHT22 and all soil sensors in one call.
 * Populates complete sensor reading structure.
 *
 * @param[out] reading Pointer to sensor reading structure to fill
 * @return ESP_OK if ambient OR soil read successfully, error code otherwise
 */
esp_err_t sensor_reader_get_all(sensor_reading_t* reading);

/**
 * @brief Get sensor reader status
 *
 * @param[out] status Pointer to status structure to fill
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if status is NULL
 */
esp_err_t sensor_reader_get_status(sensor_status_t* status);

/**
 * @brief Check if all sensors are healthy
 *
 * @return true if all configured sensors are operational
 */
bool sensor_reader_is_healthy(void);

/**
 * @brief Get health status of specific sensor
 *
 * @param type Sensor type to query
 * @param[out] health Pointer to health structure to fill
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if invalid type
 */
esp_err_t sensor_reader_get_sensor_health(sensor_type_t type, sensor_health_t* health);

/**
 * @brief Reset error counters for specific sensor
 *
 * @param type Sensor type (or SENSOR_TYPE_COUNT for all)
 * @return ESP_OK on success
 */
esp_err_t sensor_reader_reset_errors(sensor_type_t type);

/**
 * @brief Calibrate soil moisture sensor
 *
 * [NOT CURRENTLY USED - Phase 2 Feature]
 *
 * CURRENT WORKFLOW: Manual calibration via #define in moisture_sensor.c
 * 1. Enable DEBUG logs to view RAW ADC values
 * 2. Note values in dry/wet conditions from logs
 * 3. Update #define VALUE_WHEN_DRY_CAP and VALUE_WHEN_WET_CAP
 * 4. Recompile and flash firmware
 *
 * FUTURE PHASE 2: Dynamic calibration from WiFi Provisioning UI and Raspberry API
 * - User triggers calibration from web interface during first setup
 * - Values stored in NVS for persistence across reboots
 * - No firmware recompilation needed
 * - Accessible via HTTP API: POST /api/calibrate/{sensor_index}
 *
 * This function will set calibration values for dry and wet conditions
 * and save them to NVS.
 *
 * @param sensor_index Soil sensor index (0-2)
 * @param dry_mv Voltage reading for dry soil (mV) - measured in field
 * @param wet_mv Voltage reading for wet soil (mV) - measured after irrigation
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if invalid index
 *
 * @note Will be activated in Phase 2 when WiFi Provisioning UI is ready
 */
esp_err_t sensor_reader_calibrate_soil(uint8_t sensor_index,
                                       uint16_t dry_mv,
                                       uint16_t wet_mv);

/**
 * @brief Get soil sensor calibration values
 *
 * [NOT CURRENTLY USED - Phase 2 Feature]
 *
 * Retrieves current calibration values from configuration.
 * In Phase 2, these will be loaded from NVS on startup.
 *
 * @param sensor_index Soil sensor index (0-2)
 * @param[out] dry_mv Pointer to store dry calibration value
 * @param[out] wet_mv Pointer to store wet calibration value
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if invalid index
 */
esp_err_t sensor_reader_get_soil_calibration(uint8_t sensor_index,
                                             uint16_t* dry_mv,
                                             uint16_t* wet_mv);

/* ============================ CONFIGURATION ============================ */

/**
 * @brief Default sensor configuration
 */
#define SENSOR_READER_DEFAULT_CONFIG() {        \
    .soil_sensor_count = 3,                     \
    .enable_soil_filtering = true,              \
    .filter_window_size = 5,                    \
    .dht22_gpio = GPIO_DHT22,                   \
    .dht22_read_timeout_ms = 2000,              \
    .adc_attenuation = ADC_ATTEN_DB_11,         \
    .soil_cal_dry_mv = {2800, 2800, 2800},      \
    .soil_cal_wet_mv = {1200, 1200, 1200},      \
    .max_consecutive_errors = 5                 \
}

/**
 * @brief DHT22 sensor specifications
 */
#define DHT22_TEMP_MIN          -40.0f  ///< Minimum temperature (°C)
#define DHT22_TEMP_MAX          80.0f   ///< Maximum temperature (°C)
#define DHT22_HUMIDITY_MIN      0.0f    ///< Minimum humidity (%)
#define DHT22_HUMIDITY_MAX      100.0f  ///< Maximum humidity (%)
#define DHT22_TEMP_ACCURACY     0.5f    ///< Temperature accuracy (±°C)
#define DHT22_HUMIDITY_ACCURACY 2.0f    ///< Humidity accuracy (±%)

/**
 * @brief Soil moisture sensor specifications
 */
#define SOIL_MOISTURE_MIN       0.0f    ///< Minimum moisture (%)
#define SOIL_MOISTURE_MAX       100.0f  ///< Maximum moisture (%)
#define SOIL_ADC_VREF_MV        3300    ///< ADC reference voltage (mV)
#define SOIL_ADC_MAX_VALUE      4095    ///< 12-bit ADC max value

/**
 * @brief Sensor reading intervals (recommendations)
 */
#define SENSOR_MIN_READ_INTERVAL_MS     2000    ///< Minimum interval (DHT22 limit)
#define SENSOR_RECOMMENDED_INTERVAL_MS  30000   ///< Recommended interval (30s)

/**
 * @brief Sensor reader NVS namespace
 */
#define SENSOR_READER_NVS_NAMESPACE "sensor_cfg"

/**
 * @brief Sensor calibration NVS keys
 */
#define SENSOR_NVS_KEY_SOIL1_DRY    "soil1_dry"
#define SENSOR_NVS_KEY_SOIL1_WET    "soil1_wet"
#define SENSOR_NVS_KEY_SOIL2_DRY    "soil2_dry"
#define SENSOR_NVS_KEY_SOIL2_WET    "soil2_wet"
#define SENSOR_NVS_KEY_SOIL3_DRY    "soil3_dry"
#define SENSOR_NVS_KEY_SOIL3_WET    "soil3_wet"

#ifdef __cplusplus
}
#endif

#endif // SENSOR_READER_H
