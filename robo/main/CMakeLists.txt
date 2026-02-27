idf_component_register(SRCS "main.c" "motor.c" "executor.c" "display.c" "eyes.c" "dfplayer.c"
                       INCLUDE_DIRS "."
                       REQUIRES common esp_wifi esp_driver_gpio esp_driver_ledc
                                esp_lcd esp_driver_spi esp_driver_i2c esp_timer led_strip
                       PRIV_REQUIRES esp_driver_uart)
