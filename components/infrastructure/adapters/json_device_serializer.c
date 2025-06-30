#include "json_device_serializer.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define MAC_STRING_LENGTH 18  // "AA:BB:CC:DD:EE:FF\0"
#define IP_STRING_LENGTH 16   // "255.255.255.255\0"
#define MIN_JSON_BUFFER_SIZE 150
#define MIN_AMBIENT_JSON_SIZE 80
#define MIN_SOIL_JSON_SIZE 120
#define MIN_COMPLETE_JSON_SIZE 250

static bool is_valid_mac(const uint8_t mac_bytes[6]);
static bool is_valid_ip(uint32_t ip_binary);
static bool is_valid_device_name(const char* device_name);
static bool is_valid_crop_name(const char* crop_name);
static bool is_valid_firmware_version(const char* firmware_version);
static void escape_json_string(const char* input, char* output, size_t output_size);
static bool is_valid_sensor_value(float value, float min_range, float max_range);
static esp_err_t float_to_string(float value, char* str_buffer, size_t buffer_size);

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

static bool is_valid_sensor_value(float value, float min_range, float max_range)
{
    if (isnan(value) || isinf(value)) {
        return false;
    }
    if (value == -1.0f) {
        return false; // Sensor not connected
    }
    return (value >= min_range && value <= max_range);
}

