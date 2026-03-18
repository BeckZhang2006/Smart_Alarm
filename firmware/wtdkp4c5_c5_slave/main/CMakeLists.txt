idf_component_register(SRCS "main.c" "sdio_slave_csi.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi nvs_flash sdmmc driver)
