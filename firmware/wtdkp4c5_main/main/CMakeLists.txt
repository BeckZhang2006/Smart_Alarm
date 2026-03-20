idf_component_register(SRCS "main.c" "csi_processor.c" "alarm_manager.c" "web_server.c" 
                            "sdio_host_csi.c" "storage.c" "display.c"
                       INCLUDE_DIRS "."
                       EMBED_FILES "web/index.html" "web/style.css" "web/script.js"
                       REQUIRES esp_wifi esp_netif nvs_flash esp_http_server driver json)
