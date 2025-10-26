# Esquema de Funcionamiento: Riego Automático por Humedad del Suelo

**Proyecto**: Smart Irrigation System (Liwaisi Tech)
**Versión**: 2.0.0 (Component-Based Architecture)
**Fecha**: 2025-10-26
**Escenario**: Detección automática de riego cuando promedio de sensores < 51%

---

## Introducción

Este documento describe el **flujo completo** de cómo el sistema detecta automáticamente que el suelo está seco y activa el riego **sin intervención humana**, basándose en el promedio de los sensores de humedad del suelo.

**Nota importante**: El threshold crítico configurado es **30%** (no 51%), pero el documento detalla todo el rango de umbrales y cómo funciona la evaluación.

---

## Vista General del Flujo

```
LOOP PRINCIPAL (Cada 60 segundos)
        ↓
    Detectar conectividad WiFi/MQTT
        ↓
    Leer sensores (DHT22 + 3 ADC de suelo)
        ↓
    Calcular promedio de humedad del suelo
        ↓
    ¿Estamos en modo ONLINE o OFFLINE?
        ↓
    ┌─────────────────────────────────────────┐
    │ ONLINE (WiFi/MQTT conectado)            │
    ├─────────────────────────────────────────┤
    │ - Solo RECOMIENDA acciones               │
    │ - NO ejecuta automáticamente             │
    │ - Espera comando MQTT de N8N             │
    └─────────────────────────────────────────┘
        ↓
    ┌─────────────────────────────────────────┐
    │ OFFLINE (Sin WiFi/MQTT)                 │
    ├─────────────────────────────────────────┤
    │ - EJECUTA automáticamente                │
    │ - Si soil < 30% → ABRE VÁLVULA           │
    │ - Si soil >= 75% → CIERRA VÁLVULA        │
    └─────────────────────────────────────────┘
        ↓
    Envía notificación a N8N (webhook)
        ↓
    Espera próximo ciclo (60s o intervalo offline)
```

---

## Funciones Involucradas en el Proceso

### 1. **PUNTO DE PARTIDA: `irrigation_evaluation_task()`**

**Ubicación**: `components/irrigation_controller/irrigation_controller.c:127-210`
**Tipo**: Tarea FreeRTOS (Task) que se ejecuta periódicamente
**Ciclo**: Cada 60 segundos (online) o cada intervalo offline (2h/1h/30m/15m)

```c
static void irrigation_evaluation_task(void *param)
{
    ESP_LOGI(TAG, "Irrigation evaluation task started");

    while (1) {
        // PASO 1: Detectar conectividad
        bool is_online = wifi_manager_is_connected();

        // PASO 2: Leer TODOS los sensores
        sensor_reading_t reading;
        esp_err_t sensor_ret = sensor_reader_get_all(&reading);

        if (sensor_ret == ESP_OK) {
            // PASO 3: Ejecutar máquina de estados según estado actual
            switch (current_state) {
                case IRRIGATION_IDLE:
                    irrigation_state_idle_handler(&reading);  // ← BUSCA RIEGO
                    break;

                case IRRIGATION_ACTIVE:
                    irrigation_state_running_handler(&reading); // ← MONITOREA
                    break;

                case IRRIGATION_ERROR:
                    irrigation_state_error_handler(&reading);
                    break;

                case IRRIGATION_THERMAL_PROTECTION:
                    irrigation_state_thermal_stop_handler(&reading);
                    break;
            }
        }

        // PASO 4: Esperar próximo ciclo
        vTaskDelay(pdMS_TO_TICKS(eval_interval_ms));
    }
}
```

**Responsabilidades**:
1. ✅ Crea loop infinito de evaluación
2. ✅ Lee sensores cada ciclo
3. ✅ Ejecuta máquina de estados
4. ✅ Controla intervalos (60s online, 2h/1h/30m/15m offline)

---

### 2. **LECTURA DE SENSORES: `sensor_reader_get_all()`**

**Ubicación**: `components/sensor_reader/sensor_reader.c`
**Responsabilidad**: Lee DHT22 y 3 sensores ADC de humedad del suelo

```c
esp_err_t sensor_reader_get_all(sensor_reading_t* reading)
{
    // Leer DHT22 (temperatura + humedad ambiente)
    // Leer 3 sensores ADC (soil_humidity[0], [1], [2])
    // Promediar valores con filtro si está habilitado
    // Retornar estructura completa
}
```

**Retorna**:
```c
typedef struct {
    ambient_data_t ambient;     // Temp y humedad del aire
    soil_data_t soil;           // 3 valores de soil_humidity[0,1,2]
} sensor_reading_t;
```

---

### 3. **MÁQUINA DE ESTADOS: Estado IDLE → Busca Riego**

