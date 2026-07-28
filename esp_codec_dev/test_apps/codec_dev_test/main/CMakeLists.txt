
set(priv_requires
        unity
        driver
        esp_driver_i2c
        esp_driver_i2s
        esp_driver_ledc
        esp_pm
        esp_timer
        esp_codec_dev
)

if(CONFIG_IDF_TARGET_ESP32 OR CONFIG_IDF_TARGET_ESP32S3 OR CONFIG_IDF_TARGET_ESP32P4)
    list(APPEND priv_requires esp_psram)
endif()

if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "6.0")
    list(APPEND priv_requires
            esp_driver_gpio
            esp_driver_i2c
            esp_driver_i2s
            esp_driver_ledc
            esp_driver_uart
            esp_pm
            esp_timer
    )
endif()

set(srcs
        "app/test_app_main.c"
        "mock/test_my_codec.c"
        "mock/test_layout_mock.c"
        "common/test_board_periph.c"
        "mock/test_codec_print.c"
        "common/my_codec.c"
        "common/my_codec_proc.c"
)

# list(APPEND srcs "mock/test_data_convert.c")

if(CONFIG_IDF_TARGET_ESP32S3)
    list(APPEND srcs "boards/es8389/test_board_es8389.c")
    list(APPEND srcs "boards/esp32s3/test_board_i2s_open_order.c")
    list(APPEND srcs "boards/esp32s3/test_board_single_channel.c")
    list(APPEND srcs "boards/esp32s3/test_board.c")
endif()

if(CONFIG_IDF_TARGET_ESP32S31)
    list(APPEND srcs "boards/es8389/test_board_es8389.c")
endif()

if(CONFIG_IDF_TARGET_ESP32)
    list(APPEND srcs "boards/esp32/test_board_i2s_hw_1.c")
endif()

if(CONFIG_IDF_TARGET_ESP32P4)
    list(APPEND srcs
            "boards/esp32p4/test_p4_ev_board.c"
            "boards/esp32p4/test_p4_ev_board_lp.c"
    )
endif()

if(CONFIG_IDF_TARGET_ESP32C3)
    list(APPEND srcs "boards/esp32c3/test_board_c3_lyra.c")
endif()

idf_component_register(SRCS ${srcs}
                       PRIV_INCLUDE_DIRS include
                       PRIV_REQUIRES ${priv_requires}
                       WHOLE_ARCHIVE TRUE
                       EMBED_FILES "16k_mono_16bit.pcm"
                       )

target_compile_options(${COMPONENT_LIB} PRIVATE -Werror=unused-but-set-variable)
