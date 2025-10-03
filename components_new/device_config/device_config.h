/**
 * @file device_config.h
 * @brief Device Configuration Component - NVS Storage and Config Management
 *
 * Centralized configuration management with NVS persistence.
 * Provides simple interface for all device settings.
 *
 * Component Responsibilities:
 * - NVS initialization and management
 * - Device information (name, crop, version)
 * - WiFi credentials storage
 * - MQTT broker configuration
 * - Irrigation parameters
 * - Sensor configuration
 * - Default values and factory reset
 *
 * Migration from hexagonal: Consolidates device_config_service + NVS operations
 *
 * @author Liwaisi Tech
 * @date 2025-10-01
 * @version 2.0.0 - Component-Based Architecture
 */

#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "common_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ TYPES AND ENUMS ============================ */

/**
 * @brief Configuration category
 */
typedef enum {
    CONFIG_CATEGORY_DEVICE = 0,     ///< Device info (name, crop, version)
    CONFIG_CATEGORY_WIFI,           ///< WiFi credentials
    CONFIG_CATEGORY_MQTT,           ///< MQTT broker settings
    CONFIG_CATEGORY_IRRIGATION,     ///< Irrigation parameters
    CONFIG_CATEGORY_SENSOR,         ///< Sensor settings
    CONFIG_CATEGORY_ALL             ///< All categories
} config_category_t;

/**
 * @brief Configuration source
 */
typedef enum {
    CONFIG_SOURCE_DEFAULT = 0,      ///< Default hardcoded values
    CONFIG_SOURCE_NVS,              ///< Loaded from NVS
    CONFIG_SOURCE_RUNTIME           ///< Set at runtime
} config_source_t;

/**
 * @brief Device configuration status
 */
typedef struct {
    bool nvs_initialized;           ///< NVS initialized
    bool device_configured;         ///< Device info configured
    bool wifi_configured;           ///< WiFi credentials configured
    bool mqtt_configured;           ///< MQTT broker configured
    config_source_t source;         ///< Current config source
    uint32_t last_modified_time;    ///< Last modification timestamp
} config_status_t;

/* ============================ PUBLIC API ============================ */

/**
 * @brief Initialize device configuration component
 *
 * Initializes NVS and loads configuration from flash.
 * If no configuration found, loads defaults.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t device_config_init(void);

/**
 * @brief Deinitialize device configuration
 *
 * Commits any pending changes and deinitializes NVS.
 *
 * @return ESP_OK on success
 */
esp_err_t device_config_deinit(void);

/**
 * @brief Load configuration from NVS
 *
 * Loads specified category from NVS. If not found, loads defaults.
 *
 * @param category Configuration category to load
 * @return ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if not in NVS
 */
esp_err_t device_config_load(config_category_t category);

/**
 * @brief Save configuration to NVS
 *
 * Saves specified category to NVS for persistence.
 *
 * @param category Configuration category to save
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t device_config_save(config_category_t category);

/**
 * @brief Reset configuration to defaults
 *
 * Resets specified category to factory defaults.
 * Does not erase NVS unless commit is called.
 *
 * @param category Configuration category to reset
 * @return ESP_OK on success
 */
esp_err_t device_config_reset(config_category_t category);

/**
 * @brief Erase all configuration from NVS
 *
 * Factory reset - erases all NVS data and loads defaults.
 *
 * @return ESP_OK on success
 */
esp_err_t device_config_erase_all(void);

/**
 * @brief Get configuration status
 *
 * @param[out] status Pointer to status structure to fill
 * @return ESP_OK on success
 */
esp_err_t device_config_get_status(config_status_t* status);

/* ============================ DEVICE INFO API ============================ */

/**
 * @brief Get device name
 *
 * @param[out] name Buffer to store device name (min 32 bytes)
 * @param name_len Size of name buffer
 * @return ESP_OK on success
 */
esp_err_t device_config_get_device_name(char* name, size_t name_len);

/**
 * @brief Set device name
 *
 * @param name Device name (max 31 chars)
 * @return ESP_OK on success
 */
esp_err_t device_config_set_device_name(const char* name);

/**
 * @brief Get crop name
 *
 * @param[out] crop Buffer to store crop name (min 16 bytes)
 * @param crop_len Size of crop buffer
 * @return ESP_OK on success
 */
