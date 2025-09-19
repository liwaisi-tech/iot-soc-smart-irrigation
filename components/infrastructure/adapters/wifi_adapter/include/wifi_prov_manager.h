#ifndef WIFI_PROV_MANAGER_H
#define WIFI_PROV_MANAGER_H

#include "esp_err.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "wifi_provisioning/manager.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(WIFI_PROV_EVENTS);

typedef enum {
    WIFI_PROV_EVENT_STARTED,
    WIFI_PROV_EVENT_CREDENTIALS_RECEIVED,
    WIFI_PROV_EVENT_CREDENTIALS_SUCCESS,
    WIFI_PROV_EVENT_CREDENTIALS_FAILED,
    WIFI_PROV_EVENT_VALIDATION_TIMEOUT,
    WIFI_PROV_EVENT_AUTH_FAILED,
    WIFI_PROV_EVENT_NETWORK_NOT_FOUND,
    WIFI_PROV_EVENT_COMPLETED,
    WIFI_PROV_EVENT_FAILED
} wifi_prov_event_id_t;

typedef enum {
    WIFI_PROV_STATE_INIT,
    WIFI_PROV_STATE_CHECK_RESET,
    WIFI_PROV_STATE_CHECK_PROVISIONED,
    WIFI_PROV_STATE_PROVISIONING,
    WIFI_PROV_STATE_CONNECTING,
    WIFI_PROV_STATE_VALIDATING,
    WIFI_PROV_STATE_CONNECTED,
    WIFI_PROV_STATE_ERROR
} wifi_prov_state_t;

typedef struct {
    wifi_prov_state_t state;
    bool provisioned;
    bool provisioning_active;
    char ssid[33];
    uint8_t mac_address[6];
} wifi_prov_manager_t;

esp_err_t wifi_prov_manager_init(void);

esp_err_t wifi_prov_manager_start(void);

esp_err_t wifi_prov_manager_stop(void);

esp_err_t wifi_prov_manager_deinit(void);

esp_err_t wifi_prov_manager_is_provisioned(bool *provisioned);

esp_err_t wifi_prov_manager_reset_credentials(void);

esp_err_t wifi_prov_manager_wait_for_completion(void);

wifi_prov_state_t wifi_prov_manager_get_state(void);

esp_err_t wifi_prov_manager_get_config(wifi_config_t *config);

typedef enum {
    WIFI_VALIDATION_OK = 0,
    WIFI_VALIDATION_TIMEOUT,
    WIFI_VALIDATION_AUTH_FAILED,
    WIFI_VALIDATION_NETWORK_NOT_FOUND,
    WIFI_VALIDATION_GENERAL_ERROR
} wifi_validation_result_t;

esp_err_t wifi_prov_manager_validate_credentials(const char* ssid, const char* password, wifi_validation_result_t* result);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_PROV_MANAGER_H */