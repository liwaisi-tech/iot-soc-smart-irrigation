# CLAUDE.md - Smart Irrigation System Project Guide

**Last Updated**: 2025-10-26 | **Version**: 2.1.0 | **Phase**: Phase 5 (In Progress)

## 🔴 CRITICAL: PHASE 5 RULES

1. **COMPONENT-BASED MANDATORY**: SRC, MIS, DD, Memory-First, Task-Oriented (see ESTADO_ACTUAL_IMPLEMENTACION.md)
2. **REFERENCE TEMPLATES**: sensor_reader, device_config, mqtt_client (thread-safe + validated)
3. **THREAD-SAFETY**: All shared state protected (spinlock/mutex) - ZERO exceptions
4. **EXECUTION PLAN**: Read PLAN_EJECUCION_IRRIGATION_CONTROLLER.md first, then Logica_de_riego.md
5. **BINARY CONSTRAINTS**: Current 942.80 KB | Max 1.5 MB | After irrigation_controller: < 1.4 MB
6. **NO PHASE MIXING**: irrigation_controller Phase 5 ONLY - complete 100% before system_monitor
7. **DOCUMENTATION**: Update CLAUDE.md + ESTADO_ACTUAL_IMPLEMENTACION.md + commit after each step

---

## 🎯 CURRENT STATUS - Phase 4 Complete, Phase 5 Ready

| Component | Status | Thread-Safe | Location | Notes |
|-----------|--------|---|---|---|
| sensor_reader | ✅ | ✅ 100% | `components/sensor_reader/` | DHT22 + 3 ADC |
| device_config | ✅ | ✅ 100% | `components/device_config/` | NVS + WiFi + MQTT + Irrigation |
| wifi_manager | ✅ | ✅ 100% | `components/wifi_manager/` | 4 spinlocks protecting state |
| mqtt_client | ✅ | ✅ 100% | `components/mqtt_client/` | Kconfig: MQTT_BROKER_URI |
| http_server | ✅ | ✅ 100% | `components/http_server/` | REST endpoints |
| **notification_service** | ✅ | ✅ 100% | `components/notification_service/` | **NEW**: N8N webhooks (refactored from irrigation_controller) |
| **irrigation_controller** | ✅ | ✅ 100% | `components/irrigation_controller/` | **Phase 5 Complete**: valve control + state machine |

**Binary**: 951.82 KB (54% free = 1.13 MB) | **Compilation**: ✅ No errors | **ESP-IDF**: v5.4.2

---

## 📚 PHASE 5 DOCUMENTATION MAP - START HERE

**Read in this order:**
1. **PLAN_EJECUCION_IRRIGATION_CONTROLLER.md** ← **READ FIRST**
2. **Logica_de_riego.md** - Irrigation logic & thresholds
3. **detalles_implementacion_nva_arqutectura.md** - Architecture principles (lines 49-103)
4. **ESTADO_ACTUAL_IMPLEMENTACION.md** - Thread-safety patterns (lines 162-180)

---


## Project Overview

This is a **Smart Irrigation System** IoT project built with **ESP-IDF** for ESP32, successfully **migrated** from Hexagonal Architecture to **Component-Based Architecture**.

**Target Market**: Rural Colombia
**Architecture**: Component-Based Architecture (migration completed 2025-10-09)
**Framework**: ESP-IDF v5.4.2
**Language**: C (Clean Code for embedded systems)
**IDE**: Visual Studio Code with ESP-IDF extension

## Architecture (Component-Based v2.0.0)

| Component | Purpose | Status | Thread-Safe |
|-----------|---------|--------|-------------|
| **sensor_reader** | DHT22 + 3x ADC sensors | ✅ 100% | ✅ portMUX_TYPE |
| **device_config** | NVS configuration (WiFi, MQTT, Irrigation) | ✅ 100% | ✅ Mutex |
| **wifi_manager** | WiFi + provisioning + boot counter | ✅ 100% | ✅ 3 spinlocks |
| **mqtt_client** | MQTT + WebSockets (Kconfig: MQTT_BROKER_URI) | ✅ 100% | ✅ Task-based |
| **http_server** | REST endpoints (/whoami, /sensors, /ping) | ✅ 100% | ✅ ESP-IDF native |
| **notification_service** | N8N webhooks (refactored from irrigation_controller) | ✅ 100% | ✅ portMUX_TYPE |
| **irrigation_controller** | Valve control, state machine, MQTT commands | ✅ 100% | ✅ portMUX_TYPE |

## Component-Based Architecture Principles

### **Architecture Philosophy**
Direct, simple, resource-optimized component design for embedded systems.

