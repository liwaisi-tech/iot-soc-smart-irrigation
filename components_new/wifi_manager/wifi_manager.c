/**
 * @file wifi_manager.c
 * @brief WiFi Manager Component - Consolidated WiFi Management
 *
 * Consolidates provisioning, connection management, and boot counter functionality.
 * Component-based architecture - no dependencies on domain/application layers.
 *
 * @author Liwaisi Tech
 * @date 2025-10-03
 * @version 2.0.0 - Component-Based Architecture Migration
 */

#include "wifi_manager.h"
#include "device_config.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "soc/rtc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/ip4_addr.h"
#include <string.h>
#include <sys/param.h>

static const char *TAG = "wifi_manager";

/* ============================ BOOT COUNTER SECTION ============================ */

#define BOOT_COUNTER_MAGIC_NUMBER      0xDEADBEEF
#define BOOT_COUNTER_RESET_THRESHOLD   3
#define BOOT_COUNTER_TIME_WINDOW_MS    30000  // 30 seconds

typedef struct {
    uint32_t magic_number;
    uint32_t boot_count;
    uint32_t first_boot_time;
    uint32_t last_boot_time;
} boot_counter_t;

RTC_DATA_ATTR static boot_counter_t s_boot_counter;

/* Boot counter forward declarations */
static esp_err_t boot_counter_init(void);
static esp_err_t boot_counter_increment(void);
static esp_err_t boot_counter_clear(void);
static esp_err_t boot_counter_check_reset_pattern(bool *should_provision);
static bool boot_counter_is_in_time_window(void);
static esp_err_t boot_counter_set_normal_operation(void);

/* ============================ CONNECTION MANAGER SECTION ============================ */

#ifndef CONFIG_WIFI_PROV_CONNECTION_MAX_RETRIES
#define CONFIG_WIFI_PROV_CONNECTION_MAX_RETRIES 5
#endif

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

typedef struct {
    wifi_manager_connection_state_t state;
    bool connected;
    bool has_ip;
    int retry_count;
    int max_retries;
    char ssid[33];
    uint8_t mac_address[6];
    esp_ip4_addr_t ip_addr;
} wifi_connection_manager_t;

static wifi_connection_manager_t s_conn_manager = {0};
static portMUX_TYPE s_conn_manager_spinlock = portMUX_INITIALIZER_UNLOCKED;
static EventGroupHandle_t s_wifi_event_group = NULL;
static esp_netif_t *s_sta_netif = NULL;

/* Connection manager forward declarations */
static esp_err_t connection_manager_init(void);
static esp_err_t connection_manager_start(void);
static esp_err_t connection_manager_stop(void);
static esp_err_t connection_manager_deinit(void);
static esp_err_t connection_manager_connect(const wifi_config_t *config);
static esp_err_t connection_manager_disconnect(void);
static void connection_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

/* ============================ PROVISIONING MANAGER SECTION ============================ */

#ifndef CONFIG_WIFI_PROV_SOFTAP_SSID
#define CONFIG_WIFI_PROV_SOFTAP_SSID "Liwaisi-Config"
#endif

#define WIFI_PROV_SOFTAP_SSID_DEFAULT CONFIG_WIFI_PROV_SOFTAP_SSID
#define WIFI_PROV_SOFTAP_PASSWORD NULL
#define WIFI_PROV_AP_MAX_STA_CONN 4

#define VALIDATION_SUCCESS_BIT BIT0
#define VALIDATION_FAILED_BIT BIT1

typedef struct {
    wifi_manager_prov_state_t state;
    bool provisioned;
    bool provisioning_active;
    char ssid[33];
    uint8_t mac_address[6];
} wifi_prov_manager_t;

static wifi_prov_manager_t s_prov_manager = {0};
static portMUX_TYPE s_prov_manager_spinlock = portMUX_INITIALIZER_UNLOCKED;
static httpd_handle_t s_server = NULL;
static esp_netif_t *s_ap_netif = NULL;
static wifi_config_t s_received_wifi_config = {0};
static wifi_manager_validation_result_t s_validation_result = WIFI_VALIDATION_OK;
static portMUX_TYPE s_validation_spinlock = portMUX_INITIALIZER_UNLOCKED;
static EventGroupHandle_t s_validation_event_group = NULL;

// Minified HTML for WiFi configuration (~4KB)
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

/* Provisioning manager forward declarations */
static esp_err_t prov_manager_init(void);
static esp_err_t prov_manager_start(void);
static esp_err_t prov_manager_stop(void);
static esp_err_t prov_manager_deinit(void);
static esp_err_t prov_manager_is_provisioned(bool *provisioned);
static esp_err_t prov_manager_reset_credentials(void);
static esp_err_t prov_manager_get_config(wifi_config_t *config);
static esp_err_t prov_manager_validate_credentials(const char* ssid, const char* password, wifi_manager_validation_result_t* result);
static void url_decode(const char* src, char* dst, size_t dst_size);
static void prov_validation_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static esp_err_t root_get_handler(httpd_req_t *req);
static esp_err_t scan_get_handler(httpd_req_t *req);
static esp_err_t config_post_handler(httpd_req_t *req);

/* ============================ MAIN WIFI MANAGER SECTION ============================ */

ESP_EVENT_DEFINE_BASE(WIFI_MANAGER_EVENTS);
ESP_EVENT_DEFINE_BASE(WIFI_MANAGER_PROV_EVENTS);
ESP_EVENT_DEFINE_BASE(WIFI_MANAGER_CONNECTION_EVENTS);

static wifi_manager_status_t s_manager_status = {0};
static portMUX_TYPE s_manager_status_spinlock = portMUX_INITIALIZER_UNLOCKED;
static bool s_manager_initialized = false;

/* Main event handlers */
static void wifi_manager_provisioning_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void wifi_manager_connection_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

/* ============================================================================
 * BOOT COUNTER IMPLEMENTATION
 * ============================================================================ */

static esp_err_t boot_counter_init(void)
{
    ESP_LOGI(TAG, "Initializing boot counter");

    if (s_boot_counter.magic_number != BOOT_COUNTER_MAGIC_NUMBER) {
        ESP_LOGI(TAG, "Boot counter not initialized, creating new counter");
        s_boot_counter.magic_number = BOOT_COUNTER_MAGIC_NUMBER;
        s_boot_counter.boot_count = 0;
        s_boot_counter.last_boot_time = 0;
        s_boot_counter.first_boot_time = 0;
    } else {
        ESP_LOGI(TAG, "Boot counter found in RTC memory, current count: %lu", s_boot_counter.boot_count);
    }

    return ESP_OK;
}

