idf_component_register(SRCS "main.c" "csi_data.c" "person_detector.c" "uart_comm.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi nvs_flash driver)
