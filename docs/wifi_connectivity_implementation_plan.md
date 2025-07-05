# WiFi Connectivity Implementation Plan

## Overview
This document outlines the detailed implementation plan for WiFi provisioning in the Liwaisi IoT Smart Irrigation System. The implementation follows hexagonal architecture principles and integrates ESP-IDF's WiFi provisioning manager.

## Architecture Design

### Component Structure
```
components/infrastructure/adapters/wifi_adapter/
├── include/
│   ├── wifi_prov_manager.h      # Public API for provisioning
│   ├── boot_counter.h           # Reset detection system
│   └── wifi_connection_manager.h # WiFi connection handling
├── src/
│   ├── wifi_prov_manager.c      # Main provisioning logic
│   ├── boot_counter.c           # Reset pattern detection
│   └── wifi_connection_manager.c # WiFi connection management
└── CMakeLists.txt
```

## Implementation Phases

### Phase 1: Foundation Infrastructure
**Priority**: CRITICAL
**Dependencies**: None
**Estimated Time**: 2-3 hours

#### Tasks:
1. **Create WiFi adapter component structure**
   - Create directory structure in `components/infrastructure/adapters/`
   - Set up CMakeLists.txt with proper dependencies
   - Create basic header files with function signatures

2. **Implement boot counter system**
   - RTC memory management for boot counting
   - Reset pattern detection (5 consecutive power cycles)
   - Counter persistence and clearing logic

3. **Basic WiFi initialization**
   - WiFi stack initialization
   - Event loop setup for WiFi events
   - Basic configuration structures

### Phase 2: Provisioning Core
**Priority**: CRITICAL
**Dependencies**: Phase 1 complete
**Estimated Time**: 4-5 hours

#### Tasks:
4. **Implement WiFi provisioning manager**
   - ESP-IDF provisioning manager initialization
   - SoftAP configuration ("Liwaisi-Config" network)
   - Security configuration (WIFI_PROV_SECURITY_1)
   - Event handler implementation

5. **Create provisioning state machine**
   - Boot → Check Reset → Check Provisioned → Action
   - Provisioning mode vs Normal operation states
   - State transitions and error handling

6. **Implement provisioning event handlers**
   - WIFI_PROV_START, WIFI_PROV_CRED_RECV events
   - WIFI_PROV_CRED_SUCCESS, WIFI_PROV_CRED_FAIL events
   - WIFI_PROV_END event handling

### Phase 3: WiFi Connection Management
**Priority**: CRITICAL
**Dependencies**: Phase 2 complete
**Estimated Time**: 3-4 hours

#### Tasks:
7. **Implement WiFi connection manager**
   - Connect using provisioned credentials
   - Integration with existing WiFi reconnection logic
   - Connection failure handling

8. **Create WiFi event handling system**
   - WiFi connected/disconnected events
   - IP address obtained events
   - Connection retry logic integration

9. **Implement credential persistence**
   - NVS storage for WiFi credentials
   - Credential validation and retrieval
   - Backup/restore functionality

### Phase 4: Main Application Integration
**Priority**: CRITICAL
**Dependencies**: Phase 3 complete
**Estimated Time**: 2-3 hours

#### Tasks:
10. **Modify main application flow**
    - Update app_main() to include provisioning check
    - Integrate with existing WiFi initialization
    - Seamless transition between modes

11. **Update existing WiFi components**
    - Modify existing WiFi connection logic
    - Ensure compatibility with provisioned credentials
    - Update error handling paths

12. **Add configuration management**
    - Menuconfig options for provisioning
    - Compile-time configuration validation
    - Runtime parameter handling

### Phase 5: Testing and Validation
**Priority**: HIGH
**Dependencies**: Phase 4 complete
**Estimated Time**: 3-4 hours

#### Tasks:
13. **Unit testing implementation**
    - Boot counter functionality tests
    - Provisioning state machine tests
    - WiFi connection manager tests

14. **Integration testing**
    - End-to-end provisioning flow
    - Reset detection validation
    - Normal operation after provisioning

15. **Error handling and edge cases**
    - Invalid credentials handling
    - Network connectivity issues
    - Power cycle scenarios

## Technical Implementation Details

### Key Components

#### 1. Boot Counter System
```c
// RTC memory structure for boot counting
RTC_DATA_ATTR static boot_counter_t boot_counter = {0};

typedef struct {
    uint32_t boot_count;
    uint32_t last_boot_time;
    uint32_t magic_number;  // Validation
} boot_counter_t;
```

