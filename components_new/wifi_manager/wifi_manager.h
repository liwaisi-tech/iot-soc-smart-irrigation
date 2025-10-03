/**
 * @file wifi_manager.h
 * @brief WiFi Manager Component - Consolidated WiFi Management
 *
 * Consolidates provisioning, connection management, and boot counter functionality.
 * Component-based architecture - no dependencies on domain/application layers.
 *
 * Component Responsibilities:
 * - WiFi initialization and connection
 * - Web-based provisioning with credential validation
 * - Automatic reconnection with retry logic
 * - Boot counter for factory reset pattern (3 reboots in 30s)
 * - Connection status monitoring
 * - IP address management
 *
 * Migration from hexagonal: Consolidates wifi_adapter + wifi_connection_manager +
 *                          wifi_prov_manager + boot_counter
 *
 * @author Liwaisi Tech
 * @date 2025-10-03
 * @version 2.0.0 - Component-Based Architecture Migration
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

/* ============================ EVENT BASES ============================ */

/**
 * @brief WiFi manager event base
 */
ESP_EVENT_DECLARE_BASE(WIFI_MANAGER_EVENTS);

/**
 * @brief WiFi provisioning events
 */
ESP_EVENT_DECLARE_BASE(WIFI_MANAGER_PROV_EVENTS);

/**
 * @brief WiFi connection events
 */
ESP_EVENT_DECLARE_BASE(WIFI_MANAGER_CONNECTION_EVENTS);

/* ============================ TYPES AND ENUMS ============================ */

/**
 * @brief WiFi manager main state
 */
typedef enum {
    WIFI_MANAGER_STATE_UNINITIALIZED = 0,   ///< Component not initialized
    WIFI_MANAGER_STATE_CHECKING_PROVISION,  ///< Checking provisioning status
    WIFI_MANAGER_STATE_PROVISIONING,        ///< In provisioning mode (SoftAP)
    WIFI_MANAGER_STATE_CONNECTING,          ///< Connection attempt in progress
    WIFI_MANAGER_STATE_CONNECTED,           ///< Connected to AP
    WIFI_MANAGER_STATE_DISCONNECTED,        ///< Disconnected from AP
    WIFI_MANAGER_STATE_ERROR                ///< Error state
} wifi_manager_state_t;

/**
 * @brief WiFi connection state (internal)
 */
typedef enum {
    WIFI_CONNECTION_STATE_IDLE = 0,         ///< Idle state
    WIFI_CONNECTION_STATE_CONNECTING,       ///< Connecting
    WIFI_CONNECTION_STATE_CONNECTED,        ///< Connected
    WIFI_CONNECTION_STATE_DISCONNECTED,     ///< Disconnected
    WIFI_CONNECTION_STATE_FAILED            ///< Connection failed
} wifi_manager_connection_state_t;

/**
 * @brief WiFi provisioning state (internal)
 */
typedef enum {
    WIFI_PROV_STATE_INIT = 0,               ///< Initialized
    WIFI_PROV_STATE_PROVISIONING,           ///< Provisioning active
    WIFI_PROV_STATE_VALIDATING,             ///< Validating credentials
    WIFI_PROV_STATE_CONNECTED,              ///< Connected successfully
    WIFI_PROV_STATE_ERROR                   ///< Error state
} wifi_manager_prov_state_t;

/**
 * @brief WiFi credential validation result
 */
typedef enum {
    WIFI_VALIDATION_OK = 0,                 ///< Credentials valid
    WIFI_VALIDATION_AUTH_FAILED,            ///< Authentication failed (wrong password)
    WIFI_VALIDATION_NETWORK_NOT_FOUND,      ///< Network SSID not found
    WIFI_VALIDATION_TIMEOUT                 ///< Connection timeout
} wifi_manager_validation_result_t;

/**
 * @brief WiFi manager status
 */
typedef struct {
    wifi_manager_state_t state;             ///< Current WiFi state
    bool provisioned;                       ///< Has valid credentials stored
    bool connected;                         ///< Connected to WiFi
    bool has_ip;                            ///< Has valid IP address
    char ssid[33];                          ///< Connected SSID (or empty)
    uint8_t mac_address[6];                 ///< Device MAC address
    esp_ip4_addr_t ip_addr;                 ///< Current IP address
} wifi_manager_status_t;

/* ============================ EVENT IDs ============================ */

/**
 * @brief WiFi manager main event IDs
 */
typedef enum {
    WIFI_MANAGER_EVENT_INIT_COMPLETE = 0,   ///< Initialization complete
    WIFI_MANAGER_EVENT_PROVISIONING_STARTED,///< Provisioning started
    WIFI_MANAGER_EVENT_PROVISIONING_COMPLETED,///< Provisioning completed
    WIFI_MANAGER_EVENT_CONNECTED,           ///< Connected to AP
    WIFI_MANAGER_EVENT_DISCONNECTED,        ///< Disconnected from AP
    WIFI_MANAGER_EVENT_IP_OBTAINED,         ///< IP address obtained
    WIFI_MANAGER_EVENT_CONNECTION_FAILED,   ///< Connection failed
    WIFI_MANAGER_EVENT_RESET_REQUESTED      ///< Factory reset requested (boot pattern)
} wifi_manager_event_id_t;

/**
 * @brief WiFi provisioning event IDs
 */
