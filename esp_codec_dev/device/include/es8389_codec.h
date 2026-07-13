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

#define ES8389_CODEC_DEFAULT_ADDR  (0x20)

/**
 * @brief  ES8389 codec configuration
 */
typedef struct {
    const audio_codec_ctrl_if_t *ctrl_if;  /*!< Codec Control interface */
    const audio_codec_gpio_if_t *gpio_if;  /*!< Codec GPIO interface */
    audio_hw_sys_cfg_t           sys_cfg;  /*!< System clock configuration */
    audio_hw_adc_cfg_t           adc_cfg;  /*!< ADC / microphone configuration */
    audio_hw_dac_cfg_t           dac_cfg;  /*!< DAC configuration for internal loopback */
    audio_hw_pa_cfg_t            pa_cfg;   /*!< PA and analog hardware gain */
} es8389_codec_cfg_t;

/**
 * @brief  New ES8389 codec interface
 *
 * @param[in]  codec_cfg  ES8389 codec configuration
 *
 * @return
 *       - NULL    Fail to new ES8389 codec interface
 *       - Others  ES8389 codec interface
 */
const audio_codec_if_t *es8389_codec_new(es8389_codec_cfg_t *codec_cfg);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
