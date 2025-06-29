#ifndef DOMAIN_VALUE_OBJECTS_DEVICE_INFO_H
#define DOMAIN_VALUE_OBJECTS_DEVICE_INFO_H

#include <stdint.h>

/**
 * @file device_info.h
 * @brief Value Object para información del dispositivo ESP32
 * 
 * Estructura de datos de información del dispositivo siguiendo principios DDD.
 * Representa información inmutable de identificación y configuración del dispositivo.
 * 
 * Especificaciones Técnicas - Información del Dispositivo:
 * - Total de memoria: 66 bytes
 * - MAC Address: 6 bytes (formato binario más eficiente que string)
 * - IP Address: 4 bytes (uint32_t más eficiente que string)
 * - Device Name: 32 bytes (suficiente para nombres descriptivos)
 * - Crop Name: 16 bytes (suficiente para nombres de cultivos)
 * - Firmware Version: 8 bytes (formato semver como "v1.2.3")
 */

/**
 * @brief Value Object para información del dispositivo ESP32
 * 
 * Estructura inmutable que encapsula la información de identificación
 * y configuración del dispositivo IoT. Optimizada para eficiencia de memoria.
 */
typedef struct {
    uint8_t mac_address[6];       // MAC address (6 bytes raw)
    uint32_t ip_address;          // IP address (4 bytes en formato binario)
    char device_name[32];         // Nombre del dispositivo (31 chars + null terminator)
    char crop_name[16];           // Nombre del cultivo (15 chars + null terminator)
    char firmware_version[8];     // Versión firmware (7 chars + null terminator, ej: "v1.2.3")
} device_info_t;

#endif // DOMAIN_VALUE_OBJECTS_DEVICE_INFO_H