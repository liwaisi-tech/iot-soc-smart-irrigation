#include "wifi_connection_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

#ifndef CONFIG_WIFI_PROV_CONNECTION_MAX_RETRIES
#define CONFIG_WIFI_PROV_CONNECTION_MAX_RETRIES 5
#endif

static const char *TAG = "wifi_connection_manager";

static wifi_connection_manager_t s_wifi_manager = {0};
static EventGroupHandle_t s_wifi_event_group = NULL;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

ESP_EVENT_DEFINE_BASE(WIFI_CONNECTION_EVENTS);

static esp_netif_t *s_sta_netif = NULL;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi station started");
        s_wifi_manager.state = WIFI_CONNECTION_STATE_CONNECTING;
        esp_wifi_connect();
        
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi station disconnected");
        s_wifi_manager.state = WIFI_CONNECTION_STATE_DISCONNECTED;
        s_wifi_manager.connected = false;
        s_wifi_manager.has_ip = false;
        
        if (s_wifi_manager.retry_count < s_wifi_manager.max_retries) {
            esp_wifi_connect();
            s_wifi_manager.retry_count++;
            ESP_LOGI(TAG, "Retry connection (%d/%d)", s_wifi_manager.retry_count, s_wifi_manager.max_retries);
        } else {
            ESP_LOGE(TAG, "WiFi connection failed after %d retries", s_wifi_manager.max_retries);
            s_wifi_manager.state = WIFI_CONNECTION_STATE_FAILED;
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            
            esp_event_post(WIFI_CONNECTION_EVENTS, WIFI_CONNECTION_EVENT_RETRY_EXHAUSTED, NULL, 0, portMAX_DELAY);
        }
        
        esp_event_post(WIFI_CONNECTION_EVENTS, WIFI_CONNECTION_EVENT_DISCONNECTED, NULL, 0, portMAX_DELAY);
        
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        
        s_wifi_manager.state = WIFI_CONNECTION_STATE_CONNECTED;
        s_wifi_manager.connected = true;
        s_wifi_manager.has_ip = true;
        s_wifi_manager.retry_count = 0;
        s_wifi_manager.ip_addr = event->ip_info.ip;
        
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        
        esp_event_post(WIFI_CONNECTION_EVENTS, WIFI_CONNECTION_EVENT_CONNECTED, NULL, 0, portMAX_DELAY);
        esp_event_post(WIFI_CONNECTION_EVENTS, WIFI_CONNECTION_EVENT_GOT_IP, &event->ip_info.ip, sizeof(esp_ip4_addr_t), portMAX_DELAY);
    }
}

esp_err_t wifi_connection_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi connection manager");
    
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi event group");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_ERROR_CHECK(esp_netif_init());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create default WiFi station interface");
        return ESP_FAIL;
    }
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    s_wifi_manager.state = WIFI_CONNECTION_STATE_IDLE;
    s_wifi_manager.connected = false;
    s_wifi_manager.has_ip = false;
    s_wifi_manager.retry_count = 0;
    s_wifi_manager.max_retries = CONFIG_WIFI_PROV_CONNECTION_MAX_RETRIES;
    
    esp_read_mac(s_wifi_manager.mac_address, ESP_MAC_WIFI_STA);
    
    ESP_LOGI(TAG, "WiFi connection manager initialized successfully");
    return ESP_OK;
}

esp_err_t wifi_connection_manager_start(void)
{
    ESP_LOGI(TAG, "Starting WiFi connection manager");
    
    ESP_ERROR_CHECK(esp_wifi_start());
    
    esp_event_post(WIFI_CONNECTION_EVENTS, WIFI_CONNECTION_EVENT_STARTED, NULL, 0, portMAX_DELAY);
    
    return ESP_OK;
}

