idf_component_register(SRCS "sensors.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver freertos esp_driver_gpio esp_timer)
