#ifndef WIFI_CONNECTION_MANAGER_H
#define WIFI_CONNECTION_MANAGER_H

#include "esp_err.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(WIFI_CONNECTION_EVENTS);

typedef enum {
    WIFI_CONNECTION_EVENT_STARTED,
    WIFI_CONNECTION_EVENT_CONNECTED,
    WIFI_CONNECTION_EVENT_DISCONNECTED,
    WIFI_CONNECTION_EVENT_GOT_IP,
    WIFI_CONNECTION_EVENT_LOST_IP,
    WIFI_CONNECTION_EVENT_FAILED,
    WIFI_CONNECTION_EVENT_RETRY_EXHAUSTED,
    WIFI_CONNECTION_EVENT_AUTH_FAILED,
    WIFI_CONNECTION_EVENT_NETWORK_NOT_FOUND
} wifi_connection_event_id_t;

typedef enum {
    WIFI_CONNECTION_STATE_IDLE,
    WIFI_CONNECTION_STATE_CONNECTING,
    WIFI_CONNECTION_STATE_CONNECTED,
    WIFI_CONNECTION_STATE_DISCONNECTED,
    WIFI_CONNECTION_STATE_FAILED
} wifi_connection_state_t;

typedef struct {
    wifi_connection_state_t state;
    bool connected;
    bool has_ip;
    int retry_count;
    int max_retries;
    char ssid[33];
    esp_ip4_addr_t ip_addr;
    uint8_t mac_address[6];
} wifi_connection_manager_t;

esp_err_t wifi_connection_manager_init(void);

esp_err_t wifi_connection_manager_start(void);

esp_err_t wifi_connection_manager_stop(void);

esp_err_t wifi_connection_manager_deinit(void);

esp_err_t wifi_connection_manager_connect(const wifi_config_t *config);

esp_err_t wifi_connection_manager_disconnect(void);

esp_err_t wifi_connection_manager_reconnect(void);

wifi_connection_state_t wifi_connection_manager_get_state(void);

bool wifi_connection_manager_is_connected(void);

bool wifi_connection_manager_has_ip(void);

esp_err_t wifi_connection_manager_get_ip(esp_ip4_addr_t *ip);

esp_err_t wifi_connection_manager_get_ssid(char *ssid, size_t ssid_len);

esp_err_t wifi_connection_manager_get_mac(uint8_t *mac);

esp_err_t wifi_connection_manager_set_retry_config(int max_retries);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_CONNECTION_MANAGER_H */