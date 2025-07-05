#include "wifi_prov_manager.h"
#include "boot_counter.h"
#include "wifi_connection_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"
#include <string.h>

static const char *TAG = "wifi_prov_manager";

ESP_EVENT_DEFINE_BASE(WIFI_PROV_EVENTS);

static wifi_prov_manager_t s_prov_manager = {0};
static bool s_prov_manager_initialized = false;

#define WIFI_PROV_SOFTAP_SSID_DEFAULT "Liwaisi-Config"
#define WIFI_PROV_SOFTAP_PASSWORD NULL
#define WIFI_PROV_SECURITY_POP_DEFAULT "liwaisi2025"

static void wifi_prov_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "Provisioning started");
                s_prov_manager.state = WIFI_PROV_STATE_PROVISIONING;
                s_prov_manager.provisioning_active = true;
                esp_event_post(WIFI_PROV_EVENTS, WIFI_PROV_EVENT_STARTED, NULL, 0, portMAX_DELAY);
                break;
                
            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGI(TAG, "Received WiFi credentials for SSID: %s", wifi_sta_cfg->ssid);
                
                strncpy(s_prov_manager.ssid, (char *)wifi_sta_cfg->ssid, sizeof(s_prov_manager.ssid) - 1);
                s_prov_manager.ssid[sizeof(s_prov_manager.ssid) - 1] = '\0';
                
                esp_event_post(WIFI_PROV_EVENTS, WIFI_PROV_EVENT_CREDENTIALS_RECEIVED, 
                               wifi_sta_cfg, sizeof(wifi_sta_config_t), portMAX_DELAY);
                break;
            }
            
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "WiFi credentials validation successful");
                s_prov_manager.state = WIFI_PROV_STATE_CONNECTED;
                esp_event_post(WIFI_PROV_EVENTS, WIFI_PROV_EVENT_CREDENTIALS_SUCCESS, NULL, 0, portMAX_DELAY);
                break;
                
            case WIFI_PROV_CRED_FAIL:
                ESP_LOGE(TAG, "WiFi credentials validation failed");
                esp_event_post(WIFI_PROV_EVENTS, WIFI_PROV_EVENT_CREDENTIALS_FAILED, NULL, 0, portMAX_DELAY);
                break;
                
            case WIFI_PROV_END:
                ESP_LOGI(TAG, "Provisioning ended");
                s_prov_manager.provisioning_active = false;
                s_prov_manager.provisioned = true;
                
                boot_counter_set_normal_operation();
                
                esp_event_post(WIFI_PROV_EVENTS, WIFI_PROV_EVENT_COMPLETED, NULL, 0, portMAX_DELAY);
                break;
                
            default:
                ESP_LOGW(TAG, "Unknown provisioning event: %ld", event_id);
                break;
        }
    }
}

esp_err_t wifi_prov_manager_init(void)
{
    if (s_prov_manager_initialized) {
        ESP_LOGW(TAG, "Provisioning manager already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing WiFi provisioning manager");
    
    esp_err_t ret = boot_counter_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize boot counter: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = boot_counter_increment();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to increment boot counter: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &wifi_prov_event_handler, NULL));
    
    s_prov_manager.state = WIFI_PROV_STATE_INIT;
    s_prov_manager.provisioned = false;
    s_prov_manager.provisioning_active = false;
    
    esp_read_mac(s_prov_manager.mac_address, ESP_MAC_WIFI_STA);
    
    s_prov_manager_initialized = true;
    
    ESP_LOGI(TAG, "WiFi provisioning manager initialized successfully");
    return ESP_OK;
}

esp_err_t wifi_prov_manager_start(void)
{
    if (!s_prov_manager_initialized) {
        ESP_LOGE(TAG, "Provisioning manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting WiFi provisioning");
    
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_softap,
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
        .app_event_handler = WIFI_PROV_EVENT_HANDLER_NONE
    };
    
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));
    
    const char *service_name = WIFI_PROV_SOFTAP_SSID_DEFAULT;
    const char *service_key = WIFI_PROV_SOFTAP_PASSWORD;
    wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
    const char *pop = WIFI_PROV_SECURITY_POP_DEFAULT;
    
    ESP_LOGI(TAG, "Starting provisioning with service name: %s", service_name);
    ESP_LOGI(TAG, "Proof of possession: %s", pop);
    
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, pop, service_name, service_key));
    
    s_prov_manager.state = WIFI_PROV_STATE_PROVISIONING;
    s_prov_manager.provisioning_active = true;
    
    return ESP_OK;
}

esp_err_t wifi_prov_manager_stop(void)
{
    if (!s_prov_manager_initialized) {
        ESP_LOGE(TAG, "Provisioning manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Stopping WiFi provisioning");
    
    if (s_prov_manager.provisioning_active) {
        wifi_prov_mgr_stop_provisioning();
        s_prov_manager.provisioning_active = false;
    }
    
    return ESP_OK;
}

esp_err_t wifi_prov_manager_deinit(void)
{
    if (!s_prov_manager_initialized) {
        ESP_LOGW(TAG, "Provisioning manager not initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Deinitializing WiFi provisioning manager");
    
    wifi_prov_manager_stop();
    
    wifi_prov_mgr_deinit();
    
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &wifi_prov_event_handler));
    
    s_prov_manager_initialized = false;
    
    return ESP_OK;
}

esp_err_t wifi_prov_manager_is_provisioned(bool *provisioned)
{
    if (!s_prov_manager_initialized) {
        ESP_LOGE(TAG, "Provisioning manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (provisioned == NULL) {
        ESP_LOGE(TAG, "Invalid parameter: provisioned is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = wifi_prov_mgr_is_provisioned(provisioned);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to check provisioning status: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_prov_manager.provisioned = *provisioned;
    
    ESP_LOGI(TAG, "Device provisioning status: %s", *provisioned ? "provisioned" : "not provisioned");
    
    return ESP_OK;
}

esp_err_t wifi_prov_manager_reset_credentials(void)
{
    if (!s_prov_manager_initialized) {
        ESP_LOGE(TAG, "Provisioning manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Resetting WiFi credentials");
    
    esp_err_t ret = wifi_prov_mgr_reset_provisioning();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset provisioning: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_prov_manager.provisioned = false;
    s_prov_manager.state = WIFI_PROV_STATE_INIT;
    memset(s_prov_manager.ssid, 0, sizeof(s_prov_manager.ssid));
    
    return ESP_OK;
}

esp_err_t wifi_prov_manager_wait_for_completion(void)
{
    if (!s_prov_manager_initialized) {
        ESP_LOGE(TAG, "Provisioning manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Waiting for provisioning completion");
    
    while (s_prov_manager.provisioning_active) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    ESP_LOGI(TAG, "Provisioning completed");
    
    return ESP_OK;
}

wifi_prov_state_t wifi_prov_manager_get_state(void)
{
    return s_prov_manager.state;
}

esp_err_t wifi_prov_manager_get_config(wifi_config_t *config)
{
    if (!s_prov_manager_initialized) {
        ESP_LOGE(TAG, "Provisioning manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid parameter: config is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_prov_manager.provisioned) {
        ESP_LOGE(TAG, "Device not provisioned");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = esp_wifi_get_config(WIFI_IF_STA, config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get WiFi config: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}