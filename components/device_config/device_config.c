/**
 * @file device_config.c
 * @brief Device Configuration Component Implementation
 *
 * Centralized configuration management with NVS persistence.
 * Consolidates device info, WiFi, MQTT, irrigation, and sensor settings.
 *
 * Migration from: components/domain/services/device_config_service.c
 * Expanded with: WiFi config, MQTT config, irrigation config, sensor config
 *
 * @author Liwaisi Tech
 * @date 2025-10-04
 * @version 2.0.0 - Component-Based Architecture
 */

#include "device_config.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include <string.h>
#include <time.h>

/* ============================ CONSTANTS ============================ */

static const char *TAG = "device_config";

// Default values for device info
#define DEFAULT_DEVICE_NAME         DEVICE_CONFIG_DEFAULT_NAME
#define DEFAULT_DEVICE_LOCATION     DEVICE_CONFIG_DEFAULT_LOCATION
#define DEFAULT_CROP_NAME           DEVICE_CONFIG_DEFAULT_CROP
#define DEFAULT_FIRMWARE_VERSION    DEVICE_CONFIG_FIRMWARE_VERSION

// Default values for MQTT
#define DEFAULT_MQTT_BROKER         DEVICE_CONFIG_DEFAULT_MQTT_BROKER
#define DEFAULT_MQTT_PORT           DEVICE_CONFIG_DEFAULT_MQTT_PORT

// Default values for irrigation
#define DEFAULT_SOIL_THRESHOLD      45.0f   // 45% trigger point
#define DEFAULT_MAX_DURATION        120     // 2 hours max

// Default values for sensors
#define DEFAULT_SENSOR_COUNT        3       // 3 soil sensors
#define DEFAULT_READING_INTERVAL    60      // 60 seconds

/* ============================ PRIVATE STATE ============================ */

static bool s_initialized = false;
static nvs_handle_t s_nvs_handle = 0;
static SemaphoreHandle_t s_config_mutex = NULL;

/* ============================ PRIVATE HELPERS ============================ */

/**
 * @brief Validate string parameter
 */
static bool validate_string(const char* str, size_t max_len) {
    if (str == NULL || strlen(str) == 0) {
        return false;
    }
    if (strlen(str) > max_len) {
        return false;
    }
    return true;
}

/**
 * @brief Take configuration mutex with timeout
 */
static bool take_mutex(TickType_t timeout) {
    if (s_config_mutex == NULL) {
        return true;  // No mutex created yet
    }
    return xSemaphoreTake(s_config_mutex, timeout) == pdTRUE;
}

/**
 * @brief Release configuration mutex
 */
static void give_mutex(void) {
    if (s_config_mutex != NULL) {
        xSemaphoreGive(s_config_mutex);
    }
}

/* ============================ INITIALIZATION ============================ */

esp_err_t device_config_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Device config already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing device configuration component");

    // Initialize NVS flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erasing, reinitializing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    // Open NVS namespace in read/write mode
    ret = nvs_open(DEVICE_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &s_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s",
                 DEVICE_CONFIG_NVS_NAMESPACE, esp_err_to_name(ret));
        return ret;
    }

    // Create mutex for thread safety
    s_config_mutex = xSemaphoreCreateMutex();
    if (s_config_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create config mutex");
        nvs_close(s_nvs_handle);
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Device configuration initialized successfully");

    return ESP_OK;
}

esp_err_t device_config_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing device configuration");

    // Commit any pending changes
    if (s_nvs_handle != 0) {
        nvs_commit(s_nvs_handle);
        nvs_close(s_nvs_handle);
        s_nvs_handle = 0;
    }

    // Delete mutex
    if (s_config_mutex != NULL) {
        vSemaphoreDelete(s_config_mutex);
        s_config_mutex = NULL;
    }

    s_initialized = false;
    return ESP_OK;
}

/* ============================ DEVICE INFO API ============================ */

esp_err_t device_config_get_device_name(char* name, size_t name_len)
{
    if (name == NULL || name_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        ESP_LOGE(TAG, "Device config not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!take_mutex(pdMS_TO_TICKS(1000))) {
        ESP_LOGE(TAG, "Failed to take mutex");
        return ESP_ERR_TIMEOUT;
    }

    size_t required_size = name_len;
    esp_err_t ret = nvs_get_str(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_NAME, name, &required_size);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        // Use default value
        strncpy(name, DEFAULT_DEVICE_NAME, name_len - 1);
        name[name_len - 1] = '\0';
        ret = ESP_OK;
        ESP_LOGD(TAG, "Device name not found in NVS, using default: %s", name);
    } else if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Retrieved device name: %s", name);
    } else {
        ESP_LOGE(TAG, "Failed to get device name: %s", esp_err_to_name(ret));
    }

    give_mutex();
    return ret;
}

