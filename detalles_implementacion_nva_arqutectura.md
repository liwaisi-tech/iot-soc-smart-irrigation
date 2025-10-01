# Arquitectura por Componentes ESP32 - Sistema Riego Inteligente

## 🎯 ARQUITECTURA RECOMENDADA: Component-Based para ESP32

### Principios Arquitecturales Aplicables

#### **1. Single Responsibility Component (SRC)**
```c
// Cada componente tiene UNA responsabilidad específica
components/
├── wifi_manager/          // Solo conectividad WiFi
├── mqtt_client/           // Solo comunicación MQTT  
├── sensor_reader/         // Solo lectura de sensores
├── irrigation_controller/ // Solo control de riego
├── device_config/         // Solo configuración del dispositivo
└── system_monitor/        // Solo monitoreo del sistema
```

#### **2. Minimal Interface Segregation (MIS)**
```c
// Interfaces mínimas y específicas, no genéricas
typedef struct {
    esp_err_t (*read_soil_humidity)(float* values, size_t count);
    esp_err_t (*read_ambient_data)(float* temp, float* hum);
} sensor_interface_t;

typedef struct {
    esp_err_t (*start_irrigation)(uint16_t duration_minutes);
    esp_err_t (*stop_irrigation)(void);
    bool (*is_irrigating)(void);
} irrigation_interface_t;
```

#### **3. Direct Dependencies (DD)**
```c
// Dependencias directas sin abstracción excesiva
#include "sensor_reader.h"
#include "irrigation_controller.h"
#include "mqtt_client.h"

// En lugar de múltiples capas de abstracción
void irrigation_task(void* params) {
    float soil_values[3];
    if (sensor_reader_get_soil_humidity(soil_values, 3) == ESP_OK) {
        if (should_irrigate(soil_values)) {
            irrigation_controller_start(15); // 15 minutos
        }
    }
}
```

---

## 🏗️ ESTRUCTURA DE COMPONENTES PROPUESTA

### **Estructura del Proyecto**
```
smart_irrigation_system/
├── components/
│   ├── wifi_manager/          # Conectividad WiFi + reconexión
│   │   ├── wifi_manager.h
│   │   ├── wifi_manager.c
│   │   └── CMakeLists.txt
│   │
│   ├── mqtt_client/           # Cliente MQTT + WebSockets
│   │   ├── mqtt_client.h
│   │   ├── mqtt_client.c
│   │   └── CMakeLists.txt
│   │
│   ├── sensor_reader/         # Lectura de todos los sensores
│   │   ├── sensor_reader.h    # Interface pública
│   │   ├── sensor_reader.c    # Lógica principal
│   │   ├── dht22_driver.c     # Driver DHT22
│   │   ├── soil_adc_driver.c  # Driver sensores suelo
│   │   └── CMakeLists.txt
│   │
│   ├── irrigation_controller/ # Control de riego + seguridad
│   │   ├── irrigation_controller.h
│   │   ├── irrigation_controller.c
│   │   ├── valve_driver.c     # Driver válvula GPIO
│   │   ├── safety_logic.c     # Lógica de seguridad
│   │   └── CMakeLists.txt
│   │
│   ├── device_config/         # Configuración + NVS
│   │   ├── device_config.h
│   │   ├── device_config.c
│   │   └── CMakeLists.txt
│   │
│   ├── system_monitor/        # Monitoreo + health check
│   │   ├── system_monitor.h
│   │   ├── system_monitor.c
│   │   └── CMakeLists.txt
│   │
│   └── http_server/           # Servidor HTTP + endpoints
│       ├── http_server.h
│       ├── http_server.c
│       └── CMakeLists.txt
│
├── main/
│   ├── main.c                 # Orquestación principal
│   ├── app_config.h           # Configuración global
│   └── CMakeLists.txt
│
└── include/
    ├── common_types.h         # Tipos comunes del sistema
    └── error_codes.h          # Códigos de error específicos
```

---

## 🔧 IMPLEMENTACIÓN DE COMPONENTES

