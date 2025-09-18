# CLAUDE.md - Smart Irrigation System Project Guide

## Project Overview

This is a **Smart Irrigation System** IoT project built with **ESP-IDF** for ESP32, implementing **Hexagonal Architecture** (Ports and Adapters) with **Domain Driven Design** (DDD). The system monitors environmental and soil sensors, controls irrigation valves via MQTT, and provides HTTP endpoints for device information.

**Target Market**: Rural Colombia
**Architecture**: Hexagonal (Clean Architecture)
**Framework**: ESP-IDF v5.4.2
**Language**: C (Clean Code for embedded systems)
**IDE**: Visual Studio Code with ESP-IDF extension
**Specialized Agent**: Use `esp32_smart_irrigation_agent_prompt.md` for AI assistance

## Project Structure & Architecture

```
smart_irrigation_system/
├── components/
│   ├── domain/              # Pure business logic (Domain Layer)
│   │   ├── entities/        # Core business entities
│   │   │   ├── sensor.h     # Sensor entity
│   │   │   ├── irrigation.h # Irrigation entity
│   │   │   └── device.h     # Device entity
│   │   ├── value_objects/   # Immutable value objects
│   │   │   ├── ambient_sensor_data.h         # Temperature/humidity readings
│   │   │   ├── soil_sensor_data.h            # Soil moisture readings
│   │   │   ├── complete_sensor_data.h        # Combined sensor payload
│   │   │   ├── device_info.h                 # Device information
│   │   │   └── device_registration_message.h # MQTT registration format
│   │   ├── repositories/    # Data access interfaces
│   │   └── services/        # Domain services
│   │       ├── irrigation_logic.h  # Irrigation business rules
│   │       ├── sensor_manager.h    # Sensor management logic
│   │       └── device_config_service.h  # Device configuration logic
│   ├── application/         # Use Cases (Application Layer)
│   │   ├── use_cases/       # Application use cases
│   │   │   ├── read_sensors.h         # Read sensors use case
│   │   │   ├── control_irrigation.h   # Control irrigation use case
│   │   │   ├── device_registration.h  # Device registration use case
│   │   │   └── publish_sensor_data.h  # Data publishing use case
│   │   ├── dtos/            # Data Transfer Objects
│   │   └── ports/           # Application ports (interfaces)
│   └── infrastructure/      # External adapters (Infrastructure Layer)
│       ├── adapters/        # External service adapters
│       │   ├── mqtt_adapter/     # MQTT communication adapter
│       │   ├── http_adapter/     # HTTP server adapter
│       │   ├── wifi_adapter/     # WiFi connectivity adapter
│       │   ├── json_device_serializer.h  # JSON serialization
│       │   └── shared_resource_manager.h # Semaphore coordination
│       ├── drivers/         # Hardware drivers
│       │   ├── dht_sensor/       # Environmental sensor driver (DHT22)
│       │   ├── soil_moisture/    # Soil moisture sensor drivers (ADC)
│       │   └── valve_control/    # Valve control drivers (GPIO/relay)
│       ├── network/         # Network implementations
│       └── persistence/     # Data persistence (NVS)
├── main/                    # Application entry point
│   ├── main.c              # Main application file
│   └── CMakeLists.txt      # Main component build config
├── docs/                   # Project documentation
├── tests/                  # Unit and integration tests
└── config/                 # Configuration files
```

## Hexagonal Architecture Implementation

### Layer Dependencies (Strictly Enforced)
```
Infrastructure Layer → Application Layer → Domain Layer

✓ ALLOWED:   Infrastructure depends on Application interfaces
✓ ALLOWED:   Application depends on Domain entities
✗ FORBIDDEN: Domain depends on anything external
✗ FORBIDDEN: Application depends on Infrastructure directly
```

### Domain Layer (Pure Business Logic)
- **Zero Dependencies**: No ESP-IDF, no external libraries, only standard C
- **Business Rules**: Irrigation thresholds, sensor validation, safety interlocks
- **Entities**: Represent core business concepts (sensors, irrigation, device)
- **Value Objects**: Immutable data structures (sensor readings, commands)
- **Services**: Domain business logic (irrigation algorithms, sensor coordination)

### Application Layer (Use Cases)
- **Orchestration**: Coordinates domain services and infrastructure adapters
- **Use Cases**: Specific application behaviors (read sensors, control irrigation)
- **Ports**: Interfaces to infrastructure (repository interfaces, adapter interfaces)
- **DTOs**: Data transfer between layers

### Infrastructure Layer (External Adapters)
- **Adapters**: Implement application ports (MQTT, HTTP, WiFi)
- **Drivers**: Hardware abstraction (sensors, valves, GPIO)
- **Persistence**: Configuration storage (NVS)
- **Network**: Communication protocols implementation

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

