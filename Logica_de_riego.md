# Lógica de Riego Automático - Especificaciones Finales

**Última actualización**: 2025-10-18
**Estado**: ✅ Definición completa para Phase 5 (Irrigation Controller)

---

## 1. Límites Operacionales - DEFINITIVOS

### 1.1 Umbrales de Humedad del Suelo

| Métrica | Valor | Descripción |
|---------|-------|-------------|
| **Inicio de Riego** | < 51% (promedio) | Se activa riego cuando promedio de 3 sensores < 51% |
| **Parada Normal** | ≥ 70-75% (TODOS) | Se detiene cuando TODOS los sensores ≥ 70-75% |
| **Parada Emergencia** | ≥ 80% (ALGUNO) | Auto-stop inmediato si CUALQUIER sensor ≥ 80% |
| **Nivel Crítico** | 30% | Emergencia (Fase 6: modo supervivencia) |
| **Nivel Óptimo** | 75% | Humedad ideal del suelo |

### 1.2 Límites de Tiempo

| Límite | Valor | Acción |
|--------|-------|--------|
| **Duración máxima sesión** | 2 horas (120 min) | Auto-stop + alert N8N |
| **Intervalo mínimo entre sesiones** | 4 horas (240 min) | No permitir riego antes de 4h |
| **Válvula abierta máximo** | 40 minutos | Alert N8N si se excede |
| **Timeout MQTT override** | 30 minutos (inactividad) | Vuelve a modo automático |

### 1.3 Protecciones Térmicas

| Protección | Trigger | Acción |
|-----------|---------|--------|
| **Temperatura crítica** | T° > 40°C | Auto-stop inmediato |
| **Zona estrés térmico** | 32-34°C (12:00-15:00) | Reducir frecuencia (Fase 6) |
| **Ventana óptima nocturna** | 00:00-06:00 (26-28°C) | Preferir riego en ventana (Fase 6) |

---

## 2. Especificaciones de Hardware - FASE 5

### 2.1 Sensores y Actuadores

| Componente | Cantidad | Especificación |
|-----------|----------|-----------------|
| **DHT22** | 1 | Temperatura + Humedad ambiente |
| **Sensores Capacitivos** | 3 | Humedad del suelo (ADC) |
| **LED Simulador** | 1 | GPIO output (simula válvula Fase 5) |
| **Relay Module** | 0 | Fase 6 (no incluir aún) |

### 2.2 Asignaciones GPIO - DEFINITIVAS

```c
// Actuador
#define IRRIGATION_VALVE_LED_GPIO        GPIO_NUM_25  // LED simulador (Phase 5) - Changed from 21 (I2C SDA conflict)

// Sensores (ya configurados en sensor_reader)
#define SOIL_MOISTURE_1_ADC              ADC1_CHANNEL_0  // GPIO 36
#define SOIL_MOISTURE_2_ADC              ADC1_CHANNEL_3  // GPIO 39
#define SOIL_MOISTURE_3_ADC              ADC1_CHANNEL_6  // GPIO 34
#define STATUS_LED_GPIO                  GPIO_NUM_18  // LED estado general
```

### 2.3 Conectividad

| Servicio | Propósito | Estado |
|----------|-----------|--------|
| **WiFi** | Detectar conectividad online/offline | Phase 5 |
| **MQTT** | Comandos remotos (start/stop override) | Phase 5 |
| **N8N Webhook** | Notificaciones de eventos | Phase 5 |

---

## 3. Modos de Operación

### 3.1 Modo ONLINE (WiFi/MQTT Conectado)

```
Ciclo de Evaluación: 60 segundos

1. Leer sensores (DHT22 + 3 ADC)
2. Calcular promedio humedad suelo
3. Evaluar state machine
4. Ejecutar acción (abrir/cerrar válvula)
5. Verificar protecciones (timeouts, temperatura)
6. Esperar 60 segundos → repetir
```

**Intervalo**: Cada 60 segundos
**Prioridad**: Alta (respuesta rápida a cambios)

### 3.2 Modo OFFLINE (Sin WiFi/MQTT)

```
Ciclo de Evaluación: 2 horas (configurable en Kconfig)

Condiciones:
- Sin conexión WiFi durante > 60 segundos
- Sin conexión MQTT durante > 60 segundos

Comportamiento:
- Mismo state machine pero con intervalos largos
- Evaluación cada 2 horas (modo Normal)
- NO se guarda historial de sensores
- NO se envían datos a MQTT
- Riego sigue automático basado en umbrales

Transición:
- Si WiFi se reconecta → volver a modo ONLINE
- Si WiFi pierde conexión → entrar a modo OFFLINE
```

**Intervalo**: Cada 2 horas
**Configuración**: Menuconfig Kconfig

---

## 4. Estado del Riego - State Machine

### 4.1 Estados Definidos

