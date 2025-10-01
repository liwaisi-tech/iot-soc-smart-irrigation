Lógica de Riego Automático 
Requerimientos Técnicos Optimizados
 🎯 Límites Operacionales 
    - Condición de Parada: Riego se detiene cuando TODOS los sensores alcanzan 70-75% humedad
    - Límites de Seguridad: 2h máximo por sesión, 4h mínimo entre sesiones  
    - Protección Térmica: Auto-stop si T° > 40°C
    - Protección Sobre-riego: Auto-stop si ALGÚN SENSOR de humedad del suelo > 80% (humedad suelo óptima es 75%)

🌡️ Umbrales Basados en Dataset Real Colombia

    - Temperatura Crítica: 32°C (no 30°C)
    - Humedad Ambiente Óptima: 60% (no 50%)
    - Humedad Suelo Crítica: 30% (emergencia)
    - Humedad Suelo Óptima: 75% (parada normal)
    - Ventana Riego Nocturno: 00:00-06:00 (26-28°C, 73-78% HR)
    - Zona Estrés Térmico: 12:00-15:00 (32-34°C, 45-65% HR)

Especificaciones de Hardware
3.1 Sensores
Sensor Ambiental: Temperatura y humedad del aire (DHT22)
Sensores de Suelo: 1-3 sensores de humedad del suelo (capacitivos)
Actuador: Electroválvula tipo lanch de 4.7 v controlada  por 2 relay module con 2 canales 
Pinpout:
1.GND: ground 
2.IN1: high(pulso)
3.IN2: high(pulso) 
4.VCC: 5V DC 
5.Outputs: K1 y K2
3.2 Conexiones GPIO 
 pines seleccionados para sensores (ver config de hardware)
Pin de control para relé de electroválvula por seleccionar con caracteristicas de pines in del rele
Configuración de alimentación

Configuración Hardware ESP32

GPIO Assignments Optimizados para que no use pines de sujeción GPIO0, GPIO2, GPIO5, GPIO12, and GPIO15 modificar los pines de los perifericos por unos que no sean de sujecion. 
 
    #define IRRIGATION_VALVE_1_GPIO     GPIO_NUM_21??   // Relé válvula 1  
    #define SOIL_MOISTURE_1_ADC         ADC1_CHANNEL_0  // GPIO 36
    #define SOIL_MOISTURE_2_ADC         ADC1_CHANNEL_3  // GPIO 39  
    #define SOIL_MOISTURE_3_ADC         ADC1_CHANNEL_6  // GPIO 34
    #define STATUS_LED_GPIO             GPIO_NUM_18  // LED indicador estado

    // Task Priorities Críticas
    #define IRRIGATION_TASK_PRIORITY    4    // Crítica - control riego
    #define SENSOR_TASK_PRIORITY        3    // Actual sensor task  
    #define SAFETY_WATCHDOG_PRIORITY    5    // Máxima - watchdog seguridad

    🔄 Modos de Operación Adaptativos
    Online (WiFi/MQTT activo):
    - Sensado: 60 segundos
    - Evaluación: Continua
    - Envío datos: 60 segundos
    Offline Adaptativo (Sin conectividad):
    - Normal: Sensado cada 2 horas
    - Warning: Sensado cada 1 hora (humedad bajando)
    - Critical: Sensado cada 30 min (condición crítica)
    - Emergency: Sensado cada 15 min (supervivencia cultivo)

    📊 Optimizaciones Memoria ESP32

    - Uso RAM Objetivo: <150KB (de 200KB disponibles)
    - Estructuras Compactas: 16 bytes por lectura (vs 32 bytes)
    - Deep Sleep: 80% tiempo en modo offline
    - Buffers Circulares: 48 horas historial sensor
Modo Offline

8.1 Condiciones de Activación
Requisito: Sin conectividad WiFi/MQTT
Trigger: Valores de humedad del suelo por debajo de umbral configurable
Evaluación: Promedio de sensores de suelo disponibles
8.2 Parámetros
Umbral mínimo de humedad
Tiempo máximo de riego
Intervalo entre evaluaciones
Lógica de failsafe