#include "wifi_prov_manager.h"
#include "boot_counter.h"
#include "wifi_connection_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/ip4_addr.h"
#include <string.h>
#include <sys/param.h>

static const char *TAG = "wifi_prov_manager";

ESP_EVENT_DEFINE_BASE(WIFI_PROV_EVENTS);

static wifi_prov_manager_t s_prov_manager = {0};
static bool s_prov_manager_initialized = false;
static httpd_handle_t s_server = NULL;
static esp_netif_t *s_ap_netif = NULL;

#ifndef CONFIG_WIFI_PROV_SOFTAP_SSID
#define CONFIG_WIFI_PROV_SOFTAP_SSID "Liwaisi-Config"
#endif

#define WIFI_PROV_SOFTAP_SSID_DEFAULT CONFIG_WIFI_PROV_SOFTAP_SSID
#define WIFI_PROV_SOFTAP_PASSWORD NULL
#define WIFI_PROV_AP_MAX_STA_CONN 4

// HTML webpage for WiFi configuration
static const char html_page[] = 
"<!DOCTYPE html>"
"<html><head><title>Liwaisi WiFi Setup</title>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"<style>"
"body{font-family:Arial,sans-serif;margin:0;padding:20px;background:#f0f0f0;}"
".container{max-width:400px;margin:0 auto;background:white;padding:30px;border-radius:10px;box-shadow:0 0 10px rgba(0,0,0,0.1);}"
"h1{color:#2c3e50;text-align:center;margin-bottom:30px;}"
".form-group{margin-bottom:20px;}"
"label{display:block;margin-bottom:5px;font-weight:bold;color:#34495e;}"
"input,select{width:100%;padding:10px;border:1px solid #ddd;border-radius:5px;font-size:16px;box-sizing:border-box;}"
"button{width:100%;background:#3498db;color:white;padding:12px;border:none;border-radius:5px;font-size:16px;cursor:pointer;margin-top:10px;}"
"button:hover{background:#2980b9;}"
".scan-btn{background:#27ae60;margin-bottom:10px;}"
".scan-btn:hover{background:#229954;}"
"#networks{margin-top:10px;}"
".network{padding:10px;border:1px solid #ddd;margin:5px 0;border-radius:5px;cursor:pointer;}"
".network:hover{background:#ecf0f1;}"
".status{text-align:center;margin-top:20px;padding:10px;border-radius:5px;}"
".success{background:#d5edda;color:#155724;border:1px solid #c3e6cb;}"
".error{background:#f8d7da;color:#721c24;border:1px solid #f5c6cb;}"
".info{background:#d1ecf1;color:#0c5460;border:1px solid #bee5eb;}"
"</style></head><body>"
"<div class='container'>"
"<h1>🌱 Liwaisi Smart Irrigation</h1>"
"<h2>WiFi Configuration</h2>"
"<div class='form-group'>"
"<button class='scan-btn' onclick='scanNetworks()'>🔍 Scan WiFi Networks</button>"
"<div id='networks'></div>"
"</div>"
"<form id='wifiForm'>"
"<div class='form-group'>"
"<label for='ssid'>Network Name (SSID):</label>"
"<input type='text' id='ssid' name='ssid' required>"
"</div>"
"<div class='form-group'>"
"<label for='password'>Password:</label>"
"<input type='password' id='password' name='password'>"
"</div>"
"<button type='submit'>🔗 Connect WiFi</button>"
"</form>"
"<div id='status'></div>"
"</div>"
"<script>"
"function scanNetworks(){"
"document.getElementById('status').innerHTML='<div class=\"info\">Scanning networks...</div>';"
"fetch('/scan').then(r=>r.json()).then(data=>{"
"let html='';"
"data.networks.forEach(net=>{"
"html+=`<div class=\"network\" onclick=\"selectNetwork('${net.ssid}','${net.auth}')\">`;"
"html+=`📶 ${net.ssid} ${net.auth!='open'?'🔒':''}</div>`;"
"});"
"document.getElementById('networks').innerHTML=html;"
"document.getElementById('status').innerHTML='<div class=\"success\">Found '+data.networks.length+' networks</div>';"
"}).catch(e=>{"
"document.getElementById('status').innerHTML='<div class=\"error\">Scan failed</div>';"
"});}"
"function selectNetwork(ssid,auth){"
"document.getElementById('ssid').value=ssid;"
"document.getElementById('password').focus();"
"}"
"document.getElementById('wifiForm').onsubmit=function(e){"
"e.preventDefault();"
"let ssid=document.getElementById('ssid').value;"
"let password=document.getElementById('password').value;"
"if(!ssid){alert('Please enter network name');return;}"
"document.getElementById('status').innerHTML='<div class=\"info\">Connecting to '+ssid+'...</div>';"
"fetch('/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'ssid='+encodeURIComponent(ssid)+'&password='+encodeURIComponent(password)})"
".then(r=>r.json()).then(data=>{"
"if(data.success){"
"document.getElementById('status').innerHTML='<div class=\"success\">✅ Connected successfully!<br>Device will restart and connect to your WiFi.</div>';"
"setTimeout(()=>{window.location.href='/';},3000);"
"}else{"
"document.getElementById('status').innerHTML='<div class=\"error\">❌ Connection failed: '+data.message+'</div>';"
"}"
"}).catch(e=>{"
"document.getElementById('status').innerHTML='<div class=\"error\">❌ Connection failed</div>';"
"});}"
"</script></body></html>";

