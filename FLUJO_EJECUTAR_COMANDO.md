# FLUJO DETALLADO: `irrigation_controller_execute_command()`

**¿Cómo funciona? ¿Quién la llama? ¿Cómo recibe instrucciones?**

---

## 🔄 FLUJO COMPLETO (Paso a Paso)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         USUARIO/APLICACIÓN                              │
│                    (Móvil, Web, Dashboard)                              │
└────────────────────────────────┬────────────────────────────────────────┘
                                 │
                    Envía comando MQTT a topic:
                    irrigation/control/{MAC_ADDRESS}
                                 │
                    Payload JSON:
                    {
                      "command": "start",
                      "duration_minutes": 15
                    }
                                 │
                                 ▼
                ┌──────────────────────────────┐
                │   MQTT BROKER (Cloud)        │
                │  (wss://broker.com:8083)     │
                └────────────┬─────────────────┘
                             │
              Broker rutea el mensaje al cliente ESP32
              (subscrito a irrigation/control/...)
                             │
                             ▼
        ┌────────────────────────────────────────────────┐
        │         MQTT CLIENT (mqtt_adapter.c)           │
        │                                                │
        │  1. Recibe evento MQTT_EVENT_DATA              │
        │  2. Valida que es topic: irrigation/control/*  │
        │  3. Parsea JSON payload                        │
        │  4. Extrae comando y duración                  │
        │  5. Llama callback registrado                  │
        └────────────┬─────────────────────────────────┘
                     │
        mqtt_handle_irrigation_command()
        (línea 880 en mqtt_adapter.c)
                     │
    ┌────────────────┴───────────────────┐
    │ cmd_callback(                       │
    │   command=IRRIGATION_CMD_START,     │
    │   duration_minutes=15,              │
    │   user_data=NULL                    │
    │ )                                   │
    └────────────┬────────────────────────┘
                 │
                 ▼
    ┌────────────────────────────────────────────────┐
    │  IRRIGATION CONTROLLER                         │
    │  irrigation_controller_execute_command()        │
    │  (línea 777 en irrigation_controller.c)         │
    │                                                 │
    │  [Aquí es donde entra tu función]              │
    └────────┬───────────────────────────────────────┘
             │
    ┌────────▼───────────────────────────────┐
    │ Paso 1: Validar entrada                │
    │  - ¿Está inicializado?                 │
    │  - ¿Comando válido?                    │
    └────────┬────────────────────────────────┘
             │
             ▼
    ┌──────────────────────────────────────┐
    │ Paso 2: Leer estado actual (spinlock)│
    │  - safety_lock                       │
    │  - next_allowed_session              │
    │  - total_runtime_today_sec           │
    └────────┬─────────────────────────────┘
             │
    ┌────────▼─────────────────────────────────────────┐
    │ Paso 3: Evaluar comando                          │
    │                                                   │
    │ ┌─────────────────────────────────────────────┐ │
    │ │ SI EMERGENCY_STOP:                          │ │
    │ │  - valve_driver_emergency_close_all()       │ │
    │ │  - Activar safety_lock = true               │ │
    │ │  - Cambiar estado → EMERGENCY_STOP          │ │
    │ │  - Enviar webhook N8N                       │ │
    │ │  - RETORNAR ESP_OK                          │ │
    │ └─────────────────────────────────────────────┘ │
    │                                                   │
    │ ┌─────────────────────────────────────────────┐ │
    │ │ SI STOP:                                    │ │
    │ │  - valve_driver_close(primary_valve)        │ │
    │ │  - Cambiar estado → IDLE                    │ │
    │ │  - Acumular runtime al contador diario      │ │
    │ │  - Limpiar mqtt_override_active             │ │
    │ │  - RETORNAR ESP_OK                          │ │
    │ └─────────────────────────────────────────────┘ │
    │                                                   │
    │ ┌─────────────────────────────────────────────┐ │
    │ │ SI START:                                   │ │
    │ │  - Validar safety_lock no activo            │ │
    │ │  - Validar timing (4h mínimo)               │ │
    │ │  - Validar max_daily (360 min)              │ │
    │ │  - Validar duración (< 120 min)             │ │
    │ │  - valve_driver_open(primary_valve)         │ │
    │ │  - Cambiar estado → ACTIVE                  │ │
    │ │  - Incrementar session_count                │ │
    │ │  - Reset timers watchdog                    │ │
    │ │  - Enviar webhook N8N                       │ │
    │ │  - RETORNAR ESP_OK                          │ │
    │ └─────────────────────────────────────────────┘ │
    └────────┬─────────────────────────────────────────┘
             │
             ▼
    ┌──────────────────────────────────────┐
    │ Paso 4: Abrir/cerrar válvula(s)      │
    │                                      │
    │ valve_driver_open()                  │
    │ valve_driver_close()                 │
    │ valve_driver_emergency_close_all()   │
    │                                      │
    │ (Cambia GPIO a HIGH/LOW)             │
    └────────┬─────────────────────────────┘
             │
             ▼
    ┌──────────────────────────────────────┐
    │ Paso 5: Cambiar estado interno       │
    │                                      │
    │ s_irrig_ctx.current_state            │
    │ s_irrig_ctx.safety_lock              │
    │ s_irrig_ctx.session_count            │
    │ s_irrig_ctx.is_valve_open            │
    │                                      │
    │ (Protegido con spinlock)             │
    └────────┬─────────────────────────────┘
             │
             ▼
    ┌──────────────────────────────────────┐
    │ Paso 6: Enviar notificación N8N      │
    │                                      │
    │ send_n8n_webhook()                   │
    │  - Evento (irrigation_on/off/etc)    │
    │  - Datos sensores                    │
    │  - Timestamp                         │
    │                                      │
    │ (HTTP POST asíncrono)                │
    └────────┬─────────────────────────────┘
             │
             ▼
    ┌──────────────────────────────────────┐
    │ Paso 7: RETORNAR RESULTADO           │
    │                                      │
    │ ESP_OK                               │
    │   → Comando ejecutado exitosamente   │
    │                                      │
    │ ESP_ERR_INVALID_STATE                │
    │   → Safety lock activo               │
    │   → No pasó intervalo mínimo         │
    │   → Límite diario alcanzado          │
    │                                      │
    │ ESP_ERR_INVALID_ARG                  │
    │   → Comando inválido                 │
    │   → Parámetro fuera de rango         │
    └────────────────────────────────────┘
```

---

## 🔗 CADENA DE LLAMADAS COMPLETA

### Nivel 1: Usuario/Aplicación
```bash
Usuario abre app móvil → Click "Iniciar Riego"
```

### Nivel 2: MQTT Broker (Cloud)
```
App móvil envía:
  Topic: irrigation/control/aabbccddeeff
  Payload: {"command": "start", "duration_minutes": 15}
```

### Nivel 3: MQTT Adapter (mqtt_adapter.c)
```c
// Línea 506 en mqtt_adapter.c
mqtt_handle_irrigation_command(event);
  └─> Parsea JSON payload
  └─> Extrae comando="start"
  └─> Extrae duration_minutes=15
  └─> Llama callback registrado
```

### Nivel 4: Callback Registrado
```c
// Debe ser registrado así (en main.c o donde inicialices):
mqtt_client_register_command_callback(
    irrigation_controller_execute_command,
    NULL  // user_data
);

// Cuando MQTT recibe mensaje:
s_mqtt_ctx.cmd_callback(
    IRRIGATION_CMD_START,
    15,  // duration
    NULL  // user_data
);
```

### Nivel 5: Tu Función (irrigation_controller_execute_command)
```c
esp_err_t irrigation_controller_execute_command(
    IRRIGATION_CMD_START,  // comando
    15                      // duración
)
{
    // Valida
    // Abre válvula
    // Cambia estado
    // Envía webhook
    // Retorna resultado
}
```

---

## 📋 SECUENCIA DE EVENTOS

| Evento | Componente | Acción |
|--------|-----------|--------|
| 1 | Usuario | Abre app, presiona "START" |
| 2 | App Móvil | Publica MQTT: `irrigation/control/MAC` |
| 3 | MQTT Broker | Rutea mensaje a ESP32 |
| 4 | MQTT Client | Recibe en callback `_handle_irrigation_command()` |
| 5 | MQTT Client | Parsea JSON, extrae comando |
| 6 | MQTT Callback | Llama `irrigation_controller_execute_command()` |
| 7 | **Irrigation Controller** | **← TÚ ERES AQUÍ** |
| 8 | Irrigation Controller | Valida seguridad |
| 9 | Irrigation Controller | Abre válvula (valve_driver) |
| 10 | GPIO Driver | Cambia GPIO HIGH → válvula abierta |
| 11 | Irrigation Controller | Cambia estado interno |
| 12 | Irrigation Controller | Envía webhook HTTP a N8N |
| 13 | N8N | Recibe evento, genera notificación |
| 14 | Usuario | Recibe notificación "Riego iniciado" |

---

## 🎯 ¿CÓMO ENTRA LA INSTRUCCIÓN?

### Opción 1: VÍA MQTT (Lo más común)
```
Usuario → App Móvil → MQTT Broker → MQTT Client → execute_command()
```

**Requisito**: Registrar callback en mqtt_client
```c
// En main.c o donde inicialices:
mqtt_client_register_command_callback(
    irrigation_controller_execute_command,
    NULL
);
```

### Opción 2: VÍA FUNCIÓN DIRECTA (Testing/Local)
```c
// En main.c o cualquier parte del código:
irrigation_controller_execute_command(IRRIGATION_CMD_START, 15);
```

### Opción 3: VÍA HTTP API (Futuro)
```
Crear endpoint HTTP: POST /api/irrigation/control
Parsea JSON
Llama execute_command()
```

---

## 📝 EJEMPLO: FLUJO COMPLETO EN CÓDIGO

```c
// ============ 1. SETUP (main.c) ============
void app_main() {
    // ... inicializaciones ...

    // Registrar callback MQTT
    mqtt_client_register_command_callback(
        irrigation_controller_execute_command,
        NULL
    );

    // Inicializar controller
    irrigation_controller_init(NULL);
}

// ============ 2. MENSAJE LLEGA (mqtt_adapter.c) ============
void mqtt_handle_irrigation_command(esp_mqtt_event_t* event) {
    // Topic: "irrigation/control/aabbccddeeff"
    // Payload: '{"command": "start", "duration_minutes": 15}'

    cJSON *json = cJSON_Parse((const char*)event->data);

    cJSON *cmd_item = cJSON_GetObjectItem(json, "command");
    irrigation_command_t command = IRRIGATION_CMD_START;

    cJSON *duration_item = cJSON_GetObjectItem(json, "duration_minutes");
    uint16_t duration = 15;

    // LLAMA TU FUNCIÓN AQUÍ
    s_mqtt_ctx.cmd_callback(command, duration, s_mqtt_ctx.cmd_callback_user_data);
}

// ============ 3. TU FUNCIÓN EJECUTA (irrigation_controller.c) ============
esp_err_t irrigation_controller_execute_command(
    irrigation_command_t command,
    uint16_t duration_minutes
) {
    ESP_LOGI(TAG, "Execute command: %d, duration: %d min", command, duration_minutes);

    // ... validaciones ...

    if (command == IRRIGATION_CMD_START) {
        // Abre válvula
        valve_driver_open(s_irrig_ctx.config.primary_valve);

        // Cambia estado
        portENTER_CRITICAL(&s_irrigation_spinlock);
        {
            s_irrig_ctx.is_valve_open = true;
            s_irrig_ctx.current_state = IRRIGATION_ACTIVE;
            s_irrig_ctx.session_count++;
        }
        portEXIT_CRITICAL(&s_irrigation_spinlock);

        // Envía webhook
        send_n8n_webhook("irrigation_on", soil_avg, humidity, temp);

        return ESP_OK;
    }

    return ESP_ERR_INVALID_ARG;
}

// ============ 4. RESULTADO ============
// Válvula se abre → agua fluye → sensores detectan cambio
// Dashboard se actualiza con notificación
// N8N recibe webhook y genera alertas
```

---

## 🔐 PROTECCIONES EN CADA PASO

| Paso | Protección | Detalles |
|------|-----------|----------|
| **Recepción** | JSON parsing | Valida estructura |
| **Validación 1** | Inicialización | ¿Está el sistema listo? |
| **Validación 2** | Safety lock | ¿Está bloqueado por emergencia? |
| **Validación 3** | Timing | ¿Han pasado 4h desde última sesión? |
| **Validación 4** | Límite diario | ¿Se alcanzó max 360 min/día? |
| **Validación 5** | Duración | ¿Es válida (<120 min)? |
| **Ejecución** | Spinlock | Acceso exclusivo a estado |
| **Post-ejecución** | Watchdog | Monitorea duración actual |

---

## 🚨 ERRORES POSIBLES Y RECUPERACIÓN

```c
// ESCENARIO 1: Safety lock activo
execute_command(START, 15)
→ ESP_ERR_INVALID_STATE
→ Usuario debe hacer unlock primero
→ irrigation_controller_unlock_safety()

// ESCENARIO 2: No pasó intervalo mínimo
execute_command(START, 15)
→ ESP_ERR_INVALID_STATE
→ Esperar 4h más
→ Sistema rechaza comando

// ESCENARIO 3: Límite diario alcanzado
execute_command(START, 15)
→ ESP_ERR_INVALID_STATE
→ Esperar a medianoche para reset
→ O usar reset_daily_stats() manualmente

// ESCENARIO 4: Comando inválido
execute_command(INVALID_COMMAND, 15)
→ ESP_ERR_INVALID_ARG
→ Revisar payload JSON
→ Enviar "start", "stop", o "emergency_stop"
```

---

## 🔄 ESTADO DURANTE EJECUCIÓN

```
ANTES:
  current_state: IRRIGATION_IDLE
  is_valve_open: false
  safety_lock: false

DURANTE START:
  Abre válvula GPIO → activa relay físico

DESPUÉS:
  current_state: IRRIGATION_ACTIVE
  is_valve_open: true
  session_start_time: <timestamp>
  active_valve_num: 1

DURANTE EJECUCIÓN:
  Task evaluación cada 60s monitorea:
    - Humedad del suelo (¿alcanzó target?)
    - Temperatura (¿>40°C?)
    - Duración (¿>120 min?)
    - Si cumple condición → auto-stop
```

---

## 📊 DIAGRAMA DE ESTADO

```
                    ┌─────────────┐
                    │ IDLE        │
                    │ Valve=OFF   │
                    └──────┬──────┘
                           │
              execute_command(START)
                           │
                           ▼
                    ┌─────────────┐
         ┌─────────→│ ACTIVE      │◄─────────┐
         │          │ Valve=ON    │          │
         │          └──────┬──────┘          │
         │                 │                 │
         │    Evaluation Task checks:        │
         │    ├─ soil >= 75% → STOP         │
         │    ├─ soil >= 80% → EMERGENCY    │
         │    ├─ temp > 40°C → THERMAL      │
         │    └─ duration > 120min → STOP   │
         │                 │                 │
         │                 ▼                 │
         │    execute_command(STOP)         │
         │                 │                 │
         └─────────────────┴─────────────────┘
                           │
                    ┌──────▼──────┐
                    │ IDLE        │
                    │ Valve=OFF   │
                    └─────────────┘
```

---

## 🎯 RESUMEN

**`irrigation_controller_execute_command()` es el punto de entrada para comandos MQTT**:

1. **QUIÉN LLAMA**: El cliente MQTT (`mqtt_adapter.c`) a través de callback registrado
2. **CÓMO RECIBE**: Via topic `irrigation/control/{MAC}` con JSON payload
3. **QUÉ HACE**: Valida, abre/cierra válvula, cambia estado, envía notificación
4. **RETORNA**: ESP_OK (éxito) o error code (validación falló)
5. **DESPUÉS**: Task de evaluación monitorea y auto-detiene si se cumplen condiciones

**Es el puente entre el mundo MQTT (nube) y el mundo físico (válvulas/GPIO)**.

