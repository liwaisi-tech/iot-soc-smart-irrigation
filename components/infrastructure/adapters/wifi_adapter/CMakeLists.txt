idf_component_register(SRCS "src/wifi_prov_manager.c"
                            "src/boot_counter.c"
                            "src/wifi_connection_manager.c"
                       INCLUDE_DIRS "include"
                       REQUIRES wifi_provisioning 
                                protocomm
                                esp_wifi
                                nvs_flash
                                esp_event
                                esp_netif
                                esp_timer
                                domain
                                application)