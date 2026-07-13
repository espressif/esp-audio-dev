/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
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
 * @brief  Dummy codec
 *
 *         This codec is used when a board has no external codec chip but still uses a speaker PA.
 *         In `esp_codec_dev`, PA power control is tied to the codec open/close lifecycle.
 *         By configuring `pa_pin` with this dummy codec, applications can still use APIs such as
 *         `esp_codec_dev_open()` to automatically enable/disable the PA.
 */
typedef struct {
    const audio_codec_gpio_if_t *gpio_if;  /*!< GPIO interface used for PA control */
    audio_hw_pa_cfg_t            pa_cfg;   /*!< PA configuration */
} dummy_codec_cfg_t;

/**
 * @brief  New dummy codec interface
 *
 * @param[in]  cfg  Dummy codec configuration
 *
 * @return
 *       - NULL    Fail to new dummy codec interface
 *       - Others  Dummy codec interface
 */
const audio_codec_if_t *dummy_codec_new(dummy_codec_cfg_t *cfg);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
