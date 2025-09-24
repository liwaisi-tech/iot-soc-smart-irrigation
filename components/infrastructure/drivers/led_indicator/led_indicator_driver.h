#ifndef INFRASTRUCTURE_DRIVERS_LED_INDICATOR_DRIVER_H
#define INFRASTRUCTURE_DRIVERS_LED_INDICATOR_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "system_mode.h"

/**
 * @file led_indicator_driver.h
 * @brief Driver para indicador LED de estado del sistema
 *
 * Proporciona indicación visual del estado del sistema de riego,
 * conectividad, modo offline y alertas de seguridad mediante
 * patrones de parpadeo y colores LED.
 *
 * Especificaciones Técnicas - LED Indicator Driver:
 * - Hardware: LED RGB o 3 LEDs individuales (R/G/B)
 * - Patrones: 8 patrones de parpadeo diferentes
 * - Colores: Rojo (error), Verde (OK), Azul (offline), Amarillo (advertencia)
 * - Power-optimized: Reduce brillo automáticamente en modo batería
 * - Non-blocking: Operación basada en timers, no bloquea tasks
 */

/**
 * @brief Configuración de pines GPIO para LED
 */
#define LED_RED_GPIO_PIN           GPIO_NUM_16   // LED Rojo
#define LED_GREEN_GPIO_PIN         GPIO_NUM_17   // LED Verde
#define LED_BLUE_GPIO_PIN          GPIO_NUM_18   // LED Azul
#define LED_STATUS_GPIO_PIN        GPIO_NUM_2    // LED Estado (si solo hay uno)

/**
 * @brief Colores de LED soportados
 */
typedef enum {
    LED_COLOR_OFF = 0,                        // LED apagado
    LED_COLOR_RED,                            // Rojo - Error/Emergencia
    LED_COLOR_GREEN,                          // Verde - OK/Normal
    LED_COLOR_BLUE,                           // Azul - Offline/Información
    LED_COLOR_YELLOW,                         // Amarillo - Advertencia
    LED_COLOR_PURPLE,                         // Púrpura - Calibrando
    LED_COLOR_CYAN,                           // Cian - Configurando
    LED_COLOR_WHITE,                          // Blanco - Test/Inicializando
    LED_COLOR_MAX
} led_color_t;

/**
 * @brief Patrones de parpadeo
 */
typedef enum {
    LED_PATTERN_SOLID = 0,                    // Encendido constante
    LED_PATTERN_SLOW_BLINK,                   // Parpadeo lento (1 Hz)
    LED_PATTERN_FAST_BLINK,                   // Parpadeo rápido (4 Hz)
    LED_PATTERN_PULSE,                        // Pulso suave (fade in/out)
    LED_PATTERN_DOUBLE_BLINK,                 // Doble parpadeo cada 2s
    LED_PATTERN_TRIPLE_BLINK,                 // Triple parpadeo cada 3s
    LED_PATTERN_MORSE_SOS,                    // Patrón SOS (emergencia)
    LED_PATTERN_HEARTBEAT,                    // Patrón latido corazón
    LED_PATTERN_MAX
} led_pattern_t;

/**
 * @brief Estados del sistema representados por LED
 */
typedef enum {
    LED_SYSTEM_STATE_INITIALIZING = 0,        // Sistema inicializando
    LED_SYSTEM_STATE_WIFI_CONNECTING,         // Conectando WiFi
    LED_SYSTEM_STATE_MQTT_CONNECTING,         // Conectando MQTT
    LED_SYSTEM_STATE_ONLINE_NORMAL,           // Online normal
    LED_SYSTEM_STATE_IRRIGATION_ACTIVE,       // Riego activo
    LED_SYSTEM_STATE_OFFLINE_NORMAL,          // Offline normal
    LED_SYSTEM_STATE_OFFLINE_WARNING,         // Offline advertencia
    LED_SYSTEM_STATE_OFFLINE_CRITICAL,        // Offline crítico
    LED_SYSTEM_STATE_OFFLINE_EMERGENCY,       // Offline emergencia
    LED_SYSTEM_STATE_SENSOR_ERROR,            // Error sensores
    LED_SYSTEM_STATE_SAFETY_ALERT,            // Alerta seguridad
    LED_SYSTEM_STATE_EMERGENCY_STOP,          // Parada emergencia
    LED_SYSTEM_STATE_LOW_BATTERY,             // Batería baja
    LED_SYSTEM_STATE_CALIBRATING,             // Calibrando sensores
    LED_SYSTEM_STATE_MAINTENANCE,             // Modo mantenimiento
    LED_SYSTEM_STATE_MAX
} led_system_state_t;