static wifi_config_t s_received_wifi_config = {0};

// HTTP handler for main page
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// HTTP handler for WiFi scan
static esp_err_t scan_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Starting WiFi scan for web interface");
    
    // Start WiFi scan
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300
    };
    
    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(ret));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "{\"error\":\"Scan failed\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    
    // Get scan results
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    
    wifi_ap_record_t *ap_list = malloc(ap_count * sizeof(wifi_ap_record_t));
    if (ap_list == NULL) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "{\"error\":\"Memory allocation failed\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    
    esp_wifi_scan_get_ap_records(&ap_count, ap_list);
    
    // Build JSON response
    char *json_response = malloc(4096);
    if (json_response == NULL) {
        free(ap_list);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "{\"error\":\"Memory allocation failed\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    
    strcpy(json_response, "{\"networks\":[");
    
    for (int i = 0; i < ap_count && i < 20; i++) { // Limit to 20 networks
        char network_entry[200];
        const char *auth_mode = "open";
        
        if (ap_list[i].authmode != WIFI_AUTH_OPEN) {
            auth_mode = "secured";
        }
        
        snprintf(network_entry, sizeof(network_entry),
                "%s{\"ssid\": \"%s\", \"rssi\": %d, \"auth\": \"%s\"}",
                i > 0 ? "," : "",
                (char*)ap_list[i].ssid,
                ap_list[i].rssi,
                auth_mode);
        
        strcat(json_response, network_entry);
    }
    
    strcat(json_response, "]}");
    
    free(ap_list);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response, strlen(json_response));
    
    free(json_response);
    
    ESP_LOGI(TAG, "WiFi scan completed, found %d networks", ap_count);
    return ESP_OK;
}

