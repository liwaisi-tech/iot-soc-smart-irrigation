#include "json_device_serializer.h"
#include <stdio.h>
#include <string.h>

#define MAC_STRING_LENGTH 18  // "AA:BB:CC:DD:EE:FF\0"
#define IP_STRING_LENGTH 16   // "255.255.255.255\0"
#define MIN_JSON_BUFFER_SIZE 150

static bool is_valid_mac(const uint8_t mac_bytes[6]);
static bool is_valid_ip(uint32_t ip_binary);
static bool is_valid_device_name(const char* device_name);
static bool is_valid_crop_name(const char* crop_name);
static bool is_valid_firmware_version(const char* firmware_version);
static void escape_json_string(const char* input, char* output, size_t output_size);

esp_err_t serialize_device_registration(device_info_t* device_info, char* buffer, size_t buffer_size)
{
    if (device_info == NULL || buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (buffer_size < MIN_JSON_BUFFER_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (!is_valid_mac(device_info->mac_address) ||
        !is_valid_ip(device_info->ip_address) ||
        !is_valid_device_name(device_info->device_name) ||
        !is_valid_crop_name(device_info->crop_name) ||
        !is_valid_firmware_version(device_info->firmware_version)) {
        return ESP_ERR_INVALID_STATE;
    }

    char mac_string[MAC_STRING_LENGTH];
    char ip_string[IP_STRING_LENGTH];
    char escaped_device_name[64];
    char escaped_crop_name[32];
    char escaped_firmware_version[16];

    esp_err_t ret;
    
    ret = mac_to_string(device_info->mac_address, mac_string, sizeof(mac_string));
    if (ret != ESP_OK) {
        return ret;
    }

    ret = ip_to_string(device_info->ip_address, ip_string, sizeof(ip_string));
    if (ret != ESP_OK) {
        return ret;
    }

    escape_json_string(device_info->device_name, escaped_device_name, sizeof(escaped_device_name));
    escape_json_string(device_info->crop_name, escaped_crop_name, sizeof(escaped_crop_name));
    escape_json_string(device_info->firmware_version, escaped_firmware_version, sizeof(escaped_firmware_version));

    int written = snprintf(buffer, buffer_size,
        "{"
        "\"event_type\":\"device_registration\","
        "\"mac_address\":\"%s\","
        "\"ip_address\":\"%s\","
        "\"device_name\":\"%s\","
        "\"crop_name\":\"%s\","
        "\"firmware_version\":\"%s\""
        "}",
        mac_string,
        ip_string,
        escaped_device_name,
        escaped_crop_name,
        escaped_firmware_version
    );

    if (written < 0 || (size_t)written >= buffer_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t mac_to_string(const uint8_t mac_bytes[6], char* mac_string, size_t string_size)
{
    if (mac_bytes == NULL || mac_string == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (string_size < MAC_STRING_LENGTH) {
        return ESP_ERR_INVALID_SIZE;
    }

    int written = snprintf(mac_string, string_size,
        "%02X:%02X:%02X:%02X:%02X:%02X",
        mac_bytes[0], mac_bytes[1], mac_bytes[2],
        mac_bytes[3], mac_bytes[4], mac_bytes[5]
    );

    if (written < 0 || (size_t)written >= string_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t ip_to_string(uint32_t ip_binary, char* ip_string, size_t string_size)
{
    if (ip_string == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (string_size < IP_STRING_LENGTH) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t ip_bytes[4];
    ip_bytes[0] = (ip_binary >> 24) & 0xFF;
    ip_bytes[1] = (ip_binary >> 16) & 0xFF;
    ip_bytes[2] = (ip_binary >> 8) & 0xFF;
    ip_bytes[3] = ip_binary & 0xFF;

    int written = snprintf(ip_string, string_size,
        "%u.%u.%u.%u",
        ip_bytes[0], ip_bytes[1], ip_bytes[2], ip_bytes[3]
    );

    if (written < 0 || (size_t)written >= string_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static bool is_valid_mac(const uint8_t mac_bytes[6])
{
    bool all_zeros = true;
    bool all_ones = true;

    for (int i = 0; i < 6; i++) {
        if (mac_bytes[i] != 0x00) {
            all_zeros = false;
        }
        if (mac_bytes[i] != 0xFF) {
            all_ones = false;
        }
    }

    return !all_zeros && !all_ones;
}

static bool is_valid_ip(uint32_t ip_binary)
{
    return ip_binary != 0;
}

static bool is_valid_device_name(const char* device_name)
{
    return device_name != NULL && strlen(device_name) > 0 && strlen(device_name) < 32;
}

static bool is_valid_crop_name(const char* crop_name)
{
    return crop_name != NULL && strlen(crop_name) > 0 && strlen(crop_name) < 16;
}

static bool is_valid_firmware_version(const char* firmware_version)
{
    return firmware_version != NULL && strlen(firmware_version) > 0 && strlen(firmware_version) < 8;
}

static void escape_json_string(const char* input, char* output, size_t output_size)
{
    size_t input_len = strlen(input);
    size_t output_pos = 0;

    for (size_t i = 0; i < input_len && output_pos < (output_size - 1); i++) {
        char c = input[i];
        
        if (c == '"' || c == '\\') {
            if (output_pos < (output_size - 2)) {
                output[output_pos++] = '\\';
                output[output_pos++] = c;
            }
        } else {
            output[output_pos++] = c;
        }
    }
    
    output[output_pos] = '\0';
}