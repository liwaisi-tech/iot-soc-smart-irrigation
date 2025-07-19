#include "whoami.h"
#include "../server.h"
#include "../middleware/logging_middleware.h"
#include "wifi_adapter.h"
#include "json_device_serializer.h"
#include "device_info.h"
#include "device_config_service.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "WHOAMI_ENDPOINT";

static esp_err_t get_device_info(device_info_t *device_info)
{
    if (device_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Get MAC address
    esp_err_t ret = wifi_adapter_get_mac(device_info->mac_address);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get MAC address: %s", esp_err_to_name(ret));
        // Set default MAC if WiFi not available
        memset(device_info->mac_address, 0, 6);
    }
    
    // Get IP address
    esp_ip4_addr_t ip;
    ret = wifi_adapter_get_ip(&ip);
    if (ret == ESP_OK) {
        // Convert from network byte order to host byte order for proper string conversion
        device_info->ip_address = ((ip.addr & 0xFF) << 24) | 
                                  (((ip.addr >> 8) & 0xFF) << 16) | 
                                  (((ip.addr >> 16) & 0xFF) << 8) | 
                                  ((ip.addr >> 24) & 0xFF);
    } else {
        ESP_LOGW(TAG, "Failed to get IP address: %s", esp_err_to_name(ret));
        device_info->ip_address = 0; // No IP available
    }
    
    // Get device name from configuration service
    ret = device_config_service_get_name(device_info->device_name, sizeof(device_info->device_name));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get device name from config: %s", esp_err_to_name(ret));
        strncpy(device_info->device_name, "Smart Irrigation System", sizeof(device_info->device_name) - 1);
        device_info->device_name[sizeof(device_info->device_name) - 1] = '\0';
    }
    
    strncpy(device_info->crop_name, "IoT", sizeof(device_info->crop_name) - 1);
    device_info->crop_name[sizeof(device_info->crop_name) - 1] = '\0';
    
    strncpy(device_info->firmware_version, "v1.0.0", sizeof(device_info->firmware_version) - 1);
    device_info->firmware_version[sizeof(device_info->firmware_version) - 1] = '\0';
    
    return ESP_OK;
}

esp_err_t whoami_get_handler(httpd_req_t *req)
{
    // Initialize request context for enhanced logging
    http_request_context_t log_context = http_middleware_init_request_context(req);
    http_middleware_log_request_start(req, &log_context, &HTTP_LOGGING_CONFIG_DEFAULT);
    
    // Get real device information
    device_info_t device_info;
    esp_err_t ret = get_device_info(&device_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get device info: %s", esp_err_to_name(ret));
        http_middleware_log_request_end(req, &log_context, 500, &HTTP_LOGGING_CONFIG_DEFAULT);
        return http_response_send_error(req, 500, "Internal Server Error", "Failed to retrieve device information");
    }
    
    // Convert MAC and IP to strings
    char mac_string[18];
    char ip_string[16];
    
    ret = mac_to_string(device_info.mac_address, mac_string, sizeof(mac_string));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to convert MAC to string: %s", esp_err_to_name(ret));
        strncpy(mac_string, "00:00:00:00:00:00", sizeof(mac_string));
    }
    
    ret = ip_to_string(device_info.ip_address, ip_string, sizeof(ip_string));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to convert IP to string: %s", esp_err_to_name(ret));
        strncpy(ip_string, "0.0.0.0", sizeof(ip_string));
    }
    
    // Get device location from configuration service
    char device_location[DEVICE_LOCATION_MAX_LEN + 1];
    ret = device_config_service_get_location(device_location, sizeof(device_location));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get device location from config: %s", esp_err_to_name(ret));
        strncpy(device_location, "Not configured", sizeof(device_location) - 1);
        device_location[sizeof(device_location) - 1] = '\0';
    }
    
    // Build JSON response with real device information
    char response_buffer[768];
    int written = snprintf(response_buffer, sizeof(response_buffer),
        "{"
        "\"device\":{"
        "\"name\":\"%s\","
        "\"location\":\"%s\","
        "\"version\":\"%s\","
        "\"mac_address\":\"%s\","
        "\"ip_address\":\"%s\","
        "\"crop_name\":\"%s\""
        "},"
        "\"endpoints\":["
        "{"
        "\"path\":\"/whoami\","
        "\"method\":\"GET\","
        "\"description\":\"Device information and available endpoints\""
        "},"
        "{"
        "\"path\":\"/config\","
        "\"method\":\"POST\","
        "\"description\":\"Save device name, location and WiFi configuration\""
        "}"
        "]"
        "}",
        device_info.device_name,
        device_location,
        device_info.firmware_version,
        mac_string,
        ip_string,
        device_info.crop_name
    );
    
    if (written < 0 || written >= sizeof(response_buffer)) {
        ESP_LOGE(TAG, "JSON response buffer overflow");
        http_middleware_log_request_end(req, &log_context, 500, &HTTP_LOGGING_CONFIG_DEFAULT);
        return http_response_send_error(req, 500, "Internal Server Error", "Response buffer overflow");
    }
    
    esp_err_t response_ret = http_response_send_json(req, response_buffer);
    http_middleware_log_request_end(req, &log_context, (response_ret == ESP_OK ? 200 : 500), &HTTP_LOGGING_CONFIG_DEFAULT);
    
    return response_ret;
}

esp_err_t whoami_register_endpoints(httpd_handle_t server)
{
    if (server == NULL) {
        ESP_LOGE(TAG, "Server handle is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    httpd_uri_t whoami_uri = {
        .uri = "/whoami",
        .method = HTTP_GET,
        .handler = whoami_get_handler,
        .user_ctx = NULL
    };
    
    esp_err_t ret = httpd_register_uri_handler(server, &whoami_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error registering /whoami handler: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Registered endpoint: GET /whoami");
    return ESP_OK;
}