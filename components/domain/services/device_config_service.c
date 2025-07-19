#include "device_config_service.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "device_config_service";
static bool s_service_initialized = false;

esp_err_t device_config_service_init(void)
{
    if (s_service_initialized) {
        ESP_LOGW(TAG, "Device config service already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing device configuration service");
    
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_service_initialized = true;
    ESP_LOGI(TAG, "Device configuration service initialized successfully");
    return ESP_OK;
}

esp_err_t device_config_service_get_name(char *name, size_t name_len)
{
    if (name == NULL || name_len == 0) {
        ESP_LOGE(TAG, "Invalid parameters for get device name");
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_service_initialized) {
        ESP_LOGE(TAG, "Device config service not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(DEVICE_CONFIG_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for device name, using default: %s", esp_err_to_name(ret));
        strncpy(name, "Liwaisi Smart Irrigation", name_len - 1);
        name[name_len - 1] = '\0';
        return ESP_OK;
    }

    size_t required_size = name_len;
    ret = nvs_get_str(nvs_handle, DEVICE_NAME_KEY, name, &required_size);
    nvs_close(nvs_handle);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Retrieved device name: %s", name);
    } else {
        ESP_LOGW(TAG, "Failed to load device name, using default: %s", esp_err_to_name(ret));
        strncpy(name, "Liwaisi Smart Irrigation", name_len - 1);
        name[name_len - 1] = '\0';
    }
    
    return ESP_OK;
}

esp_err_t device_config_service_set_name(const char *name)
{
    if (name == NULL || strlen(name) == 0) {
        ESP_LOGE(TAG, "Invalid device name");
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(name) > DEVICE_NAME_MAX_LEN) {
        ESP_LOGE(TAG, "Device name too long (max %d characters)", DEVICE_NAME_MAX_LEN);
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_service_initialized) {
        ESP_LOGE(TAG, "Device config service not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(DEVICE_CONFIG_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_str(nvs_handle, DEVICE_NAME_KEY, name);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Device name saved: %s", name);
        } else {
            ESP_LOGE(TAG, "Failed to commit device name: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGE(TAG, "Failed to save device name: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t device_config_service_get_location(char *location, size_t location_len)
{
    if (location == NULL || location_len == 0) {
        ESP_LOGE(TAG, "Invalid parameters for get device location");
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_service_initialized) {
        ESP_LOGE(TAG, "Device config service not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(DEVICE_CONFIG_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for device location, using default: %s", esp_err_to_name(ret));
        strncpy(location, "Not configured", location_len - 1);
        location[location_len - 1] = '\0';
        return ESP_OK;
    }

    size_t required_size = location_len;
    ret = nvs_get_str(nvs_handle, DEVICE_LOCATION_KEY, location, &required_size);
    nvs_close(nvs_handle);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Retrieved device location: %s", location);
    } else {
        ESP_LOGW(TAG, "Failed to load device location, using default: %s", esp_err_to_name(ret));
        strncpy(location, "Not configured", location_len - 1);
        location[location_len - 1] = '\0';
    }
    
    return ESP_OK;
}

esp_err_t device_config_service_set_location(const char *location)
{
    if (location == NULL || strlen(location) == 0) {
        ESP_LOGE(TAG, "Invalid device location");
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(location) > DEVICE_LOCATION_MAX_LEN) {
        ESP_LOGE(TAG, "Device location too long (max %d characters)", DEVICE_LOCATION_MAX_LEN);
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_service_initialized) {
        ESP_LOGE(TAG, "Device config service not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(DEVICE_CONFIG_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_str(nvs_handle, DEVICE_LOCATION_KEY, location);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Device location saved: %s", location);
        } else {
            ESP_LOGE(TAG, "Failed to commit device location: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGE(TAG, "Failed to save device location: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t device_config_service_get_config(device_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid parameter: config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    device_config_service_get_name(config->device_name, sizeof(config->device_name));
    device_config_service_get_location(config->device_location, sizeof(config->device_location));

    // Both calls handle defaults internally, so we always return OK
    ESP_LOGI(TAG, "Retrieved device config - Name: '%s', Location: '%s'", 
             config->device_name, config->device_location);
    
    return ESP_OK;
}

esp_err_t device_config_service_set_config(const device_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid parameter: config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Saving device config - Name: '%s', Location: '%s'", 
             config->device_name, config->device_location);

    esp_err_t ret1 = device_config_service_set_name(config->device_name);
    esp_err_t ret2 = device_config_service_set_location(config->device_location);

    if (ret1 != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save device name: %s", esp_err_to_name(ret1));
        return ret1;
    }
    
    if (ret2 != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save device location: %s", esp_err_to_name(ret2));
        return ret2;
    }

    ESP_LOGI(TAG, "Device configuration saved successfully");
    return ESP_OK;
}

esp_err_t device_config_service_is_configured(bool *configured)
{
    if (configured == NULL) {
        ESP_LOGE(TAG, "Invalid parameter: configured is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_service_initialized) {
        ESP_LOGE(TAG, "Device config service not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    device_config_t config;
    esp_err_t ret = device_config_service_get_config(&config);
    
    if (ret == ESP_OK) {
        // Check if both name and location are set and not default values
        bool name_configured = (strlen(config.device_name) > 0 && 
                               strcmp(config.device_name, "Liwaisi Smart Irrigation") != 0);
        bool location_configured = (strlen(config.device_location) > 0 && 
                                   strcmp(config.device_location, "Not configured") != 0);
        
        *configured = name_configured && location_configured;
        ESP_LOGI(TAG, "Device configuration status: %s", *configured ? "configured" : "not configured");
    } else {
        *configured = false;
        ESP_LOGE(TAG, "Failed to check device configuration: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t device_config_service_clear(void)
{
    if (!s_service_initialized) {
        ESP_LOGE(TAG, "Device config service not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Clearing device configuration");

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(DEVICE_CONFIG_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }

    nvs_erase_key(nvs_handle, DEVICE_NAME_KEY);
    nvs_erase_key(nvs_handle, DEVICE_LOCATION_KEY);
    
    ret = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Device configuration cleared successfully");
    } else {
        ESP_LOGE(TAG, "Failed to commit NVS changes: %s", esp_err_to_name(ret));
    }

    return ret;
}