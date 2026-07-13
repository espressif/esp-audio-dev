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
 * @brief  Create an ES8311 ALC hardware audio processing handle
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
int audio_hw_es8311_alc_new(const audio_hw_base_t *h, audio_hw_alc_handle_t *alc);

/**
 * @brief  Create an ES8311 mute hardware audio processing handle
 *
 * @param[in]   h     Codec hardware base interface
 * @param[out]  mute  Created mute hardware audio processing handle
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h or mute is NULL
 *       - ESP_CODEC_DEV_NO_MEM       Memory allocation failed
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 */
int audio_hw_es8311_mute_new(const audio_hw_base_t *h, audio_hw_mute_handle_t *mute);

/**
 * @brief  Create an ES8311 DRC hardware audio processing handle
 *
 * @param[in]   h    Codec hardware base interface
 * @param[out]  drc  Created DRC hardware audio processing handle
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h or drc is NULL
 *       - ESP_CODEC_DEV_NO_MEM       Memory allocation failed
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 */
int audio_hw_es8311_drc_new(const audio_hw_base_t *h, audio_hw_drc_handle_t *drc);

/**
 * @brief  Create an ES8311 EQ hardware audio processing handle
 *
 * @param[in]   h   Codec hardware base interface
 * @param[out]  eq  Created EQ hardware audio processing handle
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h or eq is NULL
 *       - ESP_CODEC_DEV_NO_MEM       Memory allocation failed
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 */
int audio_hw_es8311_eq_new(const audio_hw_base_t *h, audio_hw_eq_handle_t *eq);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