Cuando el sistema está en estado **IDLE** (sin riego), se ejecuta:

### 3a. **`irrigation_state_idle_handler()`**

**Ubicación**: `components/irrigation_controller/irrigation_controller.c:310-352`

```c
static void irrigation_state_idle_handler(const sensor_reading_t* reading)
{
    if (reading == NULL) {
        return;
    }

    // CÁLCULO 1: Promedio de 3 sensores
    float soil_avg = (reading->soil.soil_humidity[0] +
                      reading->soil.soil_humidity[1] +
                      reading->soil.soil_humidity[2]) / 3.0f;

    ESP_LOGD(TAG, "IDLE state: soil_avg=%.1f%% (start threshold=%.1f%%)",
             soil_avg, s_irrig_ctx.config.soil_threshold_critical);

    // DECISIÓN: ¿soil_avg < threshold_critical (30%)?
    if (soil_avg < s_irrig_ctx.config.soil_threshold_critical) {
        ESP_LOGI(TAG, "Soil too dry (%.1f%% < %.1f%%) - starting irrigation",
                 soil_avg, s_irrig_ctx.config.soil_threshold_critical);

        // ACCIÓN: ABRE VÁLVULA
        valve_driver_open(s_irrig_ctx.config.primary_valve);

        // ACTUALIZAR ESTADO (spinlock-protected)
        portENTER_CRITICAL(&s_irrigation_spinlock);
        {
            s_irrig_ctx.is_valve_open = true;
            s_irrig_ctx.active_valve_num = s_irrig_ctx.config.primary_valve;
            s_irrig_ctx.session_start_time = time(NULL);
            s_irrig_ctx.current_state = IRRIGATION_ACTIVE;
            s_irrig_ctx.session_count++;
        }
        portEXIT_CRITICAL(&s_irrigation_spinlock);

        // RESETEAR WATCHDOGS
        safety_watchdog_reset_session();
        safety_watchdog_reset_valve_timer();

        // ENVIAR NOTIFICACIÓN N8N
        send_n8n_webhook("irrigation_on", soil_avg,
                        reading->ambient.humidity,
                        reading->ambient.temperature);
    }
}
```

**Lógica**:
1. ✅ Calcula promedio: `soil_avg = (sensor1 + sensor2 + sensor3) / 3`
2. ✅ Compara con `THRESHOLD_SOIL_CRITICAL = 30%`
3. ✅ Si `soil_avg < 30%` → **ABRE VÁLVULA**
4. ✅ Actualiza estado a ACTIVE
5. ✅ Notifica a N8N

---

### 3b. **`valve_driver_open()`** - Control Físico

**Ubicación**: `components/irrigation_controller/drivers/valve_driver/valve_driver.c`

```c
esp_err_t valve_driver_open(uint8_t valve_num)
{
    // Configura GPIO a nivel HIGH
    // Relé se activa → Agua fluye
    // Retorna ESP_OK si éxito
}
```

**Mapeo GPIO**:
```c
#define VALVE_1_GPIO GPIO_NUM_21  // Phase 5: LED simulator
#define VALVE_2_GPIO GPIO_NUM_4   // Phase 6: Relay module
```

---

### 4. **MODO ONLINE vs OFFLINE: `irrigation_controller_evaluate_and_act()`**

**Ubicación**: `components/irrigation_controller/irrigation_controller.c:922-1066`

Esta función **central** diferencia entre dos modos:

#### **MODO ONLINE (WiFi/MQTT conectado)**

```c
if (is_online) {
    // ONLINE MODE: Solo recomienda, NO ejecuta automáticamente

    if (current_state == IRRIGATION_IDLE) {
        if (soil_avg < s_irrig_ctx.config.soil_threshold_critical) {
            eval.decision = IRRIGATION_DECISION_START;
            // ← Guarda recomendación pero NO abre válvula
            // Espera comando MQTT desde N8N
        }
    }
}
```

**Comportamiento**:
- ❌ **NO activa automáticamente**
- ✅ Recomienda acción
- ✅ Espera comando MQTT de N8N

---

#### **MODO OFFLINE (Sin WiFi/MQTT)**

```c
else {  // OFFLINE MODE
    // OFFLINE MODE: Evalúa y EJECUTA automáticamente

    offline_evaluation_t offline_eval = offline_mode_evaluate(soil_avg);

    if (current_state == IRRIGATION_IDLE) {
        // Verificar nivel offline
        if (offline_eval.level >= OFFLINE_LEVEL_CRITICAL) {
            // soil < 40% (CRITICAL or EMERGENCY)
            // ← ABRE VÁLVULA AUTOMÁTICAMENTE

            esp_err_t ret = valve_driver_open(s_irrig_ctx.config.primary_valve);

            if (ret == ESP_OK) {
                // Actualizar estado
                s_irrig_ctx.is_valve_open = true;
                s_irrig_ctx.current_state = IRRIGATION_ACTIVE;

                // Notificar N8N
                send_n8n_webhook("irrigation_on", soil_avg, ...);
            }
        }
    }
}
```

