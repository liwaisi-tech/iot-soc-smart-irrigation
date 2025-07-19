#ifndef DEVICE_CONFIG_SERVICE_H
#define DEVICE_CONFIG_SERVICE_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_NAME_MAX_LEN 50
#define DEVICE_LOCATION_MAX_LEN 150
#define DEVICE_CONFIG_NAMESPACE "device_config"
#define DEVICE_NAME_KEY "device_name"
#define DEVICE_LOCATION_KEY "device_location"

typedef struct {
    char device_name[DEVICE_NAME_MAX_LEN + 1];
    char device_location[DEVICE_LOCATION_MAX_LEN + 1];
} device_config_t;

esp_err_t device_config_service_init(void);

esp_err_t device_config_service_get_name(char *name, size_t name_len);

esp_err_t device_config_service_set_name(const char *name);

esp_err_t device_config_service_get_location(char *location, size_t location_len);

esp_err_t device_config_service_set_location(const char *location);

esp_err_t device_config_service_get_config(device_config_t *config);

esp_err_t device_config_service_set_config(const device_config_t *config);

esp_err_t device_config_service_is_configured(bool *configured);

esp_err_t device_config_service_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_CONFIG_SERVICE_H */