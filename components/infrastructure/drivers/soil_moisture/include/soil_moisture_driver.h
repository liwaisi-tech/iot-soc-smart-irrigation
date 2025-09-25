#ifndef INFRASTRUCTURE_DRIVERS_SOIL_MOISTURE_DRIVER_H
#define INFRASTRUCTURE_DRIVERS_SOIL_MOISTURE_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/adc.h"
#include "soil_sensor_data.h"

/**
 * @file soil_moisture_driver.h
 * @brief Driver para sensores de humedad del suelo
 *
 * Implementa lectura de sensores capacitivos de humedad del suelo
 * mediante ADC con calibración automática, filtrado de ruido y
 * compensación por temperatura para condiciones de suelo colombiano.
 *
 * Especificaciones Técnicas - Soil Moisture Driver:
 * - Hardware: Sensores capacitivos v1.2 (3.3V/5V compatibles)
 * - ADC: ESP32 ADC1 canales 0, 3, 6 (GPIO 36, 39, 34)
 * - Resolución: 12-bit (0-4095) con atenuación 11dB
 * - Calibración: Air-dry (0%) y water-saturated (100%)
 * - Filtrado: Mediana + outlier detection + averaging
 * - Compensación: Temperatura ambiente para drift correction
 */

/**
 * @brief Configuración de pines ADC para sensores
 */
#define SOIL_SENSOR_1_ADC_CHANNEL     ADC1_CHANNEL_0  // GPIO 36
#define SOIL_SENSOR_2_ADC_CHANNEL     ADC1_CHANNEL_3  // GPIO 39
#define SOIL_SENSOR_3_ADC_CHANNEL     ADC1_CHANNEL_6  // GPIO 34

/**
 * @brief Configuración ADC por defecto
 */
#define SOIL_ADC_ATTEN                ADC_ATTEN_DB_11  // 0-3.9V range
#define SOIL_ADC_WIDTH                ADC_WIDTH_BIT_12 // 12-bit resolution
#define SOIL_ADC_SAMPLES_PER_READING  10               // Muestras por lectura

/**
 * @brief Estados de calibración de sensor
 */
typedef enum {
    SOIL_CALIBRATION_UNCALIBRATED = 0,        // Sin calibrar
    SOIL_CALIBRATION_AIR_DRY,                 // Solo calibrado aire seco
    SOIL_CALIBRATION_WATER_SAT,               // Solo calibrado saturado agua
    SOIL_CALIBRATION_COMPLETE,                // Calibración completa
    SOIL_CALIBRATION_FAILED,                  // Calibración fallida
    SOIL_CALIBRATION_MAX
} soil_calibration_state_t;

/**
 * @brief Configuración individual de sensor
 */
typedef struct {
    uint8_t sensor_number;                    // Número sensor (1-3)
    adc1_channel_t adc_channel;               // Canal ADC asignado
    bool is_enabled;                          // Si está habilitado
    bool is_connected;                        // Si está físicamente conectado

    // Calibración
    uint16_t air_dry_adc_value;               // Valor ADC aire seco (0%)
    uint16_t water_saturated_adc_value;       // Valor ADC saturado (100%)
    soil_calibration_state_t calibration_state; // Estado calibración
    uint32_t last_calibration_timestamp;      // Última calibración

    // Filtrado y validación
    uint16_t min_valid_adc;                   // 200 - Valor ADC mínimo válido
    uint16_t max_valid_adc;                   // 3800 - Valor ADC máximo válido
    uint8_t filter_window_size;               // 5 - Tamaño ventana filtro

    // Compensación
    float temperature_coefficient;            // -0.002 - Coef temperatura (%/°C)
    float baseline_temperature;               // 25.0 - Temperatura referencia
} soil_sensor_config_t;

/**
 * @brief Estado en tiempo real de sensor
 */
typedef struct {
    uint8_t sensor_number;                    // Número sensor
    uint16_t raw_adc_value;                   // Último valor ADC crudo
    uint16_t filtered_adc_value;              // Valor ADC filtrado
    float raw_voltage;                        // Voltaje medido
    float humidity_percentage;                // Humedad calculada (%)
    float temperature_compensated_humidity;   // Humedad compensada temperatura

    // Calidad de lectura
    bool reading_valid;                       // Si lectura es válida
    uint8_t noise_level;                      // Nivel ruido (0-100)
    uint16_t response_time_ms;                // Tiempo respuesta última lectura

    // Historial circular para filtrado
    uint16_t reading_history[10];             // Últimas 10 lecturas ADC
    uint8_t history_index;                    // Índice circular
    uint8_t history_count;                    // Número entradas válidas

    // Estado del sensor
    uint32_t last_reading_timestamp;          // Última lectura exitosa
    uint32_t consecutive_failures;            // Fallos consecutivos
    bool requires_calibration;                // Si requiere calibración
} soil_sensor_state_t;

