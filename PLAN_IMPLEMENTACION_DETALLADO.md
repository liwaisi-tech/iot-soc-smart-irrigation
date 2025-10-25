# PLAN DETALLADO DE IMPLEMENTACIÓN - PHASE 5 (Continuación)

**Resumen Ejecutivo**: Completar 5 funciones faltantes en `irrigation_controller` con 1 nuevo driver (`offline_mode_driver`)

---

## 📍 UBICACIÓN DE CADA IMPLEMENTACIÓN

### 1. `irrigation_controller_execute_command()`
**Archivo**: `components/irrigation_controller/irrigation_controller.c` (nueva función)
**Línea aprox**: Después de `irrigation_state_thermal_stop_handler()` (línea 520)
**Complejidad**: ⭐⭐ (Media)
**Dependencias**: valve_driver, safety_watchdog

```c
esp_err_t irrigation_controller_execute_command(irrigation_command_t command,
                                                uint16_t duration_minutes)
{
    // 1. Validar entrada
    // 2. Proteger con spinlock
    // 3. Ejecutar según comando
    // 4. Cambiar estado
    // 5. Loguear
}
```

**Validaciones de Seguridad**:
- ✅ Verificar safety_lock (si EMERGENCY_STOP está activo)
- ✅ Verificar next_allowed_session (timing entre sesiones)
- ✅ Verificar max_daily_minutes (límite diario)
- ✅ Verificar estado actual compatible (ej: no START si ya ACTIVE)

**Lógica por Comando**:
```
START:
  - Validar seguridad ✓
  - Abrir válvula → valve_driver_open()
  - Cambiar estado → IRRIGATION_ACTIVE
  - Registrar MQTT_OVERRIDE_ACTIVE

STOP:
  - Cerrar válvula → valve_driver_close()
  - Cambiar estado → IRRIGATION_IDLE
  - Guardar duración sesión

EMERGENCY_STOP:
  - Cerrar TODAS válvulas → valve_driver_emergency_close_all()
  - Activar safety_lock = true
  - Cambiar estado → IRRIGATION_EMERGENCY_STOP
  - Loguear crítico
```

---

### 2. `irrigation_controller_evaluate_and_act()`
**Archivo**: `components/irrigation_controller/irrigation_controller.c` (nueva función)
**Línea aprox**: Después de `execute_command()` (línea 650+)
**Complejidad**: ⭐⭐⭐ (Alta - lógica condicional compleja)
**Dependencias**: offline_mode_driver, safety_watchdog, valve_driver

```c
esp_err_t irrigation_controller_evaluate_and_act(const soil_data_t* soil_data,
                                                 const ambient_data_t* ambient_data,
                                                 irrigation_evaluation_t* evaluation)
{
    // 1. Validar entrada
    // 2. Calcular promedio de sensores
    // 3. Evaluar thresholds
    // 4. Si ONLINE: solo recomendar (guardar en last_eval)
    // 5. Si OFFLINE: ejecutar automáticamente
    // 6. Retornar resultado
}
```

**Diferencia Clave ONLINE vs OFFLINE**:

```
┌─────────────────────────────────────┐
│  Datos de Sensores Ingresados       │
└─────────────┬───────────────────────┘
              │
         ┌────▼──────────────┐
         │ Evaluar Thresholds│
         └────┬──────────────┘
              │
       ┌──────▼──────────┐
       │ ¿Está ONLINE?   │
       └──┬──────────┬───┘
          │          │
        SÍ│          │NO
    ┌─────▼─┐  ┌──────────────┐
    │RECOMEN-│  │ ¿Offline OK? │
    │DAR     │  └──┬──────────┬┘
    │(guardar│     │          │
    │ solo)  │   SÍ│          │NO
    └────────┘ ┌───▼─┐  ┌──────────┐
               │ACTUAR│  │Mantenerse│
               │AUTO-└──►(conservador)
               │MÁTICO  │
               └────────┘
```

**Cálculos Necesarios**:
```c
// Entrada: arrays de 3 sensores
soil_avg = (soil[0] + soil[1] + soil[2]) / 3.0f;
soil_max = max(soil[0], soil[1], soil[2]);
soil_min = min(soil[0], soil[1], soil[2]);

// Evaluación
if (soil_max >= soil_threshold_max) {
    → DECISION_STOP "over_moisture"
} else if (soil_avg < soil_threshold_critical) {
    → DECISION_START "dry_soil"
} else if (soil_avg >= soil_threshold_optimal) {
    → DECISION_CONTINUE o DECISION_STOP según estado
}
```

---

