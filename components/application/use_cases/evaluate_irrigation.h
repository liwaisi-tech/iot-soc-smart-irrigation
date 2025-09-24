#ifndef APPLICATION_USE_CASES_EVALUATE_IRRIGATION_H
#define APPLICATION_USE_CASES_EVALUATE_IRRIGATION_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "ambient_sensor_data.h"
#include "soil_sensor_data.h"
#include "irrigation_status.h"
#include "irrigation_command.h"
#include "safety_limits.h"
#include "system_mode.h"

/**
 * @file evaluate_irrigation.h
 * @brief Use Case para evaluación inteligente de necesidades de riego
 *
 * Orquesta todos los domain services para tomar decisiones inteligentes
 * de riego basadas en sensores, condiciones ambientales, seguridad y
 * contexto operacional del sistema.
 *
 * Especificaciones Técnicas - Evaluate Irrigation Use Case:
 * - Evaluación multi-criterio: suelo + ambiente + tiempo + energía
 * - Algoritmos Colombia-optimizados: Dataset real rural Colombian
 * - Safety-first: Validación seguridad antes de cualquier decisión
 * - Context-aware: Considera modo offline, horario, batería
 * - Confidence scoring: Nivel de confianza en cada decisión
 */

/**
 * @brief Tipos de evaluación de riego
 */
typedef enum {
    IRRIGATION_EVALUATION_AUTOMATIC = 0,    // Evaluación automática periódica
    IRRIGATION_EVALUATION_MANUAL,           // Evaluación manual solicitada
    IRRIGATION_EVALUATION_EMERGENCY,        // Evaluación de emergencia
    IRRIGATION_EVALUATION_SCHEDULED,        // Evaluación programada
    IRRIGATION_EVALUATION_SENSOR_TRIGGER,   // Triggered por cambio en sensores
    IRRIGATION_EVALUATION_MAX
} irrigation_evaluation_type_t;

/**
 * @brief Decisiones de evaluación de riego
 */
typedef enum {
    IRRIGATION_EVALUATION_DECISION_NO_ACTION = 0,        // No requiere acción
    IRRIGATION_EVALUATION_DECISION_START_IRRIGATION,     // Iniciar riego
    IRRIGATION_EVALUATION_DECISION_STOP_IRRIGATION,      // Detener riego
    IRRIGATION_EVALUATION_DECISION_EMERGENCY_STOP,       // Parada de emergencia
    IRRIGATION_EVALUATION_DECISION_WAIT_CONDITIONS,      // Esperar mejores condiciones
    IRRIGATION_EVALUATION_DECISION_REDUCE_DURATION,      // Reducir duración actual
    IRRIGATION_EVALUATION_DECISION_EXTEND_DURATION,      // Extender duración actual
    IRRIGATION_EVALUATION_DECISION_MAX
} irrigation_evaluation_decision_t;

/**
 * @brief Factores considerados en la evaluación
 */
typedef enum {
    IRRIGATION_FACTOR_SOIL_MOISTURE = 0,      // Humedad del suelo
    IRRIGATION_FACTOR_AMBIENT_CONDITIONS,     // Condiciones ambientales
    IRRIGATION_FACTOR_TIME_WINDOW,            // Ventana temporal (nocturno/diurno)
    IRRIGATION_FACTOR_THERMAL_STRESS,         // Estrés térmico
    IRRIGATION_FACTOR_POWER_MODE,             // Modo de energía
    IRRIGATION_FACTOR_CONNECTIVITY,           // Estado de conectividad
    IRRIGATION_FACTOR_SAFETY_LIMITS,          // Límites de seguridad
    IRRIGATION_FACTOR_IRRIGATION_HISTORY,     // Historial de riego
    IRRIGATION_FACTOR_MAX
} irrigation_evaluation_factor_t;

/**
 * @brief Contexto de evaluación de riego
 */
