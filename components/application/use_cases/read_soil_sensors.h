#ifndef APPLICATION_USE_CASES_READ_SOIL_SENSORS_H
#define APPLICATION_USE_CASES_READ_SOIL_SENSORS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "soil_sensor_data.h"
#include "ambient_sensor_data.h"
#include "safety_limits.h"

/**
 * @file read_soil_sensors.h
 * @brief Use Case para lectura especializada de sensores de suelo
 *
 * Maneja la lectura, validación, calibración y interpretación de
 * sensores de humedad del suelo con algoritmos optimizados para
 * condiciones de suelo colombiano y detección de anomalías.
 *
 * Especificaciones Técnicas - Read Soil Sensors Use Case:
 * - Multi-sensor: Soporte 1-3 sensores capacitivos simultáneos
 * - Calibración automática: Air-dry y water-saturated points
 * - Detección anomalías: Sensores desconectados, lecturas erróneas
 * - Compensación temperatura: Ajuste por temperatura del suelo
 * - Averaging inteligente: Filtrado de ruido electromagnético
 */

/**
 * @brief Estados de lectura de sensores
 */
typedef enum {
    SOIL_READING_SUCCESS = 0,                 // Lectura exitosa
    SOIL_READING_SENSOR_TIMEOUT,              // Timeout en sensor
    SOIL_READING_SENSOR_DISCONNECTED,         // Sensor desconectado
    SOIL_READING_INVALID_VALUE,               // Valor fuera de rango
    SOIL_READING_CALIBRATION_REQUIRED,        // Requiere calibración
    SOIL_READING_ELECTROMAGNETIC_INTERFERENCE,// Interferencia electromagnética
    SOIL_READING_TEMPERATURE_ERROR,           // Error lectura temperatura
    SOIL_READING_PARTIAL_SUCCESS,             // Éxito parcial (algunos sensores)
    SOIL_READING_SYSTEM_ERROR,                // Error general del sistema
    SOIL_READING_MAX
} soil_reading_result_t;

/**
 * @brief Tipos de sensores de suelo soportados
 */
typedef enum {
    SOIL_SENSOR_TYPE_CAPACITIVE = 0,          // Sensor capacitivo (recomendado)
    SOIL_SENSOR_TYPE_RESISTIVE,               // Sensor resistivo (básico)
    SOIL_SENSOR_TYPE_FREQUENCY_DOMAIN,        // Sensor FDR (avanzado)
    SOIL_SENSOR_TYPE_UNKNOWN,                 // Tipo no identificado
    SOIL_SENSOR_TYPE_MAX
} soil_sensor_type_t;

/**
 * @brief Estado individual de sensor de suelo
 */
typedef struct {
    uint8_t sensor_number;                    // Número del sensor (1-3)
    soil_sensor_type_t sensor_type;           // Tipo de sensor
    bool is_connected;                        // Si está conectado
    bool is_calibrated;                       // Si está calibrado
    uint16_t raw_adc_value;                   // Valor ADC crudo
    float raw_voltage;                        // Voltaje leído
    float humidity_percentage;                // Humedad calculada (%)
    float temperature_celsius;                // Temperatura del suelo (°C)
    soil_reading_result_t reading_status;     // Estado de la lectura
    uint32_t last_reading_timestamp;          // Última lectura exitosa
    uint16_t consecutive_failures;            // Fallos consecutivos
} soil_sensor_state_t;

/**
 * @brief Configuración de calibración por sensor
 */
typedef struct {
    uint16_t air_dry_adc_value;               // Valor ADC en aire seco (0%)
    uint16_t water_saturated_adc_value;       // Valor ADC saturado agua (100%)
    float voltage_air_dry;                    // Voltaje en aire seco
    float voltage_water_saturated;            // Voltaje saturado agua
    bool auto_calibration_enabled;            // Si está activa auto-calibración
    uint32_t last_calibration_timestamp;      // Última calibración
} soil_sensor_calibration_t;

/**
 * @brief Configuración completa de lectura de sensores
 */
typedef struct {
    uint8_t num_sensors_enabled;              // Número de sensores activos (1-3)
    uint16_t reading_interval_ms;             // Intervalo entre lecturas
    uint8_t readings_per_average;             // Lecturas para promedio (3-10)
    uint16_t sensor_warmup_time_ms;           // Tiempo de calentamiento
    uint16_t adc_stabilization_time_ms;       // Tiempo estabilización ADC

    // Configuración de filtrado
    bool enable_median_filter;                // Habilitar filtro mediana
    bool enable_outlier_detection;            // Detectar valores atípicos
    float outlier_threshold_percentage;       // 15.0 - Umbral valores atípicos

    // Configuración de validación
    uint16_t min_valid_adc_value;             // Valor ADC mínimo válido
    uint16_t max_valid_adc_value;             // Valor ADC máximo válido
    float min_valid_humidity_percent;         // 0.0 - Humedad mínima válida
    float max_valid_humidity_percent;         // 100.0 - Humedad máxima válida

    // Configuración de errores
    uint8_t max_consecutive_failures;         // 5 - Máximo fallos consecutivos
    uint16_t sensor_timeout_ms;               // 2000 - Timeout lectura sensor
} soil_sensors_config_t;

