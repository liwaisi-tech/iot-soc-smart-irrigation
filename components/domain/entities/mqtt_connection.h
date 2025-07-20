#ifndef DOMAIN_ENTITIES_MQTT_CONNECTION_H
#define DOMAIN_ENTITIES_MQTT_CONNECTION_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @file mqtt_connection.h
 * @brief Entity for MQTT connection state and configuration
 * 
 * Domain entity following DDD principles for MQTT connection management.
 * Represents the state and lifecycle of MQTT broker connection.
 * 
 * Technical Specifications - MQTT Connection Entity:
 * - Total memory: ~80 bytes
 * - Broker URI: 64 bytes (sufficient for WebSocket URLs)
 * - Client ID: 32 bytes (ESP32_<MAC> format)
 * - Connection state: 4 bytes enum
 * - Statistics: 16 bytes for counters and timestamps
 */

/**
 * @brief MQTT connection state enumeration
 * 
 * Represents the current state of the MQTT connection lifecycle.
 * Used for state machine management and event handling.
 */
typedef enum {
    MQTT_CONNECTION_STATE_UNINITIALIZED = 0,
    MQTT_CONNECTION_STATE_INITIALIZED,
    MQTT_CONNECTION_STATE_CONNECTING,
    MQTT_CONNECTION_STATE_CONNECTED,
    MQTT_CONNECTION_STATE_DISCONNECTED,
    MQTT_CONNECTION_STATE_RECONNECTING,
    MQTT_CONNECTION_STATE_ERROR
} mqtt_connection_state_t;

/**
 * @brief MQTT connection configuration structure
 * 
 * Immutable configuration parameters for MQTT connection.
 * Set during initialization and used throughout connection lifecycle.
 */
typedef struct {
    char broker_uri[64];              // WebSocket URI (e.g., ws://mqtt.iot.liwaisi.tech:80/mqtt)
    char client_id[32];               // Generated client ID (ESP32_<MAC>)
    uint16_t keepalive_seconds;       // MQTT keepalive interval
    uint8_t qos_level;                // Default QoS level for publications
    bool clean_session;               // MQTT clean session flag
} mqtt_connection_config_t;

/**
 * @brief MQTT connection statistics structure
 * 
 * Runtime statistics and metrics for connection monitoring.
 * Updated during connection operations for observability.
 */
typedef struct {
    uint32_t connect_count;           // Total connection attempts
    uint32_t disconnect_count;        // Total disconnections
    uint32_t publish_count;           // Total messages published
    uint32_t last_connect_time;       // Timestamp of last successful connection
    uint32_t last_disconnect_time;    // Timestamp of last disconnection
    uint32_t current_retry_delay_ms;  // Current reconnection delay
} mqtt_connection_stats_t;

/**
 * @brief MQTT Connection Entity
 * 
 * Domain entity representing the complete state of an MQTT connection.
 * Encapsulates configuration, current state, and operational statistics.
 * 
 * This entity follows DDD principles:
 * - Encapsulates connection identity and lifecycle
 * - Maintains consistency of connection state
 * - Provides domain-specific behavior and validation
 */
typedef struct {
    mqtt_connection_config_t config;  // Immutable configuration
    mqtt_connection_state_t state;    // Current connection state
    mqtt_connection_stats_t stats;    // Runtime statistics
    bool device_registered;           // Whether device registration was sent
    void* mqtt_client_handle;         // ESP-IDF MQTT client handle (opaque)
} mqtt_connection_t;

#endif // DOMAIN_ENTITIES_MQTT_CONNECTION_H