#### 2. Provisioning Manager
```c
// Main provisioning configuration
wifi_prov_mgr_config_t config = {
    .scheme = wifi_prov_scheme_softap,
    .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
    .app_event_handler = WIFI_PROV_EVENT_HANDLER_NONE
};

// Security configuration
wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
const char *pop = CONFIG_WIFI_PROV_SECURITY_1_POP;
```

#### 3. State Machine
```c
typedef enum {
    WIFI_STATE_INIT,
    WIFI_STATE_CHECK_RESET,
    WIFI_STATE_CHECK_PROVISIONED,
    WIFI_STATE_PROVISIONING,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_ERROR
} wifi_state_t;
```

### Dependencies and Integration Points

#### ESP-IDF Components Required:
- `wifi_provisioning`
- `protocomm`
- `esp_wifi`
- `nvs_flash`
- `esp_event`

#### Integration with Existing System:
- Main application (`main/main.c`)
- Existing WiFi components (if any)
- MQTT client (depends on WiFi connectivity)
- HTTP server (depends on WiFi connectivity)

### Configuration Parameters

#### Menuconfig Options:
```kconfig
menu "WiFi Provisioning Configuration"
    config WIFI_PROV_SECURITY_1_POP
        string "Proof of Possession"
        default "liwaisi2025"
        
    config WIFI_PROV_SOFTAP_SSID
        string "Provisioning Network Name"
        default "Liwaisi-Config"
        
    config WIFI_PROV_RESET_BOOT_COUNT
        int "Boot cycles for reset"
        default 5
endmenu
```

## Risk Assessment and Mitigation

### Technical Risks:
1. **RTC Memory Limitations**: Boot counter might not persist properly
   - **Mitigation**: Implement backup storage in NVS
   
2. **Provisioning Timeout**: Users might abandon setup
   - **Mitigation**: Clear user instructions and feedback
   
3. **Integration Conflicts**: Existing WiFi code might conflict
   - **Mitigation**: Careful integration testing and gradual rollout

### User Experience Risks:
1. **Complex Setup Process**: Users might struggle with captive portal
   - **Mitigation**: Clear documentation and user testing
   
2. **Device Reset Confusion**: Users might not understand reset pattern
   - **Mitigation**: Clear instructions and visual indicators

## Testing Strategy

### Unit Tests:
- Boot counter increment/reset logic
- Provisioning state machine transitions
- WiFi connection manager functions

### Integration Tests:
- Complete provisioning flow
- Reset detection accuracy
- Normal operation after provisioning

### Field Tests:
- Real-world environment testing
- User experience validation
- Device compatibility testing

## Success Criteria

### Technical Success:
- [ ] Device enters provisioning mode after 5 power cycles
- [ ] Captive portal appears automatically on user devices
- [ ] WiFi credentials can be configured successfully
- [ ] Device connects to home WiFi after provisioning
- [ ] Credentials persist across reboots
- [ ] Normal operation resumes after provisioning

### User Experience Success:
- [ ] Setup process takes less than 5 minutes
- [ ] Works with common smartphones and tablets
- [ ] No technical knowledge required
- [ ] Clear error messages and feedback
- [ ] Reliable operation in rural environments

## Implementation Timeline

| Phase | Duration | Dependencies | Critical Path |
|-------|----------|--------------|---------------|
| Phase 1 | 2-3 hours | None | Yes |
| Phase 2 | 4-5 hours | Phase 1 | Yes |
| Phase 3 | 3-4 hours | Phase 2 | Yes |
| Phase 4 | 2-3 hours | Phase 3 | Yes |
| Phase 5 | 3-4 hours | Phase 4 | No |

**Total Estimated Time**: 14-19 hours
**Critical Path**: Phases 1-4 (11-15 hours)

## Next Steps

1. **Set up development environment**: Ensure ESP-IDF is properly configured
2. **Create component structure**: Start with Phase 1 infrastructure
3. **Implement incrementally**: Complete each phase before moving to next
4. **Test continuously**: Validate each component as it's implemented
5. **Document progress**: Keep implementation notes for future reference

---

**Document Version**: 1.0  
**Date**: July 2025  
**Author**: Claude Code Assistant  
**Status**: Implementation Ready