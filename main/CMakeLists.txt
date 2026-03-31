idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES wifi_manager mqtt_manager espnow_manager driver nvs_flash json)