static esp_err_t float_to_string(float value, char* str_buffer, size_t buffer_size)
{
    if (str_buffer == NULL || buffer_size < 8) {
        return ESP_ERR_INVALID_ARG;
    }

    if (isnan(value) || isinf(value)) {
        int written = snprintf(str_buffer, buffer_size, "null");
        if (written < 0 || (size_t)written >= buffer_size) {
            return ESP_ERR_INVALID_SIZE;
        }
        return ESP_OK;
    }

    int written = snprintf(str_buffer, buffer_size, "%.1f", value);
    if (written < 0 || (size_t)written >= buffer_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t serialize_ambient_sensor_data(ambient_sensor_data_t* ambient_data, char* json_buffer, size_t buffer_size)
{
    if (ambient_data == NULL || json_buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (buffer_size < MIN_AMBIENT_JSON_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    // Validate sensor ranges
    if (!is_valid_sensor_value(ambient_data->ambient_temperature, -40.0f, 85.0f) ||
        !is_valid_sensor_value(ambient_data->ambient_humidity, 0.0f, 100.0f)) {
        return ESP_ERR_INVALID_STATE;
    }

    char temp_str[16];
    char hum_str[16];

    esp_err_t ret;
    ret = float_to_string(ambient_data->ambient_temperature, temp_str, sizeof(temp_str));
    if (ret != ESP_OK) {
        return ret;
    }

    ret = float_to_string(ambient_data->ambient_humidity, hum_str, sizeof(hum_str));
    if (ret != ESP_OK) {
        return ret;
    }

    int written = snprintf(json_buffer, buffer_size,
        "{"
        "\"ambient_temperature\":%s,"
        "\"ambient_humidity\":%s"
        "}",
        temp_str, hum_str
    );

    if (written < 0 || (size_t)written >= buffer_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t serialize_soil_sensor_data(soil_sensor_data_t* soil_data, char* json_buffer, size_t buffer_size)
{
    if (soil_data == NULL || json_buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (buffer_size < MIN_SOIL_JSON_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    // Check if at least one sensor has valid data
    bool has_valid_sensor = false;
    if (is_valid_sensor_value(soil_data->soil_humidity_1, 0.0f, 100.0f) ||
        is_valid_sensor_value(soil_data->soil_humidity_2, 0.0f, 100.0f) ||
        is_valid_sensor_value(soil_data->soil_humidity_3, 0.0f, 100.0f) ||
        isnan(soil_data->soil_humidity_1) || isnan(soil_data->soil_humidity_2) || isnan(soil_data->soil_humidity_3)) {
        has_valid_sensor = true;
    }

    if (!has_valid_sensor) {
        return ESP_ERR_INVALID_STATE;
    }

    char json_parts[256] = "{";
    bool first_field = true;

    // Handle soil_humidity_1
    if (soil_data->soil_humidity_1 != -1.0f) {
        char soil1_str[16];
        esp_err_t ret = float_to_string(soil_data->soil_humidity_1, soil1_str, sizeof(soil1_str));
        if (ret != ESP_OK) {
            return ret;
        }

        if (!first_field) {
            strcat(json_parts, ",");
        }
        strcat(json_parts, "\"soil_humidity_1\":");
        strcat(json_parts, soil1_str);
        first_field = false;
    }

    // Handle soil_humidity_2
    if (soil_data->soil_humidity_2 != -1.0f) {
        char soil2_str[16];
        esp_err_t ret = float_to_string(soil_data->soil_humidity_2, soil2_str, sizeof(soil2_str));
        if (ret != ESP_OK) {
            return ret;
        }

        if (!first_field) {
            strcat(json_parts, ",");
        }
        strcat(json_parts, "\"soil_humidity_2\":");
        strcat(json_parts, soil2_str);
        first_field = false;
    }

    // Handle soil_humidity_3
    if (soil_data->soil_humidity_3 != -1.0f) {
        char soil3_str[16];
        esp_err_t ret = float_to_string(soil_data->soil_humidity_3, soil3_str, sizeof(soil3_str));
        if (ret != ESP_OK) {
            return ret;
        }

        if (!first_field) {
            strcat(json_parts, ",");
        }
        strcat(json_parts, "\"soil_humidity_3\":");
        strcat(json_parts, soil3_str);
        first_field = false;
    }

    strcat(json_parts, "}");

    if (strlen(json_parts) >= buffer_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    strcpy(json_buffer, json_parts);
    return ESP_OK;
}

esp_err_t serialize_complete_sensor_data(ambient_sensor_data_t* ambient_data, soil_sensor_data_t* soil_data, device_info_t* device_info, char* json_buffer, size_t buffer_size)
{
    if (ambient_data == NULL || soil_data == NULL || device_info == NULL || json_buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (buffer_size < MIN_COMPLETE_JSON_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    // Validate device info
    if (!is_valid_mac(device_info->mac_address) || !is_valid_ip(device_info->ip_address)) {
        return ESP_ERR_INVALID_STATE;
    }

    // Validate ambient sensors
    if (!is_valid_sensor_value(ambient_data->ambient_temperature, -40.0f, 85.0f) ||
        !is_valid_sensor_value(ambient_data->ambient_humidity, 0.0f, 100.0f)) {
        return ESP_ERR_INVALID_STATE;
    }

    // Get device info strings
    char mac_string[MAC_STRING_LENGTH];
    char ip_string[IP_STRING_LENGTH];

    esp_err_t ret = mac_to_string(device_info->mac_address, mac_string, sizeof(mac_string));
    if (ret != ESP_OK) {
        return ret;
    }

    ret = ip_to_string(device_info->ip_address, ip_string, sizeof(ip_string));
    if (ret != ESP_OK) {
        return ret;
    }

    // Get sensor value strings
    char temp_str[16], hum_str[16];
    char soil1_str[16], soil2_str[16], soil3_str[16];

    ret = float_to_string(ambient_data->ambient_temperature, temp_str, sizeof(temp_str));
    if (ret != ESP_OK) return ret;

    ret = float_to_string(ambient_data->ambient_humidity, hum_str, sizeof(hum_str));
    if (ret != ESP_OK) return ret;

    ret = float_to_string(soil_data->soil_humidity_1, soil1_str, sizeof(soil1_str));
    if (ret != ESP_OK) return ret;

    ret = float_to_string(soil_data->soil_humidity_2, soil2_str, sizeof(soil2_str));
    if (ret != ESP_OK) return ret;

    ret = float_to_string(soil_data->soil_humidity_3, soil3_str, sizeof(soil3_str));
    if (ret != ESP_OK) return ret;

    // Build JSON with all fields (including -1.0f as null for soil sensors)
    int written = snprintf(json_buffer, buffer_size,
        "{"
        "\"event_type\":\"sensor_data\","
        "\"mac_address\":\"%s\","
        "\"ip_address\":\"%s\","
        "\"ambient_temperature\":%s,"
        "\"ambient_humidity\":%s,"
        "\"soil_humidity_1\":%s,"
        "\"soil_humidity_2\":%s,"
        "\"soil_humidity_3\":%s"
        "}",
        mac_string, ip_string,
        temp_str, hum_str,
        soil1_str, soil2_str, soil3_str
    );

    if (written < 0 || (size_t)written >= buffer_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}