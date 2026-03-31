idf_component_register(SRCS "espnow_manager.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi nvs_flash mqtt_manager) 
                      