static esp_err_t boot_counter_increment(void)
{
    uint32_t current_time = (uint32_t)(esp_timer_get_time() / 1000);

    if (s_boot_counter.boot_count == 0) {
        s_boot_counter.first_boot_time = current_time;
        s_boot_counter.boot_count = 1;
        ESP_LOGI(TAG, "First boot detected, count: %lu", s_boot_counter.boot_count);
    } else {
        uint32_t time_since_first = current_time - s_boot_counter.first_boot_time;

        if (time_since_first > BOOT_COUNTER_TIME_WINDOW_MS) {
            ESP_LOGI(TAG, "Time window expired (%lu ms), resetting counter", time_since_first);
            s_boot_counter.boot_count = 1;
            s_boot_counter.first_boot_time = current_time;
        } else {
            s_boot_counter.boot_count++;
            ESP_LOGI(TAG, "Incremented boot count: %lu (within time window)", s_boot_counter.boot_count);
        }
    }

    s_boot_counter.last_boot_time = current_time;

    ESP_LOGI(TAG, "Boot counter: %lu, time since first: %lu ms",
             s_boot_counter.boot_count,
             current_time - s_boot_counter.first_boot_time);

    return ESP_OK;
}

static esp_err_t boot_counter_clear(void)
{
    ESP_LOGI(TAG, "Clearing boot counter");
    s_boot_counter.boot_count = 0;
    s_boot_counter.last_boot_time = 0;
    s_boot_counter.first_boot_time = 0;
    return ESP_OK;
}

static esp_err_t boot_counter_check_reset_pattern(bool *should_provision)
{
    if (should_provision == NULL) {
        ESP_LOGE(TAG, "Invalid parameter: should_provision is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    *should_provision = false;

    if (s_boot_counter.magic_number != BOOT_COUNTER_MAGIC_NUMBER) {
        ESP_LOGW(TAG, "Boot counter not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_boot_counter.boot_count >= BOOT_COUNTER_RESET_THRESHOLD) {
        if (boot_counter_is_in_time_window()) {
            *should_provision = true;
            ESP_LOGW(TAG, "Reset pattern detected! Boot count: %lu (threshold: %d)",
                     s_boot_counter.boot_count, BOOT_COUNTER_RESET_THRESHOLD);
        } else {
            ESP_LOGI(TAG, "Boot count exceeded threshold but outside time window");
        }
    }

    return ESP_OK;
}

static bool boot_counter_is_in_time_window(void)
{
    if (s_boot_counter.boot_count == 0) {
        return false;
    }

    uint32_t current_time = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t time_since_first = current_time - s_boot_counter.first_boot_time;

    return time_since_first <= BOOT_COUNTER_TIME_WINDOW_MS;
}

static esp_err_t boot_counter_set_normal_operation(void)
{
    ESP_LOGI(TAG, "Setting normal operation mode - clearing boot counter");
    return boot_counter_clear();
}

/* ============================================================================
 * CONNECTION MANAGER IMPLEMENTATION
 * ============================================================================ */

static void connection_event_handler(void* arg, esp_event_base_t event_base,
                                     int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGD(TAG, "WiFi station started");
        portENTER_CRITICAL(&s_conn_manager_spinlock);
        s_conn_manager.state = WIFI_CONNECTION_STATE_CONNECTING;
        portEXIT_CRITICAL(&s_conn_manager_spinlock);
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*) event_data;

        portENTER_CRITICAL(&s_conn_manager_spinlock);
        s_conn_manager.state = WIFI_CONNECTION_STATE_DISCONNECTED;
        s_conn_manager.connected = false;
        s_conn_manager.has_ip = false;
        portEXIT_CRITICAL(&s_conn_manager_spinlock);

        ESP_LOGW(TAG, "WiFi disconnected - reason: %d, SSID: %s", disconnected->reason, disconnected->ssid);

        // Check specific disconnection reasons
        if (disconnected->reason == WIFI_REASON_AUTH_EXPIRE ||
            disconnected->reason == WIFI_REASON_AUTH_FAIL ||
            disconnected->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
            disconnected->reason == WIFI_REASON_HANDSHAKE_TIMEOUT) {
            ESP_LOGE(TAG, "WiFi authentication failed - incorrect password");
            portENTER_CRITICAL(&s_conn_manager_spinlock);
            s_conn_manager.state = WIFI_CONNECTION_STATE_FAILED;
            portEXIT_CRITICAL(&s_conn_manager_spinlock);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            esp_event_post(WIFI_MANAGER_CONNECTION_EVENTS, WIFI_CONNECTION_EVENT_AUTH_FAILED, &disconnected->reason, sizeof(uint8_t), portMAX_DELAY);
            return;
        } else if (disconnected->reason == WIFI_REASON_NO_AP_FOUND) {
            ESP_LOGE(TAG, "WiFi network not found - check SSID");
            portENTER_CRITICAL(&s_conn_manager_spinlock);
            s_conn_manager.state = WIFI_CONNECTION_STATE_FAILED;
            portEXIT_CRITICAL(&s_conn_manager_spinlock);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            esp_event_post(WIFI_MANAGER_CONNECTION_EVENTS, WIFI_CONNECTION_EVENT_NETWORK_NOT_FOUND, &disconnected->reason, sizeof(uint8_t), portMAX_DELAY);
            return;
        }

        portENTER_CRITICAL(&s_conn_manager_spinlock);
        int retry_count = s_conn_manager.retry_count;
        int max_retries = s_conn_manager.max_retries;
        portEXIT_CRITICAL(&s_conn_manager_spinlock);

        if (retry_count < max_retries) {
            esp_wifi_connect();
            portENTER_CRITICAL(&s_conn_manager_spinlock);
            s_conn_manager.retry_count++;
            portEXIT_CRITICAL(&s_conn_manager_spinlock);
            ESP_LOGD(TAG, "Retry connection (%d/%d)", retry_count + 1, max_retries);
        } else {
            ESP_LOGE(TAG, "WiFi connection failed after %d retries", max_retries);
            portENTER_CRITICAL(&s_conn_manager_spinlock);
            s_conn_manager.state = WIFI_CONNECTION_STATE_FAILED;
            portEXIT_CRITICAL(&s_conn_manager_spinlock);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            esp_event_post(WIFI_MANAGER_CONNECTION_EVENTS, WIFI_CONNECTION_EVENT_RETRY_EXHAUSTED, NULL, 0, portMAX_DELAY);
        }

        esp_event_post(WIFI_MANAGER_CONNECTION_EVENTS, WIFI_CONNECTION_EVENT_DISCONNECTED, NULL, 0, portMAX_DELAY);

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;

        portENTER_CRITICAL(&s_conn_manager_spinlock);
        ESP_LOGD(TAG, "Connected to WiFi network: '%s'", s_conn_manager.ssid);
        portEXIT_CRITICAL(&s_conn_manager_spinlock);

        ESP_LOGD(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));

        portENTER_CRITICAL(&s_conn_manager_spinlock);
        s_conn_manager.state = WIFI_CONNECTION_STATE_CONNECTED;
        s_conn_manager.connected = true;
        s_conn_manager.has_ip = true;
        s_conn_manager.retry_count = 0;
        s_conn_manager.ip_addr = event->ip_info.ip;
        portEXIT_CRITICAL(&s_conn_manager_spinlock);

        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        esp_event_post(WIFI_MANAGER_CONNECTION_EVENTS, WIFI_CONNECTION_EVENT_CONNECTED, NULL, 0, portMAX_DELAY);
        esp_event_post(WIFI_MANAGER_CONNECTION_EVENTS, WIFI_CONNECTION_EVENT_GOT_IP, &event->ip_info.ip, sizeof(esp_ip4_addr_t), portMAX_DELAY);
    }
}

