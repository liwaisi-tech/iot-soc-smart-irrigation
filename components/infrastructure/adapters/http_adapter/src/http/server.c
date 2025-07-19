#include "server.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "HTTP_SERVER";
static httpd_handle_t server = NULL;

static esp_err_t whoami_handler(httpd_req_t *req);
static esp_err_t default_404_handler(httpd_req_t *req, httpd_err_code_t err);

esp_err_t http_server_start(void)
{
    if (server != NULL) {
        ESP_LOGW(TAG, "Servidor HTTP ya está ejecutándose");
        return ESP_OK;
    }
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80; // Hardcodeado para MVP
    config.max_uri_handlers = 10;
    config.task_priority = 5;
    config.stack_size = 8192;
    
    ESP_LOGI(TAG, "Iniciando servidor HTTP en puerto %d", config.server_port);
    
    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al iniciar servidor HTTP: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Registrar manejador de errores 404
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, default_404_handler);
    
    // Registrar endpoint /whoami
    httpd_uri_t whoami_uri = {
        .uri = "/whoami",
        .method = HTTP_GET,
        .handler = whoami_handler,
        .user_ctx = NULL
    };
    
    ret = httpd_register_uri_handler(server, &whoami_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al registrar handler /whoami: %s", esp_err_to_name(ret));
        httpd_stop(server);
        server = NULL;
        return ret;
    }
    
    ESP_LOGI(TAG, "Servidor HTTP iniciado correctamente");
    ESP_LOGI(TAG, "Endpoints disponibles: GET /whoami");
    
    return ESP_OK;
}

esp_err_t http_server_stop(void)
{
    if (server == NULL) {
        ESP_LOGW(TAG, "Servidor HTTP no está ejecutándose");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Deteniendo servidor HTTP...");
    esp_err_t ret = httpd_stop(server);
    if (ret == ESP_OK) {
        server = NULL;
        ESP_LOGI(TAG, "Servidor HTTP detenido correctamente");
    } else {
        ESP_LOGE(TAG, "Error al detener servidor HTTP: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

httpd_handle_t http_server_get_handle(void)
{
    return server;
}

esp_err_t http_server_register_handler(const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *req))
{
    if (server == NULL) {
        ESP_LOGE(TAG, "Servidor HTTP no está ejecutándose");
        return ESP_FAIL;
    }
    
    httpd_uri_t uri_handler = {
        .uri = uri,
        .method = method,
        .handler = handler,
        .user_ctx = NULL
    };
    
    return httpd_register_uri_handler(server, &uri_handler);
}

esp_err_t http_response_send_json(httpd_req_t *req, const char* json_string)
{
    if (req == NULL || json_string == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json_string, strlen(json_string));
}

esp_err_t http_response_send_error(httpd_req_t *req, int status_code, const char* message, const char* description)
{
    if (req == NULL || message == NULL || description == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char error_json[512];
    snprintf(error_json, sizeof(error_json),
             "{"
             "\"error\":{"
             "\"code\":%d,"
             "\"message\":\"%s\","
             "\"description\":\"%s\""
             "}"
             "}", 
             status_code, message, description);
    
    // Set HTTP status code manually
    const char* status_str;
    switch (status_code) {
        case 404:
            status_str = "404 Not Found";
            break;
        case 405:
            status_str = "405 Method Not Allowed";
            break;
        case 500:
            status_str = "500 Internal Server Error";
            break;
        default:
            status_str = "400 Bad Request";
            break;
    }
    
    httpd_resp_set_status(req, status_str);
    return http_response_send_json(req, error_json);
}

void http_middleware_log_request(httpd_req_t *req)
{
    if (req == NULL) {
        return;
    }
    
    const char* method_str = "UNKNOWN";
    switch (req->method) {
        case HTTP_GET: method_str = "GET"; break;
        case HTTP_POST: method_str = "POST"; break;
        case HTTP_PUT: method_str = "PUT"; break;
        case HTTP_DELETE: method_str = "DELETE"; break;
        default: break;
    }
    
    ESP_LOGI("HTTP", "%s %s", method_str, req->uri);
}

static esp_err_t whoami_handler(httpd_req_t *req)
{
    http_middleware_log_request(req);
    
    // Información básica del dispositivo para MVP
    const char* device_info = "{"
        "\"device\":{"
        "\"name\":\"Smart Irrigation System\","
        "\"version\":\"1.0.0\","
        "\"architecture\":\"Hexagonal\","
        "\"target\":\"ESP32\""
        "},"
        "\"endpoints\":["
        "{"
        "\"path\":\"/whoami\","
        "\"method\":\"GET\","
        "\"description\":\"Device information and available endpoints\""
        "}"
        "]"
        "}";
    
    return http_response_send_json(req, device_info);
}

static esp_err_t default_404_handler(httpd_req_t *req, httpd_err_code_t err)
{
    http_middleware_log_request(req);
    
    return http_response_send_error(req, 404, "Not Found", "The requested endpoint does not exist");
}