### 3. NUEVO DRIVER: `offline_mode_driver`
**Archivos**:
- `components/irrigation_controller/drivers/offline_mode/offline_mode_driver.h` (CREAR)
- `components/irrigation_controller/drivers/offline_mode/offline_mode_driver.c` (CREAR)

**Complejidad**: ⭐⭐ (Media)
**Responsabilidad ÚNICA**: Calcular nivel offline y próximo intervalo

**Estructura**:
```c
// offline_mode_driver.h
typedef enum {
    OFFLINE_LEVEL_NORMAL = 0,        // 2h (soil 45-75%)
    OFFLINE_LEVEL_WARNING,           // 1h (soil 40-45%)
    OFFLINE_LEVEL_CRITICAL,          // 30min (soil 30-40%)
    OFFLINE_LEVEL_EMERGENCY          // 15min (soil <30%)
} offline_level_t;

typedef struct {
    offline_level_t level;
    uint32_t interval_ms;
    const char* reason;
} offline_evaluation_t;
```

**API Pública**:
```c
esp_err_t offline_mode_init(void);
offline_evaluation_t offline_mode_evaluate(float soil_humidity_avg);
uint32_t offline_mode_get_interval_ms(offline_level_t level);
offline_level_t offline_mode_get_current_level(void);
esp_err_t offline_mode_deinit(void);
```

**Tabla de Decisión** (usando Kconfig):
```
Soil Humidity          Level          Interval    Razón
───────────────────────────────────────────────────────
45% - 75%              NORMAL         2 horas     Humedad normal
40% - 45%              WARNING        1 hora      Humedad baja
30% - 40%              CRITICAL       30 minutos  Humedad muy baja
< 30%                  EMERGENCY      15 minutos  Humedad crítica
```

**Integración con irrigation_evaluation_task**:
```c
// En irrigation_evaluation_task()
if (!is_online) {
    offline_evaluation_t eval = offline_mode_evaluate(soil_avg);
    s_irrig_ctx.offline_level = eval.level;
    eval_interval_ms = eval.interval_ms;  // 2h, 1h, 30min, o 15min
}
```

---

### 4. FUNCIONES DE ESTADÍSTICAS
**Archivo**: `components/irrigation_controller/irrigation_controller.c` (nuevas funciones)
**Línea aprox**: Después de `get_status()` (línea 730+)
**Complejidad**: ⭐ (Baja)
**Dependencias**: device_config (NVS), spinlock

```c
// Nueva función 1
esp_err_t irrigation_controller_get_stats(irrigation_stats_t* stats)
{
    // 1. Validar entrada
    // 2. Proteger con spinlock
    // 3. Copiar desde s_irrig_ctx.stats
    // 4. Retornar
}

// Nueva función 2
esp_err_t irrigation_controller_reset_daily_stats(void)
{
    // 1. Proteger con spinlock
    // 2. Resetear s_irrig_ctx.total_runtime_today_sec = 0
    // 3. Guardar en NVS
    // 4. Loguear
}
```

**Qué Persistir en NVS**:
```
Namespace: "irrig_cfg"

Key                          Type        Cuándo Actualizar
─────────────────────────────────────────────────────────
IRRIGATION_NVS_TOTAL_SESSIONS    u32    Cada sesión cerrada
IRRIGATION_NVS_TOTAL_RUNTIME     u32    Cada sesión cerrada (+duración)
IRRIGATION_NVS_TODAY_RUNTIME     u32    Cada sesión cerrada (+duración)
IRRIGATION_NVS_EMERGENCY_STOPS   u32    Cuando emergency_stop ejecutado
IRRIGATION_NVS_THERMAL_STOPS     u32    Cuando temperatura crítica
IRRIGATION_NVS_LAST_SESSION_TIME u32    Cuando sesión cerrada
```

**Estrategia de Escritura**:
```c
// NO escribir en cada cierre (lentísimo)
// BATCHING: Escribir cada 10 sesiones (o cada hora)

static uint32_t s_pending_writes = 0;

[Al cerrar sesión]
s_irrig_ctx.session_count++;
s_irrig_ctx.total_runtime_today_sec += duration;
s_pending_writes++;

if (s_pending_writes >= 10) {
    device_config_set_u32("irrig_cfg", IRRIGATION_NVS_TOTAL_SESSIONS, s_irrig_ctx.session_count);
    s_pending_writes = 0;
}
```

---

### 5. INTEGRACIÓN CON MQTT
**Archivo**: Modificación de `mqtt_adapter.c` (línea +400 aprox)
**Complejidad**: ⭐ (Baja - usar callback existente)