typedef enum {
    WIFI_PROV_EVENT_STARTED = 0,            ///< Provisioning started
    WIFI_PROV_EVENT_CREDENTIALS_RECEIVED,   ///< Credentials received from user
    WIFI_PROV_EVENT_CREDENTIALS_SUCCESS,    ///< Credentials validated successfully
    WIFI_PROV_EVENT_CREDENTIALS_FAILED,     ///< Credentials validation failed
    WIFI_PROV_EVENT_AUTH_FAILED,            ///< WiFi authentication failed
    WIFI_PROV_EVENT_NETWORK_NOT_FOUND,      ///< Network not found
    WIFI_PROV_EVENT_VALIDATION_TIMEOUT,     ///< Validation timeout
    WIFI_PROV_EVENT_COMPLETED,              ///< Provisioning completed
    WIFI_PROV_EVENT_FAILED                  ///< Provisioning failed
} wifi_prov_event_id_t;

/**
 * @brief WiFi connection event IDs
 */
typedef enum {
    WIFI_CONNECTION_EVENT_STARTED = 0,      ///< Connection started
    WIFI_CONNECTION_EVENT_CONNECTED,        ///< Connected to AP
    WIFI_CONNECTION_EVENT_DISCONNECTED,     ///< Disconnected from AP
    WIFI_CONNECTION_EVENT_GOT_IP,           ///< IP address obtained
    WIFI_CONNECTION_EVENT_AUTH_FAILED,      ///< Authentication failed
    WIFI_CONNECTION_EVENT_NETWORK_NOT_FOUND,///< Network not found
    WIFI_CONNECTION_EVENT_RETRY_EXHAUSTED   ///< Max retries reached
} wifi_connection_event_id_t;

/* ============================ PUBLIC API ============================ */

/**
 * @brief Initialize WiFi manager component
 *
 * Initializes ESP-IDF WiFi stack, netif, event handlers, and boot counter.
 * Must be called before any other WiFi operations.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Start WiFi manager
 *
 * Checks provisioning status and either:
 * - Connects to stored credentials if available
 * - Enters provisioning mode if no credentials found
 * - Enters provisioning mode if boot reset pattern detected (3 reboots in 30s)
 *
 * @return ESP_OK if started successfully, error code otherwise
 */
esp_err_t wifi_manager_start(void);

/**
 * @brief Stop WiFi manager
 *
 * Stops WiFi connection and provisioning mode.
 * Does not deinitialize the component.
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
 * @brief Check provisioning status and connect
 *
 * Checks if device is provisioned:
 * - If provisioned → connect with stored credentials
 * - If not provisioned → enter provisioning mode
 * - If boot reset pattern detected → force provisioning
 *
 * @return ESP_OK if operation started successfully
 */
esp_err_t wifi_manager_check_and_connect(void);

/**
 * @brief Force WiFi provisioning mode
 *
 * Stops any active connection and starts provisioning mode.
 * Creates SoftAP "Liwaisi-Config" for web-based configuration.
 * Web interface available at http://192.168.4.1
 *
 * @return ESP_OK if provisioning started successfully
 */
esp_err_t wifi_manager_force_provisioning(void);

/**
 * @brief Reset WiFi credentials
 *
 * Clears stored WiFi credentials from NVS.
 * Next start will enter provisioning mode.
 *
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_reset_credentials(void);

/**
 * @brief Clear ALL WiFi credentials (including ESP-IDF internal storage)
 *
 * Clears all WiFi credentials from:
 * - Component internal state
 * - ESP-IDF WiFi NVS storage (nvs.net80211)
 * - Legacy NVS keys
 *
 * This is a complete factory reset for WiFi settings.
 *
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_clear_all_credentials(void);

/**
 * @brief Get current WiFi manager status
 *
 * @param[out] status Pointer to status structure to fill
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if status is NULL
 */
esp_err_t wifi_manager_get_status(wifi_manager_status_t *status);

/**
 * @brief Get current WiFi manager state
 *
 * @return Current state
 */
wifi_manager_state_t wifi_manager_get_state(void);

/**
 * @brief Check if WiFi credentials are provisioned
 *
 * @return true if valid credentials stored in NVS
 */
bool wifi_manager_is_provisioned(void);

/**
 * @brief Check if WiFi is connected with IP
 *
 * @return true if connected and has valid IP, false otherwise
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Get current IP address
 *
 * @param[out] ip Pointer to IP address structure to fill
 * @return ESP_OK if connected and has IP, ESP_ERR_INVALID_STATE otherwise
 */
esp_err_t wifi_manager_get_ip(esp_ip4_addr_t *ip);

/**
 * @brief Get device MAC address
 *
 * @param[out] mac Pointer to 6-byte MAC address buffer
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if mac is NULL
 */
esp_err_t wifi_manager_get_mac(uint8_t *mac);

/**
 * @brief Get current SSID
 *
 * @param[out] ssid Buffer to store SSID (min 33 bytes)
 * @param ssid_len Size of ssid buffer
 * @return ESP_OK if available, ESP_ERR_INVALID_ARG if parameters invalid
 */
esp_err_t wifi_manager_get_ssid(char *ssid, size_t ssid_len);

/* ============================ CONFIGURATION ============================ */

/**
 * @brief Provisioning SoftAP SSID
 */
#define WIFI_MANAGER_PROV_AP_SSID "Liwaisi-Config"

/**
 * @brief Provisioning web interface IP
 */
#define WIFI_MANAGER_PROV_IP "192.168.4.1"

/**
 * @brief Boot counter reset threshold (number of reboots)
 */
#define WIFI_MANAGER_RESET_BOOT_COUNT 3

/**
 * @brief Boot counter time window (milliseconds)
 */
#define WIFI_MANAGER_RESET_TIME_WINDOW_MS 30000

/**
 * @brief Default maximum connection retries
 */
#define WIFI_MANAGER_DEFAULT_MAX_RETRIES 5

/**
 * @brief WiFi credential validation timeout (milliseconds)
 */
#define WIFI_MANAGER_VALIDATION_TIMEOUT_MS 15000

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H
