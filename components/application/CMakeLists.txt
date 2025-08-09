idf_component_register(SRCS "use_cases/publish_sensor_data.c"
                    INCLUDE_DIRS "."
                    "use_cases"
                    "dtos"
                    "ports"
                    REQUIRES domain infrastructure)