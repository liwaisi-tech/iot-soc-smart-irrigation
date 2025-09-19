#include "wifi_adapter.h"
#include "wifi_prov_manager.h"
#include "wifi_connection_manager.h"
#include "boot_counter.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "wifi_adapter";

ESP_EVENT_DEFINE_BASE(WIFI_ADAPTER_EVENTS);

static wifi_adapter_status_t s_adapter_status = {0};
static bool s_adapter_initialized = false;

static void wifi_adapter_provisioning_event_handler(void* arg, esp_event_base_t event_base,
                                                     int32_t event_id, void* event_data)
{
    if (event_base == WIFI_PROV_EVENTS) {
        switch (event_id) {
            case WIFI_PROV_EVENT_STARTED:
                ESP_LOGD(TAG, "WiFi provisioning started");
                s_adapter_status.state = WIFI_ADAPTER_STATE_PROVISIONING;
                esp_event_post(WIFI_ADAPTER_EVENTS, WIFI_ADAPTER_EVENT_PROVISIONING_STARTED, NULL, 0, portMAX_DELAY);
                break;
                
            case WIFI_PROV_EVENT_CREDENTIALS_SUCCESS:
                ESP_LOGD(TAG, "WiFi credentials validated successfully");
                break;
                
            case WIFI_PROV_EVENT_COMPLETED:
                ESP_LOGD(TAG, "WiFi provisioning completed");
                s_adapter_status.provisioned = true;
                s_adapter_status.state = WIFI_ADAPTER_STATE_CONNECTING;
                
                wifi_config_t config;
                if (wifi_prov_manager_get_config(&config) == ESP_OK) {
                    strncpy(s_adapter_status.ssid, (char *)config.sta.ssid, sizeof(s_adapter_status.ssid) - 1);
                    s_adapter_status.ssid[sizeof(s_adapter_status.ssid) - 1] = '\0';
                    
                    wifi_prov_manager_deinit();
                    
                    ESP_LOGD(TAG, "Starting WiFi connection with provisioned credentials");
                    wifi_connection_manager_connect(&config);
                }
                
                esp_event_post(WIFI_ADAPTER_EVENTS, WIFI_ADAPTER_EVENT_PROVISIONING_COMPLETED, NULL, 0, portMAX_DELAY);
                break;
                
            case WIFI_PROV_EVENT_FAILED:
                ESP_LOGE(TAG, "WiFi provisioning failed");
                s_adapter_status.state = WIFI_ADAPTER_STATE_ERROR;
                break;
                
            default:
                break;
        }
    }
}

static void wifi_adapter_connection_event_handler(void* arg, esp_event_base_t event_base,
                                                   int32_t event_id, void* event_data)
{
    if (event_base == WIFI_CONNECTION_EVENTS) {
        switch (event_id) {
            case WIFI_CONNECTION_EVENT_CONNECTED:
                ESP_LOGD(TAG, "WiFi connected successfully to: '%s'", s_adapter_status.ssid);
                s_adapter_status.connected = true;
                s_adapter_status.state = WIFI_ADAPTER_STATE_CONNECTED;
                esp_event_post(WIFI_ADAPTER_EVENTS, WIFI_ADAPTER_EVENT_CONNECTED, NULL, 0, portMAX_DELAY);
                break;
                
            case WIFI_CONNECTION_EVENT_DISCONNECTED:
                ESP_LOGD(TAG, "WiFi disconnected");
                s_adapter_status.connected = false;
                s_adapter_status.has_ip = false;
                s_adapter_status.state = WIFI_ADAPTER_STATE_DISCONNECTED;
                esp_event_post(WIFI_ADAPTER_EVENTS, WIFI_ADAPTER_EVENT_DISCONNECTED, NULL, 0, portMAX_DELAY);
                break;
                
            case WIFI_CONNECTION_EVENT_GOT_IP:
                ESP_LOGD(TAG, "WiFi got IP address");
                s_adapter_status.has_ip = true;
                if (event_data != NULL) {
                    esp_ip4_addr_t *ip = (esp_ip4_addr_t *)event_data;
                    s_adapter_status.ip_addr = *ip;
                    ESP_LOGD(TAG, "IP address: " IPSTR, IP2STR(ip));
                }
                esp_event_post(WIFI_ADAPTER_EVENTS, WIFI_ADAPTER_EVENT_IP_OBTAINED, event_data, sizeof(esp_ip4_addr_t), portMAX_DELAY);
                break;
                
            case WIFI_CONNECTION_EVENT_RETRY_EXHAUSTED:
                ESP_LOGE(TAG, "WiFi connection retry exhausted");
                s_adapter_status.state = WIFI_ADAPTER_STATE_ERROR;
                esp_event_post(WIFI_ADAPTER_EVENTS, WIFI_ADAPTER_EVENT_CONNECTION_FAILED, NULL, 0, portMAX_DELAY);
                break;
                
            default:
                break;
        }
    }
}