/**
 * @brief Resultado completo de lectura de sensores
 */
typedef struct {
    soil_reading_result_t overall_result;     // Resultado general
    soil_sensor_data_t sensor_data;           // Datos finales de sensores
    soil_sensor_state_t sensor_states[3];     // Estado individual de cada sensor
    uint8_t sensors_read_successfully;        // Número de sensores exitosos
    uint8_t sensors_with_errors;              // Número de sensores con error
    float average_soil_humidity;              // Promedio general de humedad
    float humidity_variance;                  // Varianza entre sensores
    bool requires_calibration;                // Si requiere calibración
    bool has_anomalies;                       // Si detectó anomalías
    char error_description[128];              // Descripción de errores
    uint32_t total_reading_time_ms;           // Tiempo total de lectura
} soil_reading_complete_result_t;

/**
 * @brief Estadísticas de lectura de sensores
 */
typedef struct {
    uint32_t total_readings_attempted;        // Total lecturas intentadas
    uint32_t successful_readings;             // Lecturas exitosas
    uint32_t failed_readings;                 // Lecturas fallidas
    uint32_t sensors_disconnected_events;     // Eventos desconexión sensor
    uint32_t calibration_events;              // Eventos de calibración
    uint32_t anomaly_detections;              // Detecciones de anomalías
    float average_reading_time_ms;            // Tiempo promedio lectura
    float average_soil_humidity_trend;        // Tendencia promedio humedad
    uint32_t last_successful_reading_timestamp; // Última lectura exitosa
} soil_sensors_statistics_t;

/**
 * @brief Inicializar use case de lectura de sensores de suelo
 *
 * @param safety_limits Límites de seguridad del sistema
 * @param num_sensors Número de sensores a configurar (1-3)
 * @return ESP_OK en éxito
 */
esp_err_t read_soil_sensors_init(
    const safety_limits_t* safety_limits,
    uint8_t num_sensors
);

/**
 * @brief Leer todos los sensores de suelo configurados
 *
 * Función principal que realiza lectura completa, validación
 * y procesamiento de todos los sensores de suelo.
 *
 * @param ambient_data Datos ambientales para compensación temperatura
 * @return Resultado completo de lectura
 */
soil_reading_complete_result_t read_soil_sensors_read_all(
    const ambient_sensor_data_t* ambient_data
);

/**
 * @brief Leer sensor individual específico
 *
 * @param sensor_number Número de sensor (1-3)
 * @param ambient_temperature Temperatura para compensación
 * @param sensor_state Puntero donde escribir estado del sensor
 * @return Estado de la lectura individual
 */
soil_reading_result_t read_soil_sensors_read_individual(
    uint8_t sensor_number,
    float ambient_temperature,
    soil_sensor_state_t* sensor_state
);

/**
 * @brief Calibrar sensor específico
 *
 * Proceso de calibración air-dry y water-saturated para un sensor.
 *
 * @param sensor_number Número de sensor (1-3)
 * @param calibration_type 0=air-dry, 1=water-saturated
 * @return ESP_OK en éxito
 */
esp_err_t read_soil_sensors_calibrate_sensor(
    uint8_t sensor_number,
    uint8_t calibration_type
);

/**
 * @brief Auto-calibración basada en historial
 *
 * Calibración automática basada en valores extremos históricos.
 *
 * @param sensor_number Número de sensor (1-3)
 * @return ESP_OK en éxito
 */
esp_err_t read_soil_sensors_auto_calibrate(uint8_t sensor_number);

/**
 * @brief Convertir valor ADC a porcentaje de humedad
 *
 * @param sensor_number Número de sensor (1-3)
 * @param raw_adc_value Valor ADC crudo
 * @param ambient_temperature Temperatura para compensación
 * @return Porcentaje de humedad (0.0-100.0)
 */
float read_soil_sensors_adc_to_humidity_percentage(
    uint8_t sensor_number,
    uint16_t raw_adc_value,
    float ambient_temperature
);

/**
 * @brief Aplicar filtro mediana a lecturas múltiples
 *
 * @param readings Array de lecturas
 * @param num_readings Número de lecturas
 * @return Valor filtrado
 */
uint16_t read_soil_sensors_apply_median_filter(
    const uint16_t* readings,
    uint8_t num_readings
);

/**
 * @brief Detectar valores atípicos (outliers)
 *
 * @param readings Array de lecturas
 * @param num_readings Número de lecturas
 * @param outlier_threshold Umbral para detección
 * @return Número de outliers detectados
 */
