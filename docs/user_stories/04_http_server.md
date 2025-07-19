# User Story: Servidor HTTP Básico

## Historia de Usuario
**Como** desarrollador del sistema IoT ESP32  
**Quiero** implementar un servidor HTTP básico funcional  
**Para** exponer endpoints REST que permitan consultar información del dispositivo y sensores

## Criterios de Aceptación

### Funcionales
- [ ] El servidor HTTP debe inicializar correctamente en el puerto 80
- [ ] Debe responder al endpoint `/whoami` con información del dispositivo en formato JSON
- [ ] Debe manejar errores HTTP estándar (404, 405, 500) con respuestas JSON consistentes
- [ ] Debe funcionar independientemente del estado de conectividad WiFi/MQTT
- [ ] Debe registrar todas las peticiones HTTP en consola para debugging

### No Funcionales
- [ ] El servidor debe seguir la arquitectura hexagonal existente
- [ ] Debe usar configuración mínima de ESP-IDF por defecto
- [ ] Debe ser extensible para futuros endpoints
- [ ] Debe tener documentación para desarrolladores

## Contexto Técnico

### Arquitectura
Este componente se integra en la arquitectura hexagonal existente como parte de la capa de infraestructura:

```
components/infrastructure/adapters/
├── http_adapter.h/.c         # Interfaz principal del adaptador HTTP
└── http/                     # Implementaciones específicas HTTP
    ├── server.h/.c           # Núcleo del servidor HTTP
    ├── endpoints/            # Directorio para endpoints futuros
    └── middleware/           # Directorio para middleware futuro
```

### Tecnologías
- **Framework**: ESP-IDF
- **Componente**: `esp_http_server`
- **Puerto**: 80 (hardcodeado para MVP)
- **Formato de respuesta**: JSON con headers mínimos

## Subtareas Refinadas

### 1. Configurar componente HTTP server de ESP-IDF
**Descripción**: Configurar el componente `esp_http_server` estándar de ESP-IDF para el proyecto.

**Tareas específicas**:
- Agregar dependencia `esp_http_server` en CMakeLists.txt del proyecto
- Incluir headers necesarios en los archivos correspondientes
- Usar configuración por defecto de ESP-IDF (sin menuconfig personalizado)
- Verificar que el componente compile correctamente

**Criterios de completitud**:
- El proyecto compila sin errores con el componente HTTP server incluido
- Headers de `esp_http_server.h` están disponibles en el código

---

### 2. Crear estructura básica del servidor HTTP
**Descripción**: Establecer la estructura de archivos y directorios siguiendo la arquitectura hexagonal.

**Estructura a crear**:
```
components/infrastructure/adapters/
├── http_adapter.h            # Interfaz del adaptador (puerto)
├── http_adapter.c            # Implementación del adaptador
└── http/
    ├── server.h              # Interfaz del servidor HTTP
    ├── server.c              # Implementación del servidor HTTP
    ├── endpoints/            # Directorio vacío para futuros endpoints
    └── middleware/           # Directorio vacío para futuro middleware
```

**Contenido inicial**:
- `http_adapter.h`: Función de inicialización y definiciones públicas
- `http_adapter.c`: Implementación que delega al servidor interno
- `server.h`: Estructuras y funciones del servidor HTTP
- `server.c`: Implementación básica del servidor (sin handlers aún)

**Criterios de completitud**:
- Todos los archivos y directorios creados según estructura definida
- Archivos header tienen guardas de inclusión apropiadas
- Estructura compila sin errores

---

### 3. Implementar inicialización y configuración del servidor
**Descripción**: Implementar la lógica de inicialización del servidor HTTP que sigue el patrón de la arquitectura hexagonal.

**Flujo de inicialización**:
1. `main.c` llama a `http_adapter_init()`
2. `http_adapter_init()` llama internamente a `http_server_start()`
3. `http_server_start()` configura e inicia el servidor ESP-IDF
4. Retorna código de error si falla, éxito si funciona

**Funciones a implementar**:
```c
// En http_adapter.h
esp_err_t http_adapter_init(void);
esp_err_t http_adapter_stop(void);

// En server.h
esp_err_t http_server_start(void);
esp_err_t http_server_stop(void);
```

**Manejo de errores**:
- Usar códigos de error estándar de ESP-IDF
- Bloquear y retornar error si la inicialización falla
- No implementar reintentos (dejar esa responsabilidad a main.c)

**Criterios de completitud**:
- Función de inicialización retorna `ESP_OK` en éxito
- Función de inicialización retorna código de error apropiado en falla
- No hay memory leaks en caso de error durante inicialización

