set(COMPONENT_PRIV_INCLUDEDIRS "private_inc" "device/private_inc" "platform/usb/private_inc")

set(COMPONENT_ADD_INCLUDEDIRS include interface device/include include/hw_proc include/impl)

# set conversion sources
set(COMPONENT_SRCS
  src/codec_dev_order.c
  src/codec_dev_data_cvt.c
  src/codec_dev_mirror.c
  src/audio_codec_if_delete.c
  src/audio_codec_factory.c
  src/audio_codec_sw_vol.c
  src/audio_codec_if_ops.c
  src/audio_hw_base.c
  src/esp_codec_dev.c
  src/esp_codec_dev_vol.c
  src/hw_proc/audio_hw_alc.c
  src/hw_proc/audio_hw_drc.c
  src/hw_proc/audio_hw_line.c
  src/hw_proc/audio_hw_mute.c
  src/hw_proc/audio_hw_eq.c
)

set(priv_requires
    freertos
    esp_ringbuf
    esp_hw_support
    esp_driver_gpio
    esp_driver_i2c
    esp_driver_i2s
    esp_driver_spi
    esp_adc
)

set(requires "")
if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "6.0")
  list(APPEND requires esp_hal_ana_conv esp_hal_clock)
endif()

list(APPEND COMPONENT_SRCS
  platform/audio_codec_gpio.c
  platform/audio_codec_ctrl_i2c.c
  platform/audio_codec_data_i2s.c
  platform/audio_codec_ctrl_spi.c
  platform/esp_codec_dev_os.c
  device/common/codec_ref_mgr.c
  device/template_codec/codec_template.c
)

if(CONFIG_CODEC_DATA_ADC_SUPPORT)
  list(APPEND COMPONENT_SRCS platform/audio_codec_data_adc.c)
endif()

if (CONFIG_CODEC_ES8311_SUPPORT)
  list(APPEND COMPONENT_SRCS device/es8311/es8311.c
    device/es8311/es8311_alc.c
    device/es8311/es8311_drc.c
    device/es8311/es8311_mute.c
    device/es8311/es8311_eq.c)
endif()

if (CONFIG_CODEC_ES8156_SUPPORT)
  list(APPEND COMPONENT_SRCS device/es8156/es8156.c)
endif()

if (CONFIG_CODEC_ES7243E_SUPPORT)
  list(APPEND COMPONENT_SRCS device/es7243e/es7243e.c)
endif()

if (CONFIG_CODEC_ES7210_SUPPORT)
  list(APPEND COMPONENT_SRCS device/es7210/es7210.c
    device/es7210/es7210_alc.c
    device/es7210/es7210_mute.c)
endif()

if (CONFIG_CODEC_ES7243_SUPPORT)
  list(APPEND COMPONENT_SRCS device/es7243/es7243.c)
endif()

if (CONFIG_CODEC_ES8388_SUPPORT)
  list(APPEND COMPONENT_SRCS device/es8388/es8388.c)
  list(APPEND COMPONENT_SRCS device/es8388/es8388_alc.c)
  list(APPEND COMPONENT_SRCS device/es8388/es8388_line.c)
endif()

if (CONFIG_CODEC_TAS5805M_SUPPORT)
  list(APPEND COMPONENT_SRCS device/tas5805m/tas5805m.c)
endif()

if (CONFIG_CODEC_ES8374_SUPPORT)
  list(APPEND COMPONENT_SRCS device/es8374/es8374.c)
endif()

if (CONFIG_CODEC_AW88298_SUPPORT)
  list(APPEND COMPONENT_SRCS device/aw88298/aw88298.c)
endif()

if (CONFIG_CODEC_ES8389_SUPPORT)
  list(APPEND COMPONENT_SRCS device/es8389/es8389.c)
endif()

if (CONFIG_CODEC_DUMMY_SUPPORT)
  list(APPEND COMPONENT_SRCS device/dummy/dummy_codec.c)
endif()

if (CONFIG_CODEC_UAC_SUPPORT)
  list(APPEND COMPONENT_SRCS
    platform/usb/audio_codec_uac.c
    platform/usb/esp_codec_dev_uac.c
    platform/usb/audio_codec_data_uac.c
    platform/usb/audio_codec_uac_host.c)
  list(APPEND requires usb_host_uac)
endif()

if (CONFIG_CODEC_ZL38063_SUPPORT)
  list(APPEND COMPONENT_PRIV_INCLUDEDIRS
    device/zl38063/api_lib
    device/zl38063/firmware)
  list(APPEND COMPONENT_SRCS device/zl38063/zl38063.c
    device/zl38063/api_lib/vprocTwolf_access.c
    device/zl38063/api_lib/vproc_common.c
    device/zl38063/example_apps/tw_hal_verify.c
    device/zl38063/example_apps/tw_ldcfg.c
    device/zl38063/example_apps/tw_ldfw.c
    device/zl38063/example_apps/tw_ldfwcfg.c
    device/zl38063/example_apps/tw_spi_access.c)
endif()

if (CONFIG_CODEC_CJC8910_SUPPORT)
  list(APPEND COMPONENT_SRCS device/cjc8910/cjc8910.c)
endif()

idf_component_register(SRCS "${COMPONENT_SRCS}"
                       INCLUDE_DIRS "${COMPONENT_ADD_INCLUDEDIRS}"
                       PRIV_INCLUDE_DIRS "${COMPONENT_PRIV_INCLUDEDIRS}"
                       PRIV_REQUIRES ${priv_requires}
                       REQUIRES ${requires})

idf_component_get_property(esp_codec_dev_version ${COMPONENT_NAME} COMPONENT_VERSION)
if(esp_codec_dev_version)
  target_compile_definitions(${COMPONENT_LIB} PUBLIC ESP_CODEC_DEV_VERSION=\"${esp_codec_dev_version}\")
endif()

# Library only support xtensa
if (CONFIG_CODEC_ZL38063_SUPPORT)
  if (NOT ((CONFIG_IDF_TARGET STREQUAL "esp32c6") OR (CONFIG_IDF_TARGET STREQUAL "esp32c3") OR (CONFIG_IDF_TARGET STREQUAL "esp32p4")))
    target_link_libraries(${COMPONENT_TARGET} INTERFACE "-L${CMAKE_CURRENT_LIST_DIR}/device/zl38063/firmware")
    target_link_libraries(${COMPONENT_TARGET} INTERFACE firmware)
  endif()
endif()