/**
 * @brief Configuración global del driver
 */
typedef struct {
    // Configuración ADC
    adc_atten_t adc_attenuation;              // Atenuación ADC
    adc_bits_width_t adc_width;               // Resolución ADC
    uint8_t samples_per_reading;              // Muestras por lectura
    uint16_t sample_interval_ms;              // Intervalo entre muestras

    // Configuración de filtrado
    bool enable_median_filter;                // Habilitar filtro mediana
    bool enable_average_filter;               // Habilitar filtro promedio
    bool enable_outlier_detection;            // Detectar valores atípicos
    float outlier_threshold_std_dev;          // 2.0 - Umbral outliers (desv std)

    // Configuración de calibración
    bool enable_auto_calibration;             // Auto-calibración habilitada
    uint32_t auto_calibration_interval_s;     // 86400s - Intervalo auto-cal (24h)
    float calibration_drift_threshold;        // 5.0% - Umbral deriva calibración

    // Compensación temperatura
    bool enable_temperature_compensation;     // Habilitar compensación temp
    float default_temperature_celsius;        // 25.0°C - Temp por defecto

    // Validación y errores
    uint16_t sensor_timeout_ms;               // 2000ms - Timeout lectura
    uint8_t max_consecutive_failures;         // 5 - Máximo fallos consecutivos
} soil_moisture_driver_config_t;

/**
 * @brief Estadísticas del driver
 */
typedef struct {
    uint32_t total_readings;                  // Total lecturas realizadas
    uint32_t successful_readings;             // Lecturas exitosas
    uint32_t failed_readings;                 // Lecturas fallidas
    uint32_t calibration_events;              // Eventos calibración
    uint32_t outliers_detected;               // Outliers detectados
    uint32_t auto_calibrations_performed;     // Auto-calibraciones realizadas
    float average_reading_time_ms;            // Tiempo promedio lectura
    float average_noise_level;                // Nivel promedio ruido
    uint32_t sensors_requiring_maintenance;   // Sensores requieren mantenimiento
} soil_moisture_statistics_t;

/**
 * @brief Inicializar driver de sensores de humedad del suelo
 *
 * @param config Configuración del driver
 * @param num_sensors Número de sensores a inicializar (1-3)
 * @return ESP_OK en éxito, ESP_ERR_* en error
 */
esp_err_t soil_moisture_driver_init(
    const soil_moisture_driver_config_t* config,
    uint8_t num_sensors
);

/**
 * @brief Desinicializar driver
 *
 * @return ESP_OK en éxito
 */
esp_err_t soil_moisture_driver_deinit(void);

/**
 * @brief Leer todos los sensores configurados
 *
 * @param ambient_temperature Temperatura ambiente para compensación
 * @param sensor_data Puntero donde escribir datos de sensores
 * @return ESP_OK en éxito, ESP_ERR_* en error
 */
esp_err_t soil_moisture_driver_read_all_sensors(
    float ambient_temperature,
    soil_sensor_data_t* sensor_data
);

/**
 * @brief Leer sensor individual específico
 *
 * @param sensor_number Número de sensor (1-3)
 * @param ambient_temperature Temperatura para compensación
 * @param sensor_state Puntero donde escribir estado del sensor
 * @return ESP_OK en éxito, ESP_ERR_* en error
 */
esp_err_t soil_moisture_driver_read_sensor(
    uint8_t sensor_number,
    float ambient_temperature,
    soil_sensor_state_t* sensor_state
);

/**
 * @brief Calibrar sensor específico - punto aire seco (0%)
 *
 * @param sensor_number Número de sensor (1-3)
 * @return ESP_OK en éxito
 */
esp_err_t soil_moisture_driver_calibrate_air_dry(uint8_t sensor_number);

/**
 * @brief Calibrar sensor específico - punto saturado agua (100%)
 *
 * @param sensor_number Número de sensor (1-3)
 * @return ESP_OK en éxito
 */
esp_err_t soil_moisture_driver_calibrate_water_saturated(uint8_t sensor_number);

/**
 * @brief Obtener configuración de sensor específico
 *
 * @param sensor_number Número de sensor (1-3)
 * @param config Puntero donde escribir configuración
 * @return ESP_OK en éxito
 */