## Development Phases

### Phase 1: Basic Infrastructure (CRITICAL)
**Status**: ✅ COMPLETED
- WiFi connectivity with reconnection ✅
- MQTT client over WebSockets ✅
- Basic HTTP server ✅
- Semaphore system for concurrency ✅

**Current Implementation**:
- `wifi_adapter/` - WiFi connection management with exponential backoff
- `mqtt_adapter/` - MQTT client with WebSocket support
- `http_adapter/` - HTTP server with middleware architecture
- `shared_resource_manager` - Semaphore coordination system

### Phase 2: Data & Sensors (CRITICAL)
**Status**: ✅ COMPLETED
- Sensor data structures ✅
- Sensor abstraction layer ✅
- Periodic sensor reading task ✅

**Current Implementation**:
- `domain/value_objects/` - Complete sensor data structures
- `dht_sensor/` - Environmental sensor driver
- Domain services for sensor management

### Phase 3: Data Communication (CRITICAL)
**Status**: ✅ COMPLETED
- Device registration message ✅
- MQTT data publishing ✅
- HTTP endpoints (/whoami, /sensors) ✅

**Current Implementation**:
- `use_cases/publish_sensor_data` - Data publishing use case
- `json_device_serializer` - JSON serialization for MQTT/HTTP
- HTTP endpoints with proper middleware

### Phase 4: Irrigation Control (IN PROGRESS)
**Status**: 🚧 IN PROGRESS
- MQTT command subscription ⏳
- Valve control system ⏳
- Offline automatic irrigation logic ⏳

**Next Steps**:
1. Implement irrigation command value objects
2. Add irrigation control use case
3. Implement valve control drivers
4. Add offline mode logic

### Phase 5: Optimization (PENDING)
**Status**: ⏳ PENDING
- Memory management & sleep modes
- Final task scheduling system
- Complete integration testing

## API Specifications

### HTTP Endpoints
- `GET /whoami` - Device information and available endpoints
- `GET /sensors` - Immediate reading of all sensors
- `GET /ping` - Health check endpoint

### MQTT Topics
- `irrigation/register` - Device registration
- `irrigation/data/{crop_name}/{mac_address}` - Sensor data publishing
- `irrigation/control/{mac_address}` - Irrigation commands (start/stop)

### Data Formats

#### Device Registration (MQTT)
```json
{
  "event_type": "device_registration",
  "mac_address": "AA:BB:CC:DD:EE:FF",
  "ip_address": "192.168.1.100",
  "device_name": "ESP32_Cultivo_01",
  "crop_name": "tomates",
  "firmware_version": "v1.0.0"
}
```

#### Sensor Data Publishing (MQTT)
```json
{
  "event_type": "sensor_data",
  "mac_address": "AA:BB:CC:DD:EE:FF",
  "ip_address": "192.168.1.100",
  "ambient_temperature": 25.6,
  "ambient_humidity": 65.2,
  "soil_humidity_1": 45.8,
  "soil_humidity_2": 42.1,
  "soil_humidity_3": 48.3
}
```

#### HTTP Sensor Response
```json
{
  "ambient_temperature": 25.6,
  "ambient_humidity": 65.2,
  "soil_humidity_1": 45.8,
  "soil_humidity_2": 42.1,
  "soil_humidity_3": 48.3,
  "timestamp": 1640995200
}
```

## Hardware Configuration

### GPIO Pin Assignments (Standardized)
```c
// Irrigation valve control (relay outputs)
#define IRRIGATION_VALVE_1_GPIO     GPIO_NUM_2
#define IRRIGATION_VALVE_2_GPIO     GPIO_NUM_4
#define IRRIGATION_VALVE_3_GPIO     GPIO_NUM_5

// Environmental sensor (DHT22)
#define DHT22_DATA_GPIO            GPIO_NUM_18

// Soil moisture sensors (analog ADC)
#define SOIL_MOISTURE_1_ADC        ADC1_CHANNEL_0  // GPIO 36
#define SOIL_MOISTURE_2_ADC        ADC1_CHANNEL_3  // GPIO 39
#define SOIL_MOISTURE_3_ADC        ADC1_CHANNEL_6  // GPIO 34

// Status LED
#define STATUS_LED_GPIO            GPIO_NUM_2
```

### Power Requirements
- **ESP32**: 3.3V, 240mA (active), 5μA (deep sleep)
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

## Configuration Management