// HTTP handler for WiFi connection
static esp_err_t connect_post_handler(httpd_req_t *req)
{
    char content[512];
    size_t recv_size = MIN(req->content_len, sizeof(content) - 1);
    
    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"success\":false,\"message\":\"Invalid request\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    
    content[ret] = '\0';
    ESP_LOGI(TAG, "Received WiFi config: %s", content);
    
    // Parse form data
    char ssid[33] = {0};
    char password[65] = {0};
    
    // Simple form parsing
    char *ssid_start = strstr(content, "ssid=");
    char *password_start = strstr(content, "password=");
    
    if (ssid_start) {
        ssid_start += 5; // Skip "ssid="
        char *ssid_end = strchr(ssid_start, '&');
        if (ssid_end) {
            size_t ssid_len = MIN(ssid_end - ssid_start, 32);
            strncpy(ssid, ssid_start, ssid_len);
        } else {
            strncpy(ssid, ssid_start, 32);
        }
        
        // URL decode SSID
        char decoded_ssid[33] = {0};
        int decoded_len = 0;
        for (int i = 0; ssid[i] && decoded_len < 32; i++) {
            if (ssid[i] == '+') {
                decoded_ssid[decoded_len++] = ' ';
            } else if (ssid[i] == '%' && ssid[i+1] && ssid[i+2]) {
                // URL decode %XX
                char hex[3] = {ssid[i+1], ssid[i+2], 0};
                decoded_ssid[decoded_len++] = (char)strtol(hex, NULL, 16);
                i += 2;
            } else {
                decoded_ssid[decoded_len++] = ssid[i];
            }
        }
        strcpy(ssid, decoded_ssid);
    }
    
    if (password_start) {
        password_start += 9; // Skip "password="
        char *password_end = strchr(password_start, '&');
        if (password_end) {
            size_t password_len = MIN(password_end - password_start, 64);
            strncpy(password, password_start, password_len);
        } else {
            strncpy(password, password_start, 64);
        }
        
        // URL decode password
        char decoded_password[65] = {0};
        int decoded_len = 0;
        for (int i = 0; password[i] && decoded_len < 64; i++) {
            if (password[i] == '+') {
                decoded_password[decoded_len++] = ' ';
            } else if (password[i] == '%' && password[i+1] && password[i+2]) {
                // URL decode %XX
                char hex[3] = {password[i+1], password[i+2], 0};
                decoded_password[decoded_len++] = (char)strtol(hex, NULL, 16);
                i += 2;
            } else {
                decoded_password[decoded_len++] = password[i];
            }
        }
        strcpy(password, decoded_password);
    }
    
    if (strlen(ssid) == 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":false,\"message\":\"SSID cannot be empty\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Attempting to connect to WiFi: %s", ssid);
    
    // Store WiFi config
    memset(&s_received_wifi_config, 0, sizeof(s_received_wifi_config));
    strncpy((char*)s_received_wifi_config.sta.ssid, ssid, sizeof(s_received_wifi_config.sta.ssid) - 1);
    strncpy((char*)s_received_wifi_config.sta.password, password, sizeof(s_received_wifi_config.sta.password) - 1);
    
    // Store credentials and mark as provisioned
    s_prov_manager.provisioned = true;
    s_prov_manager.state = WIFI_PROV_STATE_CONNECTED;
    strncpy(s_prov_manager.ssid, ssid, sizeof(s_prov_manager.ssid) - 1);
    
    // Respond success
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true,\"message\":\"Configuration saved\"}", HTTPD_RESP_USE_STRLEN);
    
    // Post events
    esp_event_post(WIFI_PROV_EVENTS, WIFI_PROV_EVENT_CREDENTIALS_RECEIVED, &s_received_wifi_config.sta, sizeof(wifi_sta_config_t), portMAX_DELAY);
    esp_event_post(WIFI_PROV_EVENTS, WIFI_PROV_EVENT_CREDENTIALS_SUCCESS, NULL, 0, portMAX_DELAY);
    
    // Delay then post completion event
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_event_post(WIFI_PROV_EVENTS, WIFI_PROV_EVENT_COMPLETED, NULL, 0, portMAX_DELAY);
    
    return ESP_OK;
}

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
    
    ESP_LOGI(TAG, "Starting web-based WiFi provisioning");
    
    // Create network interfaces if not exist
    if (s_ap_netif == NULL) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (s_ap_netif == NULL) {
            ESP_LOGE(TAG, "Failed to create AP network interface");
            return ESP_FAIL;
        }
    }
    
    // Note: STA netif is already created by wifi_connection_manager
    // We don't need to create another one, just use the existing one for scanning
    
    // Configure WiFi AP
    wifi_config_t ap_config = {
        .ap = {
            .ssid_len = strlen(WIFI_PROV_SOFTAP_SSID_DEFAULT),
            .channel = 1,
            .max_connection = WIFI_PROV_AP_MAX_STA_CONN,
            .authmode = WIFI_AUTH_OPEN,
            .ssid_hidden = 0,
            .beacon_interval = 100
        }
    };
    
    strncpy((char*)ap_config.ap.ssid, WIFI_PROV_SOFTAP_SSID_DEFAULT, sizeof(ap_config.ap.ssid) - 1);
    
    // Stop WiFi first, then reconfigure for AP+STA mode
    esp_wifi_stop();
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // Configure IP settings for AP
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(s_ap_netif));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(s_ap_netif, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(s_ap_netif));
    
    // Start HTTP server
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.lru_purge_enable = true;
    server_config.max_uri_handlers = 8;
    
    esp_err_t ret = httpd_start(&s_server, &server_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Register URI handlers
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &root_uri);
    
    httpd_uri_t scan_uri = {
        .uri = "/scan",
        .method = HTTP_GET,
        .handler = scan_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &scan_uri);
    
    httpd_uri_t connect_uri = {
        .uri = "/connect",
        .method = HTTP_POST,
        .handler = connect_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &connect_uri);
    
    s_prov_manager.state = WIFI_PROV_STATE_PROVISIONING;
    s_prov_manager.provisioning_active = true;
    
    ESP_LOGI(TAG, "Web-based provisioning started");
    ESP_LOGI(TAG, "AP SSID: %s", WIFI_PROV_SOFTAP_SSID_DEFAULT);
    ESP_LOGI(TAG, "Web interface: http://192.168.4.1");
    
    // Post start event
    esp_event_post(WIFI_PROV_EVENTS, WIFI_PROV_EVENT_STARTED, NULL, 0, portMAX_DELAY);
    
    return ESP_OK;
}