**Comportamiento**:
- ✅ **ACTIVA automáticamente**
- ✅ Abre válvula sin esperar comando
- ✅ Notifica a N8N después

---

### 5. **EVALUACIÓN DE MODO OFFLINE: `offline_mode_evaluate()`**

**Ubicación**: `components/irrigation_controller/drivers/offline_mode/offline_mode_driver.c:199-269`

**Propósito**: Determinar **nivel de urgencia** basado en humedad del suelo

```c
offline_evaluation_t offline_mode_evaluate(float soil_humidity_avg)
{
    offline_level_t new_level = _evaluate_level(soil_humidity_avg, current_level);

    const offline_level_config_t* config = _get_level_config(new_level);

    result.level = new_level;
    result.interval_ms = config->interval_ms;

    return result;
}
```

**Tabla de Decisión**:

| Soil Humidity | Nivel | Intervalo | Acción |
|---------------|-------|-----------|--------|
| **45-100%** | NORMAL | 2 horas | No riego |
| **40-45%** | WARNING | 1 hora | Monitorea |
| **30-40%** | CRITICAL | 30 min | **AUTO-START** ← |
| **< 30%** | EMERGENCY | 15 min | **AUTO-START** ← |

**Implementación**:

```c
static const offline_level_config_t s_level_config[] = {
    {
        .level = OFFLINE_LEVEL_NORMAL,
        .threshold_low = 45.0f,
        .threshold_high = 100.0f,
        .interval_ms = OFFLINE_INTERVAL_NORMAL_MS,      // 2h
        .name = "NORMAL (2h)"
    },
    {
        .level = OFFLINE_LEVEL_WARNING,
        .threshold_low = 40.0f,
        .threshold_high = 45.0f,
        .interval_ms = OFFLINE_INTERVAL_WARNING_MS,      // 1h
        .name = "WARNING (1h)"
    },
    {
        .level = OFFLINE_LEVEL_CRITICAL,
        .threshold_low = 30.0f,
        .threshold_high = 40.0f,
        .interval_ms = OFFLINE_INTERVAL_CRITICAL_MS,     // 30m
        .name = "CRITICAL (30m)"
    },
    {
        .level = OFFLINE_LEVEL_EMERGENCY,
        .threshold_low = 0.0f,
        .threshold_high = 30.0f,
        .interval_ms = OFFLINE_INTERVAL_EMERGENCY_MS,    // 15m
        .name = "EMERGENCY (15m)"
    }
};
```

---

### 6. **MONITOREO DURANTE RIEGO: `irrigation_state_running_handler()`**

**Ubicación**: `components/irrigation_controller/irrigation_controller.c:359-458`

Cuando el riego está **ACTIVO**, este handler verifica si debe detenerse:

```c
static void irrigation_state_running_handler(const sensor_reading_t* reading)
{
    float soil_avg = (reading->soil.soil_humidity[0] +
                      reading->soil.soil_humidity[1] +
                      reading->soil.soil_humidity[2]) / 3.0f;

    float soil_max = reading->soil.soil_humidity[0];
    // ... encontrar máximo de los 3 sensores ...

    // DECISIÓN 1: ¿Humedad excesiva (>=80%)?
    if (soil_max >= s_irrig_ctx.config.soil_threshold_max) {
        should_stop = true;
        stop_reason = "over_moisture";
        valve_driver_close(s_irrig_ctx.config.primary_valve);
    }

    // DECISIÓN 2: ¿Target alcanzado (>=75%)?
    else if (soil_avg >= s_irrig_ctx.config.soil_threshold_optimal) {
        should_stop = true;
        stop_reason = "target_reached";
        valve_driver_close(s_irrig_ctx.config.primary_valve);
    }

    // DECISIÓN 3: ¿Temperatura crítica (>40°C)?
    else if (alerts.temperature_critical) {
        should_stop = true;
        stop_reason = "temperature_critical";
    }

    // Si debe detenerse
    if (should_stop) {
        // Actualizar estado a IDLE
        s_irrig_ctx.current_state = IRRIGATION_IDLE;

        // Enviar webhook "irrigation_off"
        send_n8n_webhook("irrigation_off", soil_avg, ...);
    }
}
```

**Thresholds de Parada**:

| Condición | Threshold | Acción |
|-----------|-----------|--------|
| Humedad máxima | ≥ 80% | **DETIENE** (over-moisture) |
| Humedad promedio | ≥ 75% | **DETIENE** (target reached) |
| Temperatura | > 40°C | **DETIENE** (thermal protection) |
| Duración | > 120 min | **DETIENE** (max duration) |