### Build Configuration (menuconfig)
```
Smart Irrigation Configuration  --->
    WiFi Configuration  --->
        (YourWiFiSSID) WiFi SSID
        (YourPassword) WiFi Password
        [*] Enable WiFi Auto-Reconnection
        (30) Initial Reconnection Timeout (seconds)
        (3600) Maximum Reconnection Interval (seconds)

    MQTT Configuration  --->
        [*] Enable MQTT over WebSockets
        (wss://mqtt.yourdomain.com/mqtt) MQTT Broker URL
        (1883) MQTT Port
        (irrigation/register) Registration Topic
        (irrigation/data) Data Topic Prefix
        (irrigation/control) Control Topic Prefix
        (60) Keep Alive Interval (seconds)

    Device Configuration  --->
        (ESP32_Riego_01) Device Name
        (tomates) Crop Name
        (1.0.0) Firmware Version

    Sensor Configuration  --->
        (30) Sensor Reading Interval (seconds)
        (60) Data Publishing Interval (seconds)
        [*] Enable DHT22 Environmental Sensor
        [*] Enable Soil Moisture Sensors
        (3) Number of Soil Moisture Sensors

    Irrigation Configuration  --->
        (45) Default Soil Threshold (%)
        (15) Default Irrigation Duration (minutes)
        (30) Maximum Irrigation Duration (minutes)
        [*] Enable Offline Mode
        (300) Offline Mode Activation Delay (seconds)

    HTTP Server Configuration  --->
        (80) HTTP Server Port
        [*] Enable Request Logging
        (10) Request Timeout (seconds)
```

### NVS Storage Keys
```c
// WiFi Configuration
#define NVS_KEY_WIFI_SSID           "wifi_ssid"
#define NVS_KEY_WIFI_PASSWORD       "wifi_pass"

// MQTT Configuration
#define NVS_KEY_MQTT_BROKER_URL     "mqtt_broker"
#define NVS_KEY_MQTT_PORT           "mqtt_port"

// Device Configuration
#define NVS_KEY_DEVICE_NAME         "device_name"
#define NVS_KEY_CROP_NAME           "crop_name"

// Irrigation Configuration
#define NVS_KEY_SOIL_THRESHOLD      "soil_thresh"
#define NVS_KEY_IRRIGATION_DURATION "irrig_dur"
```

## Testing Strategy

### Unit Tests (Domain Logic)
```c
// Test irrigation decision logic
void test_irrigation_logic_soil_threshold_below(void) {
    soil_sensor_data_t test_data = {
        .soil_humidity_1 = 30.0,  // Below threshold (45%)
        .soil_humidity_2 = 35.0,  // Below threshold
        .soil_humidity_3 = 40.0,  // Below threshold
        .timestamp = 1640995200
    };

    irrigation_decision_t decision = irrigation_logic_evaluate(&test_data);
    TEST_ASSERT_EQUAL(IRRIGATION_DECISION_START, decision.action);
    TEST_ASSERT_EQUAL(15, decision.duration_minutes);
}

void test_sensor_data_validation(void) {
    ambient_sensor_data_t invalid_data = {
        .temperature = -50.0,  // Invalid temperature
        .humidity = 150.0      // Invalid humidity
    };

    bool is_valid = sensor_manager_validate_environmental_data(&invalid_data);
    TEST_ASSERT_FALSE(is_valid);
}
```

### Integration Tests (Component Interaction)
```c
// Test complete sensor-to-MQTT flow
void test_sensor_data_publishing_integration(void) {
    // Initialize components
    sensor_manager_init();
    mqtt_adapter_init();

    // Mock sensor readings
    sensor_manager_set_mock_data(25.5, 60.0, 45.0, 42.0, 48.0);

    // Execute publishing use case
    esp_err_t result = publish_sensor_data_use_case_execute();
    TEST_ASSERT_EQUAL(ESP_OK, result);

    // Verify MQTT publication
    const char* expected_topic = "irrigation/data/tomates/AA:BB:CC:DD:EE:FF";
    TEST_ASSERT_TRUE(mqtt_adapter_was_topic_published(expected_topic));
}

// Test HTTP endpoint responses
void test_http_whoami_endpoint(void) {
    http_server_init();

    http_response_t response = http_test_client_get("/whoami");
    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_TRUE(response.body_contains("ESP32_Riego_01"));
    TEST_ASSERT_TRUE(response.body_contains("tomates"));
}
```

### Hardware-in-Loop Testing
```c
// Real sensor integration tests
void test_dht22_sensor_reading(void) {
    dht_sensor_init(DHT22_DATA_GPIO);

    ambient_sensor_data_t data;
    esp_err_t result = dht_sensor_read(&data);
    TEST_ASSERT_EQUAL(ESP_OK, result);

    // Validate reasonable ranges for field conditions
    TEST_ASSERT_TRUE(data.temperature >= -10.0 && data.temperature <= 60.0);
    TEST_ASSERT_TRUE(data.humidity >= 0.0 && data.humidity <= 100.0);
}

void test_soil_moisture_adc_reading(void) {
    soil_moisture_sensor_init();

    soil_sensor_data_t data;
    esp_err_t result = soil_moisture_sensor_read_all(&data);
    TEST_ASSERT_EQUAL(ESP_OK, result);

    // Validate ADC readings convert to reasonable percentages
    TEST_ASSERT_TRUE(data.soil_humidity_1 >= 0.0 && data.soil_humidity_1 <= 100.0);
}
```

