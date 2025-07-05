# ESP32 WiFi Provisioning - Technical Documentation

## 1. Overview

This document defines the WiFi provisioning system for Liwaisi IoT ESP32 devices using ESP-IDF's built-in WiFi provisioning manager. The system allows users to configure WiFi credentials through a captive portal interface without requiring technical knowledge.

### 1.1 User Experience Flow

1. **New/Reset Device**: Device creates "Liwaisi-Config" WiFi network
2. **User Connection**: User connects to "Liwaisi-Config" (open network)
3. **Captive Portal**: Device automatically triggers captive portal on user's phone/computer
4. **WiFi Configuration**: User scans and selects their home WiFi, enters password
5. **Provisioning Complete**: Device connects to home WiFi and starts normal operation
6. **Persistent Connection**: Device remembers WiFi credentials and reconnects automatically

### 1.2 Key Design Decisions

- **Transport**: SoftAP (WiFi Access Point) with captive portal
- **Security**: WIFI_PROV_SECURITY_1 with fixed PoP "liwaisi2025"
- **Network Name**: "Liwaisi-Config" (open network)
- **Reset Method**: 5 consecutive power cycles within short timeframe
- **Timeout**: No timeout - device waits indefinitely in provisioning mode
- **Persistence**: Manual reset only - once provisioned, never auto-provision again

## 2. Technical Architecture

### 2.1 ESP-IDF Components Required

```c
// CMakeLists.txt dependencies
REQUIRES wifi_provisioning protocomm esp_wifi nvs_flash esp_event
```

### 2.2 System State Machine

```
[BOOT] → [CHECK_RESET_PATTERN] → [CHECK_PROVISIONED]
                ↓                        ↓
         [PROVISIONING_MODE]      [NORMAL_OPERATION]
                ↓                        ↓
         [WAIT_FOR_CONFIG]       [CONNECT_SAVED_WIFI]
                ↓                        ↓
         [APPLY_CREDENTIALS] → [NORMAL_OPERATION]
```

### 2.3 Core Configuration

```c
// WiFi Provisioning Configuration
wifi_prov_mgr_config_t config = {
    .scheme = wifi_prov_scheme_softap,
    .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
    .app_event_handler = WIFI_PROV_EVENT_HANDLER_NONE
};

// Security Configuration  
wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
const char *proof_of_possession = "liwaisi2025";

// Service Configuration
const char *service_name = "Liwaisi-Config";  // SoftAP SSID
const char *service_key = NULL;               // No password (open network)
```

## 3. Implementation Components

### 3.1 Reset Detection System

**Purpose**: Detect 5 consecutive power cycles to trigger provisioning mode

**Implementation Strategy**:
- Store boot counter in RTC memory (survives reboots but not power loss)
- Increment counter on each boot
- Reset counter after successful normal operation
- Trigger provisioning when counter reaches 5

**Code Structure**:
```c
// Boot counter management
typedef struct {
    uint32_t boot_count;
    uint32_t last_boot_time;
} boot_counter_t;

esp_err_t check_reset_pattern(bool *should_provision);
esp_err_t increment_boot_counter(void);
esp_err_t clear_boot_counter(void);
```

### 3.2 Provisioning Manager

**Purpose**: Handle WiFi provisioning lifecycle

**Key Functions**:
```c
esp_err_t wifi_prov_init(void);
esp_err_t wifi_prov_start(void);
esp_err_t wifi_prov_check_credentials(bool *provisioned);
void wifi_prov_event_handler(void* arg, esp_event_base_t event_base, 
                            int32_t event_id, void* event_data);
```

**Event Handling**:
- `WIFI_PROV_START`: Log provisioning started
- `WIFI_PROV_CRED_RECV`: Validate received credentials
- `WIFI_PROV_CRED_SUCCESS`: Transition to normal operation
- `WIFI_PROV_CRED_FAIL`: Log failure, continue waiting
- `WIFI_PROV_END`: Cleanup and start normal WiFi operation

### 3.3 WiFi Connection Manager

**Purpose**: Handle connection to user's home WiFi after provisioning

**Responsibilities**:
- Connect using provisioned credentials
- Handle connection failures gracefully
- Integrate with existing WiFi reconnection logic
- Never automatically re-enter provisioning mode

## 4. Integration with Existing System

### 4.1 Modification to Main Application Flow