```
IDLE
  ├─→ EVALUATING (lectura de sensores)
      ├─→ RUNNING (si promedio < 51%)
      ├─→ OFFLINE (si pierde WiFi)
      └─→ ERROR (si sensor falla)

RUNNING (Riego Activo)
  ├─→ IDLE (TODOS 70-75% O ALGUNO >= 80% O tiempo >= 120min)
  ├─→ SAFETY_STOP (T° > 40°C O valve_time > 40min)
  ├─→ OVERRIDE_MQTT (comando MQTT "stop")
  └─→ ERROR (sensor falla)

OVERRIDE_MQTT (Control Remoto)
  ├─→ RUNNING (comando MQTT "start")
  ├─→ IDLE (comando MQTT "stop")
  └─→ IDLE (timeout 30min sin comando)

OFFLINE (Sin WiFi)
  ├─→ IDLE (WiFi reconecta)
  ├─→ RUNNING (humedad < 51% en modo offline)
  └─→ ERROR (sensor falla)

SAFETY_STOP (Parada Seguridad)
  └─→ IDLE (después de alert a N8N)

ERROR (Fallo Sensor)
  └─→ IDLE (después de alert a N8N)
```

### 4.2 Razones de Parada

| Razón | Trigger | Notificación N8N |
|-------|---------|------------------|
| **Humedad Alcanzada** | TODOS  70-75% | IRRIGATION_STOPPED (normal) |
| **Sobre-humedad** | ALGUNO >= 80% | IRRIGATION_STOPPED (warning) |
| **Timeout Sesión** | > 120 min | VALVE_TIMEOUT (critical) |
| **Temperatura** | > 40°C | TEMPERATURE_CRITICAL |
| **Timeout Válvula** | > 40 min | VALVE_TIMEOUT (critical) |
| **Error Sensor** | Lectura falla | SENSOR_ERROR |
| **Override MQTT** | Comando "stop" | IRRIGATION_STOPPED (manual) |

---

## 5. Eventos y Notificaciones N8N

### 5.1 Webhook URL (Configurable en Kconfig)

```
Default: http://192.168.1.177:5678/webhook/irrigation-events

Campo en Kconfig:
- N8N_WEBHOOK_URL
- Default: "http://192.168.1.177:5678/webhook/irrigation-events"
```

### 5.2 Eventos Disparados

#### Evento 1: IRRIGATION_STARTED

```json
{
  "event_type": "IRRIGATION_STARTED",
  "mac_address": "AA:BB:CC:DD:EE:FF",
  "timestamp": 1697700000,
  "data": {
    "reason": "SENSOR_THRESHOLD",
    "soil_humidity_avg": 48.5,
    "soil_humidity_max": 50.0,
    "soil_humidity_min": 47.0,
    "ambient_temperature": 28.5,
    "ambient_humidity": 65.0
  }
}
```

#### Evento 2: IRRIGATION_STOPPED

```json
{
  "event_type": "IRRIGATION_STOPPED",
  "mac_address": "AA:BB:CC:DD:EE:FF",
  "timestamp": 1697701800,
  "data": {
    "duration_minutes": 30,
    "stop_reason": "HUMEDAD_ALCANZADA",
    "final_soil_humidity_avg": 75.2,
    "final_soil_humidity_max": 78.0,
    "final_soil_humidity_min": 73.0
  }
}
```

#### Evento 3: SENSOR_ERROR

```json
{
  "event_type": "SENSOR_ERROR",
  "mac_address": "AA:BB:CC:DD:EE:FF",
  "timestamp": 1697701000,
  "data": {
    "sensor_id": "soil_moisture_1",
    "error_type": "READ_TIMEOUT",
    "error_code": -1
  }
}
```

#### Evento 4: VALVE_TIMEOUT

```json
{
  "event_type": "VALVE_TIMEOUT",
  "mac_address": "AA:BB:CC:DD:EE:FF",
  "timestamp": 1697702400,
  "data": {
    "open_time_minutes": 40,
    "alert": "Válvula abierta más de 40 minutos - verificar"
  }
}
```

#### Evento 5: TEMPERATURE_CRITICAL

```json
{
  "event_type": "TEMPERATURE_CRITICAL",
  "mac_address": "AA:BB:CC:DD:EE:FF",
  "timestamp": 1697701500,
  "data": {
    "current_temperature": 41.5,
    "threshold": 40.0,
    "action": "IRRIGATION_STOPPED"
  }
}
```

### 5.3 Método de Envío

- **Protocolo**: HTTP POST o MQTT publish
- **Frecuencia**: Sólo cuando hay evento
- **Reintentos**: 3 intentos con backoff exponencial (Fase 6)

---

## 6. Control Remoto MQTT

### 6.1 Comandos Soportados

```
Topic: irrigation/control/{mac_address}

Comandos:
- "start"  → Fuerza inicio de riego (OVERRIDE_MQTT)
- "stop"   → Detiene riego en progreso (OVERRIDE_MQTT)

Respuesta:
Topic: irrigation/control/{mac_address}/response
Payload: {"command": "start", "status": "ok", "timestamp": ...}
```

### 6.2 Comportamiento Override

