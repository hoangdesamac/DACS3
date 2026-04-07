idf_component_register(SRCS "esp32.c"
                    INCLUDE_DIRS "."
                    REQUIRES espnow oled_display sensors motor driver esp_common freertos)