```c
void app_main(void) {
    // Initialize NVS, WiFi, Event Loop (existing)
    esp_err_t ret = nvs_flash_init();
    wifi_init();
    esp_event_loop_create_default();
    
    // NEW: Check for reset pattern
    bool should_provision = false;
    check_reset_pattern(&should_provision);
    
    if (should_provision) {
        // Enter provisioning mode
        wifi_prov_init();
        wifi_prov_start();
        // This will block until provisioning completes
        wifi_prov_mgr_wait();
        wifi_prov_mgr_deinit();
    }
    
    // Continue with existing WiFi connection logic
    start_wifi_connection();
    
    // Continue with existing application logic
    start_sensor_tasks();
    start_mqtt_client();
    start_http_server();
}
```

### 4.2 Integration with Existing WiFi Manager

The provisioning system should integrate seamlessly with your existing WiFi reconnection logic:

```c
void start_wifi_connection(void) {
    // Check if we have provisioned credentials
    bool provisioned = false;
    wifi_prov_mgr_is_provisioned(&provisioned);
    
    if (provisioned) {
        // Use existing WiFi connection logic
        connect_to_saved_wifi();
        // Your existing backoff/retry logic continues to work
    } else {
        ESP_LOGE(TAG, "No WiFi credentials found - device needs provisioning");
        // Could restart or handle error as needed
    }
}
```

### 4.3 Menuconfig Integration

Add provisioning-specific configurations to your existing menuconfig:

```kconfig
menu "WiFi Provisioning Configuration"
    config WIFI_PROV_SECURITY_1_POP
        string "Proof of Possession for WiFi Provisioning"
        default "liwaisi2025"
        help
            Password/PIN required during WiFi provisioning process
            
    config WIFI_PROV_SOFTAP_SSID
        string "WiFi Provisioning SoftAP SSID" 
        default "Liwaisi-Config"
        help
            Network name shown to users during provisioning
            
    config WIFI_PROV_RESET_BOOT_COUNT
        int "Boot cycles required for provisioning reset"
        default 5
        help
            Number of consecutive boot cycles to trigger provisioning mode
endmenu
```

## 5. File Structure and Organization

### 5.1 Recommended File Organization

```
components/wifi_provisioning/
├── include/
│   └── wifi_prov_manager.h      # Public API
├── src/
│   ├── wifi_prov_manager.c      # Main provisioning logic
│   ├── boot_counter.c           # Reset detection
│   └── prov_handlers.c          # Custom endpoint handlers (if needed)
└── CMakeLists.txt
```

### 5.2 Header File Structure

```c
// wifi_prov_manager.h
#ifndef WIFI_PROV_MANAGER_H
#define WIFI_PROV_MANAGER_H

#include "esp_err.h"
#include "esp_event.h"

// Public API
esp_err_t wifi_prov_manager_init(void);
esp_err_t wifi_prov_manager_start(void);
esp_err_t wifi_prov_manager_is_provisioned(bool *provisioned);
esp_err_t wifi_prov_manager_reset_credentials(void);

// Reset detection
esp_err_t wifi_prov_check_reset_pattern(bool *should_provision);

// Event definitions
ESP_EVENT_DECLARE_BASE(WIFI_PROV_EVENTS);

typedef enum {
    WIFI_PROV_EVENT_STARTED,
    WIFI_PROV_EVENT_CREDENTIALS_RECEIVED,
    WIFI_PROV_EVENT_COMPLETED,
    WIFI_PROV_EVENT_FAILED
} wifi_prov_event_id_t;

#endif
```

## 6. Configuration Parameters

### 6.1 Compile-Time Configuration (menuconfig)

| Parameter | Default Value | Description |
|-----------|---------------|-------------|
| `CONFIG_WIFI_PROV_SECURITY_1_POP` | "liwaisi2025" | Proof of possession for security |
| `CONFIG_WIFI_PROV_SOFTAP_SSID` | "Liwaisi-Config" | SoftAP network name |
| `CONFIG_WIFI_PROV_RESET_BOOT_COUNT` | 5 | Boot cycles for reset detection |
| `CONFIG_WIFI_PROV_SOFTAP_CHANNEL` | 1 | WiFi channel for SoftAP |
| `CONFIG_WIFI_PROV_SOFTAP_MAX_STA` | 4 | Max concurrent connections |

### 6.2 Runtime Behavior

- **Provisioning Timeout**: None (infinite wait)
- **SoftAP IP Range**: 192.168.4.1/24 (ESP-IDF default)
- **DNS Server**: Automatic (captive portal detection)
- **HTTP Server Port**: 80 (standard)

## 7. User Interface Considerations

### 7.1 Captive Portal Behavior

When users connect to "Liwaisi-Config":
1. **Automatic Detection**: Modern devices detect captive portal automatically
2. **Browser Redirect**: Any HTTP request redirects to provisioning interface  
3. **No DNS Required**: ESP32 handles all domain requests internally
4. **Mobile Optimized**: Interface works well on smartphones

