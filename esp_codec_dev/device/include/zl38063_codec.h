/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
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

/**
 * @brief  ZL38063 codec configuration
 *
 * @note  The ZL38063 codec driver provides default I2S settings in firmware:
 *        48 kHz, 16 bits, 2 channels. Other sample rates require resampling first.
 */
typedef struct {
    const audio_codec_ctrl_if_t *ctrl_if;    /*!< Codec Control interface */
    const audio_codec_gpio_if_t *gpio_if;    /*!< Codec GPIO interface */
    audio_hw_pa_cfg_t            pa_cfg;     /*!< PA configuration */
    audio_hw_reset_cfg_t         reset_cfg;  /*!< Reset configuration */
} zl38063_codec_cfg_t;

/**
 * @brief  New ZL38063 codec interface
 *
 * @param[in]  codec_cfg  ZL38063 codec configuration
 *
 * @return
 *       - NULL    Fail to new ZL38063 codec interface
 *       - Others  ZL38063 codec interface
 */
const audio_codec_if_t *zl38063_codec_new(zl38063_codec_cfg_t *codec_cfg);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
