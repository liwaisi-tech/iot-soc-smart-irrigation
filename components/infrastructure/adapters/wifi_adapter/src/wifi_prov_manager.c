#include "wifi_prov_manager.h"
#include "boot_counter.h"
#include "wifi_connection_manager.h"
#include "device_config_service.h"
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

// Minified HTML for WiFi configuration (~4KB saved)
static const char html_page[] =
"<!DOCTYPE html><html><head><title>Config Liwaisi</title><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<style>body{font-family:Arial;margin:0;padding:10px;background:#f0f0f0}.c{max-width:400px;margin:0 auto;background:#fff;padding:20px;border-radius:5px;box-shadow:0 0 5px rgba(0,0,0,.1)}"
"h1{color:#2c3e50;text-align:center;margin-bottom:20px}.g{margin-bottom:15px}label{display:block;margin-bottom:5px;font-weight:bold;color:#34495e}"
"input,textarea{width:100%;padding:8px;border:1px solid #ddd;border-radius:3px;font-size:16px;box-sizing:border-box}button{width:100%;background:#3498db;color:#fff;padding:10px;border:none;border-radius:3px;font-size:16px;cursor:pointer;margin-top:10px}"
"button:hover{background:#2980b9}.scan{background:#27ae60;margin-bottom:10px}.scan:hover{background:#229954}#networks{margin-top:10px}.net{padding:8px;border:1px solid #ddd;margin:3px 0;border-radius:3px;cursor:pointer}.net:hover{background:#ecf0f1}"
".status{text-align:center;margin-top:15px;padding:8px;border-radius:3px}.success{background:#d5edda;color:#155724}.error{background:#f8d7da;color:#721c24}.info{background:#d1ecf1;color:#0c5460}"
"</style></head><body><div class='c'><h1>🌱 Config Liwaisi</h1><div class='g'><button class='scan' onclick='scan()'>🔍 Scan WiFi</button><div id='nets'></div></div>"
"<form id='form'><div class='g'><label>Dispositivo:</label><input id='dev' maxlength='50' value='Liwaisi Smart Irrigation' required></div>"
"<div class='g'><label>Ubicación:</label><textarea id='loc' maxlength='150' rows='2' required></textarea></div>"
"<div class='g'><label>Red (SSID):</label><input id='ssid' required></div><div class='g'><label>Contraseña:</label><input type='password' id='pass'></div>"
"<button type='submit'>💾 Guardar</button></form><div id='status'></div></div>"
"<script>function scan(){document.getElementById('status').innerHTML='<div class=\"info\">Escaneando...</div>';fetch('/scan').then(r=>r.json()).then(d=>{let h='';d.networks.forEach(n=>{h+=`<div class=\"net\" onclick=\"sel('${n.ssid}')\">${n.ssid} ${n.auth!='open'?'🔒':''}</div>`;});document.getElementById('nets').innerHTML=h;document.getElementById('status').innerHTML='<div class=\"success\">'+d.networks.length+' redes</div>';}).catch(e=>{document.getElementById('status').innerHTML='<div class=\"error\">Fallo</div>';})}function sel(s){document.getElementById('ssid').value=s;document.getElementById('pass').focus()}document.getElementById('form').onsubmit=function(e){e.preventDefault();let d=document.getElementById('dev').value,l=document.getElementById('loc').value,s=document.getElementById('ssid').value,p=document.getElementById('pass').value;if(!d||!l||!s){alert('Complete todos los campos');return}document.getElementById('status').innerHTML='<div class=\"info\">⏳ Validando WiFi...</div>';fetch('/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'device_name='+encodeURIComponent(d)+'&device_location='+encodeURIComponent(l)+'&ssid='+encodeURIComponent(s)+'&password='+encodeURIComponent(p)}).then(r=>r.json()).then(d=>{if(d.success){document.getElementById('status').innerHTML='<div class=\"success\">✅ WiFi configurado! Reiniciando...</div>';setTimeout(()=>window.location.href='/',3000)}else{let msg=d.message||'Error desconocido';if(msg.includes('incorrecta')){document.getElementById('status').innerHTML='<div class=\"error\">❌ Contraseña WiFi incorrecta</div>'}else if(msg.includes('no encontrada')){document.getElementById('status').innerHTML='<div class=\"error\">❌ Red WiFi no encontrada</div>'}else if(msg.includes('Timeout')){document.getElementById('status').innerHTML='<div class=\"error\">❌ Conexión lenta - intente de nuevo</div>'}else{document.getElementById('status').innerHTML='<div class=\"error\">❌ Error: '+msg+'</div>'}document.getElementById('pass').focus()}}).catch(e=>{document.getElementById('status').innerHTML='<div class=\"error\">❌ Error de conexión</div>'})}</script></body></html>";

