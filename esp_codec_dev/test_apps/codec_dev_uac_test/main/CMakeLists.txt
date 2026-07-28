set(srcs
    "test_app_main.c"
    "test_uac_api.c"
)

idf_component_register(SRCS ${srcs}
                       PRIV_REQUIRES unity esp_codec_dev esp_driver_gpio
                       WHOLE_ARCHIVE TRUE)

target_compile_options(${COMPONENT_LIB} PRIVATE -Werror=unused-but-set-variable)