typedef struct {
    irrigation_evaluation_type_t evaluation_type; // Tipo de evaluación
    uint32_t evaluation_timestamp;             // Cuándo se realizó
    uint8_t current_hour;                      // Hora actual (0-23)
    uint8_t current_minute;                    // Minuto actual (0-59)
    bool is_offline_mode;                      // Si está en modo offline
    offline_mode_t offline_mode_level;         // Nivel de modo offline
    power_mode_t power_mode;                   // Modo de energía
    uint32_t time_since_last_irrigation_s;     // Tiempo desde último riego
    uint32_t irrigation_sessions_today;        // Sesiones de riego hoy
    bool manual_override_active;               // Si hay override manual
} irrigation_evaluation_context_t;

/**
 * @brief Análisis por factor de evaluación
 */
typedef struct {
    irrigation_evaluation_factor_t factor;     // Factor evaluado
    float weight;                              // Peso del factor (0.0-1.0)
    float score;                               // Puntuación del factor (0.0-1.0)
    bool is_critical;                          // Si es crítico para la decisión
    bool is_blocking;                          // Si bloquea la acción
    char analysis_description[64];             // Descripción del análisis
} irrigation_factor_analysis_t;

/**
 * @brief Resultado completo de evaluación
 */
typedef struct {
    irrigation_evaluation_decision_t decision;  // Decisión final
    uint8_t recommended_valve;                 // Válvula recomendada (1-3)
    uint16_t recommended_duration_minutes;     // Duración recomendada
    float confidence_level;                    // Nivel de confianza (0.0-1.0)
    float overall_urgency;                     // Urgencia general (0.0-1.0)

    // Análisis detallado
    irrigation_factor_analysis_t factor_analysis[IRRIGATION_FACTOR_MAX];
    uint8_t factors_analyzed;                  // Número de factores analizados

    // Contexto y timing
    irrigation_evaluation_context_t context;   // Contexto de evaluación
    uint32_t processing_time_ms;               // Tiempo de procesamiento

    // Explicación y logging
    char decision_explanation[256];            // Explicación de la decisión
    char safety_concerns[128];                 // Preocupaciones de seguridad
    char recommendations[128];                 // Recomendaciones adicionales
} irrigation_evaluation_result_t;

/**
 * @brief Configuración de evaluación
 */
typedef struct {
    // Pesos de factores (deben sumar 1.0)
    float soil_moisture_weight;                // 0.35 - Peso humedad suelo
    float ambient_conditions_weight;           // 0.20 - Peso condiciones ambientales
    float time_window_weight;                  // 0.15 - Peso ventana temporal
    float thermal_stress_weight;               // 0.15 - Peso estrés térmico
    float power_energy_weight;                 // 0.10 - Peso modo energía
    float safety_weight;                       // 0.05 - Peso seguridad (crítico)

    // Umbrales de confianza
    float minimum_confidence_threshold;        // 0.70 - Mínima confianza para acción
    float high_confidence_threshold;           // 0.85 - Alta confianza

    // Configuraciones de contexto
    bool enable_night_irrigation;              // Permitir riego nocturno
    bool enable_thermal_stress_irrigation;     // Permitir riego por estrés térmico
    bool consider_power_optimization;          // Considerar optimización de energía
} irrigation_evaluation_config_t;

/**
 * @brief Estadísticas de evaluaciones
 */
typedef struct {
    uint32_t total_evaluations;                // Total evaluaciones realizadas
    uint32_t decisions_no_action;              // Decisiones de no acción
    uint32_t decisions_start_irrigation;       // Decisiones de iniciar riego
    uint32_t decisions_stop_irrigation;        // Decisiones de parar riego
    uint32_t emergency_stops;                  // Paradas de emergencia
    float average_confidence_level;            // Nivel promedio de confianza
    float average_processing_time_ms;          // Tiempo promedio de procesamiento
    uint32_t safety_blocks_total;              // Total bloqueados por seguridad
} irrigation_evaluation_statistics_t;