**Core Principles Applied**:
1. **Single Responsibility Component (SRC)**: Each component has ONE specific responsibility
2. **Minimal Interface Segregation (MIS)**: Small, focused public APIs
3. **Direct Dependencies (DD)**: Components depend directly on each other, no excessive abstraction
4. **Memory-First Design**: Static allocation, stack-based temporary data
5. **Task-Oriented**: FreeRTOS tasks with optimized stack sizes

### **Component Dependencies**
```
main.c
  ├──> wifi_manager
  ├──> mqtt_client ──> device_config
  ├──> http_server ──> sensor_reader ──> device_config
  ├──> sensor_reader
  ├──> notification_service ──> wifi_manager
  ├──> irrigation_controller ──> notification_service
  │                         ├──> sensor_reader
  │                         ├──> wifi_manager
  │                         └──> mqtt_client
  └──> device_config

Direct dependencies, no layered abstraction.
Each component manages its own thread-safety internally.
```

### **Key Architectural Decisions**
- ❌ **Removed**: Hexagonal Architecture (too complex for ESP32)
- ❌ **Removed**: `shared_resource_manager` (violated SRC/MIS/DD)
- ❌ **Removed**: N8N webhooks from `irrigation_controller` (violated SRC) → extracted to `notification_service`
- ✅ **Added**: Thread-safety per component (mutexes/spinlocks internos)
- ✅ **Added**: Direct component-to-component communication
- ✅ **Added**: `notification_service` component for reusable webhook functionality
- ✅ **Simplified**: Configuration consolidated in `device_config`

## Key Features & Requirements

### Core Functionality
- **Environmental Monitoring**: Temperature, humidity sensors (DHT22)
- **Soil Monitoring**: 1-3 soil moisture sensors (analog ADC)
- **Irrigation Control**: Valve control via MQTT and offline mode
- **WiFi Connectivity**: Auto-reconnection with exponential backoff
- **MQTT Communication**: Data publishing and command subscription over WebSockets
- **HTTP API**: Device info and sensor endpoints
- **Offline Mode**: Automatic irrigation when network is unavailable
- **Concurrency**: Semaphore-based resource management

### Technical Specifications
- **Target**: ESP32 dual-core Xtensa LX6
- **Memory**: < 200KB RAM, < 1MB Flash
- **Connectivity**: WiFi, MQTT over WebSockets
- **Protocols**: HTTP REST API, MQTT
- **Power**: Battery + solar optimized
- **Real-time**: FreeRTOS-based task management

## Development Phases Status

| Phase | Status | Key Achievements |
|-------|--------|-----------------|
| 1-3 | ✅ COMPLETE | Infrastructure, sensors, MQTT communication |
| **4** | ✅ **COMPLETE** | Architecture validation, thread-safety 100%, 5 components migrated |
| **5** | 📋 **READY** | Irrigation controller (see PLAN_EJECUCION_IRRIGATION_CONTROLLER.md) |
| 6 | ⏳ FUTURE | Optimization, OTA updates, wifi_manager refactoring |

### Phase 5: Irrigation Control (NEXT)
**See**: PLAN_EJECUCION_IRRIGATION_CONTROLLER.md + Logica_de_riego.md
- Valve control drivers (GPIO/relay)
- Irrigation state machine + MQTT commands
- Offline automatic irrigation + safety interlocks
**Prerequisites**: ✅ All met (architecture validated, thread-safe, binaries OK)


## Hardware Configuration

### GPIO Pin Assignments (Standardized)
```c
// Irrigation valve control (relay outputs)
// Phase 5: GPIO_NUM_25 (LED simulator)
// Phase 6: GPIO_NUM_2, GPIO_NUM_4, GPIO_NUM_5 (relay modules)
#define IRRIGATION_VALVE_1_GPIO     GPIO_NUM_25    // Phase 5: LED simulator
#define IRRIGATION_VALVE_2_GPIO     GPIO_NUM_4     // Phase 6: Relay IN2
#define IRRIGATION_VALVE_3_GPIO     GPIO_NUM_5     // Phase 6: Relay IN3

// Environmental sensor (DHT22)
#define DHT22_DATA_GPIO            GPIO_NUM_18

// Soil moisture sensors (analog ADC)
#define SOIL_MOISTURE_1_ADC        ADC1_CHANNEL_0  // GPIO 36
#define SOIL_MOISTURE_2_ADC        ADC1_CHANNEL_3  // GPIO 39
#define SOIL_MOISTURE_3_ADC        ADC1_CHANNEL_6  // GPIO 34

// I2C (Reserved - NOT in use currently)
// GPIO_NUM_21 = SDA (available for future I2C)
// GPIO_NUM_22 = SCL (available for future I2C)
```

