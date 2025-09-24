#ifndef DOMAIN_ENTITIES_SENSOR_H
#define DOMAIN_ENTITIES_SENSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "ambient_sensor_data.h"
#include "soil_sensor_data.h"

/**
 * @file sensor.h
 * @brief Entidad de dominio para gestión de sensores
 *
 * Representa la abstracción de dominio para todos los sensores del sistema
 * siguiendo principios DDD. Encapsula validación, calibración y estado
 * de sensores independientemente de la implementación de hardware.
 *
 * Especificaciones Técnicas - Entidad Sensor:
 * - Abstracción hardware-independiente
 * - Validación de rangos por tipo de sensor
 * - Detección automática de fallos
 * - Calibración y auto-diagnóstico
 * - Historial de lecturas para análisis de tendencias
 */

/**
 * @brief Tipos de sensores soportados
 */
typedef enum {
    SENSOR_TYPE_UNKNOWN = 0,
    SENSOR_TYPE_AMBIENT_TEMPERATURE,      // Sensor de temperatura ambiente (DHT22)
    SENSOR_TYPE_AMBIENT_HUMIDITY,         // Sensor de humedad ambiente (DHT22)
    SENSOR_TYPE_SOIL_MOISTURE,            // Sensor de humedad del suelo (capacitivo)
    SENSOR_TYPE_SOIL_TEMPERATURE,         // Sensor de temperatura del suelo
    SENSOR_TYPE_LIGHT,                    // Sensor de luminosidad
    SENSOR_TYPE_PH,                       // Sensor de pH del suelo
    SENSOR_TYPE_MAX
} sensor_type_t;

/**
 * @brief Estados posibles de un sensor
 */
typedef enum {
    SENSOR_STATE_UNINITIALIZED = 0,      // Sensor no inicializado
    SENSOR_STATE_INITIALIZING,            // Sensor inicializándose
    SENSOR_STATE_READY,                   // Sensor listo para lecturas
    SENSOR_STATE_READING,                 // Sensor realizando lectura
    SENSOR_STATE_CALIBRATING,             // Sensor en calibración
    SENSOR_STATE_ERROR,                   // Sensor con error
    SENSOR_STATE_DISCONNECTED,            // Sensor desconectado
    SENSOR_STATE_MAINTENANCE,             // Sensor en mantenimiento
    SENSOR_STATE_MAX
} sensor_state_t;

/**
 * @brief Niveles de calidad de lectura
 */
typedef enum {
    SENSOR_QUALITY_UNKNOWN = 0,
    SENSOR_QUALITY_EXCELLENT,            // Lectura perfecta
    SENSOR_QUALITY_GOOD,                 // Lectura confiable
    SENSOR_QUALITY_FAIR,                 // Lectura aceptable con reservas
    SENSOR_QUALITY_POOR,                 // Lectura poco confiable
    SENSOR_QUALITY_INVALID,              // Lectura inválida
    SENSOR_QUALITY_MAX
} sensor_quality_t;

/**
 * @brief Rangos válidos para cada tipo de sensor
 */
typedef struct {
    float min_value;                      // Valor mínimo válido
    float max_value;                      // Valor máximo válido
    float min_normal;                     // Valor mínimo normal de operación
    float max_normal;                     // Valor máximo normal de operación
    float precision;                      // Precisión del sensor
    uint16_t response_time_ms;            // Tiempo de respuesta esperado
} sensor_range_t;

/**
 * @brief Historial de lecturas de sensor
 */
typedef struct {
    uint32_t timestamp;                   // Cuándo se tomó la lectura
    float value;                          // Valor leído
    sensor_quality_t quality;             // Calidad de la lectura
    uint16_t response_time_ms;            // Tiempo que tomó la lectura
} sensor_reading_history_t;

/**
 * @brief Estadísticas de un sensor
 */
typedef struct {
    uint32_t total_readings;              // Total de lecturas realizadas
    uint32_t successful_readings;         // Lecturas exitosas
    uint32_t failed_readings;             // Lecturas fallidas
    uint32_t out_of_range_readings;       // Lecturas fuera de rango
    float average_response_time_ms;       // Tiempo promedio de respuesta
    uint32_t last_calibration_timestamp;  // Última calibración
    uint32_t last_maintenance_timestamp;  // Último mantenimiento
} sensor_statistics_t;

/**
 * @brief Entidad principal de sensor
 *
 * Representa un sensor individual del sistema con toda su información
 * de estado, configuración e historial.
 */