esp_err_t wifi_prov_manager_stop(void)
{
    if (!s_prov_manager_initialized) {
        ESP_LOGE(TAG, "Provisioning manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Stopping web-based WiFi provisioning");
    
    if (s_prov_manager.provisioning_active) {
        // Stop HTTP server
        if (s_server) {
            httpd_stop(s_server);
            s_server = NULL;
        }
        
        // Transition WiFi from APSTA mode back to STA mode
        esp_wifi_stop();
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
        
        s_prov_manager.provisioning_active = false;
        s_prov_manager.state = WIFI_PROV_STATE_INIT;
        
        ESP_LOGI(TAG, "WiFi transitioned back to STA mode for normal operation");
    }
    
    return ESP_OK;
}

esp_err_t wifi_prov_manager_deinit(void)
{
    if (!s_prov_manager_initialized) {
        ESP_LOGW(TAG, "Provisioning manager not initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Deinitializing web-based WiFi provisioning manager");
    
    wifi_prov_manager_stop();
    
    // Destroy AP network interface (STA is managed by wifi_connection_manager)
    if (s_ap_netif) {
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
    }
    
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
    
    // Check ESP-IDF's actual WiFi credential storage instead of just internal state
    wifi_config_t stored_wifi_cfg;
    esp_err_t ret = esp_wifi_get_config(WIFI_IF_STA, &stored_wifi_cfg);
    
    if (ret == ESP_OK && strlen((char*)stored_wifi_cfg.sta.ssid) > 0) {
        // We have valid credentials stored in ESP-IDF's NVS
        *provisioned = true;
        
        // Update internal state to match the stored credentials
        s_received_wifi_config = stored_wifi_cfg;
        s_prov_manager.provisioned = true;
        strncpy(s_prov_manager.ssid, (char*)stored_wifi_cfg.sta.ssid, sizeof(s_prov_manager.ssid) - 1);
        s_prov_manager.ssid[sizeof(s_prov_manager.ssid) - 1] = '\0';
        
        ESP_LOGI(TAG, "Device provisioning status: provisioned (from ESP-IDF storage)");
        ESP_LOGI(TAG, "Stored SSID: '%s' (length: %d)", stored_wifi_cfg.sta.ssid, strlen((char*)stored_wifi_cfg.sta.ssid));
    } else {
        // No valid credentials found in storage
        *provisioned = false;
        s_prov_manager.provisioned = false;
        memset(&s_received_wifi_config, 0, sizeof(s_received_wifi_config));
        memset(s_prov_manager.ssid, 0, sizeof(s_prov_manager.ssid));
        
        ESP_LOGI(TAG, "Device provisioning status: not provisioned (no stored credentials)");
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to get WiFi config from storage: %s", esp_err_to_name(ret));
        }
    }
    
    return ESP_OK;
}

esp_err_t wifi_prov_manager_reset_credentials(void)
{
    if (!s_prov_manager_initialized) {
        ESP_LOGE(TAG, "Provisioning manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Resetting web-based WiFi credentials");
    
    // Clear stored credentials
    memset(&s_received_wifi_config, 0, sizeof(s_received_wifi_config));
    
    s_prov_manager.provisioned = false;
    s_prov_manager.state = WIFI_PROV_STATE_INIT;
    memset(s_prov_manager.ssid, 0, sizeof(s_prov_manager.ssid));
    
    ESP_LOGI(TAG, "WiFi credentials reset successfully");
    
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
    
    // Return the credentials captured from web interface
    *config = s_received_wifi_config;
    
    ESP_LOGI(TAG, "Returning stored WiFi config for SSID: %s", config->sta.ssid);
    
    return ESP_OK;
}