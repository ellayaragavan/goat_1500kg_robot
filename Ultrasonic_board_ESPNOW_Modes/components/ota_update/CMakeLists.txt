idf_component_register(SRCS "ota_update.c"
                    INCLUDE_DIRS include
                    EMBED_FILES web/index.html
                    REQUIRES esp_https_server nvs_flash app_update)
