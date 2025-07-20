#ifndef MQTT_CLIENT_MANAGER_H
#define MQTT_CLIENT_MANAGER_H

#include "mqtt_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get string representation of MQTT connection state
 * 
 * @param state MQTT adapter state
 * @return Constant string representation of the state
 */
const char* mqtt_client_manager_state_to_string(mqtt_adapter_state_t state);

/**
 * @brief Log MQTT connection statistics
 * 
 * @param status Pointer to MQTT adapter status structure
 */
void mqtt_client_manager_log_stats(const mqtt_adapter_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_CLIENT_MANAGER_H */