esp_err_t device_config_get_crop_name(char* crop, size_t crop_len);

/**
 * @brief Set crop name
 *
 * @param crop Crop name (max 15 chars)
 * @return ESP_OK on success
 */
esp_err_t device_config_set_crop_name(const char* crop);

/**
 * @brief Get device location
 *
 * @param[out] location Buffer to store device location (min 151 bytes)
 * @param location_len Size of location buffer
 * @return ESP_OK on success
 */
esp_err_t device_config_get_device_location(char* location, size_t location_len);

/**
 * @brief Set device location
 *
 * @param location Device location description (max 150 chars)
 * @return ESP_OK on success
 */
esp_err_t device_config_set_device_location(const char* location);

/**
 * @brief Get firmware version
 *
 * @param[out] version Buffer to store version (min 16 bytes)
 * @param version_len Size of version buffer
 * @return ESP_OK on success
 */
esp_err_t device_config_get_firmware_version(char* version, size_t version_len);

/**
 * @brief Get MAC address as string
 *
 * @param[out] mac Buffer to store MAC (min 18 bytes, format: XX:XX:XX:XX:XX:XX)
 * @param mac_len Size of mac buffer
 * @return ESP_OK on success
 */
esp_err_t device_config_get_mac_address(char* mac, size_t mac_len);

/**
 * @brief Get complete device configuration
 *
 * @param[out] config Pointer to device_config_t structure to fill
 * @return ESP_OK on success
 */
esp_err_t device_config_get_all(device_config_t* config);

/* ============================ WIFI CONFIG API ============================ */

/**
 * @brief Get WiFi SSID
 *
 * @param[out] ssid Buffer to store SSID (min 32 bytes)
 * @param ssid_len Size of ssid buffer
 * @return ESP_OK on success
 */
esp_err_t device_config_get_wifi_ssid(char* ssid, size_t ssid_len);

/**
 * @brief Get WiFi password
 *
 * @param[out] password Buffer to store password (min 64 bytes)
 * @param password_len Size of password buffer
 * @return ESP_OK on success
 */
esp_err_t device_config_get_wifi_password(char* password, size_t password_len);

/**
 * @brief Set WiFi credentials
 *
 * @param ssid WiFi SSID (max 31 chars)
 * @param password WiFi password (max 63 chars)
 * @return ESP_OK on success
 */
esp_err_t device_config_set_wifi_credentials(const char* ssid, const char* password);

/**
 * @brief Check if WiFi is configured
 *
 * @return true if WiFi credentials are stored
 */
bool device_config_is_wifi_configured(void);

/* ============================ MQTT CONFIG API ============================ */

/**
 * @brief Get MQTT broker URI
 *
 * @param[out] broker_uri Buffer to store broker URI (min 128 bytes)
 * @param uri_len Size of broker_uri buffer
 * @return ESP_OK on success
 */
esp_err_t device_config_get_mqtt_broker(char* broker_uri, size_t uri_len);

/**
 * @brief Get MQTT broker port
 *
 * @param[out] port Pointer to store port number
 * @return ESP_OK on success
 */
esp_err_t device_config_get_mqtt_port(uint16_t* port);

/**
 * @brief Get MQTT client ID
 *
 * @param[out] client_id Buffer to store client ID (min 32 bytes)
 * @param client_id_len Size of client_id buffer
 * @return ESP_OK on success
 */
esp_err_t device_config_get_mqtt_client_id(char* client_id, size_t client_id_len);

/**
 * @brief Set MQTT broker configuration
 *
 * @param broker_uri MQTT broker URI (wss://broker.com/mqtt)
 * @param port Broker port (1883 TCP, 8083 WSS)
 * @return ESP_OK on success
 */
esp_err_t device_config_set_mqtt_broker(const char* broker_uri, uint16_t port);

/**
 * @brief Check if MQTT is configured
 *
 * @return true if MQTT broker is configured
 */
bool device_config_is_mqtt_configured(void);

/* ============================ IRRIGATION CONFIG API ============================ */

/**
 * @brief Get soil humidity threshold
 *
 * @param[out] threshold Pointer to store threshold percentage
 * @return ESP_OK on success
 */
esp_err_t device_config_get_soil_threshold(float* threshold);

/**
 * @brief Set soil humidity threshold
 *
 * @param threshold Soil humidity threshold (0-100%)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out of range
 */