## Performance Expectations

### Resource Usage Targets
- **RAM**: < 200KB continuous operation (heap monitoring enabled)
- **Flash**: < 1MB total application size
- **CPU**: < 50% average utilization across both cores
- **Power**: < 100mA average (excluding valve operation)

### Timing Requirements
- **Sensor Reading**: 30-second intervals (configurable 10-300 seconds)
- **Data Publishing**: 60-second intervals (configurable)
- **HTTP Response**: < 500ms for all endpoints
- **MQTT Response**: < 2 seconds for command processing
- **Valve Activation**: < 2 seconds from command to physical response

### Network Performance
- **WiFi Connection**: < 10 seconds initial connection
- **WiFi Reconnection**: Exponential backoff (10s → 30s → 60s → 300s → 3600s max)
- **MQTT Keep-Alive**: 60 seconds with QoS 1 for reliability
- **Offline Mode**: Automatic activation after 300 seconds without connectivity

## Troubleshooting Guide

### Common Issues & Solutions

#### WiFi Connection Problems
```c
// Debug WiFi connection status
wifi_ap_record_t ap_info;
esp_wifi_sta_get_ap_info(&ap_info);
ESP_LOGI(TAG, "Connected to AP: %s, RSSI: %d", ap_info.ssid, ap_info.rssi);

// Check signal strength
if (ap_info.rssi < -70) {
    ESP_LOGW(TAG, "Weak WiFi signal - consider repositioning device");
}
```

#### MQTT Connection Issues
```c
// Verify MQTT broker connectivity
if (mqtt_adapter_get_connection_state() == MQTT_DISCONNECTED) {
    ESP_LOGI(TAG, "MQTT disconnected - checking WebSocket support");
    // Enable offline mode for irrigation safety
    irrigation_logic_enable_offline_mode();
}
```

#### Sensor Reading Errors
```c
// DHT22 sensor debugging
esp_err_t sensor_result = dht_sensor_read(&ambient_data);
if (sensor_result == ESP_ERR_TIMEOUT) {
    ESP_LOGE(TAG, "DHT22 timeout - check wiring and power");
} else if (sensor_result == ESP_ERR_INVALID_CRC) {
    ESP_LOGE(TAG, "DHT22 CRC error - electromagnetic interference");
}

// Soil moisture sensor calibration
float soil_raw_voltage = adc1_get_raw(SOIL_MOISTURE_1_ADC) * (3.3 / 4095.0);
ESP_LOGI(TAG, "Soil sensor 1 raw voltage: %.2fV", soil_raw_voltage);
```

#### Memory Issues
```c
// Monitor heap usage
size_t free_heap = esp_get_free_heap_size();
size_t min_free_heap = esp_get_minimum_free_heap_size();
ESP_LOGI(TAG, "Free heap: %u bytes, Minimum: %u bytes", free_heap, min_free_heap);

if (free_heap < 50000) {  // 50KB threshold
    ESP_LOGW(TAG, "Low memory - consider reducing buffer sizes");
}
```

### Debug Tools & Commands
```bash
# Acitviar framework idf
get_idf
# Real-time monitoring with sensor data
idf.py monitor --print_filter="*:I sensor_*:D irrigation_*:D"

# Memory usage analysis
idf.py size

# Partition table analysis
idf.py partition_table

# Core dump analysis (configure in menuconfig first)
idf.py monitor
# After crash, extract core dump:
idf.py coredump-info build/smart_irrigation.elf /dev/ttyUSB0

# Flash memory analysis
idf.py app-flash-info
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
- **Irrigation Control**: 5V relay module (1-3 channels)
- **Valves**: 12V/24V solenoid valves
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

**Version**: 1.0.0
**Last Updated**: 2025-09-17
**Maintainer**: Liwaisi Tech Team
**License**: MIT

**Current Phase**: Phase 4 (Irrigation Control) - IN PROGRESS
**Next Milestone**: Complete valve control and offline irrigation logic
**Ready for**: Production deployment in rural Colombian markets

The codebase is production-ready for sensor monitoring and data communication. Irrigation control features are currently in development, with offline mode safety features prioritized for rural reliability.

---

**This project represents critical infrastructure for rural farmers. Every design decision prioritizes reliability, maintainability, and performance under challenging field conditions.**