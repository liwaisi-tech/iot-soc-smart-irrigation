#ifndef DOMAIN_VALUE_OBJECTS_SOIL_SENSOR_DATA_H
#define DOMAIN_VALUE_OBJECTS_SOIL_SENSOR_DATA_H

/**
 * @file soil_sensor_data.h
 * @brief Value Object para datos de sensores de humedad del suelo
 * 
 * Estructura de datos de sensores de suelo siguiendo principios DDD.
 * Representa una medición inmutable de humedad del suelo de múltiples sensores.
 * 
 * Especificaciones Técnicas - Sensores de Suelo:
 * - Tipo de datos: float (4 bytes cada campo)
 * - Total de memoria: 12 bytes
 * - Precisión: Punto flotante de precisión simple con aceleración hardware FPU
 * - Campos fijos: 3 campos para esta versión MVP
 * - Sensores: Capacitivos/resistivos de humedad del suelo
 * - Valores no disponibles: Usar -1.0f o NaN para sensores no conectados
 */

/**
 * @brief Value Object para datos de sensores de humedad del suelo
 * 
 * Estructura inmutable que encapsula mediciones de humedad del suelo de hasta
 * 3 sensores. Diseñada para optimización FPU en ESP32.
 */
typedef struct {
    float soil_humidity_1;        // Humedad del suelo sensor 1 en %
    float soil_humidity_2;        // Humedad del suelo sensor 2 en %
    float soil_humidity_3;        // Humedad del suelo sensor 3 en %
} soil_sensor_data_t;

#endif // DOMAIN_VALUE_OBJECTS_SOIL_SENSOR_DATA_H