---

### 7. **PROTECCIONES DE SEGURIDAD: `safety_watchdog_*`**

**Ubicación**: `components/irrigation_controller/drivers/safety_watchdog/safety_watchdog.c`

Funciones involucradas:

```c
// Llamadas en irrigation_state_idle_handler()
safety_watchdog_reset_session();      // Resetea contador de tiempo
safety_watchdog_reset_valve_timer();  // Resetea contador de válvula

// Verificaciones periódicas
watchdog_alerts_t alerts = safety_watchdog_check(&watchdog_inputs);

// Validaciones:
// - ¿Duración > 120 min? → ALERTA
// - ¿Temperatura > 40°C? → ALERTA
// - ¿Humedad > 80%? → ALERTA
```

---

### 8. **NOTIFICACIÓN A N8N: `send_n8n_webhook()`**

**Ubicación**: `components/irrigation_controller/irrigation_controller.c:244-303`

Se invoca en dos momentos:

#### **INICIO**: `irrigation_on`
```c
send_n8n_webhook("irrigation_on", soil_avg,
                reading->ambient.humidity,
                reading->ambient.temperature);
```

**Payload**:
```json
{
  "event_type": "irrigation_on",
  "device_id": "ESP32_HUERTA_001",
  "sensor_data": {
    "soil_moisture_prom": 28.5,
    "humidity": 62.3,
    "temperature": 26.8
  }
}
```

#### **PARADA**: `irrigation_off`
```c
send_n8n_webhook("irrigation_off", soil_avg,
                reading->ambient.humidity,
                reading->ambient.temperature);
```

---

## Diagrama Completo de Flujo

```
┌─────────────────────────────────────────────────────────────────────┐
│  TAREA: irrigation_evaluation_task()  [loop cada 60s]              │
└────────────────────────┬────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────────────┐
│  PASO 1: Detectar Conectividad                                      │
│  ┌───────────────────────────────────────────────────────────────┐  │
│  │ is_online = wifi_manager_is_connected()                       │  │
│  │                                                               │  │
│  │ true → ONLINE mode   (WiFi/MQTT activo)                       │  │
│  │ false → OFFLINE mode (Sin conectividad)                       │  │
│  └───────────────────────────────────────────────────────────────┘  │
└────────────────────────┬────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────────────┐
│  PASO 2: Leer Sensores                                              │
│  ┌───────────────────────────────────────────────────────────────┐  │
│  │ sensor_reader_get_all(&reading)                               │  │
│  │                                                               │  │
│  │ Lee:                                                          │  │
│  │  - DHT22 (temperatura, humedad ambiente)                      │  │
│  │  - ADC1 (soil_humidity[0])                                    │  │
│  │  - ADC2 (soil_humidity[1])                                    │  │
│  │  - ADC3 (soil_humidity[2])                                    │  │
│  │                                                               │  │
│  │ Retorna: sensor_reading_t con todos los valores              │  │
│  └───────────────────────────────────────────────────────────────┘  │
└────────────────────────┬────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────────────┐
│  PASO 3: Ejecutar Máquina de Estados (Switch)                       │
│  ┌───────────────────────────────────────────────────────────────┐  │
│  │ switch (current_state) {                                      │  │
│  │   case IRRIGATION_IDLE:                                       │  │
│  │     → irrigation_state_idle_handler(&reading)                 │  │
│  │     ▼▼▼ AQUÍ BUSCA SI RIEGO AUTOMÁTICO ▼▼▼                  │  │
│  │                                                               │  │
│  │   case IRRIGATION_ACTIVE:                                     │  │
│  │     → irrigation_state_running_handler(&reading)              │  │
│  │     ▼▼▼ AQUÍ MONITOREA Y DETIENE ▼▼▼                         │  │
│  └───────────────────────────────────────────────────────────────┘  │
└────────────────────────┬────────────────────────────────────────────┘
                         │
         ┌───────────────┴───────────────┐
         │                               │
         ▼                               ▼
┌────────────────────────┐   ┌────────────────────────┐
│  ESTADO: IDLE          │   │  ESTADO: ACTIVE        │
│  (Sin riego)           │   │  (Riego activo)        │
└────────┬───────────────┘   └────────┬───────────────┘
         │                            │
         ▼                            ▼
    irrigation_idle_handler()      irrigation_running_handler()
    ┌──────────────────────┐       ┌────────────────────────┐
    │ 1. Calc soil_avg     │       │ 1. Calc soil_avg       │
    │    = (s1+s2+s3)/3    │       │    = (s1+s2+s3)/3      │
    │                      │       │                        │
    │ 2. soil_avg < 30%?   │       │ 2. soil_avg >= 75%?    │
    │    YES → OPEN VALVE  │       │    YES → CLOSE VALVE   │
    │    NO  → NO ACTION   │       │                        │
    │                      │       │ 3. soil_max >= 80%?    │
    │ 3. SEND WEBHOOK      │       │    YES → CLOSE VALVE   │
    │    "irrigation_on"   │       │                        │
    └──────────────────────┘       │ 4. temp > 40°C?        │
                                   │    YES → THERMAL STOP  │
                                   │                        │
                                   │ 5. SEND WEBHOOK        │
                                   │    "irrigation_off"    │
                                   └────────────────────────┘
         │                            │
         └────────────┬───────────────┘
                      │
                      ▼
    ┌────────────────────────────────────────────────┐
    │  ¿MODO ONLINE o OFFLINE?                       │
    │                                                │
    │  Si ONLINE (WiFi/MQTT):                        │
    │    - Solo RECOMIENDA acciones                  │
    │    - NO ejecuta automáticamente                │
    │    - Espera comando MQTT desde N8N             │
    │                                                │
    │  Si OFFLINE (Sin conectividad):                │
    │    - EJECUTA automáticamente                   │
    │    - irrigation_controller_evaluate_and_act()  │
    │      ├─ offline_mode_evaluate(soil_avg)       │
    │      ├─ if level >= CRITICAL → OPEN VALVE    │
    │      └─ send_n8n_webhook()                     │
    └────────────┬───────────────────────────────────┘
                 │
                 ▼
    ┌────────────────────────────────────────────────┐
    │  PASO 4: Determinar Intervalo de Próximo Ciclo │
    │                                                │
    │  Si ONLINE:                                    │
    │    → eval_interval_ms = 60,000 ms (60 seg)    │
    │                                                │
    │  Si OFFLINE:                                   │
    │    → eval_interval_ms = offline_level.interval │
    │       - NORMAL: 2h (7,200,000 ms)              │
    │       - WARNING: 1h (3,600,000 ms)             │
    │       - CRITICAL: 30m (1,800,000 ms)           │
    │       - EMERGENCY: 15m (900,000 ms)            │
    └────────────┬───────────────────────────────────┘
                 │
                 ▼
    ┌────────────────────────────────────────────────┐
    │  PASO 5: Esperar Próximo Ciclo                 │
    │  vTaskDelay(pdMS_TO_TICKS(eval_interval_ms))  │
    │                                                │
    │  Después vuelve a PASO 1                       │
    └────────────────────────────────────────────────┘
```