static esp_err_t connection_manager_init(void)
{
    ESP_LOGD(TAG, "Initializing connection manager");

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

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &connection_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connection_event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    s_conn_manager.state = WIFI_CONNECTION_STATE_IDLE;
    s_conn_manager.connected = false;
    s_conn_manager.has_ip = false;
    s_conn_manager.retry_count = 0;
    s_conn_manager.max_retries = CONFIG_WIFI_PROV_CONNECTION_MAX_RETRIES;

    esp_read_mac(s_conn_manager.mac_address, ESP_MAC_WIFI_STA);

    ESP_LOGD(TAG, "Connection manager initialized successfully");
    return ESP_OK;
}

static esp_err_t connection_manager_start(void)
{
    ESP_LOGD(TAG, "Starting connection manager");
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_event_post(WIFI_MANAGER_CONNECTION_EVENTS, WIFI_CONNECTION_EVENT_STARTED, NULL, 0, portMAX_DELAY);
    return ESP_OK;
}

static esp_err_t connection_manager_stop(void)
{
    ESP_LOGD(TAG, "Stopping connection manager");
    ESP_ERROR_CHECK(esp_wifi_stop());

    s_conn_manager.state = WIFI_CONNECTION_STATE_IDLE;
    s_conn_manager.connected = false;
    s_conn_manager.has_ip = false;
    s_conn_manager.retry_count = 0;

    return ESP_OK;
}

static esp_err_t connection_manager_deinit(void)
{
    ESP_LOGD(TAG, "Deinitializing connection manager");

    connection_manager_stop();

    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &connection_event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &connection_event_handler));

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

static esp_err_t connection_manager_connect(const wifi_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid parameter: config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "Connecting to WiFi network: %s", config->sta.ssid);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, (wifi_config_t *)config));

    strncpy(s_conn_manager.ssid, (char *)config->sta.ssid, sizeof(s_conn_manager.ssid) - 1);
    s_conn_manager.ssid[sizeof(s_conn_manager.ssid) - 1] = '\0';

    s_conn_manager.retry_count = 0;
    s_conn_manager.state = WIFI_CONNECTION_STATE_CONNECTING;

    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    esp_err_t ret = esp_wifi_connect();
    if (ret == ESP_ERR_WIFI_CONN) {
        ESP_LOGW(TAG, "WiFi already connecting, waiting for connection result");
        return ESP_OK;
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi connection: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

static esp_err_t connection_manager_disconnect(void)
{
    ESP_LOGD(TAG, "Disconnecting from WiFi");

    ESP_ERROR_CHECK(esp_wifi_disconnect());

    s_conn_manager.state = WIFI_CONNECTION_STATE_DISCONNECTED;
    s_conn_manager.connected = false;
    s_conn_manager.has_ip = false;

    esp_event_post(WIFI_MANAGER_CONNECTION_EVENTS, WIFI_CONNECTION_EVENT_DISCONNECTED, NULL, 0, portMAX_DELAY);

    return ESP_OK;
}

/* ============================================================================
 * PROVISIONING MANAGER IMPLEMENTATION
 * ============================================================================ */

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

static void prov_validation_event_handler(void* arg, esp_event_base_t event_base,
                                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_MANAGER_CONNECTION_EVENTS) {
        switch (event_id) {
            case WIFI_CONNECTION_EVENT_CONNECTED:
                ESP_LOGD(TAG, "Validation: WiFi connected successfully");
                portENTER_CRITICAL(&s_validation_spinlock);
                s_validation_result = WIFI_VALIDATION_OK;
                portEXIT_CRITICAL(&s_validation_spinlock);
                xEventGroupSetBits(s_validation_event_group, VALIDATION_SUCCESS_BIT);
                break;

            case WIFI_CONNECTION_EVENT_AUTH_FAILED:
                ESP_LOGW(TAG, "Validation: WiFi authentication failed");
                portENTER_CRITICAL(&s_validation_spinlock);
                s_validation_result = WIFI_VALIDATION_AUTH_FAILED;
                portEXIT_CRITICAL(&s_validation_spinlock);
                xEventGroupSetBits(s_validation_event_group, VALIDATION_FAILED_BIT);
                break;

            case WIFI_CONNECTION_EVENT_NETWORK_NOT_FOUND:
                ESP_LOGW(TAG, "Validation: WiFi network not found");
                portENTER_CRITICAL(&s_validation_spinlock);
                s_validation_result = WIFI_VALIDATION_NETWORK_NOT_FOUND;
                portEXIT_CRITICAL(&s_validation_spinlock);
                xEventGroupSetBits(s_validation_event_group, VALIDATION_FAILED_BIT);
                break;

            case WIFI_CONNECTION_EVENT_RETRY_EXHAUSTED:
                ESP_LOGW(TAG, "Validation: WiFi connection timeout");
                portENTER_CRITICAL(&s_validation_spinlock);
                s_validation_result = WIFI_VALIDATION_TIMEOUT;
                portEXIT_CRITICAL(&s_validation_spinlock);
                xEventGroupSetBits(s_validation_event_group, VALIDATION_FAILED_BIT);
                break;

            default:
                break;
        }
    }
}

