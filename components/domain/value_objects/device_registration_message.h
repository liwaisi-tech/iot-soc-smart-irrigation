#ifndef DOMAIN_VALUE_OBJECTS_DEVICE_REGISTRATION_MESSAGE_H
#define DOMAIN_VALUE_OBJECTS_DEVICE_REGISTRATION_MESSAGE_H

#include <stdint.h>

/**
 * @file device_registration_message.h
 * @brief Value Object for device registration MQTT message
 * 
 * Value object following DDD principles for device registration payload.
 * Represents immutable data structure for device registration messages
 * published to MQTT broker when device comes online.
 * 
 * Technical Specifications - Device Registration Message:
 * - Total memory: ~150 bytes
 * - Event type: 16 bytes (fixed "register" string)
 * - MAC address: 18 bytes (string format XX:XX:XX:XX:XX:XX)
 * - Device name: 32 bytes (from NVS configuration)
 * - IP address: 16 bytes (string format XXX.XXX.XXX.XXX)
 * - Location description: 64 bytes (from NVS configuration)
 * 
 * JSON Structure:
 * {
 *   "event_type": "register",
 *   "mac_address": "AA:BB:CC:DD:EE:FF",
 *   "device_name": "Smart Irrigation Device #1",
 *   "ip_address": "192.168.1.100",
 *   "location_description": "Field A - Tomato Crop Section 1"
 * }
 */

/**
 * @brief Device Registration Message Value Object
 * 
 * Immutable value object containing all information required for
 * device registration message published to MQTT broker.
 * 
 * This value object:
 * - Represents a complete device registration payload
 * - Is immutable once created (no setters)
 * - Contains all data needed for JSON serialization
 * - Follows the exact format expected by the backend system
 */
typedef struct {
    char event_type[16];              // Always "register" for registration messages
    char mac_address[18];             // MAC address in string format (XX:XX:XX:XX:XX:XX)
    char device_name[32];             // Device name from NVS configuration
    char ip_address[16];              // Current IP address in string format
    char location_description[64];    // Location description from NVS configuration
} device_registration_message_t;

/**
 * @brief MQTT topic configuration for device registration
 * 
 * Configuration structure defining the MQTT topic and QoS settings
 * for device registration messages.
 */
typedef struct {
    char topic[80];                   // MQTT topic path for registration
    uint8_t qos_level;                // QoS level for registration message
    bool retain;                      // Whether to retain the registration message
} device_registration_topic_config_t;

/**
 * @brief Default topic configuration for device registration
 * 
 * Default values for device registration topic configuration.
 * Can be overridden through Kconfig or runtime configuration.
 */
#define DEVICE_REGISTRATION_DEFAULT_TOPIC "/liwaisi/iot/smart-irrigation/device/registration"
#define DEVICE_REGISTRATION_DEFAULT_QOS 1
#define DEVICE_REGISTRATION_DEFAULT_RETAIN false

#endif // DOMAIN_VALUE_OBJECTS_DEVICE_REGISTRATION_MESSAGE_H