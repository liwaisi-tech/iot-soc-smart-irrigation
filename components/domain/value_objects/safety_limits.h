#ifndef DOMAIN_VALUE_OBJECTS_SAFETY_LIMITS_H
#define DOMAIN_VALUE_OBJECTS_SAFETY_LIMITS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @file safety_limits.h
 * @brief Value Object para límites de seguridad del sistema de riego
 *
 * Define los límites operacionales críticos del sistema de riego para
 * prevenir sobre-irrigación, daño a cultivos y mal funcionamiento del equipo.
 *
 * Especificaciones Técnicas - Límites de Seguridad:
 * - Basado en dataset climático real de Colombia
 * - Umbrales validados agronómicamente
 * - Protecciones multi-nivel para diferentes escenarios
 * - Configurables via menuconfig
 */

/**
 * @brief Límites de humedad del suelo (porcentajes)
 *
 * Basados en análisis de dataset real de Colombia rural y
 * requisitos agronómicos para cultivos tropicales.
 */
typedef struct {
    float critical_low_percent;           // 30.0% - Emergencia, riego inmediato
    float warning_low_percent;            // 45.0% - Advertencia, programar riego
    float target_min_percent;             // 70.0% - Objetivo mínimo, continuar riego
    float target_max_percent;             // 75.0% - Objetivo máximo, PARAR riego
    float emergency_high_percent;         // 80.0% - Peligro, PARADA EMERGENCIA
} soil_humidity_limits_t;

/**
 * @brief Límites de temperatura ambiente (grados Celsius)
 *
 * Basados en dataset climático real de Colombia (agosto 2024).
 * Patrones identificados: madrugada 26-28°C, mediodía 32-34°C.
 */
typedef struct {
    float night_irrigation_max;           // 32.0°C - Máxima para riego nocturno
    float thermal_stress_min;             // 32.0°C - Mínima para riego por estrés
    float emergency_stop_max;             // 40.0°C - Parada por protección térmica
    float optimal_range_min;              // 26.0°C - Rango óptimo mínimo
    float optimal_range_max;              // 30.0°C - Rango óptimo máximo
} temperature_limits_t;

/**
 * @brief Límites de humedad ambiente (porcentajes)
 *
 * Basados en dataset: madrugada 73-78%, mediodía 45-65%.
 */
typedef struct {
    float night_irrigation_min;           // 60.0% - Mínima para riego nocturno
    float thermal_stress_max;             // 55.0% - Máxima para riego por estrés
    float optimal_range_min;              // 60.0% - Rango óptimo mínimo
    float optimal_range_max;              // 75.0% - Rango óptimo máximo
} ambient_humidity_limits_t;

/**
 * @brief Límites de duración temporal
 */
typedef struct {
    uint16_t max_session_minutes;         // 30 - Máximo por sesión de riego
    uint16_t min_session_minutes;         // 1 - Mínimo por sesión
    uint16_t cooldown_minutes;            // 240 - Cooldown entre sesiones (4h)
    uint16_t emergency_duration_minutes;  // 5 - Duración riego de emergencia
    uint16_t default_duration_minutes;    // 15 - Duración por defecto
} duration_limits_t;

/**
 * @brief Ventanas de tiempo para riego optimizado
 */
typedef struct {
    uint8_t night_start_hour;             // 0 - Inicio ventana nocturna (00:00)
    uint8_t night_end_hour;               // 6 - Fin ventana nocturna (06:00)
    uint8_t thermal_stress_start_hour;    // 12 - Inicio ventana estrés (12:00)
    uint8_t thermal_stress_end_hour;      // 15 - Fin ventana estrés (15:00)
    uint8_t maintenance_start_hour;       // 2 - Inicio ventana mantenimiento
    uint8_t maintenance_end_hour;         // 4 - Fin ventana mantenimiento
} time_window_limits_t;

/**
 * @brief Límites de hardware y sistema
 */
