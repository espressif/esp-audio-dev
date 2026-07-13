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
 * @brief  ES8388 default I2C address
 */
#define ES8388_CODEC_DEFAULT_ADDR    (0x20)
#define ES8388_CODEC_DEFAULT_ADDR_1  (0x22)

/**
 * @brief  ES8388 codec configuration
 */
typedef struct {
    const audio_codec_ctrl_if_t *ctrl_if;  /*!< Codec Control interface */
    const audio_codec_gpio_if_t *gpio_if;  /*!< Codec GPIO interface */
    audio_hw_sys_cfg_t           sys_cfg;  /*!< System clock configuration */
    audio_hw_pa_cfg_t            pa_cfg;   /*!< PA configuration */
} es8388_codec_cfg_t;

/**
 * @brief  New ES8388 codec interface
 *
 * @param[in]  codec_cfg  ES8388 codec configuration
 *
 * @return
 *       - NULL    Fail to new ES8388 codec interface
 *       - Others  ES8388 codec interface
 */
const audio_codec_if_t *es8388_codec_new(es8388_codec_cfg_t *codec_cfg);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
