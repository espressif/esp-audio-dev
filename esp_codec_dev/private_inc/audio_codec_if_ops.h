/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "audio_codec_if.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Enable or disable ADC path through the codec interface
 *
 * @param[in]  h       Codec interface instance
 * @param[in]  enable  true to enable, false to disable
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h is NULL or ADC path is unavailable
 */
int audio_codec_adc_enable(const audio_codec_if_t *h, bool enable);

/**
 * @brief  Mute or unmute ADC channels through the codec interface
 *
 * @param[in]  h        Codec interface instance
 * @param[in]  ch_mask  Channel mask to update
 * @param[in]  mute     true to mute, false to unmute
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h is NULL or ADC mute operation is unavailable
 */
int audio_codec_adc_mute(const audio_codec_if_t *h, int ch_mask, bool mute);

/**
 * @brief  Set ADC channel gain through the codec interface
 *
 * @param[in]  h        Codec interface instance
 * @param[in]  ch_mask  Channel mask to update
 * @param[in]  db       Gain in dB
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h is NULL or ADC gain operation is unavailable
 */
int audio_codec_adc_set_vol(const audio_codec_if_t *h, int ch_mask, float db);

/**
 * @brief  Enable or disable DAC path through the codec interface
 *
 * @param[in]  h       Codec interface instance
 * @param[in]  enable  true to enable, false to disable
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h is NULL or DAC path is unavailable
 */
int audio_codec_dac_enable(const audio_codec_if_t *h, bool enable);

/**
 * @brief  Mute or unmute DAC channels through the codec interface
 *
 * @param[in]  h        Codec interface instance
 * @param[in]  ch_mask  Channel mask to update
 * @param[in]  mute     true to mute, false to unmute
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h is NULL or DAC mute operation is unavailable
 */
int audio_codec_dac_mute(const audio_codec_if_t *h, int ch_mask, bool mute);

/**
 * @brief  Set DAC channel volume through the codec interface
 *
 * @param[in]  h        Codec interface instance
 * @param[in]  ch_mask  Channel mask to update
 * @param[in]  db       Volume in dB
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h is NULL or DAC volume operation is unavailable
 */
int audio_codec_dac_set_vol(const audio_codec_if_t *h, int ch_mask, float db);

/**
 * @brief  Enable or disable DAC PA through the codec interface
 *
 * @param[in]  h       Codec interface instance
 * @param[in]  enable  true to enable, false to disable
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h is NULL or DAC PA operation is unavailable
 */
int audio_codec_dac_pa_enable(const audio_codec_if_t *h, bool enable);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
