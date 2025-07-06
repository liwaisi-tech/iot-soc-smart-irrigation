idf_component_register(SRCS "iot-soc-smart-irrigation.c"
                    INCLUDE_DIRS "."
                    REQUIRES infrastructure nvs_flash esp_event esp_system freertos)
