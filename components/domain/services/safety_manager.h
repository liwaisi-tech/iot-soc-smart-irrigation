#ifndef DOMAIN_SERVICES_SAFETY_MANAGER_H
#define DOMAIN_SERVICES_SAFETY_MANAGER_H

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
 * @file safety_manager.h
 * @brief Servicio de dominio para gestión de seguridad crítica
 *
 * Implementa todas las validaciones y protecciones de seguridad para
 * prevenir sobre-irrigación, daño a cultivos y mal funcionamiento del equipo.
 * Este servicio tiene la autoridad final para detener operaciones peligrosas.
 *
 * Especificaciones Técnicas - Safety Manager Service:
 * - Protección multi-nivel: preventiva, reactiva y de emergencia
 * - Validación crítica: humedad suelo >80% = parada inmediata
 * - Límites temporales: 30min max por sesión, 4h cooldown
 * - Protección térmica: >40°C parada automática
 * - Zero tolerance: Seguridad sobre eficiencia siempre
 */

/**
 * @brief Tipos de checks de seguridad
 */
typedef enum {
    SAFETY_CHECK_OK = 0,                      // Todo está seguro
    SAFETY_CHECK_WARNING,                     // Advertencia - monitorear
    SAFETY_CHECK_CRITICAL,                    // Crítico - acción requerida
    SAFETY_CHECK_EMERGENCY,                   // Emergencia - parada inmediata
    SAFETY_CHECK_BLOCKED,                     // Operación bloqueada por seguridad
    SAFETY_CHECK_MAX
} safety_check_result_t;

/**
 * @brief Tipos específicos de violaciones de seguridad
 */
typedef enum {
    SAFETY_VIOLATION_NONE = 0,
    SAFETY_VIOLATION_SOIL_OVER_IRRIGATION,    // Humedad suelo > 80%
    SAFETY_VIOLATION_TEMPERATURE_HIGH,        // Temperatura > 40°C
    SAFETY_VIOLATION_DURATION_EXCEEDED,       // Duración > 30 min
    SAFETY_VIOLATION_COOLDOWN_VIOLATED,       // Menos de 4h entre sesiones
    SAFETY_VIOLATION_SENSOR_FAILURE,          // Fallo crítico de sensores
    SAFETY_VIOLATION_VALVE_MALFUNCTION,       // Mal funcionamiento válvula
    SAFETY_VIOLATION_MULTIPLE_VIOLATIONS,     // Múltiples violaciones simultáneas
    SAFETY_VIOLATION_SYSTEM_OVERLOAD,         // Sistema sobrecargado
    SAFETY_VIOLATION_MAX
} safety_violation_type_t;

/**
 * @brief Resultado detallado de evaluación de seguridad
 */
typedef struct {
    safety_check_result_t overall_result;    // Resultado general
    safety_violation_type_t violation_type;  // Tipo específico de violación
    float severity_level;                    // Nivel de severidad (0.0-1.0)
    char violation_description[128];         // Descripción detallada
    char recommended_action[64];             // Acción recomendada
    uint32_t evaluation_timestamp;          // Cuándo se hizo la evaluación
    bool immediate_action_required;          // Si requiere acción inmediata
} safety_evaluation_result_t;

/**
 * @brief Contexto para evaluación de seguridad
 */
typedef struct {
    uint32_t current_session_start_time;     // Inicio sesión actual (si activa)
    uint32_t last_irrigation_end_time;       // Fin última sesión
    uint8_t active_valve_count;              // Número de válvulas activas
    uint32_t emergency_stop_count_today;     // Paradas de emergencia hoy
    bool manual_override_active;             // Si hay override manual
    power_mode_t current_power_mode;         // Modo de energía actual
} safety_evaluation_context_t;

/**
 * @brief Configuración de límites críticos de seguridad
 */
typedef struct {
    // Límites de sobre-irrigación (CRÍTICO)
    float soil_emergency_threshold_percent;  // 80.0% - Parada inmediata
    float soil_warning_threshold_percent;    // 75.0% - Advertencia

    // Límites térmicos
    float temperature_emergency_celsius;     // 40.0°C - Parada inmediata
    float temperature_warning_celsius;       // 35.0°C - Advertencia

    // Límites temporales
    uint16_t max_session_duration_minutes;   // 30 - Duración máxima por sesión
    uint16_t min_cooldown_duration_minutes;  // 240 - Cooldown mínimo (4h)

    // Límites de fallo de sensores
    uint8_t min_working_soil_sensors;        // 2 - Mínimo sensores funcionales
    uint16_t sensor_response_timeout_ms;     // 5000 - Timeout respuesta sensor

    // Límites de emergencias
    uint8_t max_emergency_stops_per_day;     // 10 - Máximo emergencias por día
    uint16_t emergency_cooldown_minutes;     // 60 - Cooldown post-emergencia
} safety_critical_limits_t;

