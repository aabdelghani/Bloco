idf_component_register(SRCS "main.c" "motor.c" "executor.c"
                       INCLUDE_DIRS "."
                       REQUIRES common esp_wifi esp_driver_gpio esp_driver_ledc)