```
MQTT Command "start":
1. Abre válvula inmediatamente
2. State → OVERRIDE_MQTT
3. Ignora evaluación automática de sensores
4. Timeout: 30 minutos sin nuevo comando
5. Después del timeout → vuelve a IDLE

MQTT Command "stop":
1. Cierra válvula inmediatamente
2. State → IDLE
3. Guarda duración de sesión
4. Envía webhook IRRIGATION_STOPPED con reason: "MQTT_COMMAND"
```

---

## 7. Configuración Menuconfig (Kconfig)

```
Smart Irrigation System Configuration
└── Irrigation Controller Configuration
    ├── Soil moisture threshold start (%)
    │   Type: int
    │   Default: 51
    │   Range: 30-70
    │
    ├── Soil moisture threshold stop (%)
    │   Type: int
    │   Default: 70
    │   Range: 50-85
    │
    ├── Soil moisture danger level (%)
    │   Type: int
    │   Default: 80
    │   Range: 70-95
    │
    ├── Max irrigation duration (minutes)
    │   Type: int
    │   Default: 120
    │   Range: 30-180
    │
    ├── Min interval between sessions (minutes)
    │   Type: int
    │   Default: 240
    │   Range: 60-480
    │
    ├── Valve timeout alert (minutes)
    │   Type: int
    │   Default: 40
    │   Range: 20-60
    │
    ├── Offline mode evaluation interval (hours)
    │   Type: int
    │   Default: 2
    │   Range: 1-6
    │
    ├── N8N Webhook URL
    │   Type: string
    │   Default: "http://192.168.1.177:5678/webhook/irrigation-events"
    │
    └── [*] Enable MQTT command override
        Type: bool
        Default: enabled
        Help: Allow remote control via MQTT commands
```

---

## 8. Integración con Componentes Existentes

### 8.1 sensor_reader

```c
#include "sensor_reader.h"

// Lectura en evaluación:
sensor_reading_t reading;
sensor_reader_get_all(&reading);

// Valores disponibles:
- reading.ambient_temperature
- reading.ambient_humidity
- reading.soil_humidity_1
- reading.soil_humidity_2
- reading.soil_humidity_3
```

### 8.2 wifi_manager

```c
#include "wifi_manager.h"

// Detectar conectividad:
bool is_online = wifi_manager_is_connected();

// Si cambio de estado:
if (was_online && !is_online) {
    // Entrar modo OFFLINE
} else if (!was_online && is_online) {
    // Salir modo OFFLINE
}
```

### 8.3 mqtt_client

```c
#include "mqtt_client.h"

// Recibir comandos:
// mqtt_client callback → irrigation_controller_mqtt_command()

// Enviar eventos (alternativa):
esp_mqtt_client_publish(client,
    "irrigation/events/{mac}",
    json_payload,
    QOS_1);
```

---

## 9. Protecciones de Seguridad - FASE 5

| Protección | Implementación | Notificación |
|-----------|----------------|-----------|
| Duración máxima | Counter en millisegundos | VALVE_TIMEOUT |
| Temperatura | Lectura DHT22 | TEMPERATURE_CRITICAL |
| Sobre-humedad | Lectura sensores ADC | IRRIGATION_STOPPED |
| Timeout MQTT | Counter inactividad | Auto-timeout |
| Sensor error | Try-catch en lectura | SENSOR_ERROR |

---

## 10. Optimizaciones Memoria - TARGET

```
RAM Objetivo: < 150 KB

Estructuras:
- irrigation_controller_status_t: ~100 bytes (static)
- irrigation_sensor_reading_t: ~32 bytes
- Task stack: 4 KB

Total estimado: < 50 KB de RAM adicional
```

---

## 11. Fase 6 (Pendiente - NO Implementar Phase 5)

❌ **NO INCLUIR EN PHASE 5**:
- Deep Sleep (80% tiempo offline)
- Ventana riego nocturno (00:00-06:00)
- Zona estrés térmico (12:00-15:00)
- Retry automático para webhooks
- Failsafe logic con sensor redundancia
- Buffer circular 48h historial
- Machine learning predicción

✅ **PLANIFICAR PARA PHASE 6**:
- Documentar en tech debt
- Diseñar interfaces (no implementar)
- Revisar consumo de recursos

---

## Resumen de Cambios respecto a Documento Original

| Aspecto | Original | Actualizado |
|---------|----------|------------|
| Inicio riego | No especificado | < 51% promedio |
| Parada riego | 70-75% | TODOS 70-75% |
| Parada emergencia | No especificado | ALGUNO >= 80% |
| 1 Válvula | 2 Modulo relay de 2 canales (indefinido) | 1 LED GPIO_NUM_21 (Phase 5) |
| Historial sensores | 48h buffer | NO (solo MQTT) |
| Modo offline | 4 modos adaptativos | 2 modos (Normal 2h, Emergencia 30min - Fase 6) |
| Webhook | No especificado | N8N configurable en Kconfig |
| Control override | No especificado | Sí, MQTT commands |
| Válvula timeout | No especificado | 40 minutos con alert |
| Deep sleep | 80% tiempo | Fase 6 (no Phase 5) |
| Failsafe | Mencionado sin detalles | Fase 6 (planeado) |