static wifi_config_t s_received_wifi_config = {0};
static wifi_validation_result_t s_validation_result = WIFI_VALIDATION_OK;
static EventGroupHandle_t s_validation_event_group = NULL;

#define VALIDATION_SUCCESS_BIT BIT0
#define VALIDATION_FAILED_BIT BIT1

// Consolidated URL decode helper function (~2.5KB saved)
static void url_decode(const char* src, char* dst, size_t dst_size) {
    int decoded_len = 0;
    for (int i = 0; src[i] && decoded_len < (dst_size - 1); i++) {
        if (src[i] == '+') {
            dst[decoded_len++] = ' ';
        } else if (src[i] == '%' && src[i+1] && src[i+2]) {
            char hex[3] = {src[i+1], src[i+2], 0};
            dst[decoded_len++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else {
            dst[decoded_len++] = src[i];
        }
    }
    dst[decoded_len] = '\0';
}

// WiFi connection event handler for credential validation
static void wifi_validation_event_handler(void* arg, esp_event_base_t event_base,
                                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_CONNECTION_EVENTS) {
        switch (event_id) {
            case WIFI_CONNECTION_EVENT_CONNECTED:
                ESP_LOGD(TAG, "Validation: WiFi connected successfully");
                s_validation_result = WIFI_VALIDATION_OK;
                xEventGroupSetBits(s_validation_event_group, VALIDATION_SUCCESS_BIT);
                break;

            case WIFI_CONNECTION_EVENT_AUTH_FAILED:
                ESP_LOGW(TAG, "Validation: WiFi authentication failed");
                s_validation_result = WIFI_VALIDATION_AUTH_FAILED;
                xEventGroupSetBits(s_validation_event_group, VALIDATION_FAILED_BIT);
                break;

            case WIFI_CONNECTION_EVENT_NETWORK_NOT_FOUND:
                ESP_LOGW(TAG, "Validation: WiFi network not found");
                s_validation_result = WIFI_VALIDATION_NETWORK_NOT_FOUND;
                xEventGroupSetBits(s_validation_event_group, VALIDATION_FAILED_BIT);
                break;

            case WIFI_CONNECTION_EVENT_RETRY_EXHAUSTED:
                ESP_LOGW(TAG, "Validation: WiFi connection timeout");
                s_validation_result = WIFI_VALIDATION_TIMEOUT;
                xEventGroupSetBits(s_validation_event_group, VALIDATION_FAILED_BIT);
                break;

            default:
                break;
        }
    }
}

// Validate WiFi credentials by attempting actual connection
esp_err_t wifi_prov_manager_validate_credentials(const char* ssid, const char* password, wifi_validation_result_t* result)
{
    if (!ssid || !result) {
        ESP_LOGE(TAG, "Invalid parameters for credential validation");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Validating WiFi credentials for SSID: %s", ssid);

    // Create validation event group if needed
    if (s_validation_event_group == NULL) {
        s_validation_event_group = xEventGroupCreate();
        if (s_validation_event_group == NULL) {
            ESP_LOGE(TAG, "Failed to create validation event group");
            return ESP_ERR_NO_MEM;
        }
    }

    // Register temporary event handler for validation
    esp_err_t ret = esp_event_handler_register(WIFI_CONNECTION_EVENTS, ESP_EVENT_ANY_ID,
                                               &wifi_validation_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register validation event handler: %s", esp_err_to_name(ret));
        return ret;
    }

    // Prepare WiFi config for validation
    wifi_config_t validation_config = {0};
    strncpy((char*)validation_config.sta.ssid, ssid, sizeof(validation_config.sta.ssid) - 1);
    if (password && strlen(password) > 0) {
        strncpy((char*)validation_config.sta.password, password, sizeof(validation_config.sta.password) - 1);
    }

    // Reset validation state
    s_validation_result = WIFI_VALIDATION_OK;
    xEventGroupClearBits(s_validation_event_group, VALIDATION_SUCCESS_BIT | VALIDATION_FAILED_BIT);

    // Attempt to connect to validate credentials
    ret = wifi_connection_manager_connect(&validation_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start validation connection: %s", esp_err_to_name(ret));
        esp_event_handler_unregister(WIFI_CONNECTION_EVENTS, ESP_EVENT_ANY_ID, &wifi_validation_event_handler);
        return ret;
    }

    // Wait for validation result with 15-second timeout (suitable for rural networks)
    EventBits_t bits = xEventGroupWaitBits(s_validation_event_group,
                                           VALIDATION_SUCCESS_BIT | VALIDATION_FAILED_BIT,
                                           pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(15000));

    // Unregister validation event handler
    esp_event_handler_unregister(WIFI_CONNECTION_EVENTS, ESP_EVENT_ANY_ID, &wifi_validation_event_handler);

    if (bits & VALIDATION_SUCCESS_BIT) {
        ESP_LOGI(TAG, "WiFi credential validation successful");
        *result = WIFI_VALIDATION_OK;
    } else if (bits & VALIDATION_FAILED_BIT) {
        ESP_LOGW(TAG, "WiFi credential validation failed: %d", s_validation_result);
        *result = s_validation_result;
    } else {
        ESP_LOGW(TAG, "WiFi credential validation timeout");
        *result = WIFI_VALIDATION_TIMEOUT;
        // Force disconnect to stop connection attempts
        wifi_connection_manager_disconnect();
    }

    return ESP_OK;
}

// HTTP handler for main page
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");

    // Send HTML in chunks to prevent buffer overflow
    const char* html = html_page;
    size_t html_len = strlen(html_page);
    size_t chunk_size = 1024;  // 1KB chunks
    size_t sent = 0;

    while (sent < html_len) {
        size_t to_send = (html_len - sent > chunk_size) ? chunk_size : (html_len - sent);
        esp_err_t ret = httpd_resp_send_chunk(req, html + sent, to_send);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send HTML chunk: %s", esp_err_to_name(ret));
            return ret;
        }
        sent += to_send;
    }

    // Send final chunk (empty) to indicate end
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// HTTP handler for WiFi scan
static esp_err_t scan_get_handler(httpd_req_t *req)
{
    ESP_LOGD(TAG, "Starting WiFi scan for web interface");

    // Check available heap before starting scan
    size_t free_heap = esp_get_free_heap_size();
    if (free_heap < 20000) {  // Require at least 20KB free
        ESP_LOGW(TAG, "Low memory before scan: %zu bytes", free_heap);
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Low memory\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // Start WiFi scan with timeout protection
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 500  // Reduced from 300 to 500ms for better reliability
    };

    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(ret));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Scan failed\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;  // Return ESP_OK to avoid connection close
    }

    // Get scan results with bounds checking
    uint16_t ap_count = 0;
    ret = esp_wifi_scan_get_ap_num(&ap_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get AP count: %s", esp_err_to_name(ret));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Scan result failed\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // Limit scan results to prevent memory issues
    if (ap_count > 15) {
        ESP_LOGW(TAG, "Too many APs found (%d), limiting to 15", ap_count);
        ap_count = 15;
    }
    
    // Safe memory allocation with NULL checks
    wifi_ap_record_t *ap_list = NULL;
    char *json_response = NULL;

    if (ap_count == 0) {
        ESP_LOGD(TAG, "No WiFi networks found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"networks\":[]}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    ap_list = malloc(ap_count * sizeof(wifi_ap_record_t));
    if (ap_list == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for AP list (%d APs)", ap_count);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Memory allocation failed\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    ret = esp_wifi_scan_get_ap_records(&ap_count, ap_list);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get AP records: %s", esp_err_to_name(ret));
        free(ap_list);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Scan records failed\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // Calculate required JSON buffer size more precisely
    size_t required_size = 50; // Base JSON structure
    for (int i = 0; i < ap_count && i < 15; i++) {
        required_size += strlen((char*)ap_list[i].ssid) + 100; // SSID + JSON overhead
    }

    // Allocate with safety margin
    json_response = malloc(required_size + 200);
    if (json_response == NULL) {
        ESP_LOGE(TAG, "Failed to allocate JSON buffer (%zu bytes)", required_size);
        free(ap_list);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Memory allocation failed\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    strcpy(json_response, "{\"networks\":[");

    for (int i = 0; i < ap_count && i < 15; i++) { // Limit to 15 networks
        char network_entry[256]; // Increased buffer size for safety
        const char *auth_mode = "open";

        // Validate SSID to prevent JSON injection
        if (strlen((char*)ap_list[i].ssid) == 0) {
            continue; // Skip hidden/empty SSIDs
        }

        if (ap_list[i].authmode != WIFI_AUTH_OPEN) {
            auth_mode = "secured";
        }

        // Safe JSON construction with bounds checking
        int written = snprintf(network_entry, sizeof(network_entry),
                              "%s{\"ssid\": \"%.*s\", \"rssi\": %d, \"auth\": \"%s\"}",
                              i > 0 ? "," : "",
                              32, (char*)ap_list[i].ssid,  // Limit SSID length
                              ap_list[i].rssi,
                              auth_mode);

        if (written >= sizeof(network_entry)) {
            ESP_LOGW(TAG, "Network entry truncated for SSID: %s", ap_list[i].ssid);
        }

        // Check if we have enough space in response buffer
        if (strlen(json_response) + strlen(network_entry) + 10 > required_size + 150) {
            ESP_LOGW(TAG, "Response buffer full, truncating at %d networks", i);
            break;
        }

        strcat(json_response, network_entry);
    }

    strcat(json_response, "]}");
    
    // Send response using chunked encoding for large responses
    httpd_resp_set_type(req, "application/json");

    size_t response_len = strlen(json_response);
    if (response_len > 1024) {
        // Send in chunks if response is large
        size_t sent = 0;
        size_t chunk_size = 512;
        while (sent < response_len) {
            size_t to_send = (response_len - sent > chunk_size) ? chunk_size : (response_len - sent);
            esp_err_t send_ret = httpd_resp_send_chunk(req, json_response + sent, to_send);
            if (send_ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send JSON chunk: %s", esp_err_to_name(send_ret));
                break;
            }
            sent += to_send;
        }
        httpd_resp_send_chunk(req, NULL, 0); // End chunked response
    } else {
        // Send as single response for small JSON
        httpd_resp_send(req, json_response, response_len);
    }

    // Cleanup resources
    free(ap_list);
    free(json_response);

    ESP_LOGD(TAG, "WiFi scan completed, found %d networks (sent %zu bytes)", ap_count, response_len);
    return ESP_OK;
}

// HTTP handler for WiFi connection
static esp_err_t connect_post_handler(httpd_req_t *req)
{
    // Check memory before processing request
    if (esp_get_free_heap_size() < 10000) {
        ESP_LOGW(TAG, "Low memory for connect request");
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":false,\"message\":\"Low memory\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char content[512];
    size_t recv_size = MIN(req->content_len, sizeof(content) - 1);
    
    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"success\":false,\"message\":\"Invalid request\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    
    content[ret] = '\0';
    ESP_LOGD(TAG, "Received WiFi config: %s", content);
    
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
        url_decode(ssid, decoded_ssid, sizeof(decoded_ssid));
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
        url_decode(password, decoded_password, sizeof(decoded_password));
        strcpy(password, decoded_password);
    }
    
    if (strlen(ssid) == 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":false,\"message\":\"SSID cannot be empty\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    ESP_LOGD(TAG, "Attempting to connect to WiFi: %s", ssid);
    
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

// HTTP handler for device configuration
static esp_err_t config_post_handler(httpd_req_t *req)
{
    // Check memory before processing request
    if (esp_get_free_heap_size() < 15000) {
        ESP_LOGW(TAG, "Low memory for config request");
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":false,\"message\":\"Low memory\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char content[1024];
    size_t recv_size = MIN(req->content_len, sizeof(content) - 1);
    
    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"success\":false,\"message\":\"Invalid request\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    
    content[ret] = '\0';
    ESP_LOGD(TAG, "Received device configuration: %s", content);
    
    // Parse form data
    char device_name[DEVICE_NAME_MAX_LEN + 1] = {0};
    char device_location[DEVICE_LOCATION_MAX_LEN + 1] = {0};
    char ssid[33] = {0};
    char password[65] = {0};
    
    // Parse device_name
    char *device_name_start = strstr(content, "device_name=");
    if (device_name_start) {
        device_name_start += 12; // Skip "device_name="
        char *device_name_end = strchr(device_name_start, '&');
        if (device_name_end) {
            size_t name_len = MIN(device_name_end - device_name_start, DEVICE_NAME_MAX_LEN);
            strncpy(device_name, device_name_start, name_len);
        } else {
            strncpy(device_name, device_name_start, DEVICE_NAME_MAX_LEN);
        }
        
        // URL decode device_name
        char decoded_name[DEVICE_NAME_MAX_LEN + 1] = {0};
        url_decode(device_name, decoded_name, sizeof(decoded_name));
        strcpy(device_name, decoded_name);
    }
    
    // Parse device_location
    char *device_location_start = strstr(content, "device_location=");
    if (device_location_start) {
        device_location_start += 16; // Skip "device_location="
        char *device_location_end = strchr(device_location_start, '&');
        if (device_location_end) {
            size_t location_len = MIN(device_location_end - device_location_start, DEVICE_LOCATION_MAX_LEN);
            strncpy(device_location, device_location_start, location_len);
        } else {
            strncpy(device_location, device_location_start, DEVICE_LOCATION_MAX_LEN);
        }
        
        // URL decode device_location
        char decoded_location[DEVICE_LOCATION_MAX_LEN + 1] = {0};
        url_decode(device_location, decoded_location, sizeof(decoded_location));
        strcpy(device_location, decoded_location);
    }
    
    // Parse SSID and password (reuse existing logic)
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
        url_decode(ssid, decoded_ssid, sizeof(decoded_ssid));
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
        url_decode(password, decoded_password, sizeof(decoded_password));
        strcpy(password, decoded_password);
    }
    
    // Validate required fields
    if (strlen(device_name) == 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":false,\"message\":\"Device name cannot be empty\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    if (strlen(device_location) == 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":false,\"message\":\"Device location cannot be empty\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    if (strlen(ssid) == 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":false,\"message\":\"SSID cannot be empty\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    ESP_LOGD(TAG, "Device configuration - Name: '%s', Location: '%s', WiFi: '%s'",
             device_name, device_location, ssid);

    // Save device configuration using the service
    device_config_t device_config;
    strncpy(device_config.device_name, device_name, DEVICE_NAME_MAX_LEN);
    device_config.device_name[DEVICE_NAME_MAX_LEN] = '\0';
    strncpy(device_config.device_location, device_location, DEVICE_LOCATION_MAX_LEN);
    device_config.device_location[DEVICE_LOCATION_MAX_LEN] = '\0';

    esp_err_t config_ret = device_config_service_set_config(&device_config);
    if (config_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save device configuration: %s", esp_err_to_name(config_ret));
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":false,\"message\":\"Failed to save device configuration\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // CRITICAL: Validate WiFi credentials before responding success
    ESP_LOGI(TAG, "Validating WiFi credentials before saving...");
    s_prov_manager.state = WIFI_PROV_STATE_VALIDATING;

    wifi_validation_result_t validation_result;
    esp_err_t validation_ret = wifi_prov_manager_validate_credentials(ssid, password, &validation_result);

    if (validation_ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi validation failed with error: %s", esp_err_to_name(validation_ret));
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":false,\"message\":\"Failed to validate WiFi credentials\"}", HTTPD_RESP_USE_STRLEN);
        s_prov_manager.state = WIFI_PROV_STATE_ERROR;
        return ESP_OK;
    }

    // Handle validation results with specific error messages
    const char* error_response = NULL;
    switch (validation_result) {
        case WIFI_VALIDATION_OK:
            ESP_LOGI(TAG, "WiFi credentials validated successfully");
            break;

        case WIFI_VALIDATION_AUTH_FAILED:
            ESP_LOGW(TAG, "WiFi authentication failed - incorrect password");
            error_response = "{\"success\":false,\"message\":\"Contraseña WiFi incorrecta\"}";
            break;

        case WIFI_VALIDATION_NETWORK_NOT_FOUND:
            ESP_LOGW(TAG, "WiFi network not found");
            error_response = "{\"success\":false,\"message\":\"Red WiFi no encontrada\"}";
            break;

        case WIFI_VALIDATION_TIMEOUT:
            ESP_LOGW(TAG, "WiFi connection timeout");
            error_response = "{\"success\":false,\"message\":\"Timeout de conexión WiFi\"}";
            break;

        default:
            ESP_LOGW(TAG, "WiFi validation failed with unknown error");
            error_response = "{\"success\":false,\"message\":\"Error de conexión WiFi\"}";
            break;
    }

    if (error_response) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, error_response, HTTPD_RESP_USE_STRLEN);
        s_prov_manager.state = WIFI_PROV_STATE_ERROR;

        // Post specific failure events
        switch (validation_result) {
            case WIFI_VALIDATION_AUTH_FAILED:
                esp_event_post(WIFI_PROV_EVENTS, WIFI_PROV_EVENT_AUTH_FAILED, NULL, 0, portMAX_DELAY);
                break;
            case WIFI_VALIDATION_NETWORK_NOT_FOUND:
                esp_event_post(WIFI_PROV_EVENTS, WIFI_PROV_EVENT_NETWORK_NOT_FOUND, NULL, 0, portMAX_DELAY);
                break;
            case WIFI_VALIDATION_TIMEOUT:
                esp_event_post(WIFI_PROV_EVENTS, WIFI_PROV_EVENT_VALIDATION_TIMEOUT, NULL, 0, portMAX_DELAY);
                break;
            default:
                esp_event_post(WIFI_PROV_EVENTS, WIFI_PROV_EVENT_CREDENTIALS_FAILED, NULL, 0, portMAX_DELAY);
                break;
        }
        return ESP_OK;
    }

    // Store WiFi configuration only after successful validation
    memset(&s_received_wifi_config, 0, sizeof(s_received_wifi_config));
    strncpy((char*)s_received_wifi_config.sta.ssid, ssid, sizeof(s_received_wifi_config.sta.ssid) - 1);
    strncpy((char*)s_received_wifi_config.sta.password, password, sizeof(s_received_wifi_config.sta.password) - 1);

    // Update provisioning state
    s_prov_manager.provisioned = true;
    s_prov_manager.state = WIFI_PROV_STATE_CONNECTED;
    strncpy(s_prov_manager.ssid, ssid, sizeof(s_prov_manager.ssid) - 1);

    // Respond with success only after validation passes
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true,\"message\":\"WiFi configurado exitosamente\"}", HTTPD_RESP_USE_STRLEN);

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
                ESP_LOGD(TAG, "Provisioning started");
                s_prov_manager.state = WIFI_PROV_STATE_PROVISIONING;
                s_prov_manager.provisioning_active = true;
                esp_event_post(WIFI_PROV_EVENTS, WIFI_PROV_EVENT_STARTED, NULL, 0, portMAX_DELAY);
                break;
                
            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGD(TAG, "Received WiFi credentials for SSID: %s", wifi_sta_cfg->ssid);
                
                strncpy(s_prov_manager.ssid, (char *)wifi_sta_cfg->ssid, sizeof(s_prov_manager.ssid) - 1);
                s_prov_manager.ssid[sizeof(s_prov_manager.ssid) - 1] = '\0';
                
                esp_event_post(WIFI_PROV_EVENTS, WIFI_PROV_EVENT_CREDENTIALS_RECEIVED, 
                               wifi_sta_cfg, sizeof(wifi_sta_config_t), portMAX_DELAY);
                break;
            }
            
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGD(TAG, "WiFi credentials validation successful");
                s_prov_manager.state = WIFI_PROV_STATE_CONNECTED;
                esp_event_post(WIFI_PROV_EVENTS, WIFI_PROV_EVENT_CREDENTIALS_SUCCESS, NULL, 0, portMAX_DELAY);
                break;
                
            case WIFI_PROV_CRED_FAIL:
                ESP_LOGE(TAG, "WiFi credentials validation failed");
                esp_event_post(WIFI_PROV_EVENTS, WIFI_PROV_EVENT_CREDENTIALS_FAILED, NULL, 0, portMAX_DELAY);
                break;
                
            case WIFI_PROV_END:
                ESP_LOGD(TAG, "Provisioning ended");
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
    
    ESP_LOGD(TAG, "Initializing WiFi provisioning manager");
    
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
    
    ESP_LOGD(TAG, "WiFi provisioning manager initialized successfully");
    return ESP_OK;
}

esp_err_t wifi_prov_manager_start(void)
{
    if (!s_prov_manager_initialized) {
        ESP_LOGE(TAG, "Provisioning manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGD(TAG, "Starting web-based WiFi provisioning");
    
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
    
    // Enhanced HTTP server configuration for stability
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.task_priority = 5;                    // Lower priority than main tasks
    server_config.stack_size = 6144;                   // Increased stack for chunked responses
    server_config.core_id = 1;                         // Pin to core 1
    server_config.max_open_sockets = 3;                // Limit concurrent connections
    server_config.max_uri_handlers = 6;                // Reduced from 8
    server_config.max_resp_headers = 8;                // Limit response headers
    server_config.send_wait_timeout = 10;              // 10 second send timeout
    server_config.recv_wait_timeout = 10;              // 10 second receive timeout
    server_config.lru_purge_enable = true;             // Enable LRU socket purging
    server_config.close_fn = NULL;                     // Use default close
    server_config.global_user_ctx = NULL;              // No global context
    server_config.global_user_ctx_free_fn = NULL;      // No cleanup needed
    server_config.global_transport_ctx = NULL;         // No transport context
    server_config.global_transport_ctx_free_fn = NULL; // No transport cleanup
    server_config.enable_so_linger = true;             // Enable socket lingering
    server_config.linger_timeout = 1;                  // 1 second linger

    esp_err_t ret = httpd_start(&s_server, &server_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start provisioning HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Provisioning HTTP server started on port %d", server_config.server_port);
    
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
    
    httpd_uri_t config_uri = {
        .uri = "/config",
        .method = HTTP_POST,
        .handler = config_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &config_uri);
    
    s_prov_manager.state = WIFI_PROV_STATE_PROVISIONING;
    s_prov_manager.provisioning_active = true;
    
    ESP_LOGD(TAG, "Web-based provisioning started");
    ESP_LOGD(TAG, "AP SSID: %s", WIFI_PROV_SOFTAP_SSID_DEFAULT);
    ESP_LOGD(TAG, "Web interface: http://192.168.4.1");
    
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
    
    ESP_LOGD(TAG, "Stopping web-based WiFi provisioning");
    
    if (s_prov_manager.provisioning_active) {
        // Stop HTTP server with proper cleanup
        if (s_server) {
            ESP_LOGI(TAG, "Stopping provisioning HTTP server...");
            esp_err_t stop_ret = httpd_stop(s_server);
            if (stop_ret != ESP_OK) {
                ESP_LOGW(TAG, "HTTP server stop returned: %s", esp_err_to_name(stop_ret));
            }
            s_server = NULL;

            // Give time for server cleanup
            vTaskDelay(pdMS_TO_TICKS(500));
            ESP_LOGI(TAG, "Provisioning HTTP server stopped");
        }

        // Transition WiFi from APSTA mode back to STA mode
        ESP_LOGI(TAG, "Transitioning WiFi from APSTA to STA mode...");
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

    ESP_LOGD(TAG, "Deinitializing web-based WiFi provisioning manager");

    wifi_prov_manager_stop();

    // Destroy AP network interface (STA is managed by wifi_connection_manager)
    if (s_ap_netif) {
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
    }

    // Clean up validation event group
    if (s_validation_event_group) {
        vEventGroupDelete(s_validation_event_group);
        s_validation_event_group = NULL;
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
        
        ESP_LOGD(TAG, "Device provisioning status: provisioned (from ESP-IDF storage)");
        ESP_LOGD(TAG, "Stored SSID: '%s' (length: %d)", stored_wifi_cfg.sta.ssid, strlen((char*)stored_wifi_cfg.sta.ssid));
    } else {
        // No valid credentials found in storage
        *provisioned = false;
        s_prov_manager.provisioned = false;
        memset(&s_received_wifi_config, 0, sizeof(s_received_wifi_config));
        memset(s_prov_manager.ssid, 0, sizeof(s_prov_manager.ssid));
        
        ESP_LOGD(TAG, "Device provisioning status: not provisioned (no stored credentials)");
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
    
    ESP_LOGD(TAG, "Resetting web-based WiFi credentials");
    
    // Clear stored credentials
    memset(&s_received_wifi_config, 0, sizeof(s_received_wifi_config));
    
    s_prov_manager.provisioned = false;
    s_prov_manager.state = WIFI_PROV_STATE_INIT;
    memset(s_prov_manager.ssid, 0, sizeof(s_prov_manager.ssid));
    
    ESP_LOGD(TAG, "WiFi credentials reset successfully");
    
    return ESP_OK;
}

esp_err_t wifi_prov_manager_wait_for_completion(void)
{
    if (!s_prov_manager_initialized) {
        ESP_LOGE(TAG, "Provisioning manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGD(TAG, "Waiting for provisioning completion");
    
    while (s_prov_manager.provisioning_active) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    ESP_LOGD(TAG, "Provisioning completed");
    
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
    
    ESP_LOGD(TAG, "Returning stored WiFi config for SSID: %s", config->sta.ssid);
    
    return ESP_OK;
}