---

### 4. Configurar puerto HTTP (típicamente 80)
**Descripción**: Configurar el servidor para usar puerto 80 hardcodeado en el código.

**Implementación**:
```c
// En server.c
httpd_config_t config = HTTPD_DEFAULT_CONFIG();
config.server_port = 80;  // Hardcodeado para MVP
```

**Consideraciones**:
- Puerto 80 directamente en el código (no usar constantes por ahora)
- Usar configuración por defecto de ESP-IDF para otros parámetros
- Agregar comentario indicando que será configurable en futuras versiones

**Criterios de completitud**:
- Servidor inicia correctamente en puerto 80
- Puerto está hardcodeado directamente en el código de inicialización

---

### 5. Crear manejo básico de requests y responses
**Descripción**: Implementar el manejo mínimo de peticiones y respuestas HTTP necesario para soportar el endpoint `/whoami`.

**Funcionalidad a implementar**:
- Registro de handler para ruta específica
- Estructura básica de handler function
- Extracción básica de método HTTP
- Envío de respuesta HTTP básica

**Funciones base**:
```c
// Tipo de handler function
typedef esp_err_t (*http_handler_func_t)(httpd_req_t *req);

// Registro de handler
esp_err_t http_server_register_handler(const char* uri, httpd_method_t method, http_handler_func_t handler);

// Helpers para respuesta
esp_err_t http_response_send_json(httpd_req_t *req, const char* json_string);
```

**Limitaciones del MVP**:
- Solo soporte para método GET
- Solo parsing básico de URI (exacta match)
- No parsing de parámetros de query
- No parsing de body de request

**Criterios de completitud**:
- Se puede registrar un handler para una ruta específica
- Handler puede enviar respuesta JSON básica
- Sistema funciona específicamente para `/whoami` con método GET

---

### 6. Implementar headers HTTP apropiados para JSON
**Descripción**: Configurar headers HTTP mínimos para respuestas JSON.

**Headers a implementar**:
- `Content-Type: application/json` (obligatorio)

**Implementación**:
```c
esp_err_t http_response_send_json(httpd_req_t *req, const char* json_string) {
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json_string, strlen(json_string));
}
```

**No incluir** (para mantener MVP simple):
- Headers de CORS
- Cache-Control headers
- Content-Length (manejado automáticamente por ESP-IDF)
- Server identification headers

**Criterios de completitud**:
- Respuestas JSON incluyen `Content-Type: application/json`
- Header se configura automáticamente en helper function

---

### 7. Crear middleware para logging de requests
**Descripción**: Implementar logging básico de peticiones HTTP usando ESP_LOGI.

**Información a loggear**:
- Método HTTP (GET, POST, etc.)
- URI solicitada
- Timestamp básico (proporcionado por ESP-IDF)

**Implementación**:
```c
void http_middleware_log_request(httpd_req_t *req) {
    ESP_LOGI("HTTP", "%s %s", http_method_str(req->method), req->uri);
}
```

**Integración**:
- Llamar logging al inicio de cada handler
- Usar tag "HTTP" para los logs
- Usar nivel INFO de ESP-IDF logging

**No implementar** (para MVP):
- Logging de headers de request
- Logging de response status
- Logging de client IP
- Logging de tiempo de respuesta

**Criterios de completitud**:
- Cada petición HTTP genera log en consola
- Log incluye método y URI
- Log usa formato consistente

---

### 8. Implementar manejo de errores HTTP (404, 500, etc.)
**Descripción**: Implementar respuestas de error estándar HTTP con formato JSON consistente.

**Errores a manejar**:
- **404 Not Found**: Cuando se solicita una ruta no registrada
- **405 Method Not Allowed**: Cuando se usa método no soportado en ruta existente
- **500 Internal Server Error**: Cuando ocurre error interno del servidor

**Formato de respuesta de error**:
```json
{
  "error": {
    "code": 404,
    "message": "Not Found",
    "description": "The requested endpoint does not exist"
  }
}
```

**Funciones a implementar**:
```c
esp_err_t http_response_send_error(httpd_req_t *req, int status_code, const char* message, const char* description);
```

**Mapeo de errores**:
- 404: "Not Found" / "The requested endpoint does not exist"
- 405: "Method Not Allowed" / "HTTP method not supported for this endpoint"  
- 500: "Internal Server Error" / "An internal server error occurred"

