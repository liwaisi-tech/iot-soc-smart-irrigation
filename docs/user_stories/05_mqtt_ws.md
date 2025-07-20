# MQTT Producer Implementation - User Story Document

## Executive Summary

Implement MQTT producer functionality for the ESP32-based smart irrigation system using ESP-IDF v5.4.2 framework with MQTT over WebSockets transport. The implementation shall follow hexagonal architecture principles with Domain Driven Design, integrating with existing codebase patterns for dependency injection, semaphore management, and NVS configuration handling.

## User Story

**As a** smart irrigation system device  
**I want to** establish MQTT communication over WebSockets and publish device registration messages  
**So that** the central monitoring system can track device presence, current IP addresses, and maintain real-time connectivity status for rural agricultural monitoring applications.

## Technical Requirements

### Core Functional Requirements

#### FR-001: MQTT Client Initialization
- Initialize ESP-MQTT client with WebSocket transport (`ws://`) configuration
- Configure client with URI: `ws://mqtt.iot.liwaisi.tech:80`
- Enable WebSocket transport layer via ESP-IDF menuconfig: `CONFIG_MQTT_TRANSPORT_WEBSOCKET=y`
- Set clean session flag to `true` for stateless broker behavior
- Implement automatic client ID generation using ESP32 MAC address suffix format: `ESP32_<last_3_MAC_bytes>`

#### FR-002: Connection Lifecycle Management
- Establish MQTT broker connection after successful WiFi connection (non-provisioning mode)
- Initialize MQTT client only after HTTP server successful initialization
- Implement connection state machine with proper event handling for MQTT lifecycle events
- Trigger device registration message transmission only after confirmed MQTT broker connection establishment

#### FR-003: Device Registration Message Publishing
- Publish device registration message to topic: `/liwaisi/iot/smart-irrigation/device/registration`
- Message payload structure (JSON format):
  ```
  {
    "event_type": "register",
    "mac_address": "<ESP32_MAC_ADDRESS>",
    "device_name": "<NVS_STORED_DEVICE_NAME>",
    "ip_address": "<CURRENT_DHCP_ASSIGNED_IP>",
    "location_description": "<NVS_STORED_LOCATION_DESC>"
  }
  ```
- Retrieve `device_name` and `location_description` from NVS using existing infrastructure methods
- Use QoS level 1 for registration message delivery guarantee
- Send registration message on every device initialization and WiFi connection establishment

#### FR-004: Exponential Backoff Reconnection Strategy
- Implement identical reconnection strategy as WiFi module:
  - Initial retry interval: 30 seconds
  - Exponential backoff: double interval on each failure
  - Maximum retry interval: 1 hour (3600 seconds)
  - Continuous retry attempts with sleep mode activation between attempts
- Apply reconnection logic for both connection establishment failures and unexpected disconnections

#### FR-005: Hexagonal Architecture Integration
- Analyze existing codebase dependency injection patterns and replicate consistently
- Create MQTT adapter in infrastructure layer following established architectural patterns
- Implement domain entities for MQTT connection state and message structures
- Define application use cases for device registration workflow
- Ensure proper separation of concerns between domain logic and infrastructure implementation

### Non-Functional Requirements

#### NFR-001: Concurrency and Resource Management
- Utilize shared semaphore system between HTTP and MQTT operations for resource synchronization
- Implement thread-safe MQTT operations compatible with existing HTTP server semaphore usage
- Ensure proper memory management for MQTT client and message buffers within ESP32 constraints
- Prevent resource conflicts during concurrent HTTP endpoint access and MQTT operations

#### NFR-002: Error Handling and Resilience
- Implement comprehensive error handling for WebSocket connection failures
- Log detailed error information using ESP-IDF logging framework with appropriate log levels
- Handle network disconnection scenarios gracefully with automatic recovery mechanisms
- Provide fallback behavior maintaining system functionality during MQTT unavailability

#### NFR-003: Configuration Management
- Integrate MQTT broker URI configuration through ESP-IDF menuconfig system
- Support configurable retry parameters while maintaining default exponential backoff behavior
- Enable MQTT debugging and logging level configuration for development and production environments
- Maintain configuration consistency with existing WiFi and HTTP server configuration patterns

#### NFR-004: Memory and Performance Optimization
- Optimize MQTT client buffer sizes for ESP32 memory constraints (target <200KB RAM usage)
- Implement efficient JSON serialization for registration messages without external libraries
- Minimize stack usage in MQTT event handler tasks
- Ensure MQTT operations maintain system responsiveness for HTTP endpoints

## Architecture Implementation Details

### Domain Layer Specifications
- Define `MqttConnection` entity representing connection state and configuration
- Create `DeviceRegistrationMessage` value object encapsulating registration payload structure
- Implement `MqttPublisher` domain service interface for message publishing abstractions
- Design domain events for MQTT connection state changes and registration completion

### Application Layer Specifications
- Implement `RegisterDeviceUseCase` containing business logic for device registration workflow
- Create `MqttConnectionUseCase` managing connection lifecycle and retry mechanisms
- Define application DTOs for MQTT configuration and message payloads
- Establish application ports (interfaces) for MQTT communication abstractions