---

## Tabla de Funciones Involucradas

| Función | Ubicación | Línea | Responsabilidad |
|---------|-----------|-------|-----------------|
| `irrigation_evaluation_task()` | irrigation_controller.c | 127 | Loop principal, coordina todo |
| `sensor_reader_get_all()` | sensor_reader.c | - | Lee DHT22 + 3 ADC |
| `irrigation_state_idle_handler()` | irrigation_controller.c | 310 | **Busca automático, abre válvula** |
| `valve_driver_open()` | valve_driver.c | - | Abre válvula (GPIO HIGH) |
| `irrigation_state_running_handler()` | irrigation_controller.c | 359 | Monitorea, detiene riego |
| `irrigation_controller_evaluate_and_act()` | irrigation_controller.c | 922 | **Decide ONLINE vs OFFLINE** |
| `offline_mode_evaluate()` | offline_mode_driver.c | 199 | Evalúa nivel (NORMAL/WARNING/CRITICAL/EMERGENCY) |
| `safety_watchdog_reset_session()` | safety_watchdog.c | - | Resetea protecciones |
| `safety_watchdog_check()` | safety_watchdog.c | - | Verifica alertas de seguridad |
| `send_n8n_webhook()` | irrigation_controller.c | 244 | Envía notificación HTTP |
| `wifi_manager_is_connected()` | wifi_manager.c | - | Detecta conectividad |

---

## Umbrales Críticos

```c
// Soil Humidity Thresholds (common_types.h)
#define THRESHOLD_SOIL_CRITICAL         30.0f   ← START irrigation
#define THRESHOLD_SOIL_OPTIMAL          75.0f   ← STOP irrigation
#define THRESHOLD_SOIL_MAX              80.0f   ← EMERGENCY stop (over-moisture)

// Temperature Thresholds
#define THRESHOLD_TEMP_CRITICAL         32.0f   ← Warning level
#define THRESHOLD_TEMP_THERMAL_STOP     40.0f   ← Auto-stop (thermal protection)

// Offline Mode Thresholds (offline_mode_driver.h)
#define OFFLINE_THRESHOLD_NORMAL_LOW    45.0f   ← Below: WARNING
#define OFFLINE_THRESHOLD_WARNING_LOW   40.0f   ← Below: CRITICAL
#define OFFLINE_THRESHOLD_CRITICAL_LOW  30.0f   ← Below: EMERGENCY

// Safety Limits
#define MAX_IRRIGATION_DURATION_MIN     120     ← 2h máximo por sesión
#define MIN_IRRIGATION_INTERVAL_MIN     240     ← 4h mínimo entre sesiones
#define MAX_DAILY_IRRIGATION_MIN        360     ← 6h máximo por día
```