### Power Requirements
- **ESP32**: 3.3V, 240mA (active), 5μA (deep sleep)
- **Flash Memory**: 4MB (standard ESP32 DevKit)
- **DHT22**: 3.3-5V, 2.5mA (active), 60μA (standby)
- **Soil Sensors**: 3.3V, 5mA each
- **Relay Module**: 5V, 70mA per relay
- **Total**: ~400mA active, optimized for battery + solar

## Development Guidelines

### Clean Code Principles (Project-Specific)
```c
// Function naming convention: domain_entity_action_context
esp_err_t sensor_manager_read_environmental_data(ambient_sensor_data_t* data);
esp_err_t irrigation_logic_evaluate_soil_conditions(const soil_sensor_data_t* soil_data);
esp_err_t device_config_service_get_crop_name(char* crop_name, size_t max_len);

// Constants with project context
#define IRRIGATION_DEFAULT_SOIL_THRESHOLD_PERCENT 45
#define IRRIGATION_MAX_DURATION_MINUTES 30
#define SENSOR_READING_INTERVAL_SECONDS 30
#define MQTT_RECONNECTION_MAX_INTERVAL_SECONDS 3600

// Error handling with domain context
typedef enum {
    IRRIGATION_OK = 0,
    IRRIGATION_ERROR_SENSOR_FAILURE,
    IRRIGATION_ERROR_VALVE_MALFUNCTION,
    IRRIGATION_ERROR_NETWORK_TIMEOUT,
    IRRIGATION_ERROR_INVALID_THRESHOLD
} irrigation_error_t;
```

### ESP32 Specific Best Practices
- **Task Management**: Sensor task (4KB stack), MQTT task (6KB stack), HTTP task (4KB stack)
- **Memory Usage**: Heap monitoring, no memory leaks, circular buffers for sensor history
- **GPIO Management**: Proper pin configuration with pull-up/pull-down resistors
- **Power Management**: Light sleep between sensor readings, deep sleep for maintenance periods
- **Watchdog**: Task feeding, proper timeout handling

### Architecture Rules (Enforced)
1. **Domain Independence**: Domain layer uses only standard C, no ESP-IDF dependencies
2. **Dependency Inversion**: Infrastructure implements application interfaces
3. **Single Responsibility**: Each component has one clear purpose
4. **Open/Closed**: Easy to extend sensors or communication protocols
5. **Interface Segregation**: Small, focused interfaces between layers

## API Specifications

**HTTP Endpoints**: `/whoami` (device info), `/sensors` (readings), `/ping` (health check)
**MQTT Topics**: `irrigation/register`, `irrigation/data/{crop}/{mac}`, `irrigation/control/{mac}`
**Configuration**: See `menuconfig` (WiFi, MQTT_BROKER_URI via Kconfig, thresholds, intervals)
**NVS Keys**: `wifi_ssid`, `wifi_pass`, `mqtt_broker`, `device_name`, `crop_name`, `soil_thresh`

---

## Testing & Deployment

### Quick Test
```bash
get_idf && idf.py build && idf.py size-components
curl http://192.168.1.100/whoami  # Verify device
curl http://192.168.1.100/sensors # Check sensor data
```

### Deployment Checklist
1. Configure WiFi + MQTT broker (menuconfig)
2. Calibrate soil moisture sensors (dry/wet)
3. Test irrigation valve response
4. Verify MQTT connectivity
5. Configure thresholds (45% default soil)
6. Test offline mode
7. Install in IP65 enclosure

## Performance Expectations

### Resource Usage Targets
- **RAM**: < 200KB continuous operation (heap monitoring enabled)
- **Flash**: < 1MB total application size
- **CPU**: < 50% average utilization across both cores
- **Power**: < 100mA average (excluding valve operation)

### Timing Requirements
- **Sensor Reading**: 30-second intervals (configurable 10-300 seconds)
- **Data Publishing**: 30-second intervals (configurable)
- **HTTP Response**: < 500ms for all endpoints
- **MQTT Response**: < 2 seconds for command processing
- **Valve Activation**: < 2 seconds from command to physical response

### Network Performance
- **WiFi Connection**: < 10 seconds initial connection
- **WiFi Reconnection**: Exponential backoff (10s → 30s → 60s → 300s → 3600s max)
- **MQTT Keep-Alive**: 60 seconds with QoS 1 for reliability
- **Offline Mode**: Automatic activation after 300 seconds without connectivity