esp_err_t wifi_adapter_init(void)
{
    if (s_adapter_initialized) {
        ESP_LOGW(TAG, "WiFi adapter already initialized");
        return ESP_OK;
    }
    
    ESP_LOGD(TAG, "Initializing WiFi adapter");
    
    s_adapter_status.state = WIFI_ADAPTER_STATE_UNINITIALIZED;
    s_adapter_status.provisioned = false;
    s_adapter_status.connected = false;
    s_adapter_status.has_ip = false;
    memset(s_adapter_status.ssid, 0, sizeof(s_adapter_status.ssid));
    
    ESP_ERROR_CHECK(wifi_prov_manager_init());
    ESP_ERROR_CHECK(wifi_connection_manager_init());
    
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENTS, ESP_EVENT_ANY_ID, &wifi_adapter_provisioning_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_CONNECTION_EVENTS, ESP_EVENT_ANY_ID, &wifi_adapter_connection_event_handler, NULL));
    
    wifi_connection_manager_get_mac(s_adapter_status.mac_address);
    
    s_adapter_initialized = true;
    
    ESP_LOGD(TAG, "WiFi adapter initialized successfully");
    ESP_LOGD(TAG, "MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
             s_adapter_status.mac_address[0], s_adapter_status.mac_address[1],
             s_adapter_status.mac_address[2], s_adapter_status.mac_address[3],
             s_adapter_status.mac_address[4], s_adapter_status.mac_address[5]);
    
    esp_event_post(WIFI_ADAPTER_EVENTS, WIFI_ADAPTER_EVENT_INIT_COMPLETE, NULL, 0, portMAX_DELAY);
    
    return ESP_OK;
}