### **1. Tipos Comunes del Sistema**
```c
// include/common_types.h
typedef struct {
    float temperature;         // °C
    float humidity;           // %
    uint32_t timestamp;
} ambient_data_t;

typedef struct {
    float soil_humidity[3];   // % humedad suelo
    uint8_t sensor_count;     // Sensores activos
    uint32_t timestamp;
} soil_data_t;

typedef struct {
    ambient_data_t ambient;
    soil_data_t soil;
    char device_mac[18];
    char device_ip[16];
} sensor_reading_t;

typedef enum {
    IRRIGATION_IDLE,
    IRRIGATION_ACTIVE,
    IRRIGATION_ERROR,
    IRRIGATION_EMERGENCY_STOP
} irrigation_state_t;
```

### **2. Sensor Reader Component**
```c
// components/sensor_reader/sensor_reader.h
#ifndef SENSOR_READER_H
#define SENSOR_READER_H

#include "common_types.h"
#include "esp_err.h"

// Configuración del componente
typedef struct {
    uint8_t soil_sensor_count;        // 1-3 sensores
    uint16_t reading_interval_ms;     // Intervalo lectura
    bool enable_filtering;            // Filtrado de lecturas
} sensor_config_t;

// API pública
esp_err_t sensor_reader_init(const sensor_config_t* config);
esp_err_t sensor_reader_get_ambient(ambient_data_t* data);
esp_err_t sensor_reader_get_soil(soil_data_t* data);
esp_err_t sensor_reader_get_all(sensor_reading_t* reading);
void sensor_reader_deinit(void);

// Health check
bool sensor_reader_is_healthy(void);
uint32_t sensor_reader_get_error_count(void);

#endif
```

### **3. Irrigation Controller Component**
```c
// components/irrigation_controller/irrigation_controller.h
#ifndef IRRIGATION_CONTROLLER_H
#define IRRIGATION_CONTROLLER_H

#include "common_types.h"
#include "esp_err.h"

// Configuración de riego
typedef struct {
    float soil_threshold_percent;     // 45% por defecto
    uint16_t max_duration_minutes;    // 30 min máximo
    uint16_t default_duration_minutes; // 15 min por defecto
    bool enable_offline_mode;         // Modo offline
    uint16_t offline_activation_seconds; // 300s para activar offline
} irrigation_config_t;

// Comandos de riego
typedef enum {
    IRRIGATION_CMD_START,
    IRRIGATION_CMD_STOP,
    IRRIGATION_CMD_EMERGENCY_STOP
} irrigation_command_t;

// API pública
esp_err_t irrigation_controller_init(const irrigation_config_t* config);
esp_err_t irrigation_controller_execute_command(irrigation_command_t cmd, uint16_t duration_minutes);
esp_err_t irrigation_controller_evaluate_soil(const soil_data_t* soil_data);
irrigation_state_t irrigation_controller_get_state(void);
void irrigation_controller_deinit(void);

// Modo offline
esp_err_t irrigation_controller_enable_offline_mode(bool enable);
bool irrigation_controller_is_offline_active(void);

#endif
```