/**
 * @brief Configuración del LED
 */
typedef struct {
    led_color_t color;                        // Color del LED
    led_pattern_t pattern;                    // Patrón de parpadeo
    uint8_t brightness_percent;               // Brillo (0-100%)
    uint16_t pattern_duration_ms;             // Duración del patrón
    bool auto_brightness;                     // Ajuste automático brillo
} led_configuration_t;

/**
 * @brief Mapeo de estado a configuración LED
 */
typedef struct {
    led_system_state_t system_state;          // Estado del sistema
    led_configuration_t led_config;           // Configuración LED correspondiente
    uint8_t priority;                         // Prioridad (0=máxima)
    bool requires_attention;                  // Si requiere atención inmediata
} led_state_mapping_t;

/**
 * @brief Configuración del driver LED
 */
typedef struct {
    // Configuración de hardware
    bool use_rgb_led;                         // true=LED RGB, false=LEDs separados
    bool invert_logic;                        // true=lógica invertida (cátodo común)
    uint8_t default_brightness_percent;       // 80 - Brillo por defecto

    // Configuración de energía
    bool enable_power_saving;                 // Reducir brillo en modo batería
    uint8_t battery_save_brightness_percent;  // 30 - Brillo modo batería
    bool disable_led_low_battery;             // Apagar LED si batería <10%

    // Configuración de patrones
    uint16_t slow_blink_period_ms;            // 1000ms - Período parpadeo lento
    uint16_t fast_blink_period_ms;            // 250ms - Período parpadeo rápido
    uint16_t pulse_period_ms;                 // 2000ms - Período pulso
    uint16_t pattern_transition_time_ms;      // 100ms - Tiempo transición

    // Configuración de prioridad
    bool enable_priority_override;            // Permitir override por prioridad
    uint16_t priority_display_time_ms;        // 5000ms - Tiempo display prioridad
} led_driver_config_t;

/**
 * @brief Estadísticas del LED
 */
typedef struct {
    uint32_t total_state_changes;             // Total cambios de estado
    uint32_t pattern_cycles_completed;        // Ciclos de patrón completados
    uint32_t emergency_indications;           // Indicaciones de emergencia
    uint32_t power_save_activations;          // Activaciones ahorro energía
    uint32_t total_on_time_ms;                // Tiempo total encendido
    float average_brightness_used;            // Brillo promedio utilizado
} led_statistics_t;

/**
 * @brief Inicializar driver de indicador LED
 *
 * @param config Configuración del driver
 * @return ESP_OK en éxito, ESP_ERR_* en error
 */
esp_err_t led_indicator_driver_init(const led_driver_config_t* config);

/**
 * @brief Desinicializar driver LED
 *
 * Apaga LEDs y libera recursos.
 *
 * @return ESP_OK en éxito
 */
esp_err_t led_indicator_driver_deinit(void);

/**
 * @brief Establecer estado del sistema para indicación LED
 *
 * @param system_state Estado del sistema a indicar
 * @return ESP_OK en éxito
 */
esp_err_t led_indicator_driver_set_system_state(led_system_state_t system_state);

/**
 * @brief Establecer configuración LED personalizada
 *
 * @param config Configuración LED personalizada
 * @param duration_ms Duración de la indicación (0=permanente)
 * @return ESP_OK en éxito
 */
esp_err_t led_indicator_driver_set_custom_config(
    const led_configuration_t* config,
    uint16_t duration_ms
);

/**
 * @brief Mostrar color sólido específico
 *
 * @param color Color a mostrar
 * @param brightness_percent Brillo (0-100%)
 * @return ESP_OK en éxito
 */
esp_err_t led_indicator_driver_show_color(led_color_t color, uint8_t brightness_percent);

/**
 * @brief Mostrar patrón específico
 *
 * @param color Color del patrón
 * @param pattern Patrón a mostrar
 * @param duration_ms Duración del patrón (0=permanente)
 * @return ESP_OK en éxito
 */
esp_err_t led_indicator_driver_show_pattern(
    led_color_t color,
    led_pattern_t pattern,
    uint16_t duration_ms
);

/**
 * @brief Apagar LED
 *
 * @return ESP_OK en éxito
 */
esp_err_t led_indicator_driver_turn_off(void);

/**
 * @brief Indicar modo offline con nivel específico
 *
 * Muestra patrón diferente según el nivel de criticidad offline.
 *
 * @param offline_mode Nivel de modo offline
 * @return ESP_OK en éxito
 */
esp_err_t led_indicator_driver_indicate_offline_mode(offline_mode_t offline_mode);

