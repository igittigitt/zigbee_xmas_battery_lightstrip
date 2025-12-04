idf_component_register(
    SRC_DIRS  "."
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_timer
        driver
        nvs_flash
        esp_driver_uart
        ieee802154
        freertos
)
