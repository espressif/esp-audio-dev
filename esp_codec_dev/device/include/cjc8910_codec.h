/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO., LTD
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

#define CJC8910_CODEC_DEFAULT_ADDR  (0x30)

/**
 * @brief  CJC8910 codec configuration
 *
 * @note  This driver only supports codec work in slave mode
 */
typedef struct {
    const audio_codec_ctrl_if_t *ctrl_if;  /*!< Codec Control interface */
    const audio_codec_gpio_if_t *gpio_if;  /*!< Codec GPIO interface */
    audio_hw_pa_cfg_t            pa_cfg;   /*!< PA configuration */
} cjc8910_codec_cfg_t;

/**
 * @brief  New CJC8910 codec interface
 *
 * @param[in]  codec_cfg  CJC8910 codec configuration
 *
 * @return
 *       - NULL    Not enough memory or codec failed to open
 *       - Others  CJC8910 codec interface
 */
const audio_codec_if_t *cjc8910_codec_new(cjc8910_codec_cfg_t *codec_cfg);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
