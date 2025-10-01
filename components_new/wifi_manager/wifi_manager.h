/**
 * @file wifi_manager.h
 * @brief WiFi Manager Component - Connectivity and Reconnection Management
 *
 * Simple, direct WiFi management for ESP32 irrigation system.
 * Handles connection, auto-reconnection with exponential backoff, and provisioning.
 *
 * Component Responsibilities:
 * - WiFi initialization and connection
 * - Automatic reconnection with backoff strategy
 * - WiFi provisioning (SoftAP mode)
 * - Connection status monitoring
 * - IP address management
 *
 * Migration from hexagonal: Consolidates wifi_adapter + wifi_connection_manager + wifi_prov_manager
 *
 * @author Liwaisi Tech
 * @date 2025-10-01
 * @version 2.0.0 - Component-Based Architecture
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ TYPES AND ENUMS ============================ */

/**
 * @brief WiFi connection state
 */
typedef enum {
    WIFI_STATE_UNINITIALIZED = 0,   ///< Component not initialized
    WIFI_STATE_DISCONNECTED,        ///< Initialized but disconnected
    WIFI_STATE_CONNECTING,          ///< Connection attempt in progress
    WIFI_STATE_CONNECTED,           ///< Connected to AP, no IP yet
    WIFI_STATE_GOT_IP,              ///< Connected with valid IP address
    WIFI_STATE_PROVISIONING,        ///< In provisioning mode (SoftAP)
    WIFI_STATE_ERROR                ///< Error state
} wifi_state_t;

/**
 * @brief WiFi manager status
 */
typedef struct {
    wifi_state_t state;             ///< Current WiFi state
    bool provisioned;               ///< Has valid credentials stored
    char ssid[33];                  ///< Connected SSID (or empty)
    uint8_t mac_address[6];         ///< Device MAC address
    esp_ip4_addr_t ip_addr;         ///< Current IP address
    int8_t rssi;                    ///< Signal strength (dBm)
    uint32_t reconnect_count;       ///< Number of reconnection attempts
} wifi_status_t;

/**
 * @brief WiFi configuration
 */
typedef struct {
    char ssid[32];                  ///< WiFi SSID
    char password[64];              ///< WiFi password
    uint8_t max_retry;              ///< Max connection retries (0 = infinite)
    uint32_t initial_backoff_ms;    ///< Initial backoff delay (default: 10000ms)
    uint32_t max_backoff_ms;        ///< Maximum backoff delay (default: 3600000ms)
    bool enable_provisioning;       ///< Enable provisioning mode if no credentials
} wifi_config_params_t;

/* ============================ PUBLIC API ============================ */

/**
 * @brief Initialize WiFi manager component
 *
 * Initializes ESP-IDF WiFi stack, netif, and event handlers.
 * Must be called before any other WiFi operations.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Start WiFi connection
 *
 * Attempts to connect using stored credentials (NVS).
 * If no credentials found and provisioning enabled, enters provisioning mode.
 *
 * @return ESP_OK if connection started, error code otherwise
 */
esp_err_t wifi_manager_start(void);

/**
 * @brief Stop WiFi connection
 *
 * Disconnects from AP and stops WiFi stack (but doesn't deinitialize).
 *
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_stop(void);

/**
 * @brief Deinitialize WiFi manager
 *
 * Stops WiFi, cleans up resources, and resets to uninitialized state.
 *
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_deinit(void);

/**
 * @brief Connect to WiFi with specific credentials
 *
 * Stores credentials in NVS and attempts connection.
 *
 * @param ssid WiFi SSID (max 31 chars)
 * @param password WiFi password (max 63 chars)
 * @return ESP_OK if connection initiated, error code otherwise
 */
esp_err_t wifi_manager_connect(const char* ssid, const char* password);

/**
 * @brief Force WiFi reconnection
 *
 * Disconnects and reconnects with current credentials.
 * Useful for manual reconnection after network issues.
 *
 * @return ESP_OK if reconnection initiated
 */
esp_err_t wifi_manager_reconnect(void);

/**
 * @brief Start WiFi provisioning mode
 *
 * Starts SoftAP for WiFi configuration via smartphone.
 * Device creates AP "Liwaisi-Config" for provisioning.
 *
 * @return ESP_OK if provisioning started
 */
