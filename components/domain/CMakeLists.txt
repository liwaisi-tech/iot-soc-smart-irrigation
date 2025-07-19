idf_component_register(SRCS "services/device_config_service.c"
                    INCLUDE_DIRS "."
                    "entities"
                    "value_objects" 
                    "repositories"
                    "services"
                    PRIV_REQUIRES nvs_flash)