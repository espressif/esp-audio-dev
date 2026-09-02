/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stddef.h>

#include "audio_codec_if.h"
#include "audio_codec_data_if.h"
#include "audio_codec_gpio_if.h"
#include "audio_codec_hw_cfg.h"
#include "esp_codec_dev_types.h"

#ifdef CONFIG_CODEC_ES8311_SUPPORT
#include "es8311_codec.h"
#endif  /* CONFIG_CODEC_ES8311_SUPPORT */
#ifdef CONFIG_CODEC_ES7210_SUPPORT
#include "es7210_adc.h"
#endif  /* CONFIG_CODEC_ES7210_SUPPORT */
#ifdef CONFIG_CODEC_ES7243_SUPPORT
#include "es7243_adc.h"
#endif  /* CONFIG_CODEC_ES7243_SUPPORT */
#ifdef CONFIG_CODEC_ES7243E_SUPPORT
#include "es7243e_adc.h"
#endif  /* CONFIG_CODEC_ES7243E_SUPPORT */
#ifdef CONFIG_CODEC_ES8156_SUPPORT
#include "es8156_dac.h"
#endif  /* CONFIG_CODEC_ES8156_SUPPORT */
#ifdef CONFIG_CODEC_AW88298_SUPPORT
#include "aw88298_dac.h"
#endif  /* CONFIG_CODEC_AW88298_SUPPORT */
#ifdef CONFIG_CODEC_ES8389_SUPPORT
#include "es8389_codec.h"
#endif  /* CONFIG_CODEC_ES8389_SUPPORT */
#ifdef CONFIG_CODEC_ES8374_SUPPORT
#include "es8374_codec.h"
#endif  /* CONFIG_CODEC_ES8374_SUPPORT */
#ifdef CONFIG_CODEC_ES8388_SUPPORT
#include "es8388_codec.h"
#endif  /* CONFIG_CODEC_ES8388_SUPPORT */
#ifdef CONFIG_CODEC_TAS5805M_SUPPORT
#include "tas5805m_dac.h"
#endif  /* CONFIG_CODEC_TAS5805M_SUPPORT */
#ifdef CONFIG_CODEC_ZL38063_SUPPORT
#include "zl38063_codec.h"
#endif  /* CONFIG_CODEC_ZL38063_SUPPORT */
#if CONFIG_CODEC_CJC8910_SUPPORT
#include "cjc8910_codec.h"
#endif  /* CONFIG_CODEC_CJC8910_SUPPORT */
#ifdef CONFIG_CODEC_DATA_ADC_SUPPORT
#include "esp_codec_adc_data.h"
#endif  /* CONFIG_CODEC_DATA_ADC_SUPPORT */
#if CONFIG_CODEC_DUMMY_SUPPORT
#include "dummy_codec.h"
#endif  /* CONFIG_CODEC_DUMMY_SUPPORT */
#if CONFIG_CODEC_UAC_SUPPORT
#include "esp_codec_dev_uac.h"
#endif  /* CONFIG_CODEC_UAC_SUPPORT */

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Codec I2C configuration
 */
typedef struct {
    uint8_t   addr;            /*!< I2C address, default address can be gotten from codec head files */
    void     *bus_handle;      /*!< I2C master bus handle from `i2c_new_master_bus()`; required */
    uint32_t  clock_speed_hz;  /*!< I2C SCL speed in Hz; use default when set to 0 */
} audio_codec_i2c_cfg_t;

/**
 * @brief  Codec I2S configuration
 */
typedef struct {
    uint8_t  port;       /*!< I2S port, this port need pre-installed by other modules */
    void    *rx_handle;  /*!< I2S rx handle, need provide on IDF 5.x */
    void    *tx_handle;  /*!< I2S tx handle, need provide on IDF 5.x */
    int      clk_src;    /*!< I2S clock source, need converted from `i2s_clock_src_t`. If set to 0 will use default clock source */
} audio_codec_i2s_cfg_t;

/**
 * @brief  Codec SPI configuration
 */