### **4. Main Application Orchestration**
```c
// main/main.c - Orquestación simplificada
#include "wifi_manager.h"
#include "mqtt_client.h"
#include "sensor_reader.h"
#include "irrigation_controller.h"
#include "device_config.h"
#include "system_monitor.h"
#include "http_server.h"

void app_main(void) {
    ESP_LOGI(TAG, "Iniciando Sistema de Riego Inteligente v1.4.0");
    
    // 1. Inicialización de configuración
    device_config_init();
    
    // 2. Inicialización de componentes básicos
    sensor_reader_config_t sensor_cfg = {
        .soil_sensor_count = 3,
        .reading_interval_ms = 30000,
        .enable_filtering = true
    };
    sensor_reader_init(&sensor_cfg);
    
    irrigation_config_t irrigation_cfg = {
        .soil_threshold_percent = 45.0,
        .max_duration_minutes = 30,
        .default_duration_minutes = 15,
        .enable_offline_mode = true,
        .offline_activation_seconds = 300
    };
    irrigation_controller_init(&irrigation_cfg);
    
    // 3. Conectividad
    wifi_manager_init();
    mqtt_client_init();
    http_server_init();
    
    // 4. Monitoreo del sistema
    system_monitor_init();
    
    // 5. Creación de tareas principales
    xTaskCreate(sensor_reading_task, "sensor_task", 4096, NULL, 5, NULL);
    xTaskCreate(irrigation_evaluation_task, "irrigation_task", 4096, NULL, 4, NULL);
    xTaskCreate(mqtt_communication_task, "mqtt_task", 6144, NULL, 3, NULL);
    xTaskCreate(system_health_task, "health_task", 2048, NULL, 2, NULL);
    
    ESP_LOGI(TAG, "Sistema iniciado correctamente");
}

// Tarea de lectura de sensores
void sensor_reading_task(void* params) {
    sensor_reading_t reading;
    TickType_t last_reading = 0;
    const TickType_t reading_interval = pdMS_TO_TICKS(30000); // 30 segundos
    
    while (1) {
        if (xTaskGetTickCount() - last_reading >= reading_interval) {
            if (sensor_reader_get_all(&reading) == ESP_OK) {
                // Enviar datos vía MQTT
                mqtt_client_publish_sensor_data(&reading);
                
                // Evaluar necesidad de riego
                irrigation_controller_evaluate_soil(&reading.soil);
            }
            last_reading = xTaskGetTickCount();
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // Check cada segundo
    }
}
```

---

## ⚡ PRINCIPIOS ESPECÍFICOS PARA ESP32

### **1. Memory-First Design**
```c
// Usar arrays estáticos en lugar de malloc
#define MAX_SENSOR_HISTORY 10
static sensor_reading_t sensor_history[MAX_SENSOR_HISTORY];
static uint8_t history_index = 0;

// Evitar allocación dinámica en tasks críticas
void sensor_reading_task(void* params) {
    sensor_reading_t reading; // Stack allocation
    // ... usar reading sin malloc
}
```

### **2. Task-Oriented Architecture**
```c
// Cada tarea tiene responsabilidad específica y stack optimizado
#define SENSOR_TASK_STACK_SIZE    4096  // Lectura sensores
#define IRRIGATION_TASK_STACK_SIZE 4096  // Control riego
#define MQTT_TASK_STACK_SIZE      6144  // Comunicación MQTT
#define HEALTH_TASK_STACK_SIZE    2048  // Monitoreo sistema
```

### **3. Error Handling Consistent**
```c
// Códigos de error específicos del sistema
typedef enum {
    IRRIGATION_SUCCESS = 0,
    IRRIGATION_ERROR_SENSOR_FAIL = 0x1001,
    IRRIGATION_ERROR_VALVE_STUCK = 0x1002,
    IRRIGATION_ERROR_OVER_TEMP = 0x1003,
    IRRIGATION_ERROR_LOW_MEMORY = 0x1004
} irrigation_error_t;
```

### **4. Configuration Management**
```c
// Configuración centralizada en NVS
typedef struct {
    // WiFi
    char wifi_ssid[32];
    char wifi_password[64];
    
    // MQTT
    char mqtt_broker[128];
    uint16_t mqtt_port;
    
    // Device
    char device_name[32];
    char crop_name[16];
    
    // Irrigation
    float soil_threshold;
    uint16_t irrigation_duration;
    
    // Sensores
    uint8_t soil_sensor_count;
    uint16_t reading_interval;
} device_configuration_t;
```

---

## 🔄 PLAN DE MIGRACIÓN

### **Fase 1: Preparación (2-3 horas)**
1. **Crear estructura de componentes** nueva
2. **Extraer lógica de negocio** actual del código hexagonal
3. **Identificar dependencias reales** entre módulos

### **Fase 2: Migración Core (4-6 horas)**
1. **Migrar sensor_reader** - consolidar lectura de sensores
2. **Migrar irrigation_controller** - simplificar lógica de riego
3. **Migrar mqtt_client** - mantener funcionalidad actual
4. **Crear main.c** con orquestación simplificada

### **Fase 3: Testing y Optimización (2-3 horas)**
1. **Compilar y resolver errores**
2. **Testing funcional** básico
3. **Medición de memoria** y optimización
4. **Validación con hardware**

**TIEMPO TOTAL ESTIMADO**: 8-12 horas (vs semanas con hexagonal)

---