typedef struct {
    uint8_t max_valves;                   // 3 - Número máximo de válvulas
    uint8_t min_working_sensors;          // 2 - Sensores mínimos funcionales
    uint16_t valve_response_timeout_ms;   // 5000 - Timeout respuesta válvula
    uint16_t sensor_reading_timeout_ms;   // 2000 - Timeout lectura sensor
    uint32_t offline_activation_delay_s;  // 300 - Delay activación offline (5min)
} hardware_limits_t;

/**
 * @brief Estructura completa de límites de seguridad
 *
 * Agrupa todos los límites operacionales del sistema en una
 * estructura única para fácil gestión y validación.
 */
typedef struct {
    soil_humidity_limits_t soil_humidity;
    temperature_limits_t temperature;
    ambient_humidity_limits_t ambient_humidity;
    duration_limits_t duration;
    time_window_limits_t time_windows;
    hardware_limits_t hardware;
    uint32_t last_update_timestamp;       // Última actualización de límites
    bool limits_validated;                // Flag de validación
} safety_limits_t;

/**
 * @brief Valores por defecto basados en dataset Colombia rural
 */
#define SAFETY_LIMITS_SOIL_CRITICAL_LOW         30.0f
#define SAFETY_LIMITS_SOIL_WARNING_LOW          45.0f
#define SAFETY_LIMITS_SOIL_TARGET_MIN           70.0f
#define SAFETY_LIMITS_SOIL_TARGET_MAX           75.0f
#define SAFETY_LIMITS_SOIL_EMERGENCY_HIGH       80.0f

#define SAFETY_LIMITS_TEMP_NIGHT_MAX            32.0f
#define SAFETY_LIMITS_TEMP_THERMAL_MIN          32.0f
#define SAFETY_LIMITS_TEMP_EMERGENCY_MAX        40.0f

#define SAFETY_LIMITS_AMBIENT_NIGHT_MIN         60.0f
#define SAFETY_LIMITS_AMBIENT_THERMAL_MAX       55.0f

#define SAFETY_LIMITS_DURATION_MAX_SESSION      30
#define SAFETY_LIMITS_DURATION_COOLDOWN         240
#define SAFETY_LIMITS_DURATION_EMERGENCY        5
#define SAFETY_LIMITS_DURATION_DEFAULT          15

/**
 * @brief Inicializar límites de seguridad con valores por defecto
 *
 * Configura todos los límites con valores seguros basados en
 * el análisis del dataset climático de Colombia.
 *
 * @param limits Puntero a la estructura de límites
 */
void safety_limits_init_default(safety_limits_t* limits);

/**
 * @brief Validar estructura completa de límites
 *
 * Verifica que todos los límites estén en rangos lógicos y seguros.
 *
 * @param limits Puntero a los límites a validar
 * @return true si todos los límites son válidos
 */
bool safety_limits_validate(const safety_limits_t* limits);

/**
 * @brief Validar límites de humedad del suelo
 *
 * @param limits Límites de humedad del suelo
 * @return true si los límites son válidos
 */
bool safety_limits_validate_soil_humidity(const soil_humidity_limits_t* limits);

/**
 * @brief Validar límites de temperatura
 *
 * @param limits Límites de temperatura
 * @return true si los límites son válidos
 */
bool safety_limits_validate_temperature(const temperature_limits_t* limits);

/**
 * @brief Validar límites de duración
 *
 * @param limits Límites de duración
 * @return true si los límites son válidos
 */
bool safety_limits_validate_duration(const duration_limits_t* limits);

/**
 * @brief Verificar si la humedad del suelo está en rango crítico bajo
 *
 * @param limits Límites de seguridad
 * @param soil_humidity Humedad del suelo a verificar (%)
 * @return true si está en rango crítico
 */
bool safety_limits_is_soil_critical_low(const safety_limits_t* limits, float soil_humidity);