typedef struct {
    uint8_t   spi_port;     /*!< SPI port, this port need pre-installed by other modules */
    int16_t   cs_pin;       /*!< SPI CS GPIO pin setting */
    uint32_t  clock_speed;  /*!< SPI clock unit hz (use 10MHZif set to 0)*/
} audio_codec_spi_cfg_t;

/**
 * @brief  Shared codec configuration fields for audio_codec_new()
 *
 *         Full aggregate of interface and hardware sub-configs.
 *         When build_chip_cfg is set, audio_codec_new() builds a chip-specific
 *         cfg from the used subset. Chip-specific `*_codec_cfg_t` may also be
 *         passed directly.
 */
typedef struct {
    const audio_codec_ctrl_if_t *ctrl_if;    /*!< Codec Control interface */
    const audio_codec_gpio_if_t *gpio_if;    /*!< Codec GPIO interface */
    audio_hw_sys_cfg_t           sys_cfg;    /*!< System clock configuration */
    audio_hw_adc_cfg_t           adc_cfg;    /*!< ADC/Microphone configuration */
    audio_hw_dac_cfg_t           dac_cfg;    /*!< DAC configuration for internal loopback */
    audio_hw_pa_cfg_t            pa_cfg;     /*!< PA configuration */
    audio_hw_reset_cfg_t         reset_cfg;  /*!< Reset configuration */
    audio_hw_int_cfg_t           int_cfg;    /*!< Interrupt configuration */
} audio_codec_cfg_t;

/**
 * @brief  Build a chip-specific configuration buffer from audio_codec_cfg_t
 *
 * @param[in]   cfg       Shared codec configuration (audio_codec_cfg_t)
 * @param[out]  chip_cfg  Chip-specific configuration buffer (size is chip_cfg_size)
 */
typedef void (*audio_codec_build_chip_cfg_t)(const audio_codec_cfg_t *cfg, void *chip_cfg);

/**
 * @brief  Codec factory descriptor for built-in and external drivers
 *
 *         If build_chip_cfg is NULL, only chip-specific cfg_size is accepted by audio_codec_new().
 *         Link-time registered descriptors and referenced strings and functions must have
 *         static storage duration.
 */
typedef struct {
    const char                   *name;                /*!< Codec driver name used by audio_codec_new() */
    const audio_codec_if_t     *(*create)(void *cfg);  /*!< Chip constructor */
    size_t                        chip_cfg_size;       /*!< Size of chip-specific configuration */
    audio_codec_build_chip_cfg_t  build_chip_cfg;      /*!< Optional; NULL = chip cfg only */
} audio_codec_desc_t;

/**
 * @brief  Register an external codec descriptor at link time
 *
 *         Place this macro at file scope after defining the codec constructor and optional
 *         configuration builder. The linker collects all descriptors into a registry used
 *         by audio_codec_new(). Built-in codec names take precedence over registered names.
 *
 * @note  codec_name must be a valid C identifier and unique among link-time registrations.
 * @note  The component containing this macro must be linked with WHOLE_ARCHIVE, or the
 *        descriptor symbol must be forced into the link with
 *        "-u audio_codec_desc_<codec_name>".
 * @note  cfg_size must be non-zero and must not equal sizeof(audio_codec_cfg_t);
 *        audio_codec_new() tells the two configuration forms apart by size. A chip cfg that
 *        happens to match that size must be padded with a reserved field. This is enforced
 *        at compile time.
 * @note  create_fn must copy any needed fields out of cfg. On the audio_codec_cfg_t path the
 *        chip cfg buffer is released immediately after create_fn returns.
 *
 * @param  codec_name  Codec name token used by audio_codec_new()
 * @param  create_fn   Chip constructor
 * @param  cfg_size    Size of the chip-specific configuration structure
 * @param  build_fn    Optional shared-to-chip configuration builder; may be NULL
 */