esp_err_t device_config_set_soil_threshold(float threshold);

/**
 * @brief Get maximum irrigation duration
 *
 * @param[out] duration Pointer to store duration in minutes
 * @return ESP_OK on success
 */
esp_err_t device_config_get_max_irrigation_duration(uint16_t* duration);

/**
 * @brief Set maximum irrigation duration
 *
 * @param duration Max duration in minutes (1-MAX_IRRIGATION_DURATION_MIN)
 * @return ESP_OK on success
 */
esp_err_t device_config_set_max_irrigation_duration(uint16_t duration);

/* ============================ SENSOR CONFIG API ============================ */

/**
 * @brief Get number of soil sensors
 *
 * @param[out] count Pointer to store sensor count
 * @return ESP_OK on success
 */
esp_err_t device_config_get_soil_sensor_count(uint8_t* count);

/**
 * @brief Set number of soil sensors
 *
 * @param count Sensor count (1-3)
 * @return ESP_OK on success
 */
esp_err_t device_config_set_soil_sensor_count(uint8_t count);

/**
 * @brief Get sensor reading interval
 *
 * @param[out] interval Pointer to store interval in seconds
 * @return ESP_OK on success
 */
esp_err_t device_config_get_reading_interval(uint16_t* interval);

/**
 * @brief Set sensor reading interval
 *
 * @param interval Reading interval in seconds (minimum 30s recommended)
 * @return ESP_OK on success
 */
esp_err_t device_config_set_reading_interval(uint16_t interval);

/* ============================ CONFIGURATION ============================ */

/**
 * @brief Default device name
 */
#define DEVICE_CONFIG_DEFAULT_NAME          "ESP32_Riego_01"

/**
 * @brief Default device location
 */
#define DEVICE_CONFIG_DEFAULT_LOCATION      "Finca Colombia"

/**
 * @brief Default crop name
 */
#define DEVICE_CONFIG_DEFAULT_CROP          "tomates"

/**
 * @brief Maximum lengths for device info strings
 */
#define DEVICE_CONFIG_NAME_MAX_LEN          50
#define DEVICE_CONFIG_LOCATION_MAX_LEN      150

/**
 * @brief Firmware version (should match project version)
 */
#define DEVICE_CONFIG_FIRMWARE_VERSION      "2.0.0"

/**
 * @brief Default MQTT broker
 */
#define DEVICE_CONFIG_DEFAULT_MQTT_BROKER   "wss://mqtt.liwaisi.tech/mqtt"
#define DEVICE_CONFIG_DEFAULT_MQTT_PORT     8083

/**
 * @brief NVS namespaces
 */
#define DEVICE_CONFIG_NVS_NAMESPACE         "device_cfg"

/**
 * @brief NVS keys for device info
 */
#define DEVICE_CONFIG_NVS_KEY_NAME          "dev_name"
#define DEVICE_CONFIG_NVS_KEY_LOCATION      "dev_loc"
#define DEVICE_CONFIG_NVS_KEY_CROP          "crop_name"
#define DEVICE_CONFIG_NVS_KEY_VERSION       "fw_version"

/**
 * @brief NVS keys for WiFi
 */
#define DEVICE_CONFIG_NVS_KEY_WIFI_SSID     "wifi_ssid"
#define DEVICE_CONFIG_NVS_KEY_WIFI_PASS     "wifi_pass"

/**
 * @brief NVS keys for MQTT
 */
#define DEVICE_CONFIG_NVS_KEY_MQTT_BROKER   "mqtt_broker"
#define DEVICE_CONFIG_NVS_KEY_MQTT_PORT     "mqtt_port"
#define DEVICE_CONFIG_NVS_KEY_MQTT_CLIENT   "mqtt_client"

/**
 * @brief NVS keys for irrigation
 */
#define DEVICE_CONFIG_NVS_KEY_SOIL_THRESH   "soil_thresh"
#define DEVICE_CONFIG_NVS_KEY_MAX_DURATION  "max_dur"

/**
 * @brief NVS keys for sensors
 */
#define DEVICE_CONFIG_NVS_KEY_SENSOR_COUNT  "sensor_cnt"
#define DEVICE_CONFIG_NVS_KEY_READ_INTERVAL "read_intv"

#ifdef __cplusplus
}
#endif

#endif // DEVICE_CONFIG_H
