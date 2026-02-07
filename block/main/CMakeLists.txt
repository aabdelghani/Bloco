idf_component_register(SRCS "main.c" "led.c" "programmer.c" "command.c"
                       INCLUDE_DIRS "."
                       REQUIRES common esp_driver_gpio
                       PRIV_REQUIRES json)