esp_err_t device_config_set_device_name(const char* name)
{
    if (!validate_string(name, 31)) {
        ESP_LOGE(TAG, "Invalid device name");
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        ESP_LOGE(TAG, "Device config not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!take_mutex(pdMS_TO_TICKS(1000))) {
        ESP_LOGE(TAG, "Failed to take mutex");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = nvs_set_str(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_NAME, name);
    if (ret == ESP_OK) {
        ret = nvs_commit(s_nvs_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Device name saved: %s", name);
        } else {
            ESP_LOGE(TAG, "Failed to commit device name: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGE(TAG, "Failed to set device name: %s", esp_err_to_name(ret));
    }

    give_mutex();
    return ret;
}

esp_err_t device_config_get_crop_name(char* crop, size_t crop_len)
{
    if (crop == NULL || crop_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!take_mutex(pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }

    size_t required_size = crop_len;
    esp_err_t ret = nvs_get_str(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_CROP, crop, &required_size);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        strncpy(crop, DEFAULT_CROP_NAME, crop_len - 1);
        crop[crop_len - 1] = '\0';
        ret = ESP_OK;
        ESP_LOGD(TAG, "Crop name not found, using default: %s", crop);
    }

    give_mutex();
    return ret;
}

esp_err_t device_config_set_crop_name(const char* crop)
{
    if (!validate_string(crop, 15)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!take_mutex(pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = nvs_set_str(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_CROP, crop);
    if (ret == ESP_OK) {
        ret = nvs_commit(s_nvs_handle);
        ESP_LOGI(TAG, "Crop name saved: %s", crop);
    }

    give_mutex();
    return ret;
}

esp_err_t device_config_get_device_location(char* location, size_t location_len)
{
    if (location == NULL || location_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!take_mutex(pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }

    size_t required_size = location_len;
    esp_err_t ret = nvs_get_str(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_LOCATION, location, &required_size);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        strncpy(location, DEFAULT_DEVICE_LOCATION, location_len - 1);
        location[location_len - 1] = '\0';
        ret = ESP_OK;
        ESP_LOGD(TAG, "Location not found, using default: %s", location);
    }

    give_mutex();
    return ret;
}

esp_err_t device_config_set_device_location(const char* location)
{
    if (!validate_string(location, 150)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!take_mutex(pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = nvs_set_str(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_LOCATION, location);
    if (ret == ESP_OK) {
        ret = nvs_commit(s_nvs_handle);
        ESP_LOGI(TAG, "Device location saved: %s", location);
    }

    give_mutex();
    return ret;
}

esp_err_t device_config_get_firmware_version(char* version, size_t version_len)
{
    if (version == NULL || version_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Firmware version is hardcoded, not stored in NVS
    strncpy(version, DEFAULT_FIRMWARE_VERSION, version_len - 1);
    version[version_len - 1] = '\0';

    return ESP_OK;
}

esp_err_t device_config_get_mac_address(char* mac, size_t mac_len)
{
    if (mac == NULL || mac_len < 18) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t mac_addr[6];
    esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, mac_addr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get MAC address: %s", esp_err_to_name(ret));
        return ret;
    }

    snprintf(mac, mac_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac_addr[0], mac_addr[1], mac_addr[2],
             mac_addr[3], mac_addr[4], mac_addr[5]);

    return ESP_OK;
}

esp_err_t device_config_get_all(system_config_t* config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Initialize config with defaults
    memset(config, 0, sizeof(system_config_t));

    // Device info
    device_config_get_device_name(config->device_name, sizeof(config->device_name));
    device_config_get_crop_name(config->crop_name, sizeof(config->crop_name));
    device_config_get_firmware_version(config->firmware_version, sizeof(config->firmware_version));

    // WiFi config
    device_config_get_wifi_ssid(config->wifi_ssid, sizeof(config->wifi_ssid));
    device_config_get_wifi_password(config->wifi_password, sizeof(config->wifi_password));

    // MQTT config
    device_config_get_mqtt_broker(config->mqtt_broker, sizeof(config->mqtt_broker));
    device_config_get_mqtt_port(&config->mqtt_port);
    device_config_get_mqtt_client_id(config->mqtt_client_id, sizeof(config->mqtt_client_id));

    // Irrigation config
    device_config_get_soil_threshold(&config->soil_threshold_critical);
    config->soil_threshold_optimal = THRESHOLD_SOIL_OPTIMAL;
    config->soil_threshold_max = THRESHOLD_SOIL_MAX;
    device_config_get_max_irrigation_duration(&config->max_duration_minutes);
    config->min_interval_minutes = MIN_IRRIGATION_INTERVAL_MIN;

    // Sensor config
    device_config_get_soil_sensor_count(&config->soil_sensor_count);
    device_config_get_reading_interval(&config->reading_interval_sec);

    // Safety config
    config->thermal_protection_temp = THRESHOLD_TEMP_THERMAL_STOP;
    config->enable_offline_mode = true;

    ESP_LOGD(TAG, "Retrieved complete configuration");
    return ESP_OK;
}

/* ============================ WIFI CONFIG API ============================ */

esp_err_t device_config_get_wifi_ssid(char* ssid, size_t ssid_len)
{
    if (ssid == NULL || ssid_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!take_mutex(pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }

    size_t required_size = ssid_len;
    esp_err_t ret = nvs_get_str(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_WIFI_SSID, ssid, &required_size);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ssid[0] = '\0';  // Empty string = not configured
        ret = ESP_OK;
    }

    give_mutex();
    return ret;
}

esp_err_t device_config_get_wifi_password(char* password, size_t password_len)
{
    if (password == NULL || password_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!take_mutex(pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }

    size_t required_size = password_len;
    esp_err_t ret = nvs_get_str(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_WIFI_PASS, password, &required_size);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        password[0] = '\0';  // Empty string = not configured
        ret = ESP_OK;
    }

    give_mutex();
    return ret;
}

esp_err_t device_config_set_wifi_credentials(const char* ssid, const char* password)
{
    if (!validate_string(ssid, 31) || !validate_string(password, 63)) {
        ESP_LOGE(TAG, "Invalid WiFi credentials");
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!take_mutex(pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = nvs_set_str(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_WIFI_SSID, ssid);
    if (ret == ESP_OK) {
        ret = nvs_set_str(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_WIFI_PASS, password);
    }

    if (ret == ESP_OK) {
        ret = nvs_commit(s_nvs_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "WiFi credentials saved: SSID=%s", ssid);
        }
    }

    give_mutex();
    return ret;
}

bool device_config_is_wifi_configured(void)
{
    char ssid[32];
    esp_err_t ret = device_config_get_wifi_ssid(ssid, sizeof(ssid));

    if (ret == ESP_OK && strlen(ssid) > 0) {
        return true;
    }

    return false;
}

/* ============================ MQTT CONFIG API ============================ */

esp_err_t device_config_get_mqtt_broker(char* broker_uri, size_t uri_len)
{
    if (broker_uri == NULL || uri_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!take_mutex(pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }

    size_t required_size = uri_len;
    esp_err_t ret = nvs_get_str(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_MQTT_BROKER, broker_uri, &required_size);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        strncpy(broker_uri, DEFAULT_MQTT_BROKER, uri_len - 1);
        broker_uri[uri_len - 1] = '\0';
        ret = ESP_OK;
        ESP_LOGD(TAG, "MQTT broker not found, using default: %s", broker_uri);
    }

    give_mutex();
    return ret;
}

esp_err_t device_config_get_mqtt_port(uint16_t* port)
{
    if (port == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!take_mutex(pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = nvs_get_u16(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_MQTT_PORT, port);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *port = DEFAULT_MQTT_PORT;
        ret = ESP_OK;
    }

    give_mutex();
    return ret;
}

esp_err_t device_config_get_mqtt_client_id(char* client_id, size_t client_id_len)
{
    if (client_id == NULL || client_id_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Generate client ID from MAC address
    char mac[18];
    esp_err_t ret = device_config_get_mac_address(mac, sizeof(mac));
    if (ret != ESP_OK) {
        return ret;
    }

    // Format: ESP32_AABBCCDDEEFF
    snprintf(client_id, client_id_len, "ESP32_%c%c%c%c%c%c%c%c%c%c%c%c",
             mac[0], mac[1], mac[3], mac[4], mac[6], mac[7],
             mac[9], mac[10], mac[12], mac[13], mac[15], mac[16]);

    return ESP_OK;
}

esp_err_t device_config_set_mqtt_broker(const char* broker_uri, uint16_t port)
{
    if (!validate_string(broker_uri, 127)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (port == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!take_mutex(pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = nvs_set_str(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_MQTT_BROKER, broker_uri);
    if (ret == ESP_OK) {
        ret = nvs_set_u16(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_MQTT_PORT, port);
    }

    if (ret == ESP_OK) {
        ret = nvs_commit(s_nvs_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "MQTT broker saved: %s:%d", broker_uri, port);
        }
    }

    give_mutex();
    return ret;
}

bool device_config_is_mqtt_configured(void)
{
    char broker[128];
    esp_err_t ret = device_config_get_mqtt_broker(broker, sizeof(broker));

    // If we got default value, check if it's different from hardcoded default
    if (ret == ESP_OK && strcmp(broker, DEFAULT_MQTT_BROKER) != 0) {
        return true;
    }

    return false;
}

/* ============================ IRRIGATION CONFIG API ============================ */

esp_err_t device_config_get_soil_threshold(float* threshold)
{
    if (threshold == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!take_mutex(pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }

    // NVS doesn't support float directly, store as uint32_t (percentage * 100)
    uint32_t threshold_u32 = 0;
    esp_err_t ret = nvs_get_u32(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_SOIL_THRESH, &threshold_u32);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *threshold = DEFAULT_SOIL_THRESHOLD;
        ret = ESP_OK;
    } else if (ret == ESP_OK) {
        *threshold = (float)threshold_u32 / 100.0f;
    }

    give_mutex();
    return ret;
}

esp_err_t device_config_set_soil_threshold(float threshold)
{
    if (threshold < 0.0f || threshold > 100.0f) {
        ESP_LOGE(TAG, "Invalid soil threshold: %.1f%% (must be 0-100%%)", threshold);
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!take_mutex(pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }

    // Store as uint32_t (percentage * 100)
    uint32_t threshold_u32 = (uint32_t)(threshold * 100.0f);
    esp_err_t ret = nvs_set_u32(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_SOIL_THRESH, threshold_u32);

    if (ret == ESP_OK) {
        ret = nvs_commit(s_nvs_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Soil threshold saved: %.1f%%", threshold);
        }
    }

    give_mutex();
    return ret;
}

esp_err_t device_config_get_max_irrigation_duration(uint16_t* duration)
{
    if (duration == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!take_mutex(pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = nvs_get_u16(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_MAX_DURATION, duration);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *duration = DEFAULT_MAX_DURATION;
        ret = ESP_OK;
    }

    give_mutex();
    return ret;
}

esp_err_t device_config_set_max_irrigation_duration(uint16_t duration)
{
    if (duration == 0 || duration > MAX_IRRIGATION_DURATION_MIN) {
        ESP_LOGE(TAG, "Invalid irrigation duration: %d min (must be 1-%d)",
                 duration, MAX_IRRIGATION_DURATION_MIN);
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!take_mutex(pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = nvs_set_u16(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_MAX_DURATION, duration);

    if (ret == ESP_OK) {
        ret = nvs_commit(s_nvs_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Max irrigation duration saved: %d min", duration);
        }
    }

    give_mutex();
    return ret;
}

/* ============================ SENSOR CONFIG API ============================ */

esp_err_t device_config_get_soil_sensor_count(uint8_t* count)
{
    if (count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!take_mutex(pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = nvs_get_u8(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_SENSOR_COUNT, count);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *count = DEFAULT_SENSOR_COUNT;
        ret = ESP_OK;
    }

    give_mutex();
    return ret;
}

esp_err_t device_config_set_soil_sensor_count(uint8_t count)
{
    if (count < 1 || count > 3) {
        ESP_LOGE(TAG, "Invalid sensor count: %d (must be 1-3)", count);
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!take_mutex(pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = nvs_set_u8(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_SENSOR_COUNT, count);

    if (ret == ESP_OK) {
        ret = nvs_commit(s_nvs_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Soil sensor count saved: %d", count);
        }
    }

    give_mutex();
    return ret;
}

esp_err_t device_config_get_reading_interval(uint16_t* interval)
{
    if (interval == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!take_mutex(pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = nvs_get_u16(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_READ_INTERVAL, interval);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *interval = DEFAULT_READING_INTERVAL;
        ret = ESP_OK;
    }

    give_mutex();
    return ret;
}

esp_err_t device_config_set_reading_interval(uint16_t interval)
{
    if (interval < 10 || interval > 3600) {
        ESP_LOGE(TAG, "Invalid reading interval: %d sec (must be 10-3600)", interval);
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!take_mutex(pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = nvs_set_u16(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_READ_INTERVAL, interval);

    if (ret == ESP_OK) {
        ret = nvs_commit(s_nvs_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Reading interval saved: %d sec", interval);
        }
    }

    give_mutex();
    return ret;
}

/* ============================ SYSTEM MANAGEMENT ============================ */

esp_err_t device_config_load(config_category_t category)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Configuration is loaded on-demand by getter functions
    // This function is a no-op for NVS-based storage
    ESP_LOGD(TAG, "Config load requested for category %d (no-op for NVS)", category);
    return ESP_OK;
}

esp_err_t device_config_save(config_category_t category)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!take_mutex(pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }

    // Commit all pending changes to NVS
    esp_err_t ret = nvs_commit(s_nvs_handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Configuration saved for category %d", category);
    } else {
        ESP_LOGE(TAG, "Failed to save configuration: %s", esp_err_to_name(ret));
    }

    give_mutex();
    return ret;
}

esp_err_t device_config_reset(config_category_t category)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!take_mutex(pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;

    switch (category) {
        case CONFIG_CATEGORY_DEVICE:
            nvs_erase_key(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_NAME);
            nvs_erase_key(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_LOCATION);
            nvs_erase_key(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_CROP);
            ESP_LOGI(TAG, "Device config reset to defaults");
            break;

        case CONFIG_CATEGORY_WIFI:
            nvs_erase_key(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_WIFI_SSID);
            nvs_erase_key(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_WIFI_PASS);
            ESP_LOGI(TAG, "WiFi config reset to defaults");
            break;

        case CONFIG_CATEGORY_MQTT:
            nvs_erase_key(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_MQTT_BROKER);
            nvs_erase_key(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_MQTT_PORT);
            ESP_LOGI(TAG, "MQTT config reset to defaults");
            break;

        case CONFIG_CATEGORY_IRRIGATION:
            nvs_erase_key(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_SOIL_THRESH);
            nvs_erase_key(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_MAX_DURATION);
            ESP_LOGI(TAG, "Irrigation config reset to defaults");
            break;

        case CONFIG_CATEGORY_SENSOR:
            nvs_erase_key(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_SENSOR_COUNT);
            nvs_erase_key(s_nvs_handle, DEVICE_CONFIG_NVS_KEY_READ_INTERVAL);
            ESP_LOGI(TAG, "Sensor config reset to defaults");
            break;

        case CONFIG_CATEGORY_ALL:
            ret = device_config_erase_all();
            break;

        default:
            ret = ESP_ERR_INVALID_ARG;
            break;
    }

    if (ret == ESP_OK) {
        ret = nvs_commit(s_nvs_handle);
    }

    give_mutex();
    return ret;
}

esp_err_t device_config_erase_all(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(TAG, "Erasing ALL device configuration (factory reset)");

    if (!take_mutex(pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }

    // Erase entire namespace
    esp_err_t ret = nvs_erase_all(s_nvs_handle);

    if (ret == ESP_OK) {
        ret = nvs_commit(s_nvs_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "All configuration erased successfully");
        }
    } else {
        ESP_LOGE(TAG, "Failed to erase configuration: %s", esp_err_to_name(ret));
    }

    give_mutex();
    return ret;
}

esp_err_t device_config_get_status(config_status_t* status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(status, 0, sizeof(config_status_t));

    status->nvs_initialized = s_initialized;
    status->wifi_configured = device_config_is_wifi_configured();
    status->mqtt_configured = device_config_is_mqtt_configured();

    // Check if device info is configured (non-default values)
    char name[32];
    device_config_get_device_name(name, sizeof(name));
    status->device_configured = (strcmp(name, DEFAULT_DEVICE_NAME) != 0);

    status->source = CONFIG_SOURCE_NVS;
    status->last_modified_time = (uint32_t)time(NULL);

    return ESP_OK;
}