typedef struct {
    // Identificación
    uint8_t sensor_id;                    // ID único del sensor (1-255)
    sensor_type_t type;                   // Tipo de sensor
    char description[32];                 // Descripción del sensor
    uint8_t physical_pin;                 // Pin físico de conexión

    // Estado actual
    sensor_state_t current_state;         // Estado actual del sensor
    float last_reading_value;             // Última lectura válida
    sensor_quality_t last_reading_quality; // Calidad última lectura
    uint32_t last_reading_timestamp;      // Cuándo se hizo la última lectura

    // Configuración y rangos
    sensor_range_t valid_range;           // Rangos válidos para este sensor
    bool auto_calibration_enabled;        // Si está activa auto-calibración
    uint16_t reading_interval_ms;         // Intervalo entre lecturas

    // Historial (circular buffer)
    sensor_reading_history_t reading_history[20]; // Últimas 20 lecturas
    uint8_t history_write_index;          // Índice para escribir en historial
    uint8_t history_count;                // Número de entradas en historial

    // Estadísticas
    sensor_statistics_t statistics;       // Estadísticas del sensor

    // Flags de estado
    bool is_initialized;                  // Si el sensor está inicializado
    bool is_connected;                    // Si el sensor está conectado
    bool calibration_required;            // Si requiere calibración
    bool maintenance_required;            // Si requiere mantenimiento

} sensor_entity_t;

/**
 * @brief Strings para tipos de sensor
 */
#define SENSOR_TYPE_AMBIENT_TEMP_STR      "ambient_temperature"
#define SENSOR_TYPE_AMBIENT_HUM_STR       "ambient_humidity"
#define SENSOR_TYPE_SOIL_MOISTURE_STR     "soil_moisture"
#define SENSOR_TYPE_SOIL_TEMP_STR         "soil_temperature"
#define SENSOR_TYPE_LIGHT_STR             "light"
#define SENSOR_TYPE_PH_STR                "ph"

/**
 * @brief Strings para estados de sensor
 */
#define SENSOR_STATE_READY_STR            "ready"
#define SENSOR_STATE_READING_STR          "reading"
#define SENSOR_STATE_ERROR_STR            "error"
#define SENSOR_STATE_DISCONNECTED_STR     "disconnected"
#define SENSOR_STATE_CALIBRATING_STR      "calibrating"

/**
 * @brief Constantes para entidad de sensor
 */
#define SENSOR_ENTITY_HISTORY_SIZE        20
#define SENSOR_ENTITY_DESCRIPTION_LENGTH  31
#define SENSOR_ENTITY_MAX_SENSORS         10

/**
 * @brief Crear e inicializar entidad de sensor
 *
 * @param sensor Puntero al sensor a inicializar
 * @param sensor_id ID único del sensor
 * @param type Tipo de sensor
 * @param description Descripción del sensor
 * @param physical_pin Pin físico de conexión
 * @return ESP_OK en éxito, ESP_ERR_INVALID_ARG si parámetros inválidos
 */
esp_err_t sensor_entity_create(sensor_entity_t* sensor,
                              uint8_t sensor_id,
                              sensor_type_t type,
                              const char* description,
                              uint8_t physical_pin);

/**
 * @brief Configurar rangos válidos para un tipo de sensor
 *
 * @param sensor Puntero al sensor
 * @param range Configuración de rangos válidos
 * @return ESP_OK en éxito
 */
esp_err_t sensor_entity_set_range(sensor_entity_t* sensor, const sensor_range_t* range);

/**
 * @brief Validar lectura de sensor
 *
 * Verifica que una lectura esté dentro de los rangos válidos
 * y determine la calidad de la lectura.
 *
 * @param sensor Puntero al sensor
 * @param value Valor a validar
 * @param response_time_ms Tiempo que tomó obtener la lectura
 * @return Calidad de la lectura
 */
sensor_quality_t sensor_entity_validate_reading(const sensor_entity_t* sensor,
                                               float value,
                                               uint16_t response_time_ms);

/**
 * @brief Registrar nueva lectura en el sensor
 *
 * Almacena una nueva lectura en el historial y actualiza estadísticas.
 *
 * @param sensor Puntero al sensor
 * @param value Valor leído
 * @param response_time_ms Tiempo que tomó la lectura
 * @return ESP_OK en éxito
 */
esp_err_t sensor_entity_record_reading(sensor_entity_t* sensor,
                                      float value,
                                      uint16_t response_time_ms);

/**
 * @brief Obtener última lectura válida del sensor
 *
 * @param sensor Puntero al sensor
 * @param value Puntero donde escribir el valor
 * @param timestamp Puntero donde escribir el timestamp
 * @param quality Puntero donde escribir la calidad
 * @return ESP_OK si hay lectura válida, ESP_ERR_NOT_FOUND si no hay datos
 */
esp_err_t sensor_entity_get_last_reading(const sensor_entity_t* sensor,
                                        float* value,
                                        uint32_t* timestamp,
                                        sensor_quality_t* quality);

/**
 * @brief Verificar si el sensor requiere calibración
 *
 * @param sensor Puntero al sensor
 * @return true si requiere calibración
 */
bool sensor_entity_requires_calibration(const sensor_entity_t* sensor);

/**
 * @brief Iniciar calibración del sensor
 *
 * @param sensor Puntero al sensor
 * @return ESP_OK en éxito
 */