**Criterios de completitud**:
- Rutas no existentes retornan 404 con JSON de error
- Métodos no soportados retornan 405 con JSON de error
- Errores internos retornan 500 con JSON de error
- Formato JSON de error es consistente

---

### 9. Testing: Verificar servidor básico funcional
**Descripción**: Documentar comandos de testing manual usando curl para verificar funcionalidad.

**Casos de prueba a documentar**:

1. **Servidor activo**:
```bash
curl -v http://192.168.1.XXX:80/whoami
# Esperado: Respuesta 200 con JSON del dispositivo
```

2. **Endpoint no existente (404)**:
```bash
curl -v http://192.168.1.XXX:80/nonexistent
# Esperado: Respuesta 404 con JSON de error
```

3. **Método no soportado (405)**:
```bash
curl -X POST http://192.168.1.XXX:80/whoami
# Esperado: Respuesta 405 con JSON de error
```

4. **Verificar headers**:
```bash
curl -I http://192.168.1.XXX:80/whoami
# Esperado: Content-Type: application/json
```

**Documentación de testing**:
- Incluir IP ejemplo y explicar cómo obtener IP real del dispositivo
- Documentar respuestas esperadas para cada caso
- Incluir troubleshooting básico (servidor no responde, etc.)

**Criterios de completitud**:
- Todos los comandos curl documentados funcionan correctamente
- Respuestas coinciden con lo documentado
- Testing cubre casos de éxito y error principales

---

### 10. Documentar configuración del servidor HTTP
**Descripción**: Crear documentación para desarrolladores sobre la implementación del servidor HTTP.

**Contenido de la documentación**:

1. **Arquitectura del componente**:
   - Cómo se integra en la arquitectura hexagonal
   - Separación de responsabilidades entre adapter y server
   - Estructura de directorios y archivos

2. **Guía de desarrollo**:
   - Cómo agregar nuevos endpoints
   - Cómo extender el middleware
   - Patrones de manejo de errores

3. **Decisiones técnicas**:
   - Por qué puerto 80 hardcodeado
   - Por qué headers mínimos JSON
   - Por qué logging básico en consola

4. **Ejemplos de código**:
   - Cómo registrar un nuevo handler
   - Cómo usar las funciones helper de respuesta
   - Cómo manejar errores apropiadamente

5. **Futuras extensiones**:
   - Dónde agregar configuración menuconfig
   - Cómo agregar más headers HTTP
   - Cómo extender logging

**Formato**: Archivo markdown en `docs/http_server.md`

**Criterios de completitud**:
- Documentación cubre todos los aspectos listados
- Incluye ejemplos de código funcionales
- Explica decisiones de diseño y arquitectura
- Proporciona roadmap para extensiones futuras

## Definición de Terminado (Definition of Done)

### Código
- [ ] Todos los archivos compilan sin errores ni warnings
- [ ] Código sigue las convenciones de naming del proyecto
- [ ] Funciones tienen documentación básica en headers
- [ ] No hay memory leaks detectables en inicialización/destrucción

### Testing
- [ ] Todos los casos de prueba manual funcionan correctamente
- [ ] Servidor responde apropiadamente en el puerto 80
- [ ] Manejo de errores funciona según especificación
- [ ] Logging aparece correctamente en consola

### Documentación
- [ ] Documentación de desarrollador está completa
- [ ] Comandos de testing están documentados y verificados
- [ ] Decisiones de arquitectura están explicadas

### Integración
- [ ] Componente se integra correctamente con main.c
- [ ] No afecta otros componentes existentes (WiFi, MQTT)
- [ ] Sigue patrones de la arquitectura hexagonal establecida

## Notas de Implementación

### Limitaciones del MVP
- Puerto hardcodeado (será configurable en futuras versiones)
- Solo soporte para método GET
- Headers HTTP mínimos
- Logging básico a consola únicamente
- Un solo endpoint implementado (`/whoami`)

### Consideraciones de Recursos
- Usar configuración por defecto de ESP-IDF para optimización de memoria
- No implementar features avanzadas que consuman recursos innecesarios
- Mantener handlers ligeros para evitar impacto en performance

### Extensibilidad Futura
La implementación debe preparar el terreno para:
- Endpoints adicionales (`/sensors`, comandos de control)
- Configuración vía menuconfig
- Headers HTTP adicionales
- Middleware más sofisticado
- Potencial soporte para WebSockets

---

**Estimación**: 2-3 días de desarrollo  
**Prioridad**: Alta (bloquea desarrollo de endpoints REST)  
**Dependencias**: Configuración WiFi básica completada