static esp_err_t prov_manager_validate_credentials(const char* ssid, const char* password, wifi_manager_validation_result_t* result)
{
    if (!ssid || !result) {
        ESP_LOGE(TAG, "Invalid parameters for credential validation");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Validating WiFi credentials for SSID: %s", ssid);

    if (s_validation_event_group == NULL) {
        s_validation_event_group = xEventGroupCreate();
        if (s_validation_event_group == NULL) {
            ESP_LOGE(TAG, "Failed to create validation event group");
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t ret = esp_event_handler_register(WIFI_MANAGER_CONNECTION_EVENTS, ESP_EVENT_ANY_ID,
                                               &prov_validation_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register validation event handler: %s", esp_err_to_name(ret));
        return ret;
    }

    wifi_config_t validation_config = {0};
    strncpy((char*)validation_config.sta.ssid, ssid, sizeof(validation_config.sta.ssid) - 1);
    if (password && strlen(password) > 0) {
        strncpy((char*)validation_config.sta.password, password, sizeof(validation_config.sta.password) - 1);
    }

    portENTER_CRITICAL(&s_validation_spinlock);
    s_validation_result = WIFI_VALIDATION_OK;
    portEXIT_CRITICAL(&s_validation_spinlock);
    xEventGroupClearBits(s_validation_event_group, VALIDATION_SUCCESS_BIT | VALIDATION_FAILED_BIT);

    ret = connection_manager_connect(&validation_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start validation connection: %s", esp_err_to_name(ret));
        esp_event_handler_unregister(WIFI_MANAGER_CONNECTION_EVENTS, ESP_EVENT_ANY_ID, &prov_validation_event_handler);
        return ret;
    }

    EventBits_t bits = xEventGroupWaitBits(s_validation_event_group,
                                           VALIDATION_SUCCESS_BIT | VALIDATION_FAILED_BIT,
                                           pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(15000));

    esp_event_handler_unregister(WIFI_MANAGER_CONNECTION_EVENTS, ESP_EVENT_ANY_ID, &prov_validation_event_handler);

    if (bits & VALIDATION_SUCCESS_BIT) {
        ESP_LOGI(TAG, "WiFi credential validation successful");
        *result = WIFI_VALIDATION_OK;
    } else if (bits & VALIDATION_FAILED_BIT) {
        portENTER_CRITICAL(&s_validation_spinlock);
        wifi_manager_validation_result_t validation_result = s_validation_result;
        portEXIT_CRITICAL(&s_validation_spinlock);
        ESP_LOGW(TAG, "WiFi credential validation failed: %d", validation_result);
        *result = validation_result;
    } else {
        ESP_LOGW(TAG, "WiFi credential validation timeout");
        *result = WIFI_VALIDATION_TIMEOUT;
        connection_manager_disconnect();
    }

    return ESP_OK;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");

    const char* html = html_page;
    size_t html_len = strlen(html_page);
    size_t chunk_size = 1024;
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

    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t scan_get_handler(httpd_req_t *req)
{
    ESP_LOGD(TAG, "Starting WiFi scan for web interface");

    size_t free_heap = esp_get_free_heap_size();
    if (free_heap < 20000) {
        ESP_LOGW(TAG, "Low memory before scan: %zu bytes", free_heap);
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Low memory\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 500
    };

    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(ret));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Scan failed\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    uint16_t ap_count = 0;
    ret = esp_wifi_scan_get_ap_num(&ap_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get AP count: %s", esp_err_to_name(ret));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Scan result failed\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    if (ap_count > 15) {
        ESP_LOGW(TAG, "Too many APs found (%d), limiting to 15", ap_count);
        ap_count = 15;
    }

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

    size_t required_size = 50;
    for (int i = 0; i < ap_count && i < 15; i++) {
        required_size += strlen((char*)ap_list[i].ssid) + 100;
    }

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

    for (int i = 0; i < ap_count && i < 15; i++) {
        char network_entry[256];
        const char *auth_mode = "open";

        if (strlen((char*)ap_list[i].ssid) == 0) {
            continue;
        }

        if (ap_list[i].authmode != WIFI_AUTH_OPEN) {
            auth_mode = "secured";
        }

        int written = snprintf(network_entry, sizeof(network_entry),
                              "%s{\"ssid\": \"%.*s\", \"rssi\": %d, \"auth\": \"%s\"}",
                              i > 0 ? "," : "",
                              32, (char*)ap_list[i].ssid,
                              ap_list[i].rssi,
                              auth_mode);

        if (written >= sizeof(network_entry)) {
            ESP_LOGW(TAG, "Network entry truncated for SSID: %s", ap_list[i].ssid);
        }

        if (strlen(json_response) + strlen(network_entry) + 10 > required_size + 150) {
            ESP_LOGW(TAG, "Response buffer full, truncating at %d networks", i);
            break;
        }

        strcat(json_response, network_entry);
    }

    strcat(json_response, "]}");

    httpd_resp_set_type(req, "application/json");

    size_t response_len = strlen(json_response);
    if (response_len > 1024) {
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
        httpd_resp_send_chunk(req, NULL, 0);
    } else {
        httpd_resp_send(req, json_response, response_len);
    }

    free(ap_list);
    free(json_response);

    ESP_LOGD(TAG, "WiFi scan completed, found %d networks (sent %zu bytes)", ap_count, response_len);
    return ESP_OK;
}

static esp_err_t config_post_handler(httpd_req_t *req)
{
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

    char device_name[DEVICE_CONFIG_NAME_MAX_LEN + 1] = {0};
    char device_location[DEVICE_CONFIG_LOCATION_MAX_LEN + 1] = {0};
    char ssid[33] = {0};
    char password[65] = {0};

    // Parse device_name
    char *device_name_start = strstr(content, "device_name=");
    if (device_name_start) {
        device_name_start += 12;
        char *device_name_end = strchr(device_name_start, '&');
        if (device_name_end) {
            size_t name_len = MIN(device_name_end - device_name_start, DEVICE_CONFIG_NAME_MAX_LEN);
            strncpy(device_name, device_name_start, name_len);
        } else {
            strncpy(device_name, device_name_start, DEVICE_CONFIG_NAME_MAX_LEN);
        }

        char decoded_name[DEVICE_CONFIG_NAME_MAX_LEN + 1] = {0};
        url_decode(device_name, decoded_name, sizeof(decoded_name));
        strcpy(device_name, decoded_name);
    }

    // Parse device_location
    char *device_location_start = strstr(content, "device_location=");
    if (device_location_start) {
        device_location_start += 16;
        char *device_location_end = strchr(device_location_start, '&');
        if (device_location_end) {
            size_t location_len = MIN(device_location_end - device_location_start, DEVICE_CONFIG_LOCATION_MAX_LEN);
            strncpy(device_location, device_location_start, location_len);
        } else {
            strncpy(device_location, device_location_start, DEVICE_CONFIG_LOCATION_MAX_LEN);
        }

        char decoded_location[DEVICE_CONFIG_LOCATION_MAX_LEN + 1] = {0};
        url_decode(device_location, decoded_location, sizeof(decoded_location));
        strcpy(device_location, decoded_location);
    }

    // Parse SSID and password
    char *ssid_start = strstr(content, "ssid=");
    char *password_start = strstr(content, "password=");

    if (ssid_start) {
        ssid_start += 5;
        char *ssid_end = strchr(ssid_start, '&');
        if (ssid_end) {
            size_t ssid_len = MIN(ssid_end - ssid_start, 32);
            strncpy(ssid, ssid_start, ssid_len);
        } else {
            strncpy(ssid, ssid_start, 32);
        }

        char decoded_ssid[33] = {0};
        url_decode(ssid, decoded_ssid, sizeof(decoded_ssid));
        strcpy(ssid, decoded_ssid);
    }

    if (password_start) {
        password_start += 9;
        char *password_end = strchr(password_start, '&');
        if (password_end) {
            size_t password_len = MIN(password_end - password_start, 64);
            strncpy(password, password_start, password_len);
        } else {
            strncpy(password, password_start, 64);
        }

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

    // Save device configuration using device_config component
    esp_err_t config_ret = device_config_set_device_name(device_name);
    if (config_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save device name: %s", esp_err_to_name(config_ret));
    }

    config_ret = device_config_set_device_location(device_location);
    if (config_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save device location: %s", esp_err_to_name(config_ret));
    }

    // Validate WiFi credentials before responding
    ESP_LOGI(TAG, "Validating WiFi credentials before saving...");
    s_prov_manager.state = WIFI_PROV_STATE_VALIDATING;

    wifi_manager_validation_result_t validation_result;
    esp_err_t validation_ret = prov_manager_validate_credentials(ssid, password, &validation_result);

    if (validation_ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi validation failed with error: %s", esp_err_to_name(validation_ret));
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":false,\"message\":\"Failed to validate WiFi credentials\"}", HTTPD_RESP_USE_STRLEN);
        s_prov_manager.state = WIFI_PROV_STATE_ERROR;
        return ESP_OK;
    }

    // Handle validation results
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
                esp_event_post(WIFI_MANAGER_PROV_EVENTS, WIFI_PROV_EVENT_AUTH_FAILED, NULL, 0, portMAX_DELAY);
                break;
            case WIFI_VALIDATION_NETWORK_NOT_FOUND:
                esp_event_post(WIFI_MANAGER_PROV_EVENTS, WIFI_PROV_EVENT_NETWORK_NOT_FOUND, NULL, 0, portMAX_DELAY);
                break;
            case WIFI_VALIDATION_TIMEOUT:
                esp_event_post(WIFI_MANAGER_PROV_EVENTS, WIFI_PROV_EVENT_VALIDATION_TIMEOUT, NULL, 0, portMAX_DELAY);
                break;
            default:
                esp_event_post(WIFI_MANAGER_PROV_EVENTS, WIFI_PROV_EVENT_CREDENTIALS_FAILED, NULL, 0, portMAX_DELAY);
                break;
        }
        return ESP_OK;
    }

    // Store WiFi configuration only after successful validation
    memset(&s_received_wifi_config, 0, sizeof(s_received_wifi_config));
    strncpy((char*)s_received_wifi_config.sta.ssid, ssid, sizeof(s_received_wifi_config.sta.ssid) - 1);
    strncpy((char*)s_received_wifi_config.sta.password, password, sizeof(s_received_wifi_config.sta.password) - 1);

    s_prov_manager.provisioned = true;
    s_prov_manager.state = WIFI_PROV_STATE_CONNECTED;
    strncpy(s_prov_manager.ssid, ssid, sizeof(s_prov_manager.ssid) - 1);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true,\"message\":\"WiFi configurado exitosamente\"}", HTTPD_RESP_USE_STRLEN);

    esp_event_post(WIFI_MANAGER_PROV_EVENTS, WIFI_PROV_EVENT_CREDENTIALS_RECEIVED, &s_received_wifi_config.sta, sizeof(wifi_sta_config_t), portMAX_DELAY);
    esp_event_post(WIFI_MANAGER_PROV_EVENTS, WIFI_PROV_EVENT_CREDENTIALS_SUCCESS, NULL, 0, portMAX_DELAY);

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_event_post(WIFI_MANAGER_PROV_EVENTS, WIFI_PROV_EVENT_COMPLETED, NULL, 0, portMAX_DELAY);

    return ESP_OK;
}

