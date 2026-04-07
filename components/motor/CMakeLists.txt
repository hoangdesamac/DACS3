idf_component_register(SRCS "motor.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_driver_gpio log)