/**
 * @brief Verificar si la humedad del suelo está en rango de advertencia
 *
 * @param limits Límites de seguridad
 * @param soil_humidity Humedad del suelo a verificar (%)
 * @return true si está en rango de advertencia
 */
bool safety_limits_is_soil_warning_low(const safety_limits_t* limits, float soil_humidity);

/**
 * @brief Verificar si la humedad del suelo alcanzó el objetivo
 *
 * @param limits Límites de seguridad
 * @param soil_humidity Humedad del suelo a verificar (%)
 * @return true si está en rango objetivo (70-75%)
 */
bool safety_limits_is_soil_target_reached(const safety_limits_t* limits, float soil_humidity);

/**
 * @brief Verificar si la humedad del suelo está en peligro de sobre-irrigación
 *
 * CRÍTICO: Si cualquier sensor supera 80%, activar parada de emergencia.
 *
 * @param limits Límites de seguridad
 * @param soil_humidity Humedad del suelo a verificar (%)
 * @return true si está en peligro de sobre-irrigación
 */
bool safety_limits_is_soil_emergency_high(const safety_limits_t* limits, float soil_humidity);

/**
 * @brief Verificar si la temperatura permite riego nocturno
 *
 * @param limits Límites de seguridad
 * @param temperature Temperatura ambiente (°C)
 * @return true si permite riego nocturno
 */
bool safety_limits_allows_night_irrigation(const safety_limits_t* limits, float temperature);

/**
 * @brief Verificar si las condiciones requieren riego por estrés térmico
 *
 * @param limits Límites de seguridad
 * @param temperature Temperatura ambiente (°C)
 * @param ambient_humidity Humedad ambiente (%)
 * @return true si requiere riego por estrés térmico
 */
bool safety_limits_requires_thermal_stress_irrigation(const safety_limits_t* limits,
                                                    float temperature,
                                                    float ambient_humidity);

/**
 * @brief Verificar si la temperatura requiere parada de emergencia
 *
 * @param limits Límites de seguridad
 * @param temperature Temperatura ambiente (°C)
 * @return true si requiere parada de emergencia
 */
bool safety_limits_requires_temperature_emergency_stop(const safety_limits_t* limits, float temperature);

/**
 * @brief Verificar si la duración está dentro de límites seguros
 *
 * @param limits Límites de seguridad
 * @param duration_minutes Duración a verificar
 * @return true si la duración es segura
 */
bool safety_limits_is_duration_safe(const safety_limits_t* limits, uint16_t duration_minutes);

/**
 * @brief Verificar si estamos en ventana de riego nocturno
 *
 * @param limits Límites de seguridad
 * @param current_hour Hora actual (0-23)
 * @return true si estamos en ventana nocturna
 */
bool safety_limits_is_night_window(const safety_limits_t* limits, uint8_t current_hour);

/**
 * @brief Verificar si estamos en ventana de estrés térmico
 *
 * @param limits Límites de seguridad
 * @param current_hour Hora actual (0-23)
 * @return true si estamos en ventana de estrés térmico
 */
bool safety_limits_is_thermal_stress_window(const safety_limits_t* limits, uint8_t current_hour);

/**
 * @brief Obtener duración recomendada basada en condiciones
 *
 * @param limits Límites de seguridad
 * @param is_emergency true si es riego de emergencia
 * @return Duración recomendada en minutos
 */
uint16_t safety_limits_get_recommended_duration(const safety_limits_t* limits, bool is_emergency);

/**
 * @brief Actualizar timestamp de última modificación
 *
 * @param limits Límites de seguridad
 */
void safety_limits_update_timestamp(safety_limits_t* limits);

/**
 * @brief Copiar límites de seguridad
 *
 * @param dest Destino de la copia
 * @param src Origen de la copia
 */
void safety_limits_copy(safety_limits_t* dest, const safety_limits_t* src);

#endif // DOMAIN_VALUE_OBJECTS_SAFETY_LIMITS_H