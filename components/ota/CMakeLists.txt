idf_component_register(SRCS "ota.c" "ota_ver.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES esp_https_ota esp_http_client app_update esp_app_format mbedtls esp_timer
                                     freertos log)
