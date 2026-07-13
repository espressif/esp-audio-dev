/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "audio_hw_base_priv.h"
#include "audio_hw_proc_if.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Create an ES8388 ALC hardware audio processing handle
 *
 * @param[in]   h    Codec hardware base interface
 * @param[out]  alc  Created ALC hardware audio processing handle
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h or alc is NULL
 *       - ESP_CODEC_DEV_NO_MEM       Memory allocation failed
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 */
int audio_hw_es8388_alc_new(const audio_hw_base_t *h, audio_hw_alc_handle_t *alc);

/**
 * @brief  Create an ES8388 line hardware audio processing handle
 *
 * @param[in]   h     Codec hardware base interface
 * @param[out]  line  Created line hardware audio processing handle
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h or line is NULL
 *       - ESP_CODEC_DEV_NO_MEM       Memory allocation failed
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 */
int audio_hw_es8388_line_new(const audio_hw_base_t *h, audio_hw_line_handle_t *line);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