static esp_err_t prov_manager_init(void)
{
    ESP_LOGD(TAG, "Initializing provisioning manager");

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

    s_prov_manager.state = WIFI_PROV_STATE_INIT;
    s_prov_manager.provisioned = false;
    s_prov_manager.provisioning_active = false;

    esp_read_mac(s_prov_manager.mac_address, ESP_MAC_WIFI_STA);

    ESP_LOGD(TAG, "Provisioning manager initialized successfully");
    return ESP_OK;
}

static esp_err_t prov_manager_start(void)
{
    ESP_LOGD(TAG, "Starting web-based WiFi provisioning");

    if (s_ap_netif == NULL) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (s_ap_netif == NULL) {
            ESP_LOGE(TAG, "Failed to create AP network interface");
            return ESP_FAIL;
        }
    }

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

    esp_wifi_stop();

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

    ESP_ERROR_CHECK(esp_netif_dhcps_stop(s_ap_netif));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(s_ap_netif, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(s_ap_netif));

    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.task_priority = 5;
    server_config.stack_size = 6144;
    server_config.core_id = 1;
    server_config.max_open_sockets = 3;
    server_config.max_uri_handlers = 6;
    server_config.max_resp_headers = 8;
    server_config.send_wait_timeout = 10;
    server_config.recv_wait_timeout = 10;
    server_config.lru_purge_enable = true;
    server_config.enable_so_linger = true;
    server_config.linger_timeout = 1;

    esp_err_t ret = httpd_start(&s_server, &server_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start provisioning HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Provisioning HTTP server started on port %d", server_config.server_port);

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

    esp_event_post(WIFI_MANAGER_PROV_EVENTS, WIFI_PROV_EVENT_STARTED, NULL, 0, portMAX_DELAY);

    return ESP_OK;
}