---

## Debug Commands

```bash
get_idf                    # Activate ESP-IDF
idf.py build              # Build project
idf.py size-components    # Check binary size
idf.py monitor            # Monitor output
idf.py menuconfig         # Configure project
```

### Field Diagnostic Endpoints
```bash
# Device health check
curl http://192.168.1.100/whoami

# Real-time sensor readings
curl http://192.168.1.100/sensors

# Network connectivity test
curl http://192.168.1.100/ping
```

## Deployment Considerations

### Hardware Requirements
- **Microcontroller**: ESP32 DevKit (38-pin recommended)
- **Environmental Sensor**: DHT22 (temperature/humidity)
- **Soil Sensors**: Capacitive soil moisture sensors (1-3 units)
- **Irrigation Control**: 5V  2 relay module spdt (2 channels)
- **Valve**: 4,7 v solenoid valve lanch type de 90º angulo
- **Power Supply**: 12V/5A with battery backup and solar panel
- **Enclosure**: IP65 waterproof enclosure for outdoor installation

### Network Requirements
- **WiFi Access Point**: 2.4GHz, WPA2/WPA3 security
- **Internet Connectivity**: Stable broadband for MQTT broker access
- **MQTT Broker**: Supporting WebSockets (port 1883 or 8083)
- **Bandwidth**: Minimal - ~1KB/minute data transmission

### Field Installation
```c
// Pre-deployment checklist
#define DEPLOYMENT_CHECKLIST \
    "1. Configure WiFi credentials via menuconfig\n" \
    "2. Set device name and crop type\n" \
    "3. Calibrate soil moisture sensors (dry/wet)\n" \
    "4. Test irrigation valve response\n" \
    "5. Verify MQTT broker connectivity\n" \
    "6. Configure irrigation thresholds\n" \
    "7. Test offline mode functionality\n" \
    "8. Install in weatherproof enclosure\n" \
    "9. Connect power and backup systems\n" \
    "10. Monitor operation for 24 hours"
```

### Remote Monitoring Setup
- **MQTT Broker**: Cloud-hosted or local server
- **Dashboard**: Real-time sensor monitoring
- **Alerts**: Low battery, connectivity issues, sensor failures
- **OTA Updates**: Remote firmware updates for field devices

## Getting Started for Developers

### Development Environment Setup
```bash
# 1. Install ESP-IDF v5.4.2
git clone -b v5.4.2 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh && . ./export.sh

# 2. Clone project repository
git clone https://github.com/liwaisi-tech/iot-soc-smart-irrigation.git
cd iot-soc-smart-irrigation

# 3. Configure project
idf.py menuconfig

# 4. Build and flash
Get_idf
idf.py build flash monitor
```

### AI Assistant Integration
For AI-assisted development, use the specialized agent prompt:
```
# Use this agent prompt for development assistance:
File: esp32_smart_irrigation_agent_prompt.md

# This agent understands:
- Hexagonal architecture patterns for ESP32
- Domain-driven design for IoT systems
- Project-specific sensor integration
- Rural deployment considerations
- Clean code practices for embedded C
```

### Development Workflow
1. **Understand Architecture**: Study hexagonal layer separation
2. **Follow Phases**: Implement according to development phases
3. **Domain-First**: Start with business logic, then application, then infrastructure
4. **Test Incrementally**: Unit tests → integration tests → hardware validation
5. **Document Changes**: Update this guide when adding features

### Code Quality Standards
- **Functions**: Max 20 lines, single responsibility
- **Variables**: Descriptive names with project context
- **Comments**: Explain business rules and safety considerations
- **Error Handling**: Always check return values, log appropriately
- **Memory Management**: Zero leaks, proper resource cleanup
---

## Project Status Summary

| Item | Status | Details |
|------|--------|---------|
| **Version** | 2.0.0 | Component-Based Architecture |
| **Phase** | ✅ 4 Complete, 5 Ready | Next: irrigation_controller |
| **Architecture** | ✅ 100% | Component-based in `/components/` |
| **Thread-Safety** | ✅ 100% | All 5 components protected |
| **Binary** | 942.80 KB | 56% free (1.14 MB available) |
| **Compilation** | ✅ No errors | 5/5 components migrated |

**Key Achievements (Oct 18)**:
- ✅ Component-based architecture fully migrated
- ✅ Thread-safety 100% (spinlocks + mutexes)
- ✅ MQTT configurable via Kconfig
- ✅ system_monitor Phase 6 - documentation complete

**Tech Debt**: wifi_manager refactoring deferred to Phase 6 (non-blocking)