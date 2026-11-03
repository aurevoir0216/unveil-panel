idf_component_register(
    SRC_DIRS "."
    INCLUDE_DIRS "."
    REQUIRES esp_timer esp_system esp_hw_support soc esp_rom espressif__esp_tinyusb
)

if(CONFIG_USB_HS_CONSOLE_USB_CDC_AUTO_INIT)
    target_link_libraries(${COMPONENT_LIB} INTERFACE
        "-Wl,--wrap=app_main"
        "-u __wrap_app_main"
    )
endif()
