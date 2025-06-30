#ifndef DOMAIN_VALUE_OBJECTS_AMBIENT_SENSOR_DATA_H
#define DOMAIN_VALUE_OBJECTS_AMBIENT_SENSOR_DATA_H

/**
 * @file ambient_sensor_data.h
 * @brief Value Object para datos de sensores ambientales
 * 
 * Estructura de datos de sensores ambientales siguiendo principios DDD.
 * Representa una medición inmutable de condiciones ambientales.
 * 
 * Especificaciones Técnicas - Sensores Ambientales:
 * - Tipo de datos: float (4 bytes cada campo)
 * - Total de memoria: 8 bytes
 * - Precisión: Punto flotante de precisión simple con aceleración hardware FPU
 * - Campos: 2 campos (temperatura y humedad del aire)
 * - Sensor recomendado: DHT22/SHT30/BME280
 */

/**
 * @brief Value Object para datos de sensores ambientales
 * 
 * Estructura inmutable que encapsula una medición de condiciones ambientales.
 * Diseñada para optimización FPU en ESP32.
 */
typedef struct {
    float ambient_temperature;    // Temperatura ambiente en °C
    float ambient_humidity;       // Humedad ambiente en %
} ambient_sensor_data_t;

#endif // DOMAIN_VALUE_OBJECTS_AMBIENT_SENSOR_DATA_H