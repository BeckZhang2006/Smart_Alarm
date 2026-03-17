idf_component_register(SRCS "main.c" "csi_transmitter.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi nvs_flash esp_driver_gpio)