esp_err_t wifi_adapter_start(void)
{
    if (!s_adapter_initialized) {
        ESP_LOGE(TAG, "WiFi adapter not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGD(TAG, "Starting WiFi adapter");
    
    ESP_ERROR_CHECK(wifi_connection_manager_start());
    
    return wifi_adapter_check_and_connect();
}

esp_err_t wifi_adapter_stop(void)
{
    if (!s_adapter_initialized) {
        ESP_LOGE(TAG, "WiFi adapter not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGD(TAG, "Stopping WiFi adapter");
    
    wifi_prov_manager_stop();
    wifi_connection_manager_stop();
    
    s_adapter_status.state = WIFI_ADAPTER_STATE_UNINITIALIZED;
    s_adapter_status.connected = false;
    s_adapter_status.has_ip = false;
    
    return ESP_OK;
}

esp_err_t wifi_adapter_deinit(void)
{
    if (!s_adapter_initialized) {
        ESP_LOGW(TAG, "WiFi adapter not initialized");
        return ESP_OK;
    }
    
    ESP_LOGD(TAG, "Deinitializing WiFi adapter");
    
    wifi_adapter_stop();
    
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_PROV_EVENTS, ESP_EVENT_ANY_ID, &wifi_adapter_provisioning_event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_CONNECTION_EVENTS, ESP_EVENT_ANY_ID, &wifi_adapter_connection_event_handler));
    
    wifi_prov_manager_deinit();
    wifi_connection_manager_deinit();
    
    s_adapter_initialized = false;
    
    return ESP_OK;
}

esp_err_t wifi_adapter_check_and_connect(void)
{
    if (!s_adapter_initialized) {
        ESP_LOGE(TAG, "WiFi adapter not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGD(TAG, "Checking WiFi provisioning and connection status");
    
    s_adapter_status.state = WIFI_ADAPTER_STATE_CHECKING_PROVISION;
    
    // Step 1: Handle first boot - clear any factory/test credentials
    esp_err_t ret = wifi_adapter_check_first_boot_and_clear();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "First boot check failed, continuing anyway: %s", esp_err_to_name(ret));
    }
    
    // Step 2: Check for manual reset pattern (user-requested provisioning)
    bool should_provision = false;
    ret = boot_counter_check_reset_pattern(&should_provision);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to check reset pattern: %s", esp_err_to_name(ret));
        return ret;
    }
    
    if (should_provision) {
        ESP_LOGW(TAG, "Reset pattern detected - user requested provisioning mode");
        
        // Clear stored WiFi credentials to ensure clean provisioning
        ESP_LOGD(TAG, "Clearing stored WiFi credentials due to reset pattern");
        esp_err_t clear_ret = wifi_adapter_clear_all_credentials();
        if (clear_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to clear credentials: %s", esp_err_to_name(clear_ret));
        }
        
        // Reset boot counter to prevent repeated triggering
        ESP_LOGD(TAG, "Resetting boot counter after reset pattern detection");
        boot_counter_clear();
        
        esp_event_post(WIFI_ADAPTER_EVENTS, WIFI_ADAPTER_EVENT_RESET_REQUESTED, NULL, 0, portMAX_DELAY);
        return wifi_adapter_force_provisioning();
    }
    
    // Step 3: Check if device has valid stored credentials
    bool provisioned = false;
    ret = wifi_prov_manager_is_provisioned(&provisioned);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to check provisioning status: %s", esp_err_to_name(ret));
        ESP_LOGW(TAG, "Cannot determine provisioning status - starting provisioning for safety");
        return wifi_adapter_force_provisioning();
    }
    
    s_adapter_status.provisioned = provisioned;
    
    if (provisioned) {
        ESP_LOGD(TAG, "Device has stored credentials, attempting to connect");
        s_adapter_status.state = WIFI_ADAPTER_STATE_CONNECTING;
        
        // Step 4: Validate stored credentials
        wifi_config_t config;
        ret = wifi_prov_manager_get_config(&config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get stored WiFi config: %s", esp_err_to_name(ret));
            ESP_LOGW(TAG, "Invalid stored credentials - starting provisioning");
            return wifi_adapter_force_provisioning();
        }
        
        // Validate that we actually have a valid SSID
        if (strlen((char*)config.sta.ssid) == 0) {
            ESP_LOGW(TAG, "Empty SSID in stored credentials - starting provisioning");
            return wifi_adapter_force_provisioning();
        }
        
        ESP_LOGD(TAG, "Connecting to stored WiFi network: '%s' (length: %d)", config.sta.ssid, strlen((char*)config.sta.ssid));
        strncpy(s_adapter_status.ssid, (char *)config.sta.ssid, sizeof(s_adapter_status.ssid) - 1);
        s_adapter_status.ssid[sizeof(s_adapter_status.ssid) - 1] = '\0';
        
        return wifi_connection_manager_connect(&config);
        
    } else {
        ESP_LOGD(TAG, "No stored credentials found - starting provisioning mode");
        return wifi_adapter_force_provisioning();
    }
}

