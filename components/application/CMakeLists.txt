idf_component_register(SRCS
                    "use_cases/publish_sensor_data.c"
                    "use_cases/read_soil_sensors.c"
                    "use_cases/control_irrigation.c"
                    "use_cases/offline_irrigation.c"
                    INCLUDE_DIRS "."
                    "use_cases"
                    "dtos"
                    "ports"
                    REQUIRES domain infrastructure)