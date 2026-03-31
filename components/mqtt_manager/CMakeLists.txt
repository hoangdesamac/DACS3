idf_component_register(SRCS "mqtt_manager.c"
                       INCLUDE_DIRS "include"
                       REQUIRES mqtt json esp_event log esp-tls driver espnow_manager)
                    