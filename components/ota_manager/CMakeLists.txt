idf_component_register(
    SRCS 
        "ota_manager.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_http_client
        esp_https_ota
        mqtt
        log
        freertos
        app_update


)