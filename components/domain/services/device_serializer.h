#ifndef DOMAIN_SERVICES_DEVICE_SERIALIZER_H
#define DOMAIN_SERVICES_DEVICE_SERIALIZER_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "../value_objects/device_info.h"
#include "../value_objects/ambient_sensor_data.h"
#include "../value_objects/soil_sensor_data.h"

/**
 * @file device_serializer.h
 * @brief Domain Service Interface para serialización de información del dispositivo
 * 
 * Define el contrato para serialización de device_info_t siguiendo principios DDD.
 * Esta interfaz (puerto) permite diferentes implementaciones (adaptadores) de
 * serialización sin acoplar el dominio a un formato específico.
 * 
 * Siguiendo Hexagonal Architecture:
 * - Puerto (Domain): Esta interfaz define QUÉ se necesita
 * - Adaptador (Infrastructure): Las implementaciones definen CÓMO se hace
 */

/**
 * @brief Serializa información del dispositivo para registro
 * 
 * Convierte device_info_t a formato de intercambio para mensajes de registro.
 * El formato específico depende de la implementación del adaptador.
 * 
 * @param device_info Puntero a estructura con información del dispositivo
 * @param buffer Buffer donde escribir el resultado serializado
 * @param buffer_size Tamaño del buffer en bytes
 * 
 * @return esp_err_t
 *         - ESP_OK: Serialización exitosa
 *         - ESP_ERR_INVALID_ARG: device_info o buffer es NULL
 *         - ESP_ERR_INVALID_SIZE: buffer_size insuficiente
 *         - ESP_ERR_INVALID_STATE: Campos del device_info inválidos
 */
esp_err_t serialize_device_registration(device_info_t* device_info, char* buffer, size_t buffer_size);

/**
 * @brief Convierte MAC address a representación string
 * 
 * Transforma array de 6 bytes a formato hexadecimal con separadores.
 * 
 * @param mac_bytes Array de 6 bytes con MAC address
 * @param mac_string Buffer para string resultante (mínimo 18 bytes)
 * @param string_size Tamaño del buffer string
 * 
 * @return esp_err_t
 *         - ESP_OK: Conversión exitosa
 *         - ESP_ERR_INVALID_ARG: mac_bytes o mac_string es NULL
 *         - ESP_ERR_INVALID_SIZE: string_size insuficiente
 */
esp_err_t mac_to_string(const uint8_t mac_bytes[6], char* mac_string, size_t string_size);

/**
 * @brief Convierte IP address a representación string
 * 
 * Transforma uint32_t a formato decimal con puntos.
 * 
 * @param ip_binary IP address en formato binario
 * @param ip_string Buffer para string resultante (mínimo 16 bytes)
 * @param string_size Tamaño del buffer string
 * 
 * @return esp_err_t
 *         - ESP_OK: Conversión exitosa
 *         - ESP_ERR_INVALID_ARG: ip_string es NULL
 *         - ESP_ERR_INVALID_SIZE: string_size insuficiente
 */
esp_err_t ip_to_string(uint32_t ip_binary, char* ip_string, size_t string_size);

/**
 * @brief Serializa datos de sensores ambientales a JSON
 * 
 * Convierte ambient_sensor_data_t a formato JSON para transmisión.
 * 
 * @param ambient_data Puntero a datos de sensores ambientales
 * @param json_buffer Buffer donde escribir el JSON resultante
 * @param buffer_size Tamaño del buffer en bytes (mínimo 80 bytes)
 * 
 * @return esp_err_t
 *         - ESP_OK: Serialización exitosa
 *         - ESP_ERR_INVALID_ARG: ambient_data o json_buffer es NULL
 *         - ESP_ERR_INVALID_SIZE: buffer_size insuficiente
 *         - ESP_ERR_INVALID_STATE: Valores de sensores fuera de rango
 */
esp_err_t serialize_ambient_sensor_data(ambient_sensor_data_t* ambient_data, char* json_buffer, size_t buffer_size);

/**
 * @brief Serializa datos de sensores de suelo a JSON
 * 
 * Convierte soil_sensor_data_t a formato JSON para transmisión.
 * Omite sensores con valor -1.0f, incluye sensores NaN como null.
 * 
 * @param soil_data Puntero a datos de sensores de suelo
 * @param json_buffer Buffer donde escribir el JSON resultante
 * @param buffer_size Tamaño del buffer en bytes (mínimo 120 bytes)
 * 
 * @return esp_err_t
 *         - ESP_OK: Serialización exitosa
 *         - ESP_ERR_INVALID_ARG: soil_data o json_buffer es NULL
 *         - ESP_ERR_INVALID_SIZE: buffer_size insuficiente
 *         - ESP_ERR_INVALID_STATE: Todos los sensores tienen valores inválidos
 */
esp_err_t serialize_soil_sensor_data(soil_sensor_data_t* soil_data, char* json_buffer, size_t buffer_size);

/**
 * @brief Serializa payload completo de sensores para MQTT
 * 
 * Convierte datos completos del dispositivo y sensores a formato JSON MQTT.
 * Incluye event_type, información del dispositivo y todos los datos de sensores.
 * 
 * @param ambient_data Puntero a datos de sensores ambientales
 * @param soil_data Puntero a datos de sensores de suelo
 * @param device_info Puntero a información del dispositivo
 * @param json_buffer Buffer donde escribir el JSON resultante
 * @param buffer_size Tamaño del buffer en bytes (mínimo 250 bytes)
 * 
 * @return esp_err_t
 *         - ESP_OK: Serialización exitosa
 *         - ESP_ERR_INVALID_ARG: Algún puntero es NULL
 *         - ESP_ERR_INVALID_SIZE: buffer_size insuficiente
 *         - ESP_ERR_INVALID_STATE: Datos inválidos en alguna estructura
 */
esp_err_t serialize_complete_sensor_data(ambient_sensor_data_t* ambient_data, soil_sensor_data_t* soil_data, device_info_t* device_info, char* json_buffer, size_t buffer_size);

#endif // DOMAIN_SERVICES_DEVICE_SERIALIZER_H