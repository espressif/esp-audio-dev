/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "audio_codec_if.h"
#include "audio_codec_hw_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define ES7210_CODEC_DEFAULT_ADDR  (0x80)

/**
 * @brief  ES7210 codec configuration, only supports ADC feature
 *
 *         ES7210 always works in TDM mode with all 4 MICs enabled.
 *         Use fs->channel_mask to select which channels to read.
 */
typedef struct {
    const audio_codec_ctrl_if_t *ctrl_if;  /*!< Codec Control interface */
    audio_hw_sys_cfg_t           sys_cfg;  /*!< System clock configuration */
    audio_hw_adc_cfg_t           adc_cfg;  /*!< ADC configuration */
} es7210_codec_cfg_t;

/**
 * @brief  New ES7210 codec interface
 *
 * @param[in]  codec_cfg  ES7210 codec configuration
 *
 * @return
 *       - NULL    Fail to new ES7210 codec interface
 *       - Others  ES7210 codec interface
 */
const audio_codec_if_t *es7210_codec_new(es7210_codec_cfg_t *codec_cfg);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
