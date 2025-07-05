#ifndef WIFI_ADAPTER_H
#define WIFI_ADAPTER_H

#include "esp_err.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(WIFI_ADAPTER_EVENTS);

typedef enum {
    WIFI_ADAPTER_EVENT_INIT_COMPLETE,
    WIFI_ADAPTER_EVENT_PROVISIONING_STARTED,
    WIFI_ADAPTER_EVENT_PROVISIONING_COMPLETED,
    WIFI_ADAPTER_EVENT_CONNECTED,
    WIFI_ADAPTER_EVENT_DISCONNECTED,
    WIFI_ADAPTER_EVENT_IP_OBTAINED,
    WIFI_ADAPTER_EVENT_CONNECTION_FAILED,
    WIFI_ADAPTER_EVENT_RESET_REQUESTED
} wifi_adapter_event_id_t;

typedef enum {
    WIFI_ADAPTER_STATE_UNINITIALIZED,
    WIFI_ADAPTER_STATE_CHECKING_PROVISION,
    WIFI_ADAPTER_STATE_PROVISIONING,
    WIFI_ADAPTER_STATE_CONNECTING,
    WIFI_ADAPTER_STATE_CONNECTED,
    WIFI_ADAPTER_STATE_DISCONNECTED,
    WIFI_ADAPTER_STATE_ERROR
} wifi_adapter_state_t;

typedef struct {
    wifi_adapter_state_t state;
    bool provisioned;
    bool connected;
    bool has_ip;
    char ssid[33];
    uint8_t mac_address[6];
    esp_ip4_addr_t ip_addr;
} wifi_adapter_status_t;

esp_err_t wifi_adapter_init(void);

esp_err_t wifi_adapter_start(void);

esp_err_t wifi_adapter_stop(void);

esp_err_t wifi_adapter_deinit(void);

esp_err_t wifi_adapter_check_and_connect(void);

esp_err_t wifi_adapter_force_provisioning(void);

esp_err_t wifi_adapter_reset_credentials(void);

esp_err_t wifi_adapter_get_status(wifi_adapter_status_t *status);

bool wifi_adapter_is_provisioned(void);

bool wifi_adapter_is_connected(void);

esp_err_t wifi_adapter_get_ip(esp_ip4_addr_t *ip);

esp_err_t wifi_adapter_get_mac(uint8_t *mac);

esp_err_t wifi_adapter_get_ssid(char *ssid, size_t ssid_len);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_ADAPTER_H */