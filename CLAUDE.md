# CLAUDE.md - Smart Irrigation System Project Guide

## Project Overview

This is a **Smart Irrigation System** IoT project built with **ESP-IDF** for ESP32, implementing **Hexagonal Architecture** (Ports and Adapters) with **Domain Driven Design** (DDD). The system monitors environmental and soil sensors, controls irrigation valves via MQTT, and provides HTTP endpoints for device information.

**Target Market**: Rural Colombia  
**Architecture**: Hexagonal (Clean Architecture)  
**Framework**: ESP-IDF v5.4.2  
**Language**: C (Clean Code for embedded systems)  
**IDE**: Visual Studio Code with ESP-IDF extension

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
│   │   ├── repositories/    # Data access interfaces
│   │   └── services/        # Domain services
│   │       ├── irrigation_logic.h  # Irrigation business rules
│   │       └── sensor_manager.h    # Sensor management logic
│   ├── application/         # Use Cases (Application Layer)
│   │   ├── use_cases/       # Application use cases
│   │   │   ├── read_sensors.h         # Read sensors use case
│   │   │   ├── control_irrigation.h   # Control irrigation use case
│   │   │   └── device_registration.h  # Device registration use case
│   │   ├── dtos/            # Data Transfer Objects
│   │   └── ports/           # Application ports (interfaces)
│   └── infrastructure/      # External adapters (Infrastructure Layer)
│       ├── adapters/        # External service adapters
│       │   ├── mqtt_adapter.h    # MQTT communication adapter
│       │   ├── http_adapter.h    # HTTP server adapter
│       │   └── wifi_adapter.h    # WiFi connectivity adapter
│       ├── drivers/         # Hardware drivers
│       │   ├── sensor_driver.h   # Sensor hardware drivers
│       │   ├── valve_driver.h    # Valve control drivers
│       │   └── gpio_driver.h     # GPIO control drivers
│       ├── network/         # Network implementations
│       └── persistence/     # Data persistence (NVS)
├── main/                    # Application entry point
│   ├── main.c              # Main application file
│   └── CMakeLists.txt      # Main component build config
├── docs/                   # Project documentation
├── tests/                  # Unit and integration tests
└── config/                 # Configuration files
```

## Key Features & Requirements

### Core Functionality
- **Environmental Monitoring**: Temperature, humidity sensors
- **Soil Monitoring**: 1-3 soil moisture sensors
- **Irrigation Control**: Valve control via MQTT and offline mode
- **WiFi Connectivity**: Auto-reconnection with exponential backoff
- **MQTT Communication**: Data publishing and command subscription over WebSockets
- **HTTP API**: Device info and sensor endpoints
- **Offline Mode**: Automatic irrigation when network is unavailable
- **Concurrency**: Semaphore-based resource management

### Technical Specifications
- **Target**: ESP32
- **Connectivity**: WiFi, MQTT over WebSockets
- **Protocols**: HTTP REST API, MQTT
- **Memory Management**: Optimized for embedded systems
- **Real-time**: FreeRTOS-based task management
- **Power Management**: Sleep modes and low power optimization

## Development Phases

### Phase 1: Basic Infrastructure
**Priority**: CRITICAL
- WiFi connectivity with reconnection
- MQTT client over WebSockets  
- Basic HTTP server
- Semaphore system for concurrency

### Phase 2: Data & Sensors
**Priority**: CRITICAL
- Sensor data structures
- Sensor abstraction layer (mocks initially)
- Periodic sensor reading task

### Phase 3: Data Communication
**Priority**: CRITICAL
- Device registration message
- MQTT data publishing
- HTTP endpoints (/whoami, /sensors)

### Phase 4: Irrigation Control
**Priority**: CRITICAL
- MQTT command subscription
- Valve control system
- Offline automatic irrigation logic

### Phase 5: Optimization
**Priority**: MEDIUM
- Memory management & sleep modes
- Final task scheduling system
- Complete integration testing

## API Specifications

### HTTP Endpoints
- `GET /whoami` - Device information and available endpoints
- `GET /sensors` - Immediate reading of all sensors

### MQTT Topics
- `irrigation/register` - Device registration
- `irrigation/data/{crop_name}/{mac_address}` - Sensor data publishing
- `irrigation/control/{mac_address}` - Irrigation commands (start/stop)

### Data Formats
All data exchanged in JSON format with specific schemas for sensor readings, device registration, and irrigation commands.

## Development Guidelines

### Clean Code Principles
- **Functions**: Single responsibility, max 20 lines
- **Variables**: Descriptive names, appropriate scope
- **Comments**: Explain WHY, not WHAT
- **Error Handling**: Always check return values
- **Memory Management**: No memory leaks, proper cleanup

### ESP32 Specific Best Practices
- **Task Management**: Appropriate stack sizes and priorities
- **Memory Usage**: Optimize RAM and Flash usage
- **GPIO Management**: Proper pin configuration and cleanup
- **Power Management**: Use sleep modes when appropriate
- **Watchdog**: Implement proper watchdog handling

### Architecture Rules
- **Domain Independence**: Domain layer has no external dependencies
- **Dependency Direction**: Dependencies point inward (toward domain)
- **Port/Adapter Pattern**: Clear separation of concerns
- **Testability**: Each layer can be tested independently

## Configuration Management

### Build Configuration
Use `idf.py menuconfig` to configure:
- WiFi credentials (SSID, Password)
- MQTT broker URL and settings
- Device name and crop identification
- Sensor reading intervals
- Irrigation parameters and thresholds

### Environment Variables
The system uses ESP-IDF's configuration system for managing different environments (development, testing, production).

## Testing Strategy

### Unit Tests
- Domain logic testing (business rules)
- Use case testing (application logic)
- Driver testing (hardware abstraction)

### Integration Tests
- WiFi connectivity scenarios
- MQTT communication flow
- HTTP endpoint functionality
- Sensor reading accuracy
- Irrigation control reliability

### Hardware Testing
- Real sensor integration
- Valve control verification
- Power consumption validation
- Network reliability testing

## Common Development Tasks

### Adding New Sensors
1. Update sensor entity in domain layer
2. Implement driver in infrastructure layer
3. Update sensor manager service
4. Modify data structures and DTOs
5. Update HTTP and MQTT interfaces

### Adding New MQTT Commands
1. Define command in domain value objects
2. Update irrigation use case
3. Implement MQTT command parser
4. Add validation and error handling
5. Update integration tests

### Modifying Business Rules
1. Update domain services (irrigation_logic.c)
2. Modify use cases if needed
3. Update configuration parameters
4. Add/modify unit tests
5. Document rule changes

## Troubleshooting Guide

### Common Issues
- **WiFi Connection Problems**: Check credentials, signal strength
- **MQTT Connection Issues**: Verify broker URL, port, WebSocket support
- **Sensor Reading Errors**: Check GPIO connections, power supply
- **Memory Issues**: Monitor heap usage, optimize buffer sizes
- **Task Scheduling Problems**: Check stack sizes, priorities

### Debug Tools
- ESP-IDF Monitor: `idf.py monitor`
- Memory Analysis: `idf.py size`
- Core Dump Analysis: Enable in menuconfig
- JTAG Debugging: Configure OpenOCD for VS Code

## Deployment Considerations

### Hardware Requirements
- ESP32 DevKit
- Environmental sensor (DHT22 or equivalent)
- Soil moisture sensors (1-3 units)
- Irrigation valve (12V/24V)
- Relay module
- Power supply system

### Network Requirements
- WiFi access with internet connectivity
- MQTT broker supporting WebSockets
- Stable network for remote monitoring

### Field Deployment
- Weatherproof enclosures
- Reliable power supply (battery + solar)
- Sensor calibration procedures
- Remote monitoring and maintenance

## Performance Expectations

### Resource Usage
- **RAM**: < 200KB typical usage
- **Flash**: < 1MB application size
- **CPU**: < 50% average utilization
- **Power**: Optimized for battery operation

### Timing Requirements
- **Sensor Reading**: 30-second intervals (configurable)
- **MQTT Publishing**: 60-second intervals (configurable)
- **WiFi Reconnection**: Exponential backoff up to 1 hour
- **HTTP Response**: < 500ms for all endpoints

---

## Getting Started for Claude CLI Users

This project follows hexagonal architecture principles and clean code practices. When working on this codebase:

1. **Understand the Architecture**: Domain → Application → Infrastructure
2. **Follow the Phases**: Implement features according to the development phases
3. **Maintain Clean Code**: Keep functions small, focused, and well-tested
4. **Test Incrementally**: Test each component as you build it
5. **Document Changes**: Update this guide when adding new features

The codebase is designed to be maintainable, testable, and scalable for IoT applications in rural Colombian markets.

## Claude Code Development Guidelines

Following [Claude Code Best Practices](https://www.anthropic.com/engineering/claude-code-best-practices), please adhere to these specific requirements for this project:

### Git and Version Control Requirements

1. **GitHub CLI Usage**: 
   - Always use `gh` command when possible for GitHub operations
   - Prefer `gh pr create`, `gh issue create`, `gh repo clone` over web interface
   - Use `gh` for checking PR status, comments, and repository information

2. **Commit Message Standards**:
   - Always use proper emojis from [Gitmoji](https://gitmoji.dev/) in commit messages
   - Examples: `:sparkles:` for new features, `:bug:` for bug fixes, `:construction:` for work in progress
   - Format: `<emoji> <description>` followed by detailed bullet points if needed

3. **Co-Authorship Requirements**:
   - Always set co-author to `braejan` with email `ing.brayanarias@gmail.com`
   - Include in commit message: `Co-Authored-By: braejan <ing.brayanarias@gmail.com>`
   - Also include Claude attribution: `🤖 Generated with [Claude Code](https://claude.ai/code)`

### ESP-IDF Development Requirements

4. **ESP-IDF Environment Setup**:
   - When `idf.py` command fails, always use `get_idf` alias first to setup environment
   - Pattern: `get_idf && idf.py <command>`
   - This ensures proper ESP-IDF environment activation before any operations

### Example Commit Message Format

```
:construction: implement WiFi connectivity adapter

- Add WiFi initialization and connection handling
- Implement exponential backoff for reconnection
- Add proper error handling and logging
- Follow hexagonal architecture patterns

🤖 Generated with [Claude Code](https://claude.ai/code)

Co-Authored-By: braejan <ing.brayanarias@gmail.com>
```

### Example GitHub CLI Usage

```bash
# Create pull request
gh pr create --title ":sparkles: add sensor data collection" --body "Description here"

# Check PR status
gh pr status

# Create issue
gh issue create --title ":bug: memory leak in sensor driver" --body "Issue description"
```

### Example ESP-IDF Commands

```bash
# Build project
get_idf && idf.py build

# Flash to device
get_idf && idf.py -p /dev/ttyUSB0 flash

# Monitor output
get_idf && idf.py -p /dev/ttyUSB0 monitor --print-filter="*:V" --timestamps
```

These guidelines ensure consistency with Claude Code best practices while maintaining the project's specific requirements and development workflow.

---

**Project Version**: 1.0.0  
**Last Updated**: 26-06-2025
**Maintainer**: Liwaisi Tech Team