#ifdef __cplusplus
#define AUDIO_CODEC_REGISTER(codec_name, create_fn, cfg_size, build_fn)             \
    extern "C" {                                                                    \
        static_assert((cfg_size) > 0,                                               \
                      "Chip cfg size must be non-zero");                            \
        static_assert((cfg_size) != sizeof(audio_codec_cfg_t),                      \
                      "Chip cfg size must differ from sizeof(audio_codec_cfg_t)");  \
        extern const audio_codec_desc_t audio_codec_desc_##codec_name               \
        __attribute__((used, section(".audio_codec_desc"))) = {                     \
            .name = #codec_name,                                                    \
            .create = (const audio_codec_if_t *(*)(void *))(create_fn),             \
            .chip_cfg_size = (cfg_size),                                            \
            .build_chip_cfg = (build_fn),                                           \
        };                                                                          \
    }
#else
#define AUDIO_CODEC_REGISTER(codec_name, create_fn, cfg_size, build_fn)          \
    _Static_assert((cfg_size) > 0,                                               \
                   "Chip cfg size must be non-zero");                            \
    _Static_assert((cfg_size) != sizeof(audio_codec_cfg_t),                      \
                   "Chip cfg size must differ from sizeof(audio_codec_cfg_t)");  \
    const audio_codec_desc_t audio_codec_desc_##codec_name                       \
    __attribute__((used, section(".audio_codec_desc"))) = {                      \
        .name = #codec_name,                                                     \
        .create = (const audio_codec_if_t *(*)(void *))(create_fn),              \
        .chip_cfg_size = (cfg_size),                                             \
        .build_chip_cfg = (build_fn),                                            \
    }
#endif  /* __cplusplus */

/**
 * @brief  Get default codec GPIO interface
 *
 * @return
 *       - NULL    Failed
 *       - Others  Codec GPIO interface
 */
const audio_codec_gpio_if_t *audio_codec_new_gpio(void);

/**
 * @brief  Get default SPI control interface
 *
 * @param[in]  spi_cfg  SPI configuration
 *
 * @return
 *       - NULL    Failed
 *       - Others  SPI control interface
 */
const audio_codec_ctrl_if_t *audio_codec_new_spi_ctrl(audio_codec_spi_cfg_t *spi_cfg);

/**
 * @brief  Get default I2C control interface
 *
 * @param[in]  i2c_cfg  I2C configuration
 *
 * @return
 *       - NULL    Failed
 *       - Others  I2C control interface
 */
const audio_codec_ctrl_if_t *audio_codec_new_i2c_ctrl(audio_codec_i2c_cfg_t *i2c_cfg);

/**
 * @brief  Get default I2S data interface
 *
 *         Multiple instances may share one I2S port. RX-only or TX-only setups can
 *         require two data_if handles on the same port (one for capture, one for playback).
 *
 * @note  Allocates the data interface and may allocate shared per-port state. Do not
 *        operate the same returned handle from multiple tasks concurrently.
 * @note  On the same I2S port, RX-only or TX-only setups may require two data_if
 *        instances (A and B) with complementary TX/RX handle assignment. Full duplex
 *        typically uses one data_if with both TX and RX handles.
 *
 * @param[in]  i2s_cfg  I2S configuration
 *
 * @return
 *       - NULL    Failed
 *       - Others  I2S data interface
 */
const audio_codec_data_if_t *audio_codec_new_i2s_data(audio_codec_i2s_cfg_t *i2s_cfg);

/**
 * @brief  Create a codec interface instance by driver name
 *
 * @note  codec_cfg may be either:
 *        - audio_codec_cfg_t; factory builds chip cfg via build_chip_cfg when provided
 *        - chip-specific configuration such as es8311_codec_cfg_t; passed through as-is
 *        cfg_size must match the chosen struct. Chip constructors may also be called directly.
 * @note  External codecs can be made available through AUDIO_CODEC_REGISTER().
 *        Chip-private fields outside audio_codec_cfg_t must use the chip-specific cfg path.
 *
 * @param[in]  codec_name  Codec driver name
 * @param[in]  codec_cfg   audio_codec_cfg_t or chip-specific configuration; caller retains ownership
 * @param[in]  cfg_size    Size of codec_cfg in bytes
 *
 * @return
 *       - NULL    Failed to create codec interface
 *       - Others  Codec interface instance
 */
const audio_codec_if_t *audio_codec_new(const char *codec_name, const void *codec_cfg, int cfg_size);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