### Infrastructure Layer Specifications
- Develop `MqttWebSocketAdapter` implementing ESP-MQTT WebSocket client integration
- Create adapter implementations for existing domain repository interfaces
- Implement ESP-IDF specific error handling and logging within infrastructure boundaries
- Integrate with existing NVS access patterns for device name and location retrieval

### Integration Points
- Interface with existing WiFi connection manager for connection state notifications
- Coordinate with HTTP server initialization sequence for proper startup ordering
- Utilize established semaphore management system for thread synchronization
- Leverage existing NVS configuration infrastructure for device metadata access

## Technical Constraints

### ESP-IDF Framework Constraints
- Maintain compatibility with ESP-IDF v5.4.2 ESP-MQTT library APIs
- Adhere to ESP32 memory limitations and FreeRTOS task management requirements
- Utilize ESP-IDF native WebSocket transport implementation without external dependencies
- Follow ESP-IDF coding standards and error handling conventions

### Hardware Platform Constraints
- Optimize for ESP32 single-core and dual-core variants processing capabilities
- Consider power consumption implications for battery-powered deployment scenarios
- Account for limited flash storage for MQTT client configuration and state persistence
- Ensure reliable operation under rural network connectivity conditions with intermittent connectivity

### System Integration Constraints
- Maintain compatibility with existing hexagonal architecture implementation patterns
- Preserve established configuration management and logging infrastructure
- Ensure seamless integration with current WiFi reconnection and HTTP server management
- Maintain backward compatibility with existing NVS data structures and access methods

## Acceptance Criteria

### AC-001: MQTT Client Initialization
- **Given** ESP32 device boots with valid WiFi configuration
- **When** WiFi connection establishes in non-provisioning mode  
- **Then** MQTT WebSocket client initializes with correct broker URI and configuration parameters

### AC-002: Device Registration Publishing
- **Given** MQTT client successfully connects to broker
- **When** both HTTP server and MQTT client are operational
- **Then** device registration message publishes to correct topic with valid JSON payload containing current IP address and NVS-stored device metadata

### AC-003: Reconnection Resilience
- **Given** MQTT connection experiences network interruption or broker unavailability
- **When** connection failure occurs
- **Then** exponential backoff reconnection attempts initiate with proper retry intervals and continue until connection restoration

### AC-004: Resource Synchronization
- **Given** concurrent HTTP requests and MQTT operations
- **When** multiple operations access shared resources simultaneously
- **Then** shared semaphore system prevents resource conflicts and maintains data integrity

### AC-005: Architecture Compliance
- **Given** existing hexagonal architecture codebase structure
- **When** MQTT implementation integrates with current system
- **Then** dependency injection patterns, layer separation, and interface definitions remain consistent with established architectural principles

## Implementation Priority

### Phase 1: Foundation (Critical Priority)
- MQTT WebSocket client initialization and configuration
- Basic connection establishment with broker
- Integration with existing WiFi connection lifecycle

### Phase 2: Core Functionality (Critical Priority)  
- Device registration message implementation
- JSON payload serialization and publishing
- NVS integration for device metadata retrieval

### Phase 3: Resilience (High Priority)
- Exponential backoff reconnection implementation
- Error handling and logging integration
- Shared semaphore system integration

### Phase 4: Optimization (Medium Priority)
- Memory usage optimization and performance tuning
- Advanced error recovery mechanisms
- Configuration parameter refinement

## Dependencies and Prerequisites

### External Dependencies
- Stable WiFi connection management system (existing)
- HTTP server initialization infrastructure (existing)
- NVS configuration management system (existing)
- Shared semaphore management implementation (existing)

### Development Dependencies
- ESP-IDF v5.4.2 with ESP-MQTT component enabled
- WebSocket transport configuration in menuconfig
- Development environment with proper ESP32 toolchain setup
- Access to NATS broker running on `mqtt.iot.liwaisi.tech` for testing

### Integration Dependencies
- Analysis of existing codebase dependency injection patterns
- Understanding of current NVS data structures and access methods
- Documentation of existing semaphore usage patterns
- Coordination with HTTP server startup sequence timing

## Testing Strategy

### Unit Testing Requirements
- Mock ESP-MQTT client for domain and application layer testing
- Test device registration message JSON serialization accuracy
- Validate exponential backoff timing calculations
- Verify proper error handling for various failure scenarios

### Integration Testing Requirements
- End-to-end testing with actual NATS broker WebSocket endpoint
- WiFi connection and disconnection scenario testing
- Concurrent HTTP and MQTT operation testing with semaphore validation
- Memory usage and performance testing under various load conditions

### System Testing Requirements
- Rural network connectivity simulation with intermittent connections
- Long-duration stability testing with repeated connection cycles
- Device registration accuracy verification in target database system
- Power consumption measurement during various MQTT operation states

---

**Document Version**: 1.0.0  
**Target Implementation**: Claude Code Agent  
**Architecture**: Hexagonal with Domain Driven Design  
**Framework**: ESP-IDF v5.4.2  
**Transport Protocol**: MQTT over WebSockets (ws://)  
**Deployment Target**: ESP32-based Smart Irrigation IoT Device