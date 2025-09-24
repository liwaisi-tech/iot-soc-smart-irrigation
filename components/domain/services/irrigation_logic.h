#ifndef DOMAIN_SERVICES_IRRIGATION_LOGIC_H
#define DOMAIN_SERVICES_IRRIGATION_LOGIC_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "ambient_sensor_data.h"
#include "soil_sensor_data.h"
#include "safety_limits.h"
#include "irrigation_command.h"

/**
 * @file irrigation_logic.h
 * @brief Servicio de dominio para lógica de riego inteligente
 *
 * Implementa toda la lógica de negocio para tomar decisiones de riego
 * basadas en condiciones ambientales, estado del suelo y patrones
 * climáticos optimizados para Colombia rural.
 *
 * Especificaciones Técnicas - Irrigation Logic Service:
 * - Algoritmos basados en dataset climático real de Colombia
 * - Evaluación multi-criterio: suelo, ambiente, tiempo, energía
 * - Modos de riego: nocturno optimizado y estrés térmico
 * - Protecciones de seguridad: sobre-irrigación, límites térmicos
 * - Zero dependencies: Solo usa otros componentes del domain
 */

/**
 * @brief Tipos de decisiones de riego
 */
typedef enum {
    IRRIGATION_DECISION_NO_ACTION = 0,        // No se requiere acción
    IRRIGATION_DECISION_START_NIGHT,          // Iniciar riego nocturno (00:00-06:00)
    IRRIGATION_DECISION_START_THERMAL,        // Iniciar riego por estrés térmico
    IRRIGATION_DECISION_START_EMERGENCY,      // Iniciar riego de emergencia
    IRRIGATION_DECISION_CONTINUE,             // Continuar riego actual
    IRRIGATION_DECISION_STOP_TARGET,          // Parar - objetivo alcanzado (70-75%)
    IRRIGATION_DECISION_STOP_TIMEOUT,         // Parar - tiempo límite alcanzado
    IRRIGATION_DECISION_EMERGENCY_STOP,       // Parada de emergencia - sobre-riego
    IRRIGATION_DECISION_WAIT_COOLDOWN,        // Esperar cooldown entre sesiones
    IRRIGATION_DECISION_MAX
} irrigation_decision_type_t;

/**
 * @brief Razones específicas para decisiones de riego
 */
typedef enum {
    IRRIGATION_REASON_SOIL_CRITICAL = 0,      // Humedad suelo < 30%
    IRRIGATION_REASON_SOIL_LOW,               // Humedad promedio < 45%
    IRRIGATION_REASON_THERMAL_STRESS,         // Condiciones de estrés térmico
    IRRIGATION_REASON_NIGHT_OPTIMAL,          // Ventana nocturna óptima
    IRRIGATION_REASON_TARGET_REACHED,         // 70-75% humedad alcanzada
    IRRIGATION_REASON_OVER_IRRIGATION,        // Humedad > 80% - PELIGRO
    IRRIGATION_REASON_TEMPERATURE_HIGH,       // Temperatura > 40°C
    IRRIGATION_REASON_DURATION_EXCEEDED,      // Duración máxima excedida
    IRRIGATION_REASON_SENSOR_FAILURE,         // Fallo en sensores
    IRRIGATION_REASON_POWER_SAVING,           // Modo ahorro energético
    IRRIGATION_REASON_MAX
} irrigation_decision_reason_t;

/**
 * @brief Resultado completo de evaluación de riego
 */
typedef struct {
    irrigation_decision_type_t decision;      // Decisión principal
    irrigation_decision_reason_t reason;      // Razón específica
    uint8_t recommended_valve;                // Válvula recomendada (1-3)
    uint16_t recommended_duration_minutes;    // Duración recomendada
    float confidence_level;                   // Nivel de confianza (0.0-1.0)
    char explanation[128];                    // Explicación detallada
    uint32_t evaluation_timestamp;            // Cuándo se hizo la evaluación
} irrigation_decision_t;

/**
 * @brief Contexto para evaluación de riego
 */
typedef struct {
    uint8_t current_hour;                     // Hora actual (0-23)
    uint8_t current_minute;                   // Minuto actual (0-59)
    uint32_t time_since_last_irrigation_s;    // Tiempo desde último riego
    bool is_offline_mode;                     // Si está en modo offline
    power_mode_t current_power_mode;          // Modo de energía actual
    uint32_t irrigation_sessions_today;       // Sesiones de riego hoy
} irrigation_evaluation_context_t;

/**
 * @brief Configuración de algoritmo de riego nocturno
 *
 * Basado en análisis de dataset Colombia: ventana 00:00-06:00,
 * temperaturas 26-28°C, humedad ambiente 73-78%.
 */