esp_err_t soil_moisture_driver_get_sensor_config(
    uint8_t sensor_number,
    soil_sensor_config_t* config
);

/**
 * @brief Establecer configuración de sensor específico
 *
 * @param sensor_number Número de sensor (1-3)
 * @param config Nueva configuración del sensor
 * @return ESP_OK en éxito
 */
esp_err_t soil_moisture_driver_set_sensor_config(
    uint8_t sensor_number,
    const soil_sensor_config_t* config
);

/**
 * @brief Verificar si sensor está conectado
 *
 * @param sensor_number Número de sensor (1-3)
 * @return true si está conectado físicamente
 */
bool soil_moisture_driver_is_sensor_connected(uint8_t sensor_number);

/**
 * @brief Verificar estado de calibración de sensor
 *
 * @param sensor_number Número de sensor (1-3)
 * @return Estado de calibración actual
 */
soil_calibration_state_t soil_moisture_driver_get_calibration_state(uint8_t sensor_number);

/**
 * @brief Ejecutar auto-calibración en sensor
 *
 * @param sensor_number Número de sensor (1-3)
 * @return ESP_OK en éxito
 */
esp_err_t soil_moisture_driver_auto_calibrate(uint8_t sensor_number);

/**
 * @brief Resetear calibración de sensor
 *
 * @param sensor_number Número de sensor (1-3)
 * @return ESP_OK en éxito
 */
esp_err_t soil_moisture_driver_reset_calibration(uint8_t sensor_number);

/**
 * @brief Obtener estado en tiempo real de sensor
 *
 * @param sensor_number Número de sensor (1-3)
 * @param state Puntero donde escribir estado
 * @return ESP_OK en éxito
 */
esp_err_t soil_moisture_driver_get_sensor_state(
    uint8_t sensor_number,
    soil_sensor_state_t* state
);

/**
 * @brief Ejecutar test de conectividad de todos los sensores
 *
 * @return ESP_OK si todos los sensores pasan el test
 */
esp_err_t soil_moisture_driver_run_connectivity_test(void);

/**
 * @brief Obtener número de sensores habilitados
 *
 * @return Número de sensores habilitados (0-3)
 */
uint8_t soil_moisture_driver_get_enabled_sensors_count(void);

/**
 * @brief Habilitar/deshabilitar sensor específico
 *
 * @param sensor_number Número de sensor (1-3)
 * @param enable true para habilitar, false para deshabilitar
 * @return ESP_OK en éxito
 */
esp_err_t soil_moisture_driver_enable_sensor(uint8_t sensor_number, bool enable);

/**
 * @brief Obtener configuración global del driver
 *
 * @param config Puntero donde escribir configuración
 */
void soil_moisture_driver_get_config(soil_moisture_driver_config_t* config);

/**
 * @brief Establecer configuración global del driver
 *
 * @param config Nueva configuración global
 * @return ESP_OK en éxito
 */
esp_err_t soil_moisture_driver_set_config(const soil_moisture_driver_config_t* config);

/**
 * @brief Obtener estadísticas del driver
 *
 * @param stats Puntero donde escribir estadísticas
 */
void soil_moisture_driver_get_statistics(soil_moisture_statistics_t* stats);

/**
 * @brief Resetear estadísticas del driver
 */
void soil_moisture_driver_reset_statistics(void);

/**
 * @brief Callback para eventos de sensor
 *
 * @param sensor_number Sensor que generó evento
 * @param event_type Tipo de evento
 * @param sensor_state Estado actual del sensor
 */
typedef void (*soil_sensor_event_callback_t)(
    uint8_t sensor_number,
    const char* event_type,
    const soil_sensor_state_t* sensor_state
);

/**
 * @brief Registrar callback para eventos de sensores
 *
 * @param callback Función callback a registrar
 * @return ESP_OK en éxito
 */
esp_err_t soil_moisture_driver_register_callback(soil_sensor_event_callback_t callback);

/**
 * @brief Convertir estado de calibración a string
 *
 * @param state Estado de calibración
 * @return String correspondiente
 */
const char* soil_moisture_driver_calibration_state_to_string(soil_calibration_state_t state);

/**
 * @brief Obtener timestamp actual del sistema
 *
 * @return Timestamp actual en milisegundos
 */
uint32_t soil_moisture_driver_get_current_timestamp_ms(void);

#endif // INFRASTRUCTURE_DRIVERS_SOIL_MOISTURE_DRIVER_H