/**
 * @brief Indicar riego activo
 *
 * @param valve_number Válvula activa (1-3, 0=múltiples)
 * @return ESP_OK en éxito
 */
esp_err_t led_indicator_driver_indicate_irrigation_active(uint8_t valve_number);

/**
 * @brief Indicar emergencia o alerta crítica
 *
 * Patrón de máxima prioridad que override otros estados.
 *
 * @param emergency_type Tipo de emergencia
 * @return ESP_OK en éxito
 */
esp_err_t led_indicator_driver_indicate_emergency(const char* emergency_type);

/**
 * @brief Indicar progreso de operación
 *
 * Muestra progreso de 0-100% mediante intensidad o parpadeo.
 *
 * @param progress_percent Progreso (0-100%)
 * @param operation_color Color de la operación
 * @return ESP_OK en éxito
 */
esp_err_t led_indicator_driver_indicate_progress(
    uint8_t progress_percent,
    led_color_t operation_color
);

/**
 * @brief Ejecutar secuencia de test
 *
 * Prueba todos los colores y patrones disponibles.
 *
 * @param test_duration_ms Duración total del test
 * @return ESP_OK en éxito
 */
esp_err_t led_indicator_driver_run_test_sequence(uint16_t test_duration_ms);

/**
 * @brief Establecer brillo global
 *
 * @param brightness_percent Nuevo brillo global (0-100%)
 * @return ESP_OK en éxito
 */
esp_err_t led_indicator_driver_set_brightness(uint8_t brightness_percent);

/**
 * @brief Habilitar/deshabilitar modo ahorro energético
 *
 * @param enable true para habilitar ahorro energético
 * @return ESP_OK en éxito
 */
esp_err_t led_indicator_driver_set_power_save_mode(bool enable);

/**
 * @brief Obtener estado actual del LED
 *
 * @param current_config Puntero donde escribir configuración actual
 * @return ESP_OK en éxito
 */
esp_err_t led_indicator_driver_get_current_config(led_configuration_t* current_config);

/**
 * @brief Verificar si LED está encendido
 *
 * @return true si LED está encendido
 */
bool led_indicator_driver_is_led_on(void);

/**
 * @brief Pausar indicación actual
 *
 * @return ESP_OK en éxito
 */
esp_err_t led_indicator_driver_pause(void);

/**
 * @brief Reanudar indicación pausada
 *
 * @return ESP_OK en éxito
 */
esp_err_t led_indicator_driver_resume(void);

/**
 * @brief Obtener configuración actual del driver
 *
 * @param config Puntero donde escribir configuración
 */
void led_indicator_driver_get_config(led_driver_config_t* config);

/**
 * @brief Actualizar configuración del driver
 *
 * @param config Nueva configuración
 * @return ESP_OK en éxito
 */
esp_err_t led_indicator_driver_set_config(const led_driver_config_t* config);

/**
 * @brief Obtener estadísticas del LED
 *
 * @param stats Puntero donde escribir estadísticas
 */
void led_indicator_driver_get_statistics(led_statistics_t* stats);

/**
 * @brief Resetear estadísticas
 */
void led_indicator_driver_reset_statistics(void);

/**
 * @brief Callback para eventos de estado del sistema
 *
 * @param old_state Estado anterior
 * @param new_state Nuevo estado
 * @param context Contexto adicional del cambio
 */
typedef void (*led_state_change_callback_t)(
    led_system_state_t old_state,
    led_system_state_t new_state,
    void* context
);

/**
 * @brief Registrar callback para cambios de estado
 *
 * @param callback Función callback a registrar
 * @return ESP_OK en éxito
 */
esp_err_t led_indicator_driver_register_callback(led_state_change_callback_t callback);

/**
 * @brief Convertir color de LED a string
 *
 * @param color Color de LED
 * @return String correspondiente al color
 */
const char* led_indicator_driver_color_to_string(led_color_t color);

/**
 * @brief Convertir patrón de LED a string
 *
 * @param pattern Patrón de LED
 * @return String correspondiente al patrón
 */
const char* led_indicator_driver_pattern_to_string(led_pattern_t pattern);

/**
 * @brief Convertir estado del sistema a string
 *
 * @param state Estado del sistema
 * @return String correspondiente al estado
 */
const char* led_indicator_driver_system_state_to_string(led_system_state_t state);

/**
 * @brief Obtener timestamp actual del sistema
 *
 * @return Timestamp actual en milisegundos
 */
uint32_t led_indicator_driver_get_current_timestamp_ms(void);

#endif // INFRASTRUCTURE_DRIVERS_LED_INDICATOR_DRIVER_H