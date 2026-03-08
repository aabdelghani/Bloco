idf_component_register(SRCS "main.c" "camera.c" "classifier.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES common esp_wifi esp_driver_gpio esp_timer led_strip
                       PRIV_REQUIRES esp_psram)
