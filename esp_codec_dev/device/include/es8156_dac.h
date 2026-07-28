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

#define ES8156_CODEC_DEFAULT_ADDR  (0x10)

/**
 * @brief  ES8156 codec configuration
 */
typedef struct {
    const audio_codec_ctrl_if_t *ctrl_if;  /*!< Codec Control interface */
    const audio_codec_gpio_if_t *gpio_if;  /*!< Codec GPIO interface */
    audio_hw_pa_cfg_t            pa_cfg;   /*!< PA configuration */
} es8156_codec_cfg_t;

/**
 * @brief  New ES8156 codec interface
 *
 * @param[in]  codec_cfg  ES8156 codec configuration
 *
 * @return
 *       - NULL    Fail to new ES8156 codec interface
 *       - Others  ES8156 codec interface
 */
const audio_codec_if_t *es8156_codec_new(es8156_codec_cfg_t *codec_cfg);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