esp_err_t wifi_manager_start_provisioning(void);

/**
 * @brief Stop WiFi provisioning mode
 *
 * Stops SoftAP and returns to normal station mode.
 *
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_stop_provisioning(void);

/**
 * @brief Clear stored WiFi credentials
 *
 * Removes credentials from NVS. Next start will enter provisioning mode.
 *
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_clear_credentials(void);

/**
 * @brief Get current WiFi status
 *
 * @param[out] status Pointer to status structure to fill
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if status is NULL
 */
esp_err_t wifi_manager_get_status(wifi_status_t* status);

/**
 * @brief Check if WiFi is connected with IP
 *
 * @return true if connected and has valid IP, false otherwise
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Check if WiFi credentials are provisioned
 *
 * @return true if valid credentials stored in NVS
 */
bool wifi_manager_is_provisioned(void);

/**
 * @brief Get current IP address
 *
 * @param[out] ip Pointer to IP address structure to fill
 * @return ESP_OK if connected and has IP, ESP_FAIL otherwise
 */
esp_err_t wifi_manager_get_ip(esp_ip4_addr_t* ip);

/**
 * @brief Get device MAC address
 *
 * @param[out] mac Pointer to 6-byte MAC address buffer
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if mac is NULL
 */
esp_err_t wifi_manager_get_mac(uint8_t* mac);

/**
 * @brief Get current SSID
 *
 * @param[out] ssid Buffer to store SSID (min 33 bytes)
 * @param ssid_len Size of ssid buffer
 * @return ESP_OK if connected, ESP_FAIL otherwise
 */
esp_err_t wifi_manager_get_ssid(char* ssid, size_t ssid_len);

/**
 * @brief Get WiFi signal strength (RSSI)
 *
 * @param[out] rssi Pointer to store RSSI value in dBm
 * @return ESP_OK if connected, ESP_FAIL otherwise
 */
esp_err_t wifi_manager_get_rssi(int8_t* rssi);

/* ============================ EVENT DEFINITIONS ============================ */

/**
 * @brief WiFi manager event base
 */
ESP_EVENT_DECLARE_BASE(WIFI_MANAGER_EVENTS);

/**
 * @brief WiFi manager event IDs
 */
typedef enum {
    WIFI_MANAGER_EVENT_INITIALIZED,     ///< Component initialized
    WIFI_MANAGER_EVENT_CONNECTING,      ///< Connection attempt started
    WIFI_MANAGER_EVENT_CONNECTED,       ///< Connected to AP
    WIFI_MANAGER_EVENT_GOT_IP,          ///< IP address obtained
    WIFI_MANAGER_EVENT_DISCONNECTED,    ///< Disconnected from AP
    WIFI_MANAGER_EVENT_PROVISIONING_STARTED, ///< Provisioning mode started
    WIFI_MANAGER_EVENT_PROVISIONING_COMPLETED, ///< Provisioning completed
    WIFI_MANAGER_EVENT_CONNECTION_FAILED, ///< Connection failed
    WIFI_MANAGER_EVENT_RECONNECTING     ///< Reconnection attempt in progress
} wifi_manager_event_id_t;

/* ============================ CONFIGURATION ============================ */

/**
 * @brief Default WiFi configuration
 */
#define WIFI_MANAGER_DEFAULT_CONFIG() { \
    .ssid = "",                         \
    .password = "",                     \
    .max_retry = 0,                     \
    .initial_backoff_ms = 10000,        \
    .max_backoff_ms = 3600000,          \
    .enable_provisioning = true         \
}

/**
 * @brief WiFi manager NVS namespace
 */
#define WIFI_MANAGER_NVS_NAMESPACE "wifi_cfg"

/**
 * @brief WiFi manager NVS keys
 */
#define WIFI_MANAGER_NVS_KEY_SSID       "ssid"
#define WIFI_MANAGER_NVS_KEY_PASSWORD   "password"
#define WIFI_MANAGER_NVS_KEY_PROVISIONED "provisioned"

/**
 * @brief Provisioning SoftAP configuration
 */
#define WIFI_PROVISIONING_AP_SSID       "Liwaisi-Config"
#define WIFI_PROVISIONING_AP_PASSWORD   ""  // Open AP for easy config
#define WIFI_PROVISIONING_MAX_CONN      4

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H