typedef struct {
    uint8_t window_start_hour;                // 0 - Inicio ventana (00:00)
    uint8_t window_end_hour;                  // 6 - Fin ventana (06:00)
    float max_temperature_celsius;            // 32.0°C - Temp máxima
    float min_ambient_humidity_percent;       // 60.0% - Hum ambiente mínima
    float soil_activation_threshold_percent;  // 45.0% - Umbral activación
    uint16_t default_duration_minutes;        // 15 - Duración por defecto
    uint16_t max_duration_minutes;            // 30 - Duración máxima
} night_irrigation_config_t;

/**
 * @brief Configuración de algoritmo de riego por estrés térmico
 *
 * Basado en dataset: ventana 12:00-15:00, picos 32-34°C,
 * humedad ambiente mínima 45-65%.
 */
typedef struct {
    uint8_t window_start_hour;                // 12 - Inicio ventana (12:00)
    uint8_t window_end_hour;                  // 15 - Fin ventana (15:00)
    float min_temperature_celsius;            // 32.0°C - Temp mínima activación
    float max_ambient_humidity_percent;       // 55.0% - Hum ambiente máxima
    float soil_minimum_threshold_percent;     // 30.0% - Umbral mínimo suelo
    uint16_t session_duration_minutes;        // 1 - Duración por sesión
    uint32_t cooldown_interval_seconds;       // 21600 - Cooldown (6 horas)
} thermal_stress_config_t;

/**
 * @brief Inicializar servicio de lógica de riego
 *
 * Configura el servicio con los límites de seguridad y algoritmos
 * optimizados para el clima de Colombia.
 *
 * @param safety_limits Límites de seguridad del sistema
 * @return ESP_OK en éxito
 */
esp_err_t irrigation_logic_init(const safety_limits_t* safety_limits);

/**
 * @brief Evaluar condiciones actuales y determinar acción de riego
 *
 * Función principal del servicio que analiza todas las condiciones
 * y determina la mejor acción de riego según las reglas de negocio.
 *
 * @param ambient_data Datos de sensores ambientales
 * @param soil_data Datos de sensores de suelo
 * @param context Contexto de la evaluación (hora, modo, etc.)
 * @return Decisión de riego con recomendaciones específicas
 */
irrigation_decision_t irrigation_logic_evaluate_conditions(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    const irrigation_evaluation_context_t* context
);

/**
 * @brief Evaluar específicamente riego nocturno
 *
 * Algoritmo optimizado para la ventana nocturna (00:00-06:00) basado
 * en condiciones climáticas típicas de Colombia rural.
 *
 * @param ambient_data Datos ambientales
 * @param soil_data Datos de suelo
 * @param context Contexto de evaluación
 * @return Decisión específica para riego nocturno
 */
irrigation_decision_t irrigation_logic_evaluate_night_irrigation(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    const irrigation_evaluation_context_t* context
);

/**
 * @brief Evaluar riego por estrés térmico
 *
 * Algoritmo para riego de emergencia durante picos de calor
 * (12:00-15:00) cuando las condiciones son críticas.
 *
 * @param ambient_data Datos ambientales
 * @param soil_data Datos de suelo
 * @param context Contexto de evaluación
 * @return Decisión específica para estrés térmico
 */
irrigation_decision_t irrigation_logic_evaluate_thermal_stress_irrigation(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    const irrigation_evaluation_context_t* context
);

/**
 * @brief Verificar si debe parar el riego actual
 *
 * CRÍTICO: Evalúa si el riego debe parar por haber alcanzado
 * el objetivo (70-75%) o por protección de sobre-riego (>80%).
 *
 * @param soil_data Datos actuales de sensores de suelo
 * @return Decisión de parada con razón específica
 */
irrigation_decision_t irrigation_logic_evaluate_stop_conditions(
    const soil_sensor_data_t* soil_data
);

/**
 * @brief Verificar si hay condiciones de emergencia
 *
 * Detecta condiciones que requieren parada inmediata:
 * - Sobre-irrigación: cualquier sensor > 80%
 * - Temperatura peligrosa: > 40°C
 * - Fallo de sensores críticos
 *
 * @param ambient_data Datos ambientales
 * @param soil_data Datos de suelo
 * @return true si hay condición de emergencia
 */
bool irrigation_logic_has_emergency_condition(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data
);

/**
 * @brief Calcular duración óptima de riego
 *
 * Basado en la diferencia entre humedad actual y objetivo,
 * tipo de suelo, condiciones ambientales y modo de energía.
 *
 * @param soil_data Datos de suelo actuales
 * @param ambient_data Datos ambientales
 * @param is_emergency true si es riego de emergencia
 * @param power_mode Modo de energía actual
 * @return Duración recomendada en minutos
 */
