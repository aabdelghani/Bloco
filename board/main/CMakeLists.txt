idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES common esp_wifi esp_driver_gpio json led_strip esp_timer)