/**
 * @brief Estadísticas de seguridad del sistema
 */
typedef struct {
    uint32_t total_safety_checks_performed;  // Total de checks realizados
    uint32_t warnings_issued_today;          // Advertencias emitidas hoy
    uint32_t critical_alerts_today;          // Alertas críticas hoy
    uint32_t emergency_stops_today;          // Paradas de emergencia hoy
    uint32_t operations_blocked_today;       // Operaciones bloqueadas hoy
    uint32_t last_emergency_timestamp;       // Última emergencia
    char last_emergency_reason[64];          // Razón última emergencia
    float system_safety_score;               // Puntuación seguridad (0.0-1.0)
} safety_statistics_t;

/**
 * @brief Inicializar servicio de gestión de seguridad
 *
 * @param safety_limits Límites de seguridad del sistema
 * @return ESP_OK en éxito
 */
esp_err_t safety_manager_init(const safety_limits_t* safety_limits);

/**
 * @brief Evaluar seguridad antes de iniciar riego
 *
 * Realiza validación completa de seguridad antes de permitir
 * el inicio de cualquier operación de riego.
 *
 * @param command Comando de riego a validar
 * @param current_status Estado actual del sistema
 * @param ambient_data Datos ambientales actuales
 * @param soil_data Datos de suelo actuales
 * @param context Contexto de la evaluación
 * @return Resultado detallado de la evaluación
 */
safety_evaluation_result_t safety_manager_evaluate_start_irrigation(
    const irrigation_command_t* command,
    const irrigation_status_t* current_status,
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    const safety_evaluation_context_t* context
);

/**
 * @brief Evaluar seguridad durante riego activo
 *
 * Monitoreo continuo de condiciones de seguridad durante
 * operaciones de riego activas.
 *
 * @param current_status Estado actual del sistema
 * @param ambient_data Datos ambientales actuales
 * @param soil_data Datos de suelo actuales
 * @param context Contexto de la evaluación
 * @return Resultado de evaluación continua
 */
safety_evaluation_result_t safety_manager_evaluate_during_irrigation(
    const irrigation_status_t* current_status,
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    const safety_evaluation_context_t* context
);

/**
 * @brief Verificar si hay condición de emergencia CRÍTICA
 *
 * Check más rápido y específico para condiciones que requieren
 * parada inmediata sin análisis adicional.
 *
 * @param ambient_data Datos ambientales
 * @param soil_data Datos de suelo
 * @return true si hay condición de emergencia inmediata
 */
bool safety_manager_has_emergency_condition(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data
);

/**
 * @brief Verificar protección contra sobre-irrigación
 *
 * CRÍTICO: Verifica si CUALQUIER sensor de suelo supera el 80%
 * de humedad, lo que requiere parada inmediata.
 *
 * @param soil_data Datos de sensores de suelo
 * @return true si hay riesgo de sobre-irrigación
 */
bool safety_manager_check_over_irrigation_protection(const soil_sensor_data_t* soil_data);

/**
 * @brief Verificar protección térmica
 *
 * @param ambient_data Datos ambientales
 * @return true si la temperatura requiere parada
 */
bool safety_manager_check_thermal_protection(const ambient_sensor_data_t* ambient_data);

/**
 * @brief Verificar límites de duración
 *
 * @param current_status Estado actual con tiempos de válvulas
 * @return true si alguna válvula ha excedido el tiempo límite
 */
bool safety_manager_check_duration_limits(const irrigation_status_t* current_status);

/**
 * @brief Verificar período de cooldown
 *
 * Valida que haya pasado suficiente tiempo desde la última
 * sesión de riego (4 horas por defecto).
 *
 * @param context Contexto con tiempos de última irrigación
 * @param valve_number Válvula a verificar
 * @return true si está en período de cooldown
 */
bool safety_manager_check_cooldown_period(
    const safety_evaluation_context_t* context,
    uint8_t valve_number
);

/**
 * @brief Verificar estado de sensores críticos
 *
 * Valida que hay suficientes sensores funcionales para
 * tomar decisiones de riego seguras.
 *
 * @param soil_data Datos de sensores de suelo
 * @param ambient_data Datos ambientales
 * @return true si los sensores críticos están operativos
 */
