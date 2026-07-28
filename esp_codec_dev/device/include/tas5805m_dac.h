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

#define TAS5805M_CODEC_DEFAULT_ADDR  (0x5c)

/**
 * @brief  TAS5805M codec configuration
 */
typedef struct {
    const audio_codec_ctrl_if_t *ctrl_if;    /*!< Codec Control interface */
    const audio_codec_gpio_if_t *gpio_if;    /*!< Codec GPIO interface */
    audio_hw_sys_cfg_t           sys_cfg;    /*!< System clock configuration */
    audio_hw_pa_cfg_t            pa_cfg;     /*!< PA configuration */
    audio_hw_reset_cfg_t         reset_cfg;  /*!< Reset configuration */
} tas5805m_codec_cfg_t;

/**
 * @brief  New TAS5805M codec interface
 *
 * @param[in]  codec_cfg  TAS5805M codec configuration
 *
 * @return
 *       - NULL    Fail to new TAS5805M codec interface
 *       - Others  TAS5805M codec interface
 */
const audio_codec_if_t *tas5805m_codec_new(tas5805m_codec_cfg_t *codec_cfg);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
