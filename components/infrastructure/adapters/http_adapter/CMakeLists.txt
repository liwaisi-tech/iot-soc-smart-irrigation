idf_component_register(SRCS "src/http_adapter.c"
                            "src/http/server.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_http_server esp_log esp_system freertos)