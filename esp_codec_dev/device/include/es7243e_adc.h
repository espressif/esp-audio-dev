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

#define ES7243E_CODEC_DEFAULT_ADDR  (0x20)

/**
 * @brief  ES7243E codec configuration
 */
typedef struct {
    const audio_codec_ctrl_if_t *ctrl_if;  /*!< Codec Control interface */
    audio_hw_adc_cfg_t           adc_cfg;  /*!< ADC configuration */
} es7243e_codec_cfg_t;

/**
 * @brief  New ES7243E codec interface
 *
 * @param[in]  codec_cfg  ES7243E codec configuration
 *
 * @return
 *       - NULL    Fail to new ES7243E codec interface
 *       - Others  ES7243E codec interface
 */
const audio_codec_if_t *es7243e_codec_new(es7243e_codec_cfg_t *codec_cfg);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
