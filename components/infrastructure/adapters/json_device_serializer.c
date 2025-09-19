#include "json_device_serializer.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

// JSON Template Macros for Ultra-light Serialization
#define JSON_DEVICE_REGISTRATION_FMT \
    "{\"event_type\":\"device_registration\",\"mac_address\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"ip_address\":\"%u.%u.%u.%u\",\"device_name\":\"%.31s\",\"crop_name\":\"%.15s\",\"firmware_version\":\"%.7s\"}"

#define JSON_SENSOR_DATA_FMT \
    "{\"event_type\":\"sensor_data\",\"mac_address\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"ip_address\":\"%u.%u.%u.%u\",\"ambient_temperature\":%.1f,\"ambient_humidity\":%.1f,\"soil_humidity_1\":%.1f,\"soil_humidity_2\":%.1f,\"soil_humidity_3\":%.1f}"

#define JSON_AMBIENT_FMT \
    "{\"ambient_temperature\":%.1f,\"ambient_humidity\":%.1f}"

#define JSON_SOIL_FMT \
    "{\"soil_humidity_1\":%.1f,\"soil_humidity_2\":%.1f,\"soil_humidity_3\":%.1f}"

// Basic validation macros
#define IS_VALID_MAC(mac) ((mac)[0] | (mac)[1] | (mac)[2] | (mac)[3] | (mac)[4] | (mac)[5])
#define IS_VALID_IP(ip) (ip != 0)
#define IS_VALID_TEMP(t) ((t) >= -40.0f && (t) <= 85.0f && !isnan(t))
#define IS_VALID_HUM(h) ((h) >= 0.0f && (h) <= 100.0f && !isnan(h))
#define IS_VALID_SOIL(s) (((s) >= 0.0f && (s) <= 100.0f) || (s) == -1.0f)

esp_err_t serialize_device_registration(device_info_t* device_info, char* buffer, size_t buffer_size)
{
    if (!device_info || !buffer || buffer_size < 150) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!IS_VALID_MAC(device_info->mac_address) || !IS_VALID_IP(device_info->ip_address)) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t* mac = device_info->mac_address;
    uint32_t ip = device_info->ip_address;

    int written = snprintf(buffer, buffer_size, JSON_DEVICE_REGISTRATION_FMT,
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
        (unsigned int)((ip >> 24) & 0xFF), (unsigned int)((ip >> 16) & 0xFF),
        (unsigned int)((ip >> 8) & 0xFF), (unsigned int)(ip & 0xFF),
        device_info->device_name,
        device_info->crop_name,
        device_info->firmware_version
    );

    return (written > 0 && written < buffer_size) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t mac_to_string(const uint8_t mac_bytes[6], char* mac_string, size_t string_size)
{
    if (!mac_bytes || !mac_string || string_size < 18) {
        return ESP_ERR_INVALID_ARG;
    }

    int written = snprintf(mac_string, string_size, "%02X:%02X:%02X:%02X:%02X:%02X",
        mac_bytes[0], mac_bytes[1], mac_bytes[2], mac_bytes[3], mac_bytes[4], mac_bytes[5]);

    return (written > 0 && written < string_size) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t ip_to_string(uint32_t ip_binary, char* ip_string, size_t string_size)
{
    if (!ip_string || string_size < 16) {
        return ESP_ERR_INVALID_ARG;
    }

    int written = snprintf(ip_string, string_size, "%u.%u.%u.%u",
        (unsigned int)((ip_binary >> 24) & 0xFF), (unsigned int)((ip_binary >> 16) & 0xFF),
        (unsigned int)((ip_binary >> 8) & 0xFF), (unsigned int)(ip_binary & 0xFF));

    return (written > 0 && written < string_size) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

// All validation and helper functions removed - using inline macros instead

esp_err_t serialize_ambient_sensor_data(ambient_sensor_data_t* ambient_data, char* json_buffer, size_t buffer_size)
{
    if (!ambient_data || !json_buffer || buffer_size < 80) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!IS_VALID_TEMP(ambient_data->ambient_temperature) || !IS_VALID_HUM(ambient_data->ambient_humidity)) {
        return ESP_ERR_INVALID_STATE;
    }

    int written = snprintf(json_buffer, buffer_size, JSON_AMBIENT_FMT,
        ambient_data->ambient_temperature, ambient_data->ambient_humidity);

    return (written > 0 && written < buffer_size) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t serialize_soil_sensor_data(soil_sensor_data_t* soil_data, char* json_buffer, size_t buffer_size)
{
    if (!soil_data || !json_buffer || buffer_size < 120) {
        return ESP_ERR_INVALID_ARG;
    }

    // Basic validation - at least one sensor should be valid
    if (!IS_VALID_SOIL(soil_data->soil_humidity_1) &&
        !IS_VALID_SOIL(soil_data->soil_humidity_2) &&
        !IS_VALID_SOIL(soil_data->soil_humidity_3)) {
        return ESP_ERR_INVALID_STATE;
    }

    int written = snprintf(json_buffer, buffer_size, JSON_SOIL_FMT,
        soil_data->soil_humidity_1, soil_data->soil_humidity_2, soil_data->soil_humidity_3);

    return (written > 0 && written < buffer_size) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t serialize_complete_sensor_data(ambient_sensor_data_t* ambient_data, soil_sensor_data_t* soil_data, device_info_t* device_info, char* json_buffer, size_t buffer_size)
{
    if (!ambient_data || !soil_data || !device_info || !json_buffer || buffer_size < 250) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!IS_VALID_MAC(device_info->mac_address) || !IS_VALID_IP(device_info->ip_address) ||
        !IS_VALID_TEMP(ambient_data->ambient_temperature) || !IS_VALID_HUM(ambient_data->ambient_humidity)) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t* mac = device_info->mac_address;
    uint32_t ip = device_info->ip_address;

    int written = snprintf(json_buffer, buffer_size, JSON_SENSOR_DATA_FMT,
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
        (unsigned int)((ip >> 24) & 0xFF), (unsigned int)((ip >> 16) & 0xFF),
        (unsigned int)((ip >> 8) & 0xFF), (unsigned int)(ip & 0xFF),
        ambient_data->ambient_temperature, ambient_data->ambient_humidity,
        soil_data->soil_humidity_1, soil_data->soil_humidity_2, soil_data->soil_humidity_3);

    return (written > 0 && written < buffer_size) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}