#ifndef DOMAIN_VALUE_OBJECTS_COMPLETE_SENSOR_DATA_H
#define DOMAIN_VALUE_OBJECTS_COMPLETE_SENSOR_DATA_H

#include "ambient_sensor_data.h"
#include "soil_sensor_data.h"

/**
 * @file complete_sensor_data.h
 * @brief Value Object para el conjunto completo de datos de sensores
 * 
 * Estructura de datos combinada que encapsula toda la información de sensores
 * ambientales y de suelo siguiendo principios DDD.
 * 
 * Especificaciones Técnicas - Estructura Combinada:
 * - Total de memoria: 20 bytes (8 bytes ambient + 12 bytes soil)
 * - Precisión: Punto flotante de precisión simple con aceleración hardware FPU
 * - Compatibilidad: ESP-IDF v5.4.2, JSON payload MQTT, endpoint HTTP /sensors
 * - Modularidad: Permite uso independiente de sensores ambientales y de suelo
 */

/**
 * @brief Value Object para el conjunto completo de datos de sensores
 * 
 * Estructura inmutable que combina mediciones ambientales y de suelo
 * para crear un payload completo compatible con MQTT y HTTP endpoints.
 * Diseñada para optimización FPU en ESP32.
 */
typedef struct {
    ambient_sensor_data_t ambient;    // Datos ambientales
    soil_sensor_data_t soil;          // Datos de suelo
} complete_sensor_data_t;

#endif // DOMAIN_VALUE_OBJECTS_COMPLETE_SENSOR_DATA_H