---

## Ejemplo Paso a Paso: Sensor < 30%

### Escenario: Sistema OFFLINE con suelo seco (25% humedad)

```
CICLO 1 (t=0s):
  ├─ WiFi DOWN → is_online = false (OFFLINE mode)
  ├─ sensor_reader_get_all()
  │  └─ ADC1: 22%, ADC2: 26%, ADC3: 27%
  │  └─ soil_avg = (22+26+27)/3 = 25%
  ├─ current_state = IDLE
  ├─ irrigation_state_idle_handler(&reading)
  │  ├─ soil_avg = 25% < THRESHOLD_SOIL_CRITICAL (30%)? ✅ YES
  │  ├─ valve_driver_open(valve_1)  → GPIO_NUM_21 = HIGH
  │  ├─ s_irrig_ctx.is_valve_open = true
  │  ├─ s_irrig_ctx.current_state = ACTIVE
  │  ├─ safety_watchdog_reset_session()
  │  ├─ safety_watchdog_reset_valve_timer()
  │  └─ send_n8n_webhook("irrigation_on", 25.0, ...)
  │     └─ POST http://192.168.1.41:5678/webhook-test/irrigation-events
  ├─ offline_mode_evaluate(25.0)
  │  └─ level = EMERGENCY (< 30%)
  │  └─ interval_ms = 900,000 (15 min)
  └─ vTaskDelay(900,000 ms) ← Próximo ciclo en 15 minutos

CICLO 2 (t=15m):
  ├─ WiFi DOWN (aún offline)
  ├─ sensor_reader_get_all()
  │  └─ soil_avg = 35% (ha subido)
  ├─ current_state = ACTIVE (está regando)
  ├─ irrigation_state_running_handler(&reading)
  │  ├─ soil_avg = 35% < THRESHOLD_SOIL_OPTIMAL (75%)?
  │  ├─ NO, sigue regando (target no alcanzado)
  │  └─ NO DETIENE
  ├─ offline_mode_evaluate(35.0)
  │  └─ level = CRITICAL (30-40%)
  │  └─ interval_ms = 1,800,000 (30 min)
  └─ vTaskDelay(1,800,000 ms)

CICLO 3 (t=45m):
  ├─ WiFi UP → is_online = true (VUELVE ONLINE)
  ├─ sensor_reader_get_all()
  │  └─ soil_avg = 73%
  ├─ current_state = ACTIVE
  ├─ irrigation_state_running_handler(&reading)
  │  ├─ soil_avg = 73% < THRESHOLD_SOIL_OPTIMAL (75%)?
  │  ├─ Casi, pero no todavía
  │  └─ NO DETIENE
  ├─ eval_interval_ms = 60,000 (vuelve a 60s online)
  └─ vTaskDelay(60,000 ms)

CICLO 4 (t=46m):
  ├─ WiFi UP (online)
  ├─ sensor_reader_get_all()
  │  └─ soil_avg = 76%
  ├─ current_state = ACTIVE
  ├─ irrigation_state_running_handler(&reading)
  │  ├─ soil_avg = 76% >= THRESHOLD_SOIL_OPTIMAL (75%)? ✅ YES
  │  ├─ valve_driver_close(valve_1) → GPIO_NUM_21 = LOW
  │  ├─ s_irrig_ctx.is_valve_open = false
  │  ├─ s_irrig_ctx.current_state = IDLE
  │  └─ send_n8n_webhook("irrigation_off", 76.0, ...)
  │     └─ POST http://192.168.1.41:5678/webhook-test/irrigation-events
  ├─ eval_interval_ms = 60,000
  └─ vTaskDelay(60,000 ms)

✅ RIEGO COMPLETADO: 22% → 76% en ~46 minutos
```

---

## Casos Especiales

### Caso 1: Sensor Falla Crítica

```
sensor_reader_get_all() retorna ESP_ERR_INVALID_STATE
  ↓
irrigation_evaluation_task() detecta error
  ↓
current_state = IRRIGATION_ERROR
  ↓
irrigation_state_error_handler()
  ├─ valve_driver_close() (cierra por seguridad)
  └─ send_n8n_webhook("sensor_error", 0.0, 0.0, 0.0)
```

### Caso 2: Temperatura Crítica (> 40°C)

