idf_component_register(SRCS "sensors.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_adc freertos esp_driver_gpio)