/**
 * @brief Inicializar use case de evaluación de riego
 *
 * @param safety_limits Límites de seguridad del sistema
 * @return ESP_OK en éxito
 */
esp_err_t evaluate_irrigation_init(const safety_limits_t* safety_limits);

/**
 * @brief Realizar evaluación completa de riego
 *
 * Función principal que orquesta todos los domain services para
 * tomar una decisión inteligente de riego.
 *
 * @param ambient_data Datos de sensores ambientales
 * @param soil_data Datos de sensores de suelo
 * @param current_status Estado actual del sistema
 * @param context Contexto de la evaluación
 * @return Resultado completo de evaluación
 */
irrigation_evaluation_result_t evaluate_irrigation_make_decision(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    const irrigation_status_t* current_status,
    const irrigation_evaluation_context_t* context
);

/**
 * @brief Evaluar factor de humedad del suelo
 *
 * @param soil_data Datos de sensores de suelo
 * @param context Contexto de evaluación
 * @return Análisis del factor
 */
irrigation_factor_analysis_t evaluate_irrigation_analyze_soil_moisture(
    const soil_sensor_data_t* soil_data,
    const irrigation_evaluation_context_t* context
);

/**
 * @brief Evaluar factor de condiciones ambientales
 *
 * @param ambient_data Datos ambientales
 * @param context Contexto de evaluación
 * @return Análisis del factor
 */
irrigation_factor_analysis_t evaluate_irrigation_analyze_ambient_conditions(
    const ambient_sensor_data_t* ambient_data,
    const irrigation_evaluation_context_t* context
);

/**
 * @brief Evaluar factor de ventana temporal
 *
 * @param context Contexto de evaluación
 * @param ambient_data Datos ambientales para validar condiciones
 * @return Análisis del factor
 */
irrigation_factor_analysis_t evaluate_irrigation_analyze_time_window(
    const irrigation_evaluation_context_t* context,
    const ambient_sensor_data_t* ambient_data
);

/**
 * @brief Evaluar factor de estrés térmico
 *
 * @param ambient_data Datos ambientales
 * @param soil_data Datos de suelo
 * @param context Contexto de evaluación
 * @return Análisis del factor
 */
irrigation_factor_analysis_t evaluate_irrigation_analyze_thermal_stress(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    const irrigation_evaluation_context_t* context
);

/**
 * @brief Evaluar factor de modo de energía
 *
 * @param context Contexto de evaluación
 * @return Análisis del factor
 */
irrigation_factor_analysis_t evaluate_irrigation_analyze_power_mode(
    const irrigation_evaluation_context_t* context
);

/**
 * @brief Evaluar factor de seguridad
 *
 * @param ambient_data Datos ambientales
 * @param soil_data Datos de suelo
 * @param current_status Estado actual del sistema
 * @return Análisis del factor
 */
irrigation_factor_analysis_t evaluate_irrigation_analyze_safety_limits(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    const irrigation_status_t* current_status
);

/**
 * @brief Calcular decisión final basada en análisis de factores
 *
 * @param factor_analyses Array de análisis de factores
 * @param num_factors Número de factores analizados
 * @param context Contexto de evaluación
 * @return Decisión final calculada
 */
irrigation_evaluation_decision_t evaluate_irrigation_calculate_final_decision(
    const irrigation_factor_analysis_t* factor_analyses,
    uint8_t num_factors,
    const irrigation_evaluation_context_t* context
);

/**
 * @brief Calcular nivel de confianza de la decisión
 *
 * @param factor_analyses Array de análisis de factores
 * @param num_factors Número de factores analizados
 * @param decision Decisión tomada
 * @return Nivel de confianza (0.0-1.0)
 */