```
while (riego activo):
  ├─ safety_watchdog_check() detecta T > 40°C
  ├─ irrigation_state_running_handler() ve alert
  ├─ valve_driver_close()
  ├─ current_state = IRRIGATION_THERMAL_PROTECTION
  └─ send_n8n_webhook("temperature_critical", ...)

Espera hasta T < 32°C:
  ├─ irrigation_state_thermal_stop_handler()
  ├─ Si T < 32°C:
  │  └─ current_state = IRRIGATION_IDLE
  └─ Sistema listo para próximo riego
```

### Caso 3: Safety Lock Activo (después de EMERGENCY_STOP)

```
Aunque soil_avg < 30%:
  ├─ irrigation_state_idle_handler() verifica seguridad
  ├─ if (safety_lock == true) → NO ABRE VÁLVULA
  └─ Espera desbloqueo manual: irrigation_controller_unlock_safety()
```

---

## FLUJO DETALLADO: Envío de Notificaciones a N8N

### ¿Cuándo se envía una notificación?

Las notificaciones se envían en **3 momentos clave**:

1. **Inicio de riego automático** → `irrigation_on`
2. **Parada de riego** → `irrigation_off`
3. **Eventos críticos** → `sensor_error`, `temperature_critical`, `emergency_stop`

### Puntos de Activación en el Código

#### **ACTIVACIÓN 1: Riego Iniciado Automáticamente**

**Ubicación**: `irrigation_state_idle_handler()` línea 348-350

```c
// Condición: soil_avg < 30% (THRESHOLD_SOIL_CRITICAL)
if (soil_avg < s_irrig_ctx.config.soil_threshold_critical) {
    ESP_LOGI(TAG, "Soil too dry (%.1f%% < %.1f%%) - starting irrigation",
             soil_avg, s_irrig_ctx.config.soil_threshold_critical);

    // 1. ABRE VÁLVULA
    valve_driver_open(s_irrig_ctx.config.primary_valve);

    // 2. ACTUALIZA ESTADO
    portENTER_CRITICAL(&s_irrigation_spinlock);
    {
        s_irrig_ctx.is_valve_open = true;
        s_irrig_ctx.current_state = IRRIGATION_ACTIVE;
        s_irrig_ctx.session_count++;
    }
    portEXIT_CRITICAL(&s_irrigation_spinlock);

    // 3. RESETEA WATCHDOGS
    safety_watchdog_reset_session();
    safety_watchdog_reset_valve_timer();

    // 4. ⭐ ENVÍA NOTIFICACIÓN A N8N
    send_n8n_webhook("irrigation_on", soil_avg,
                    reading->ambient.humidity,
                    reading->ambient.temperature);
}
```

---

#### **ACTIVACIÓN 2: Riego Detenido (Target Alcanzado)**

**Ubicación**: `irrigation_state_running_handler()` línea 449-451

```c
if (soil_avg >= s_irrig_ctx.config.soil_threshold_optimal) {
    should_stop = true;
    valve_driver_close(s_irrig_ctx.config.primary_valve);

    // ⭐ ENVÍA NOTIFICACIÓN A N8N
    send_n8n_webhook("irrigation_off", soil_avg,
                    reading->ambient.humidity,
                    reading->ambient.temperature);
}
```

---

#### **ACTIVACIÓN 3: Error de Sensor**

**Ubicación**: `irrigation_state_error_handler()` línea 480-486

```c
static void irrigation_state_error_handler(const sensor_reading_t* reading)
{
    valve_driver_close(1);
    valve_driver_close(2);

    // ⭐ ENVÍA NOTIFICACIÓN A N8N
    send_n8n_webhook("sensor_error", soil_avg, humidity, temperature);
}
```

---

#### **ACTIVACIÓN 4: Protección Térmica (> 40°C)**

**Ubicación**: `irrigation_state_thermal_stop_handler()` línea 516-521

```c
send_n8n_webhook("temperature_critical", soil_avg, humidity, temperature);
```

---

#### **ACTIVACIÓN 5: Emergency Stop**

**Ubicación**: `irrigation_controller_execute_command()` línea 817

```c
if (command == IRRIGATION_CMD_EMERGENCY_STOP) {
    valve_driver_emergency_close_all();
    s_irrig_ctx.safety_lock = true;
    send_n8n_webhook("emergency_stop", 0.0f, 0.0f, 0.0f);
}
```

---

### Función: `send_n8n_webhook()` - Implementación

**Ubicación**: `components/irrigation_controller/irrigation_controller.c:244-303`