static esp_err_t prov_manager_stop(void)
{
    ESP_LOGD(TAG, "Stopping web-based WiFi provisioning");

    if (s_prov_manager.provisioning_active) {
        if (s_server) {
            ESP_LOGI(TAG, "Stopping provisioning HTTP server...");
            esp_err_t stop_ret = httpd_stop(s_server);
            if (stop_ret != ESP_OK) {
                ESP_LOGW(TAG, "HTTP server stop returned: %s", esp_err_to_name(stop_ret));
            }
            s_server = NULL;
            vTaskDelay(pdMS_TO_TICKS(500));
            ESP_LOGI(TAG, "Provisioning HTTP server stopped");
        }

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

static esp_err_t prov_manager_deinit(void)
{
    ESP_LOGD(TAG, "Deinitializing web-based WiFi provisioning manager");

    prov_manager_stop();

    if (s_ap_netif) {
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
    }

    if (s_validation_event_group) {
        vEventGroupDelete(s_validation_event_group);
        s_validation_event_group = NULL;
    }

    return ESP_OK;
}

static esp_err_t prov_manager_is_provisioned(bool *provisioned)
{
    if (provisioned == NULL) {
        ESP_LOGE(TAG, "Invalid parameter: provisioned is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t stored_wifi_cfg;
    esp_err_t ret = esp_wifi_get_config(WIFI_IF_STA, &stored_wifi_cfg);

    if (ret == ESP_OK && strlen((char*)stored_wifi_cfg.sta.ssid) > 0) {
        *provisioned = true;
        s_received_wifi_config = stored_wifi_cfg;
        s_prov_manager.provisioned = true;
        strncpy(s_prov_manager.ssid, (char*)stored_wifi_cfg.sta.ssid, sizeof(s_prov_manager.ssid) - 1);
        s_prov_manager.ssid[sizeof(s_prov_manager.ssid) - 1] = '\0';

        ESP_LOGD(TAG, "Device provisioning status: provisioned (from ESP-IDF storage)");
        ESP_LOGD(TAG, "Stored SSID: '%s' (length: %d)", stored_wifi_cfg.sta.ssid, strlen((char*)stored_wifi_cfg.sta.ssid));
    } else {
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

static esp_err_t prov_manager_reset_credentials(void)
{
    ESP_LOGD(TAG, "Resetting web-based WiFi credentials");

    memset(&s_received_wifi_config, 0, sizeof(s_received_wifi_config));

    s_prov_manager.provisioned = false;
    s_prov_manager.state = WIFI_PROV_STATE_INIT;
    memset(s_prov_manager.ssid, 0, sizeof(s_prov_manager.ssid));

    ESP_LOGD(TAG, "WiFi credentials reset successfully");

    return ESP_OK;
}

static esp_err_t prov_manager_get_config(wifi_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid parameter: config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_prov_manager.provisioned) {
        ESP_LOGE(TAG, "Device not provisioned");
        return ESP_ERR_INVALID_STATE;
    }

    *config = s_received_wifi_config;

    ESP_LOGD(TAG, "Returning stored WiFi config for SSID: %s", config->sta.ssid);

    return ESP_OK;
}

/* ============================================================================
 * MAIN WIFI MANAGER PUBLIC API
 * ============================================================================ */

static void wifi_manager_provisioning_event_handler(void* arg, esp_event_base_t event_base,
                                                     int32_t event_id, void* event_data)
{
    if (event_base == WIFI_MANAGER_PROV_EVENTS) {
        switch (event_id) {
            case WIFI_PROV_EVENT_STARTED:
                ESP_LOGD(TAG, "WiFi provisioning started");
                portENTER_CRITICAL(&s_manager_status_spinlock);
                s_manager_status.state = WIFI_MANAGER_STATE_PROVISIONING;
                portEXIT_CRITICAL(&s_manager_status_spinlock);
                esp_event_post(WIFI_MANAGER_EVENTS, WIFI_MANAGER_EVENT_PROVISIONING_STARTED, NULL, 0, portMAX_DELAY);
                break;

            case WIFI_PROV_EVENT_CREDENTIALS_SUCCESS:
                ESP_LOGD(TAG, "WiFi credentials validated successfully");
                break;

            case WIFI_PROV_EVENT_COMPLETED:
                ESP_LOGD(TAG, "WiFi provisioning completed");
                portENTER_CRITICAL(&s_manager_status_spinlock);
                s_manager_status.provisioned = true;
                s_manager_status.state = WIFI_MANAGER_STATE_CONNECTING;
                portEXIT_CRITICAL(&s_manager_status_spinlock);

                wifi_config_t config;
                if (prov_manager_get_config(&config) == ESP_OK) {
                    portENTER_CRITICAL(&s_manager_status_spinlock);
                    strncpy(s_manager_status.ssid, (char *)config.sta.ssid, sizeof(s_manager_status.ssid) - 1);
                    s_manager_status.ssid[sizeof(s_manager_status.ssid) - 1] = '\0';
                    portEXIT_CRITICAL(&s_manager_status_spinlock);

                    prov_manager_deinit();

                    ESP_LOGD(TAG, "Starting WiFi connection with provisioned credentials");
                    connection_manager_connect(&config);
                }

                esp_event_post(WIFI_MANAGER_EVENTS, WIFI_MANAGER_EVENT_PROVISIONING_COMPLETED, NULL, 0, portMAX_DELAY);
                break;

            case WIFI_PROV_EVENT_FAILED:
                ESP_LOGE(TAG, "WiFi provisioning failed");
                portENTER_CRITICAL(&s_manager_status_spinlock);
                s_manager_status.state = WIFI_MANAGER_STATE_ERROR;
                portEXIT_CRITICAL(&s_manager_status_spinlock);
                break;

            default:
                break;
        }
    }
}

static void wifi_manager_connection_event_handler(void* arg, esp_event_base_t event_base,
                                                   int32_t event_id, void* event_data)
{
    if (event_base == WIFI_MANAGER_CONNECTION_EVENTS) {
        switch (event_id) {
            case WIFI_CONNECTION_EVENT_CONNECTED:
                portENTER_CRITICAL(&s_manager_status_spinlock);
                ESP_LOGD(TAG, "WiFi connected successfully to: '%s'", s_manager_status.ssid);
                s_manager_status.connected = true;
                s_manager_status.state = WIFI_MANAGER_STATE_CONNECTED;
                portEXIT_CRITICAL(&s_manager_status_spinlock);
                esp_event_post(WIFI_MANAGER_EVENTS, WIFI_MANAGER_EVENT_CONNECTED, NULL, 0, portMAX_DELAY);
                break;

            case WIFI_CONNECTION_EVENT_DISCONNECTED:
                ESP_LOGD(TAG, "WiFi disconnected");
                portENTER_CRITICAL(&s_manager_status_spinlock);
                s_manager_status.connected = false;
                s_manager_status.has_ip = false;
                s_manager_status.state = WIFI_MANAGER_STATE_DISCONNECTED;
                portEXIT_CRITICAL(&s_manager_status_spinlock);
                esp_event_post(WIFI_MANAGER_EVENTS, WIFI_MANAGER_EVENT_DISCONNECTED, NULL, 0, portMAX_DELAY);
                break;

            case WIFI_CONNECTION_EVENT_GOT_IP:
                ESP_LOGD(TAG, "WiFi got IP address");
                portENTER_CRITICAL(&s_manager_status_spinlock);
                s_manager_status.has_ip = true;
                if (event_data != NULL) {
                    esp_ip4_addr_t *ip = (esp_ip4_addr_t *)event_data;
                    s_manager_status.ip_addr = *ip;
                    ESP_LOGD(TAG, "IP address: " IPSTR, IP2STR(ip));
                }
                portEXIT_CRITICAL(&s_manager_status_spinlock);
                esp_event_post(WIFI_MANAGER_EVENTS, WIFI_MANAGER_EVENT_IP_OBTAINED, event_data, sizeof(esp_ip4_addr_t), portMAX_DELAY);
                break;

            case WIFI_CONNECTION_EVENT_RETRY_EXHAUSTED:
                ESP_LOGE(TAG, "WiFi connection retry exhausted");
                portENTER_CRITICAL(&s_manager_status_spinlock);
                s_manager_status.state = WIFI_MANAGER_STATE_ERROR;
                portEXIT_CRITICAL(&s_manager_status_spinlock);
                esp_event_post(WIFI_MANAGER_EVENTS, WIFI_MANAGER_EVENT_CONNECTION_FAILED, NULL, 0, portMAX_DELAY);
                break;

            default:
                break;
        }
    }
}

esp_err_t wifi_manager_init(void)
{
    if (s_manager_initialized) {
        ESP_LOGW(TAG, "WiFi manager already initialized");
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Initializing WiFi manager");

    portENTER_CRITICAL(&s_manager_status_spinlock);
    s_manager_status.state = WIFI_MANAGER_STATE_UNINITIALIZED;
    s_manager_status.provisioned = false;
    s_manager_status.connected = false;
    s_manager_status.has_ip = false;
    memset(s_manager_status.ssid, 0, sizeof(s_manager_status.ssid));
    portEXIT_CRITICAL(&s_manager_status_spinlock);

    ESP_ERROR_CHECK(prov_manager_init());
    ESP_ERROR_CHECK(connection_manager_init());

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_MANAGER_PROV_EVENTS, ESP_EVENT_ANY_ID, &wifi_manager_provisioning_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_MANAGER_CONNECTION_EVENTS, ESP_EVENT_ANY_ID, &wifi_manager_connection_event_handler, NULL));

    portENTER_CRITICAL(&s_manager_status_spinlock);
    portENTER_CRITICAL(&s_conn_manager_spinlock);
    memcpy(s_manager_status.mac_address, s_conn_manager.mac_address, 6);
    portEXIT_CRITICAL(&s_conn_manager_spinlock);
    portEXIT_CRITICAL(&s_manager_status_spinlock);

    s_manager_initialized = true;

    ESP_LOGD(TAG, "WiFi manager initialized successfully");
    ESP_LOGD(TAG, "MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
             s_manager_status.mac_address[0], s_manager_status.mac_address[1],
             s_manager_status.mac_address[2], s_manager_status.mac_address[3],
             s_manager_status.mac_address[4], s_manager_status.mac_address[5]);

    esp_event_post(WIFI_MANAGER_EVENTS, WIFI_MANAGER_EVENT_INIT_COMPLETE, NULL, 0, portMAX_DELAY);

    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    if (!s_manager_initialized) {
        ESP_LOGE(TAG, "WiFi manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Starting WiFi manager");

    ESP_ERROR_CHECK(connection_manager_start());

    return wifi_manager_check_and_connect();
}

esp_err_t wifi_manager_stop(void)
{
    if (!s_manager_initialized) {
        ESP_LOGE(TAG, "WiFi manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Stopping WiFi manager");

    prov_manager_stop();
    connection_manager_stop();

    portENTER_CRITICAL(&s_manager_status_spinlock);
    s_manager_status.state = WIFI_MANAGER_STATE_UNINITIALIZED;
    s_manager_status.connected = false;
    s_manager_status.has_ip = false;
    portEXIT_CRITICAL(&s_manager_status_spinlock);

    return ESP_OK;
}

esp_err_t wifi_manager_deinit(void)
{
    if (!s_manager_initialized) {
        ESP_LOGW(TAG, "WiFi manager not initialized");
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Deinitializing WiFi manager");

    wifi_manager_stop();

    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_MANAGER_PROV_EVENTS, ESP_EVENT_ANY_ID, &wifi_manager_provisioning_event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_MANAGER_CONNECTION_EVENTS, ESP_EVENT_ANY_ID, &wifi_manager_connection_event_handler));

    prov_manager_deinit();
    connection_manager_deinit();

    s_manager_initialized = false;

    return ESP_OK;
}

esp_err_t wifi_manager_check_and_connect(void)
{
    if (!s_manager_initialized) {
        ESP_LOGE(TAG, "WiFi manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Checking WiFi provisioning and connection status");

    portENTER_CRITICAL(&s_manager_status_spinlock);
    s_manager_status.state = WIFI_MANAGER_STATE_CHECKING_PROVISION;
    portEXIT_CRITICAL(&s_manager_status_spinlock);

    // Check for manual reset pattern
    bool should_provision = false;
    esp_err_t ret = boot_counter_check_reset_pattern(&should_provision);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to check reset pattern: %s", esp_err_to_name(ret));
        return ret;
    }

    if (should_provision) {
        ESP_LOGW(TAG, "Reset pattern detected - user requested provisioning mode");

        ESP_LOGD(TAG, "Clearing stored WiFi credentials due to reset pattern");
        esp_err_t clear_ret = wifi_manager_clear_all_credentials();
        if (clear_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to clear credentials: %s", esp_err_to_name(clear_ret));
        }

        ESP_LOGD(TAG, "Resetting boot counter after reset pattern detection");
        boot_counter_clear();

        esp_event_post(WIFI_MANAGER_EVENTS, WIFI_MANAGER_EVENT_RESET_REQUESTED, NULL, 0, portMAX_DELAY);
        return wifi_manager_force_provisioning();
    }

    // Check if device has valid stored credentials
    bool provisioned = false;
    ret = prov_manager_is_provisioned(&provisioned);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to check provisioning status: %s", esp_err_to_name(ret));
        ESP_LOGW(TAG, "Cannot determine provisioning status - starting provisioning for safety");
        return wifi_manager_force_provisioning();
    }

    portENTER_CRITICAL(&s_manager_status_spinlock);
    s_manager_status.provisioned = provisioned;
    portEXIT_CRITICAL(&s_manager_status_spinlock);

    if (provisioned) {
        ESP_LOGD(TAG, "Device has stored credentials, attempting to connect");
        portENTER_CRITICAL(&s_manager_status_spinlock);
        s_manager_status.state = WIFI_MANAGER_STATE_CONNECTING;
        portEXIT_CRITICAL(&s_manager_status_spinlock);

        wifi_config_t config;
        ret = prov_manager_get_config(&config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get stored WiFi config: %s", esp_err_to_name(ret));
            ESP_LOGW(TAG, "Invalid stored credentials - starting provisioning");
            return wifi_manager_force_provisioning();
        }

        if (strlen((char*)config.sta.ssid) == 0) {
            ESP_LOGW(TAG, "Empty SSID in stored credentials - starting provisioning");
            return wifi_manager_force_provisioning();
        }

        ESP_LOGD(TAG, "Connecting to stored WiFi network: '%s' (length: %d)", config.sta.ssid, strlen((char*)config.sta.ssid));
        portENTER_CRITICAL(&s_manager_status_spinlock);
        strncpy(s_manager_status.ssid, (char *)config.sta.ssid, sizeof(s_manager_status.ssid) - 1);
        s_manager_status.ssid[sizeof(s_manager_status.ssid) - 1] = '\0';
        portEXIT_CRITICAL(&s_manager_status_spinlock);

        return connection_manager_connect(&config);

    } else {
        ESP_LOGD(TAG, "No stored credentials found - starting provisioning mode");
        return wifi_manager_force_provisioning();
    }
}

esp_err_t wifi_manager_force_provisioning(void)
{
    if (!s_manager_initialized) {
        ESP_LOGE(TAG, "WiFi manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Starting forced WiFi provisioning");

    connection_manager_stop();

    portENTER_CRITICAL(&s_manager_status_spinlock);
    s_manager_status.state = WIFI_MANAGER_STATE_PROVISIONING;
    s_manager_status.connected = false;
    s_manager_status.has_ip = false;
    portEXIT_CRITICAL(&s_manager_status_spinlock);

    return prov_manager_start();
}

esp_err_t wifi_manager_reset_credentials(void)
{
    if (!s_manager_initialized) {
        ESP_LOGE(TAG, "WiFi manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Resetting WiFi credentials");

    connection_manager_disconnect();
    prov_manager_stop();

    esp_err_t ret = prov_manager_reset_credentials();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset credentials: %s", esp_err_to_name(ret));
        return ret;
    }

    portENTER_CRITICAL(&s_manager_status_spinlock);
    s_manager_status.provisioned = false;
    s_manager_status.connected = false;
    s_manager_status.has_ip = false;
    s_manager_status.state = WIFI_MANAGER_STATE_UNINITIALIZED;
    memset(s_manager_status.ssid, 0, sizeof(s_manager_status.ssid));
    portEXIT_CRITICAL(&s_manager_status_spinlock);

    return ESP_OK;
}

esp_err_t wifi_manager_get_status(wifi_manager_status_t *status)
{
    if (!s_manager_initialized) {
        ESP_LOGE(TAG, "WiFi manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (status == NULL) {
        ESP_LOGE(TAG, "Invalid parameter: status is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_manager_status_spinlock);
    *status = s_manager_status;
    portEXIT_CRITICAL(&s_manager_status_spinlock);

    return ESP_OK;
}

bool wifi_manager_is_provisioned(void)
{
    bool provisioned;
    portENTER_CRITICAL(&s_manager_status_spinlock);
    provisioned = s_manager_status.provisioned;
    portEXIT_CRITICAL(&s_manager_status_spinlock);
    return provisioned;
}

bool wifi_manager_is_connected(void)
{
    bool connected;
    portENTER_CRITICAL(&s_manager_status_spinlock);
    connected = s_manager_status.connected && s_manager_status.has_ip;
    portEXIT_CRITICAL(&s_manager_status_spinlock);
    return connected;
}

esp_err_t wifi_manager_get_ip(esp_ip4_addr_t *ip)
{
    if (!s_manager_initialized || !ip) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_manager_status_spinlock);
    if (!s_manager_status.has_ip) {
        portEXIT_CRITICAL(&s_manager_status_spinlock);
        return ESP_ERR_INVALID_STATE;
    }
    *ip = s_manager_status.ip_addr;
    portEXIT_CRITICAL(&s_manager_status_spinlock);

    return ESP_OK;
}

esp_err_t wifi_manager_get_mac(uint8_t *mac)
{
    if (!s_manager_initialized || !mac) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_manager_status_spinlock);
    memcpy(mac, s_manager_status.mac_address, 6);
    portEXIT_CRITICAL(&s_manager_status_spinlock);

    return ESP_OK;
}

esp_err_t wifi_manager_get_ssid(char *ssid, size_t ssid_len)
{
    if (!s_manager_initialized || !ssid || ssid_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_manager_status_spinlock);
    strncpy(ssid, s_manager_status.ssid, ssid_len - 1);
    portEXIT_CRITICAL(&s_manager_status_spinlock);
    ssid[ssid_len - 1] = '\0';

    return ESP_OK;
}

esp_err_t wifi_manager_clear_all_credentials(void)
{
    if (!s_manager_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    connection_manager_disconnect();
    prov_manager_stop();
    prov_manager_reset_credentials();

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

    portENTER_CRITICAL(&s_manager_status_spinlock);
    s_manager_status.provisioned = false;
    s_manager_status.connected = false;
    s_manager_status.has_ip = false;
    s_manager_status.state = WIFI_MANAGER_STATE_UNINITIALIZED;
    memset(s_manager_status.ssid, 0, sizeof(s_manager_status.ssid));
    portEXIT_CRITICAL(&s_manager_status_spinlock);

    return ESP_OK;
}

wifi_manager_state_t wifi_manager_get_state(void)
{
    wifi_manager_state_t state;
    portENTER_CRITICAL(&s_manager_status_spinlock);
    state = s_manager_status.state;
    portEXIT_CRITICAL(&s_manager_status_spinlock);
    return state;
}