bool safety_manager_check_sensor_health(
    const soil_sensor_data_t* soil_data,
    const ambient_sensor_data_t* ambient_data
);

/**
 * @brief Verificar límites diarios de emergencias
 *
 * @param context Contexto con contadores de emergencias
 * @return true si se han excedido los límites diarios
 */
bool safety_manager_check_daily_emergency_limits(const safety_evaluation_context_t* context);

/**
 * @brief Calcular nivel de severidad de una violación
 *
 * @param violation_type Tipo de violación detectada
 * @param ambient_data Datos ambientales
 * @param soil_data Datos de suelo
 * @return Nivel de severidad (0.0 = leve, 1.0 = crítico)
 */
float safety_manager_calculate_violation_severity(
    safety_violation_type_t violation_type,
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data
);

/**
 * @brief Generar descripción detallada de violación
 *
 * @param violation_type Tipo de violación
 * @param soil_data Datos de suelo (para detalles específicos)
 * @param description_buffer Buffer donde escribir descripción
 * @param buffer_size Tamaño del buffer
 */
void safety_manager_generate_violation_description(
    safety_violation_type_t violation_type,
    const soil_sensor_data_t* soil_data,
    char* description_buffer,
    size_t buffer_size
);

/**
 * @brief Generar acción recomendada para violación
 *
 * @param violation_type Tipo de violación
 * @param severity_level Nivel de severidad
 * @param action_buffer Buffer donde escribir acción
 * @param buffer_size Tamaño del buffer
 */
void safety_manager_generate_recommended_action(
    safety_violation_type_t violation_type,
    float severity_level,
    char* action_buffer,
    size_t buffer_size
);

/**
 * @brief Registrar evento de seguridad
 *
 * Almacena evento de seguridad para estadísticas y auditoría.
 *
 * @param result Resultado de evaluación de seguridad
 */
void safety_manager_log_safety_event(const safety_evaluation_result_t* result);

/**
 * @brief Incrementar contador de emergencias
 *
 * @param reason Razón de la emergencia
 */
void safety_manager_record_emergency_stop(const char* reason);

/**
 * @brief Calcular puntuación de seguridad del sistema
 *
 * Basada en historial reciente de violaciones, emergencias y
 * tiempo de operación sin incidentes.
 *
 * @return Puntuación de seguridad (0.0 = peligroso, 1.0 = muy seguro)
 */
float safety_manager_calculate_system_safety_score(void);

/**
 * @brief Obtener estadísticas de seguridad
 *
 * @param stats Puntero donde escribir las estadísticas
 */
void safety_manager_get_statistics(safety_statistics_t* stats);

/**
 * @brief Resetear estadísticas diarias
 *
 * Llamar a medianoche para limpiar contadores diarios.
 */
void safety_manager_reset_daily_statistics(void);

/**
 * @brief Obtener límites críticos actuales
 *
 * @param limits Puntero donde escribir los límites
 */
void safety_manager_get_critical_limits(safety_critical_limits_t* limits);

/**
 * @brief Actualizar límites críticos
 *
 * @param limits Nuevos límites críticos
 * @return ESP_OK en éxito
 */
esp_err_t safety_manager_set_critical_limits(const safety_critical_limits_t* limits);

/**
 * @brief Activar/desactivar modo de override de seguridad
 *
 * ADVERTENCIA: Solo usar en casos de mantenimiento/emergencia.
 * El override no afecta protecciones críticas (>80% suelo, >40°C).
 *
 * @param override_active true para activar override
 * @param reason Razón del override
 * @return ESP_OK en éxito
 */
esp_err_t safety_manager_set_override_mode(bool override_active, const char* reason);

/**
 * @brief Verificar si el override de seguridad está activo
 *
 * @return true si el override está activo
 */
bool safety_manager_is_override_active(void);

/**
 * @brief Convertir resultado de check a string
 *
 * @param result Resultado del check
 * @return String correspondiente
 */
const char* safety_manager_check_result_to_string(safety_check_result_t result);

/**
 * @brief Convertir tipo de violación a string
 *
 * @param violation_type Tipo de violación
 * @return String correspondiente
 */
const char* safety_manager_violation_type_to_string(safety_violation_type_t violation_type);

/**
 * @brief Obtener timestamp actual del sistema
 *
 * @return Timestamp actual en segundos desde epoch Unix
 */
uint32_t safety_manager_get_current_timestamp(void);

#endif // DOMAIN_SERVICES_SAFETY_MANAGER_H