### 7.2 Provisioning Interface Features

Standard ESP-IDF provisioning provides:
- **WiFi Network Scanning**: Shows available networks with signal strength
- **Password Input**: Secure password entry for selected network
- **Connection Status**: Real-time feedback on connection attempts
- **Error Handling**: Clear error messages for common issues

## 8. Security Considerations

### 8.1 Security Level Analysis

**WIFI_PROV_SECURITY_1 provides**:
- X25519 key exchange for session establishment
- Proof of possession authentication ("liwaisi2025")
- AES-CTR encryption for all provisioning data
- Protection against eavesdropping and MITM attacks

### 8.2 Trade-offs

**Security**: 
- ✅ Credentials encrypted during transmission
- ✅ Authentication required (PoP)
- ⚠️ Fixed PoP across all devices (acceptable for MVP)

**Usability**:
- ✅ Simple setup process
- ✅ Works with all modern devices
- ✅ No technical knowledge required

## 9. Testing and Validation

### 9.1 Test Cases

**Reset Detection**:
- [ ] Power cycle 4 times - should not trigger provisioning
- [ ] Power cycle 5 times - should trigger provisioning  
- [ ] Mixed power cycles and delays - should reset counter
- [ ] Normal operation after provisioning - should clear counter

**Provisioning Flow**:
- [ ] Connect to "Liwaisi-Config" network
- [ ] Captive portal automatically appears
- [ ] Can scan and see available WiFi networks
- [ ] Can enter WiFi credentials successfully
- [ ] Device connects to home WiFi after provisioning
- [ ] Device remembers credentials after restart

**Error Handling**:
- [ ] Invalid WiFi password - device continues provisioning
- [ ] Network out of range - device continues provisioning
- [ ] Interrupted provisioning - device restarts provisioning mode

### 9.2 Field Testing Recommendations

1. **Rural Environment Testing**: Test in actual rural locations with typical WiFi setups
2. **Device Compatibility**: Test with common smartphones used in Colombia
3. **User Experience**: Test with actual farmers/rural users
4. **Environmental Conditions**: Test reset mechanism in outdoor conditions

## 10. Migration from Current System

### 10.1 Backward Compatibility

The new provisioning system can coexist with your current configuration:
- Existing `CONFIG_WIFI_SSID` and `CONFIG_WIFI_PASSWORD` remain as fallback
- Provisioned credentials take precedence over compile-time credentials
- Existing WiFi reconnection logic continues to work unchanged

### 10.2 Deployment Strategy

**Phase 1**: Implement provisioning in parallel with existing system
**Phase 2**: Test with subset of devices in field
**Phase 3**: Gradually migrate to provisioning-only approach
**Phase 4**: Remove compile-time WiFi credentials

## 11. Troubleshooting Guide

### 11.1 Common Issues

**Device doesn't enter provisioning mode**:
- Verify 5 power cycles happen quickly (within ~30 seconds)
- Check RTC memory is working correctly
- Verify reset detection logic

**Can't connect to "Liwaisi-Config"**:
- Check SoftAP is starting correctly
- Verify no WiFi conflicts (channel availability)
- Ensure device has adequate power

**Captive portal doesn't appear**:
- Try navigating to http://192.168.4.1 manually
- Check device's captive portal detection settings
- Verify HTTP server is running

**Provisioning fails repeatedly**:
- Verify PoP "liwaisi2025" is entered correctly
- Check WiFi credentials are valid
- Ensure target network is within range

### 11.2 Debug Information

Enable verbose logging for provisioning components:
```c
esp_log_level_set("wifi_prov_mgr", ESP_LOG_DEBUG);
esp_log_level_set("protocomm", ESP_LOG_DEBUG);
esp_log_level_set("wifi_prov_scheme", ESP_LOG_DEBUG);
```

## 12. Future Enhancements

### 12.1 Potential Improvements

- **Custom Provisioning Interface**: Replace default with Liwaisi-branded interface
- **Device Information Display**: Show device MAC, firmware version during provisioning
- **Multiple WiFi Networks**: Allow configuration of backup networks
- **Cloud Integration**: Automatically register device with cloud service after provisioning
- **OTA Configuration**: Enable over-the-air updates during provisioning

### 12.2 Advanced Security Options

For future versions, consider:
- Unique PoP per device (printed on device labels)
- Certificate-based authentication
- Integration with mobile app for streamlined setup

---

**Document Version**: 1.0  
**Date**: July 2025  
**Author**: Liwaisi Tech Team  
**Status**: Implementation Ready