uint16_t irrigation_logic_calculate_optimal_duration(
    const soil_sensor_data_t* soil_data,
    const ambient_sensor_data_t* ambient_data,
    bool is_emergency,
    power_mode_t power_mode
);

/**
 * @brief Seleccionar válvula óptima para riego
 *
 * Elige la válvula más apropiada basándose en:
 * - Humedad individual de cada sensor
 * - Historial de uso de válvulas
 * - Distribución uniforme de riego
 *
 * @param soil_data Datos de sensores de suelo
 * @param last_used_valve Última válvula utilizada
 * @return Número de válvula recomendada (1-3)
 */
uint8_t irrigation_logic_select_optimal_valve(
    const soil_sensor_data_t* soil_data,
    uint8_t last_used_valve
);

/**
 * @brief Verificar si está en ventana de riego nocturno
 *
 * @param current_hour Hora actual (0-23)
 * @return true si está en ventana nocturna (00:00-06:00)
 */
bool irrigation_logic_is_night_window(uint8_t current_hour);

/**
 * @brief Verificar si está en ventana de estrés térmico
 *
 * @param current_hour Hora actual (0-23)
 * @return true si está en ventana de estrés térmico (12:00-15:00)
 */
bool irrigation_logic_is_thermal_stress_window(uint8_t current_hour);

/**
 * @brief Verificar si las condiciones permiten riego nocturno
 *
 * Valida temperatura (<32°C) y humedad ambiente (>60%).
 *
 * @param ambient_data Datos ambientales actuales
 * @return true si las condiciones permiten riego nocturno
 */
bool irrigation_logic_conditions_allow_night_irrigation(
    const ambient_sensor_data_t* ambient_data
);

/**
 * @brief Verificar si las condiciones requieren riego por estrés térmico
 *
 * Valida temperatura (>32°C), humedad ambiente (<55%) y suelo crítico.
 *
 * @param ambient_data Datos ambientales
 * @param soil_data Datos de suelo
 * @return true si requiere riego por estrés térmico
 */
bool irrigation_logic_conditions_require_thermal_stress(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data
);

/**
 * @brief Calcular nivel de confianza de una decisión
 *
 * Basado en calidad de datos de sensores, consistencia de lecturas
 * y certeza de las condiciones evaluadas.
 *
 * @param ambient_data Datos ambientales
 * @param soil_data Datos de suelo
 * @param decision_type Tipo de decisión tomada
 * @return Nivel de confianza (0.0 = baja, 1.0 = alta)
 */
float irrigation_logic_calculate_confidence_level(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    irrigation_decision_type_t decision_type
);

/**
 * @brief Generar explicación detallada de una decisión
 *
 * Crea un texto explicativo de por qué se tomó una decisión específica.
 *
 * @param decision Decisión tomada
 * @param ambient_data Datos ambientales usados
 * @param soil_data Datos de suelo usados
 * @param explanation_buffer Buffer donde escribir la explicación
 * @param buffer_size Tamaño del buffer
 */
void irrigation_logic_generate_explanation(
    const irrigation_decision_t* decision,
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    char* explanation_buffer,
    size_t buffer_size
);

/**
 * @brief Obtener configuración actual de riego nocturno
 *
 * @param config Puntero donde escribir la configuración
 */
void irrigation_logic_get_night_irrigation_config(night_irrigation_config_t* config);

/**
 * @brief Obtener configuración actual de estrés térmico
 *
 * @param config Puntero donde escribir la configuración
 */
void irrigation_logic_get_thermal_stress_config(thermal_stress_config_t* config);

/**
 * @brief Actualizar configuración de riego nocturno
 *
 * @param config Nueva configuración
 * @return ESP_OK en éxito
 */
esp_err_t irrigation_logic_set_night_irrigation_config(const night_irrigation_config_t* config);

/**
 * @brief Actualizar configuración de estrés térmico
 *
 * @param config Nueva configuración
 * @return ESP_OK en éxito
 */
esp_err_t irrigation_logic_set_thermal_stress_config(const thermal_stress_config_t* config);

/**
 * @brief Convertir tipo de decisión a string
 *
 * @param decision_type Tipo de decisión
 * @return String correspondiente
 */
const char* irrigation_logic_decision_type_to_string(irrigation_decision_type_t decision_type);

/**
 * @brief Convertir razón de decisión a string
 *
 * @param reason Razón de la decisión
 * @return String correspondiente
 */
const char* irrigation_logic_decision_reason_to_string(irrigation_decision_reason_t reason);

/**
 * @brief Obtener timestamp actual del sistema
 *
 * @return Timestamp actual en segundos desde epoch Unix
 */
uint32_t irrigation_logic_get_current_timestamp(void);

#endif // DOMAIN_SERVICES_IRRIGATION_LOGIC_H