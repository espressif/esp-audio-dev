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

#define ES8374_CODEC_DEFAULT_ADDR    (0x20)
#define ES8374_CODEC_DEFAULT_ADDR_1  (0x21)

/**
 * @brief  ES8374 codec configuration
 */
typedef struct {
    const audio_codec_ctrl_if_t *ctrl_if;  /*!< Codec Control interface */
    const audio_codec_gpio_if_t *gpio_if;  /*!< Codec GPIO interface */
    audio_hw_sys_cfg_t           sys_cfg;  /*!< System clock configuration */
    audio_hw_adc_cfg_t           adc_cfg;  /*!< ADC/Microphone configuration */
    audio_hw_dac_cfg_t           dac_cfg;  /*!< DAC configuration */
    audio_hw_pa_cfg_t            pa_cfg;   /*!< PA configuration */
} es8374_codec_cfg_t;

/**
 * @brief  New ES8374 codec interface
 *
 * @param[in]  codec_cfg  ES8374 codec configuration
 *
 * @return
 *       - NULL    Fail to new ES8374 codec interface
 *       - Others  ES8374 codec interface
 */
const audio_codec_if_t *es8374_codec_new(es8374_codec_cfg_t *codec_cfg);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