**Cambios Necesarios**:
1. En `mqtt_client_manager.h`: Callback ya existe ✅
2. En `mqtt_adapter.c`: En handler de mensaje MQTT
   ```c
   // Cuando se recibe mensaje en topic "irrigation/control/{MAC}"
   if (strstr(topic, "irrigation/control") != NULL) {
       if (strcmp(payload, "start") == 0) {
           irrigation_controller_execute_command(IRRIGATION_COMMAND_START, 15);
       } else if (strcmp(payload, "stop") == 0) {
           irrigation_controller_execute_command(IRRIGATION_COMMAND_STOP, 0);
       } else if (strcmp(payload, "emergency_stop") == 0) {
           irrigation_controller_execute_command(IRRIGATION_COMMAND_EMERGENCY_STOP, 0);
       }
   }
   ```

---

## 📐 DIAGRAMA DE LLAMADAS

```
main.c
  │
  ├─► irrigation_controller_init()
  │    ├─► valve_driver_init()
  │    ├─► safety_watchdog_init()
  │    ├─► offline_mode_init() ◄─── NUEVO
  │    └─► xTaskCreatePinnedToCore(irrigation_evaluation_task)
  │
  └─► [task cada 60s]
       │
       ├─► sensor_reader_get_all()
       ├─► wifi_manager_is_connected()
       │
       ├─► if (offline) {
       │    └─► offline_mode_evaluate() ◄─── NUEVO
       │   }
       │
       ├─► switch(current_state) {
       │    ├─ case IDLE: irrigation_state_idle_handler()
       │    ├─ case ACTIVE: irrigation_state_running_handler()
       │    │    └─► safety_watchdog_check() ← uses alerts
       │    │    └─► valve_driver_close() ← on stop
       │    └─ case THERMAL: ...
       │   }
       │
       └─► vTaskDelay()

[MQTT Message Received]
  │
  └─► mqtt_message_callback()
      └─► irrigation_controller_execute_command() ◄─── NUEVO
          └─► valve_driver_open/close()
```

---

## ✅ CHECKLIST DE VALIDACIÓN

### Antes de Empezar
- [ ] Revisar este documento
- [ ] Validar plan con arquitecto
- [ ] Crear branches/commits

### Fase offline_mode_driver
- [ ] Header completo con tipos
- [ ] Implementación de evaluate()
- [ ] Testing de niveles (4 casos)
- [ ] Spinlock/mutex correcto
- [ ] Sin memory leaks

### Fase execute_command()
- [ ] Validación de comando
- [ ] Lógica START/STOP/EMERGENCY
- [ ] Spinlock en cambios de estado
- [ ] Logging en cada rama
- [ ] Testing MQTT integration

### Fase evaluate_and_act()
- [ ] Cálculo de promedio sensores
- [ ] Evaluación thresholds
- [ ] ONLINE vs OFFLINE paths
- [ ] Offline_mode_driver integration
- [ ] Guardar last_evaluation

### Fase Estadísticas
- [ ] NVS persistence
- [ ] Reset diario
- [ ] Batching de escrituras
- [ ] Sin data corruption

### Integración MQTT
- [ ] Callback registrado
- [ ] Topic correcto
- [ ] Payload parsing
- [ ] Error handling

### Testing Final
- [ ] Sensor → Evaluación → Acción (ciclo completo)
- [ ] Modo ONLINE (solo recomendación)
- [ ] Modo OFFLINE (4 niveles)
- [ ] Seguridad (timeouts, temp)
- [ ] MQTT override
- [ ] Estadísticas persistidas

---

## 📊 ESTIMACIÓN DE ESFUERZO

| Tarea | Líneas Aprox | Tiempo | Complejidad |
|-------|--------------|--------|------------|
| offline_mode_driver (nuevo driver) | 200-300 | 1.5h | ⭐⭐ |
| execute_command() | 80-100 | 1h | ⭐⭐ |
| evaluate_and_act() | 120-150 | 1.5h | ⭐⭐⭐ |
| Estadísticas + NVS | 60-80 | 1h | ⭐ |
| MQTT Integration | 20-30 | 0.5h | ⭐ |
| Testing + Documentación | - | 1h | ⭐⭐ |
| **TOTAL** | **500-700** | **6-7h** | **⭐⭐⭐** |

---

## 🚀 PRÓXIMOS PASOS

1. **Validar Plan**: Usuario revisa documento
2. **Crear Estructura**: Crear archivos/headers
3. **Implementar Fase por Fase**: Siguiente en prioridad
4. **Testing**: Validar cada implementación
5. **Documentar**: Actualizar CLAUDE.md + commit

---