float evaluate_irrigation_calculate_confidence_level(
    const irrigation_factor_analysis_t* factor_analyses,
    uint8_t num_factors,
    irrigation_evaluation_decision_t decision
);

/**
 * @brief Determinar válvula óptima para riego
 *
 * @param soil_data Datos de sensores de suelo
 * @param current_status Estado actual del sistema
 * @return Número de válvula recomendada (1-3)
 */
uint8_t evaluate_irrigation_determine_optimal_valve(
    const soil_sensor_data_t* soil_data,
    const irrigation_status_t* current_status
);

/**
 * @brief Calcular duración óptima de riego
 *
 * @param soil_data Datos de suelo
 * @param ambient_data Datos ambientales
 * @param context Contexto de evaluación
 * @param decision Decisión tomada
 * @return Duración recomendada en minutos
 */
uint16_t evaluate_irrigation_calculate_optimal_duration(
    const soil_sensor_data_t* soil_data,
    const ambient_sensor_data_t* ambient_data,
    const irrigation_evaluation_context_t* context,
    irrigation_evaluation_decision_t decision
);

/**
 * @brief Generar explicación detallada de la decisión
 *
 * @param result Resultado de evaluación
 * @param explanation_buffer Buffer para explicación
 * @param buffer_size Tamaño del buffer
 */
void evaluate_irrigation_generate_explanation(
    const irrigation_evaluation_result_t* result,
    char* explanation_buffer,
    size_t buffer_size
);

/**
 * @brief Verificar si hay factores bloqueantes
 *
 * @param factor_analyses Array de análisis de factores
 * @param num_factors Número de factores
 * @return true si hay factores que bloquean la acción
 */
bool evaluate_irrigation_has_blocking_factors(
    const irrigation_factor_analysis_t* factor_analyses,
    uint8_t num_factors
);

/**
 * @brief Generar recomendaciones adicionales
 *
 * @param result Resultado de evaluación
 * @param recommendations_buffer Buffer para recomendaciones
 * @param buffer_size Tamaño del buffer
 */
void evaluate_irrigation_generate_recommendations(
    const irrigation_evaluation_result_t* result,
    char* recommendations_buffer,
    size_t buffer_size
);

/**
 * @brief Configurar pesos de factores de evaluación
 *
 * @param config Nueva configuración de evaluación
 * @return ESP_OK en éxito
 */
esp_err_t evaluate_irrigation_set_config(const irrigation_evaluation_config_t* config);

/**
 * @brief Obtener configuración actual de evaluación
 *
 * @param config Puntero donde escribir configuración
 */
void evaluate_irrigation_get_config(irrigation_evaluation_config_t* config);

/**
 * @brief Registrar resultado de evaluación para estadísticas
 *
 * @param result Resultado de evaluación completada
 */
void evaluate_irrigation_record_evaluation(const irrigation_evaluation_result_t* result);

/**
 * @brief Obtener estadísticas de evaluaciones
 *
 * @param stats Puntero donde escribir estadísticas
 */
void evaluate_irrigation_get_statistics(irrigation_evaluation_statistics_t* stats);

/**
 * @brief Resetear estadísticas de evaluación
 */
void evaluate_irrigation_reset_statistics(void);

/**
 * @brief Convertir decisión de evaluación a string
 *
 * @param decision Decisión de evaluación
 * @return String correspondiente
 */
const char* evaluate_irrigation_decision_to_string(irrigation_evaluation_decision_t decision);

/**
 * @brief Convertir factor de evaluación a string
 *
 * @param factor Factor de evaluación
 * @return String correspondiente
 */
const char* evaluate_irrigation_factor_to_string(irrigation_evaluation_factor_t factor);

/**
 * @brief Obtener timestamp actual del sistema
 *
 * @return Timestamp actual en segundos desde epoch Unix
 */
uint32_t evaluate_irrigation_get_current_timestamp(void);

#endif // APPLICATION_USE_CASES_EVALUATE_IRRIGATION_H