/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "audio_codec_if.h"
#include "audio_codec_hw_cfg.h"
#include "audio_codec_gpio_if.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define AW88298_CODEC_DEFAULT_ADDR  (0x36 << 1)

/**
 * @brief  AW88298 codec configuration
 */
typedef struct {
    const audio_codec_ctrl_if_t *ctrl_if;    /*!< Codec Control interface */
    const audio_codec_gpio_if_t *gpio_if;    /*!< Codec GPIO interface */
    audio_hw_pa_cfg_t            pa_cfg;     /*!< PA configuration */
    audio_hw_reset_cfg_t         reset_cfg;  /*!< Reset configuration */
} aw88298_codec_cfg_t;

/**
 * @brief  New AW88298 codec interface
 *
 * @note  Need to set mclk_multiple to I2S_MCLK_MULTIPLE_384 in
 *        esp_codec_dev_sample_info_t to support 44100 Hz sample rate.
 *
 * @param[in]  codec_cfg  AW88298 codec configuration
 *
 * @return
 *       - NULL    Fail to new AW88298 codec interface
 *       - Others  AW88298 codec interface
 */
const audio_codec_if_t *aw88298_codec_new(aw88298_codec_cfg_t *codec_cfg);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