esp_err_t sensor_entity_start_calibration(sensor_entity_t* sensor);

/**
 * @brief Completar calibración del sensor
 *
 * @param sensor Puntero al sensor
 * @return ESP_OK en éxito
 */
esp_err_t sensor_entity_complete_calibration(sensor_entity_t* sensor);

/**
 * @brief Detectar si el sensor está desconectado
 *
 * Basado en lecturas consecutivas fallidas y tiempo de respuesta.
 *
 * @param sensor Puntero al sensor
 * @return true si parece estar desconectado
 */
bool sensor_entity_is_disconnected(const sensor_entity_t* sensor);

/**
 * @brief Actualizar estado del sensor
 *
 * Actualiza el estado basado en el historial de lecturas y estadísticas.
 *
 * @param sensor Puntero al sensor
 */
void sensor_entity_update_state(sensor_entity_t* sensor);

/**
 * @brief Obtener promedio de lecturas recientes
 *
 * @param sensor Puntero al sensor
 * @param num_readings Número de lecturas a promediar (máx 20)
 * @return Promedio de las últimas lecturas válidas
 */
float sensor_entity_get_recent_average(const sensor_entity_t* sensor, uint8_t num_readings);

/**
 * @brief Detectar tendencia en las lecturas
 *
 * Analiza las últimas lecturas para detectar tendencias ascendentes
 * o descendentes.
 *
 * @param sensor Puntero al sensor
 * @param num_readings Número de lecturas a analizar
 * @return Pendiente de la tendencia (positiva = ascendente, negativa = descendente)
 */
float sensor_entity_get_reading_trend(const sensor_entity_t* sensor, uint8_t num_readings);

/**
 * @brief Verificar estabilidad de las lecturas
 *
 * Determina si las lecturas recientes son estables o hay mucha variación.
 *
 * @param sensor Puntero al sensor
 * @param num_readings Número de lecturas a analizar
 * @return true si las lecturas son estables
 */
bool sensor_entity_readings_are_stable(const sensor_entity_t* sensor, uint8_t num_readings);

/**
 * @brief Obtener estadísticas del sensor
 *
 * @param sensor Puntero al sensor
 * @return Puntero a las estadísticas del sensor
 */
const sensor_statistics_t* sensor_entity_get_statistics(const sensor_entity_t* sensor);

/**
 * @brief Resetear estadísticas del sensor
 *
 * @param sensor Puntero al sensor
 */
void sensor_entity_reset_statistics(sensor_entity_t* sensor);

/**
 * @brief Configurar intervalo de lectura
 *
 * @param sensor Puntero al sensor
 * @param interval_ms Nuevo intervalo en milisegundos
 * @return ESP_OK en éxito
 */
esp_err_t sensor_entity_set_reading_interval(sensor_entity_t* sensor, uint16_t interval_ms);

/**
 * @brief Activar/desactivar auto-calibración
 *
 * @param sensor Puntero al sensor
 * @param enabled true para activar auto-calibración
 */
void sensor_entity_set_auto_calibration(sensor_entity_t* sensor, bool enabled);

/**
 * @brief Marcar sensor como requiriendo mantenimiento
 *
 * @param sensor Puntero al sensor
 * @param required true si requiere mantenimiento
 */
void sensor_entity_set_maintenance_required(sensor_entity_t* sensor, bool required);

/**
 * @brief Obtener rangos válidos por tipo de sensor
 *
 * @param type Tipo de sensor
 * @param range Puntero donde escribir los rangos por defecto
 * @return ESP_OK si el tipo es válido
 */
esp_err_t sensor_entity_get_default_range_for_type(sensor_type_t type, sensor_range_t* range);

/**
 * @brief Convertir tipo de sensor a string
 *
 * @param type Tipo de sensor
 * @return String correspondiente al tipo
 */
const char* sensor_entity_type_to_string(sensor_type_t type);

/**
 * @brief Convertir string a tipo de sensor
 *
 * @param type_str String del tipo
 * @return Tipo de sensor correspondiente
 */
sensor_type_t sensor_entity_string_to_type(const char* type_str);

/**
 * @brief Convertir estado de sensor a string
 *
 * @param state Estado del sensor
 * @return String correspondiente al estado
 */
const char* sensor_entity_state_to_string(sensor_state_t state);

/**
 * @brief Convertir string a estado de sensor
 *
 * @param state_str String del estado
 * @return Estado de sensor correspondiente
 */
sensor_state_t sensor_entity_string_to_state(const char* state_str);

/**
 * @brief Convertir calidad de lectura a string
 *
 * @param quality Calidad de la lectura
 * @return String correspondiente a la calidad
 */
const char* sensor_entity_quality_to_string(sensor_quality_t quality);

/**
 * @brief Obtener timestamp actual del sistema
 *
 * @return Timestamp actual en segundos desde epoch Unix
 */
uint32_t sensor_entity_get_current_timestamp(void);

#endif // DOMAIN_ENTITIES_SENSOR_H