```c
static void send_n8n_webhook(const char* event_type,
                             float soil_moisture_prom,
                             float humidity,
                             float temperature)
{
    if (event_type == NULL) return;

    // PASO 1: Construir JSON con cJSON
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event_type", event_type);
    cJSON_AddStringToObject(root, "device_id", "ESP32_HUERTA_001");

    cJSON* sensor_data = cJSON_CreateObject();
    cJSON_AddNumberToObject(sensor_data, "soil_moisture_prom", soil_moisture_prom);
    cJSON_AddNumberToObject(sensor_data, "humidity", humidity);
    cJSON_AddNumberToObject(sensor_data, "temperature", temperature);
    cJSON_AddItemToObject(root, "sensor_data", sensor_data);

    char* json_str = cJSON_Print(root);
    cJSON_Delete(root);

    // PASO 2: Crear cliente HTTP
    esp_http_client_config_t config = {
        .url = "http://192.168.1.41:5678/webhook-test/irrigation-events",
        .method = HTTP_METHOD_POST,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // PASO 3: Configurar Headers
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "X-API-Key",
        "d1b4873f5144c6db6c87f03109010c314a7fb9edf3052f4f478bb2d967181427");

    // PASO 4: Establecer Body
    esp_http_client_set_post_field(client, json_str, strlen(json_str));

    // PASO 5: Realizar POST HTTP
    esp_err_t ret = esp_http_client_perform(client);

    if (ret == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "N8N webhook sent: %s (HTTP %d)", event_type, status_code);
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(ret));
    }

    // PASO 6: Limpiar recursos
    esp_http_client_cleanup(client);
    free(json_str);
}
```

---

### Diagrama: Flujo de Notificación a N8N

```
EVENTO DETECTADO
    ↓ (soil < 30% o soil >= 75% o error, etc)
send_n8n_webhook("irrigation_on", 28.5, 62.3, 26.8)
    ↓
PASO 1: Construir JSON
{
  "event_type": "irrigation_on",
  "device_id": "ESP32_HUERTA_001",
  "sensor_data": {
    "soil_moisture_prom": 28.5,
    "humidity": 62.3,
    "temperature": 26.8
  }
}
    ↓
PASO 2: Crear Cliente HTTP (esp_http_client_init)
    ↓
PASO 3: Configurar Headers
  - Content-Type: application/json
  - X-API-Key: d1b4873f5144c6db...
    ↓
PASO 4: Establecer Body (json_str)
    ↓
PASO 5: Realizar POST HTTP
  POST http://192.168.1.41:5678/webhook-test/irrigation-events
    ↓
RESPUESTA HTTP
  - 200 OK → ✅ Notificación recibida
  - 401 Unauthorized → ❌ API Key inválida
  - 404 Not Found → ❌ Endpoint no existe
    ↓
PASO 6: Limpiar recursos
  - esp_http_client_cleanup(client)
  - free(json_str)
    ↓
RETORNA a irrigation_evaluation_task()
```

---

### Tabla de Eventos y Payloads

| Evento | Cuándo | Valores | Línea |
|--------|--------|--------|-------|
| **irrigation_on** | soil_avg < 30% | soil_avg, humidity, temp | 348 |
| **irrigation_off** | soil_avg >= 75% | soil_avg, humidity, temp | 449 |
| **sensor_error** | DHT22/ADC falla | soil_avg, humidity, temp | 480 |
| **temperature_critical** | temp > 40°C | soil_avg, humidity, temp | 516 |
| **emergency_stop** | Manual STOP | 0.0, 0.0, 0.0 | 817 |

---

## Resumen: Funciones Clave por Orden de Ejecución

```
1️⃣ irrigation_evaluation_task()           ← Loop principal
     ↓
2️⃣ wifi_manager_is_connected()            ← Detecta conectividad
     ↓
3️⃣ sensor_reader_get_all()                ← Lee sensores
     ↓
4️⃣ switch(current_state)                  ← Máquina de estados
     ├─ irrigation_state_idle_handler()    ← BUSCA RIEGO
     └─ irrigation_state_running_handler() ← MONITOREA
        ↓
5️⃣ irrigation_controller_evaluate_and_act() ← Decide ONLINE/OFFLINE
     ├─ offline_mode_evaluate()             ← Evalúa nivel
     └─ if OFFLINE && critical → ABRE VÁLVULA
        ↓
6️⃣ valve_driver_open() / valve_driver_close() ← Control físico
     ↓
7️⃣ safety_watchdog_reset_*()              ← Activa protecciones
     ↓
8️⃣ send_n8n_webhook()                     ← ⭐ ENVÍA NOTIFICACIÓN N8N
     ├─ Construye JSON
     ├─ Crea cliente HTTP
     ├─ Configura headers (Content-Type, X-API-Key)
     ├─ Realiza POST a 192.168.1.41:5678
     └─ Limpia recursos
```

---

**Documento generado**: 2025-10-26
**Versión del proyecto**: 2.0.0 (Component-Based Architecture)
**Estado**: Ready for Phase 5 Implementation