esp_err_t wifi_adapter_force_provisioning(void)
{
    if (!s_adapter_initialized) {
        ESP_LOGE(TAG, "WiFi adapter not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGD(TAG, "Starting forced WiFi provisioning");
    
    wifi_connection_manager_stop();
    
    s_adapter_status.state = WIFI_ADAPTER_STATE_PROVISIONING;
    s_adapter_status.connected = false;
    s_adapter_status.has_ip = false;
    
    return wifi_prov_manager_start();
}

esp_err_t wifi_adapter_reset_credentials(void)
{
    if (!s_adapter_initialized) {
        ESP_LOGE(TAG, "WiFi adapter not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGD(TAG, "Resetting WiFi credentials");
    
    wifi_connection_manager_disconnect();
    wifi_prov_manager_stop();
    
    esp_err_t ret = wifi_prov_manager_reset_credentials();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset credentials: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_adapter_status.provisioned = false;
    s_adapter_status.connected = false;
    s_adapter_status.has_ip = false;
    s_adapter_status.state = WIFI_ADAPTER_STATE_UNINITIALIZED;
    memset(s_adapter_status.ssid, 0, sizeof(s_adapter_status.ssid));
    
    return ESP_OK;
}

esp_err_t wifi_adapter_get_status(wifi_adapter_status_t *status)
{
    if (!s_adapter_initialized) {
        ESP_LOGE(TAG, "WiFi adapter not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (status == NULL) {
        ESP_LOGE(TAG, "Invalid parameter: status is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    *status = s_adapter_status;
    return ESP_OK;
}

bool wifi_adapter_is_provisioned(void)
{
    return s_adapter_status.provisioned;
}

bool wifi_adapter_is_connected(void)
{
    return s_adapter_status.connected && s_adapter_status.has_ip;
}

esp_err_t wifi_adapter_get_ip(esp_ip4_addr_t *ip)
{
    if (!s_adapter_initialized || !ip || !s_adapter_status.has_ip) {
        return ESP_ERR_INVALID_STATE;
    }

    *ip = s_adapter_status.ip_addr;
    return ESP_OK;
}

esp_err_t wifi_adapter_get_mac(uint8_t *mac)
{
    if (!s_adapter_initialized || !mac) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(mac, s_adapter_status.mac_address, 6);
    return ESP_OK;
}

esp_err_t wifi_adapter_get_ssid(char *ssid, size_t ssid_len)
{
    if (!s_adapter_initialized || !ssid || ssid_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(ssid, s_adapter_status.ssid, ssid_len - 1);
    ssid[ssid_len - 1] = '\0';

    return ESP_OK;
}

esp_err_t wifi_adapter_clear_all_credentials(void)
{
    if (!s_adapter_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Stop connections and clear credentials
    wifi_connection_manager_disconnect();
    wifi_prov_manager_stop();
    wifi_prov_manager_reset_credentials();

    // Clear NVS credentials
    nvs_handle_t nvs_handle;
    if (nvs_open("nvs.net80211", NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_erase_all(nvs_handle);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }

    if (nvs_open("nvs", NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_erase_key(nvs_handle, "wifi.sta.ssid");
        nvs_erase_key(nvs_handle, "wifi.sta.password");
        nvs_erase_key(nvs_handle, "wifi.sta.pmk");
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }

    // Reset adapter status
    s_adapter_status.provisioned = false;
    s_adapter_status.connected = false;
    s_adapter_status.has_ip = false;
    s_adapter_status.state = WIFI_ADAPTER_STATE_UNINITIALIZED;
    memset(s_adapter_status.ssid, 0, sizeof(s_adapter_status.ssid));

    return ESP_OK;
}

esp_err_t wifi_adapter_check_first_boot_and_clear(void)
{
    nvs_handle_t nvs_handle;
    if (nvs_open("liwaisi_config", NVS_READWRITE, &nvs_handle) != ESP_OK) {
        return ESP_FAIL;
    }

    uint8_t first_boot_flag = 0;
    size_t required_size = sizeof(first_boot_flag);
    esp_err_t ret = nvs_get_blob(nvs_handle, "first_boot_done", &first_boot_flag, &required_size);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        // First boot - mark as completed
        first_boot_flag = 1;
        nvs_set_blob(nvs_handle, "first_boot_done", &first_boot_flag, sizeof(first_boot_flag));
        nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);
    return ESP_OK;
}