esp_err_t wifi_connection_manager_stop(void)
{
    ESP_LOGI(TAG, "Stopping WiFi connection manager");
    
    ESP_ERROR_CHECK(esp_wifi_stop());
    
    s_wifi_manager.state = WIFI_CONNECTION_STATE_IDLE;
    s_wifi_manager.connected = false;
    s_wifi_manager.has_ip = false;
    s_wifi_manager.retry_count = 0;
    
    return ESP_OK;
}

esp_err_t wifi_connection_manager_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing WiFi connection manager");
    
    wifi_connection_manager_stop();
    
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler));
    
    ESP_ERROR_CHECK(esp_wifi_deinit());
    
    if (s_sta_netif) {
        esp_netif_destroy(s_sta_netif);
        s_sta_netif = NULL;
    }
    
    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    
    return ESP_OK;
}

esp_err_t wifi_connection_manager_connect(const wifi_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid parameter: config is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Connecting to WiFi network: %s", config->sta.ssid);
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, (wifi_config_t *)config));
    
    strncpy(s_wifi_manager.ssid, (char *)config->sta.ssid, sizeof(s_wifi_manager.ssid) - 1);
    s_wifi_manager.ssid[sizeof(s_wifi_manager.ssid) - 1] = '\0';
    
    s_wifi_manager.retry_count = 0;
    s_wifi_manager.state = WIFI_CONNECTION_STATE_CONNECTING;
    
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    
    ESP_ERROR_CHECK(esp_wifi_connect());
    
    return ESP_OK;
}

esp_err_t wifi_connection_manager_disconnect(void)
{
    ESP_LOGI(TAG, "Disconnecting from WiFi");
    
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    
    s_wifi_manager.state = WIFI_CONNECTION_STATE_DISCONNECTED;
    s_wifi_manager.connected = false;
    s_wifi_manager.has_ip = false;
    
    esp_event_post(WIFI_CONNECTION_EVENTS, WIFI_CONNECTION_EVENT_DISCONNECTED, NULL, 0, portMAX_DELAY);
    
    return ESP_OK;
}

esp_err_t wifi_connection_manager_reconnect(void)
{
    ESP_LOGI(TAG, "Reconnecting to WiFi");
    
    s_wifi_manager.retry_count = 0;
    ESP_ERROR_CHECK(esp_wifi_connect());
    
    return ESP_OK;
}

wifi_connection_state_t wifi_connection_manager_get_state(void)
{
    return s_wifi_manager.state;
}

bool wifi_connection_manager_is_connected(void)
{
    return s_wifi_manager.connected;
}

bool wifi_connection_manager_has_ip(void)
{
    return s_wifi_manager.has_ip;
}

esp_err_t wifi_connection_manager_get_ip(esp_ip4_addr_t *ip)
{
    if (ip == NULL) {
        ESP_LOGE(TAG, "Invalid parameter: ip is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_wifi_manager.has_ip) {
        ESP_LOGW(TAG, "No IP address available");
        return ESP_ERR_INVALID_STATE;
    }
    
    *ip = s_wifi_manager.ip_addr;
    return ESP_OK;
}

esp_err_t wifi_connection_manager_get_ssid(char *ssid, size_t ssid_len)
{
    if (ssid == NULL || ssid_len == 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    strncpy(ssid, s_wifi_manager.ssid, ssid_len - 1);
    ssid[ssid_len - 1] = '\0';
    
    return ESP_OK;
}

esp_err_t wifi_connection_manager_get_mac(uint8_t *mac)
{
    if (mac == NULL) {
        ESP_LOGE(TAG, "Invalid parameter: mac is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(mac, s_wifi_manager.mac_address, 6);
    return ESP_OK;
}

esp_err_t wifi_connection_manager_set_retry_config(int max_retries)
{
    if (max_retries < 0) {
        ESP_LOGE(TAG, "Invalid parameter: max_retries must be >= 0");
        return ESP_ERR_INVALID_ARG;
    }
    
    s_wifi_manager.max_retries = max_retries;
    ESP_LOGI(TAG, "Max retries set to: %d", max_retries);
    
    return ESP_OK;
}