uint8_t read_soil_sensors_detect_outliers(
    const uint16_t* readings,
    uint8_t num_readings,
    float outlier_threshold
);

/**
 * @brief Verificar si sensor está desconectado
 *
 * @param sensor_state Estado del sensor a verificar
 * @return true si parece desconectado
 */
bool read_soil_sensors_is_sensor_disconnected(const soil_sensor_state_t* sensor_state);

/**
 * @brief Detectar anomalías en lectura
 *
 * @param sensor_state Estado del sensor
 * @param previous_readings Array de lecturas anteriores
 * @param num_previous Número de lecturas anteriores
 * @return true si detectó anomalías
 */
bool read_soil_sensors_detect_anomalies(
    const soil_sensor_state_t* sensor_state,
    const float* previous_readings,
    uint8_t num_previous
);

/**
 * @brief Calcular varianza entre sensores
 *
 * @param sensor_states Array de estados de sensores
 * @param num_sensors Número de sensores
 * @return Varianza calculada
 */
float read_soil_sensors_calculate_humidity_variance(
    const soil_sensor_state_t* sensor_states,
    uint8_t num_sensors
);

/**
 * @brief Determinar sensor más confiable
 *
 * @param sensor_states Array de estados de sensores
 * @param num_sensors Número de sensores
 * @return Número del sensor más confiable (1-3)
 */
uint8_t read_soil_sensors_find_most_reliable_sensor(
    const soil_sensor_state_t* sensor_states,
    uint8_t num_sensors
);

/**
 * @brief Generar promedio inteligente
 *
 * Promedio ponderado excluyendo sensores defectuosos.
 *
 * @param sensor_states Array de estados de sensores
 * @param num_sensors Número de sensores
 * @return Promedio ponderado de humedad
 */
float read_soil_sensors_calculate_weighted_average(
    const soil_sensor_state_t* sensor_states,
    uint8_t num_sensors
);

/**
 * @brief Actualizar estadísticas de sensor
 *
 * @param result Resultado de lectura completa
 */
void read_soil_sensors_update_statistics(const soil_reading_complete_result_t* result);

/**
 * @brief Generar descripción de error
 *
 * @param result Resultado con errores
 * @param error_buffer Buffer para descripción
 * @param buffer_size Tamaño del buffer
 */
void read_soil_sensors_generate_error_description(
    const soil_reading_complete_result_t* result,
    char* error_buffer,
    size_t buffer_size
);

/**
 * @brief Configurar parámetros de lectura
 *
 * @param config Nueva configuración de sensores
 * @return ESP_OK en éxito
 */
esp_err_t read_soil_sensors_set_config(const soil_sensors_config_t* config);

/**
 * @brief Obtener configuración actual
 *
 * @param config Puntero donde escribir configuración
 */
void read_soil_sensors_get_config(soil_sensors_config_t* config);

/**
 * @brief Obtener calibración de sensor específico
 *
 * @param sensor_number Número de sensor (1-3)
 * @param calibration Puntero donde escribir calibración
 * @return ESP_OK en éxito
 */
esp_err_t read_soil_sensors_get_calibration(
    uint8_t sensor_number,
    soil_sensor_calibration_t* calibration
);

/**
 * @brief Establecer calibración manual de sensor
 *
 * @param sensor_number Número de sensor (1-3)
 * @param calibration Nueva calibración
 * @return ESP_OK en éxito
 */
esp_err_t read_soil_sensors_set_calibration(
    uint8_t sensor_number,
    const soil_sensor_calibration_t* calibration
);

/**
 * @brief Resetear sensor específico
 *
 * @param sensor_number Número de sensor (1-3)
 * @return ESP_OK en éxito
 */
esp_err_t read_soil_sensors_reset_sensor(uint8_t sensor_number);

/**
 * @brief Obtener estadísticas de sensores
 *
 * @param stats Puntero donde escribir estadísticas
 */
void read_soil_sensors_get_statistics(soil_sensors_statistics_t* stats);

/**
 * @brief Resetear estadísticas
 */
void read_soil_sensors_reset_statistics(void);

/**
 * @brief Convertir resultado de lectura a string
 *
 * @param result Resultado de lectura
 * @return String correspondiente
 */
const char* read_soil_sensors_result_to_string(soil_reading_result_t result);

/**
 * @brief Convertir tipo de sensor a string
 *
 * @param sensor_type Tipo de sensor
 * @return String correspondiente
 */
const char* read_soil_sensors_type_to_string(soil_sensor_type_t sensor_type);

/**
 * @brief Obtener timestamp actual del sistema
 *
 * @return Timestamp actual en segundos desde epoch Unix
 */
uint32_t read_soil_sensors_get_current_timestamp(void);

#endif // APPLICATION_USE_CASES_READ_SOIL_SENSORS_H