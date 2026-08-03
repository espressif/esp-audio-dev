/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_codec_dev.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Auto mute hardware audio processing configuration
 */
typedef struct {
    float     noise_gate;   /*!< Noise gate threshold in dB */
    float     mute_vol;     /*!< Mute volume in dB */
    uint16_t  window_size;  /*!< Number of samples in the mute window */
} esp_audio_hw_auto_mute_cfg_t;

#define ESP_AUDIO_HW_AUTO_MUTE_CFG_DEFAULT()  {  \
    .noise_gate  = -60.0f,                       \
    .window_size = 4096,                         \
    .mute_vol    = -10.0f,                       \
}

/**
 * @brief  Soft mute hardware audio processing configuration
 */
typedef struct {
    float  ramp_rate;  /*!< Ramp rate in dB/s */
} esp_audio_hw_soft_mute_cfg_t;

#define ESP_AUDIO_HW_SOFT_MUTE_CFG_DEFAULT()  {  \
    .ramp_rate = -6.0f,                          \
}

/**
 * @brief  Set auto mute configuration
 *
 * @note  Operates on the codec chip bound to `dev`, independent of IN/OUT direction.
 *        `dev` must remain valid for the duration of the call; using it after
 *        esp_codec_dev_delete() is undefined behavior.
 * @note  This API is not ISR-safe and may block on the hw_proc lock.
 *
 * @param[in]  dev  Codec device handle
 * @param[in]  cfg  Auto mute configuration; caller retains ownership
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  dev or cfg is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 *       - ESP_CODEC_DEV_NO_MEM       hw_proc lock creation failed
 *       - ESP_CODEC_DEV_TIMEOUT      hw_proc lock acquisition timed out
 */
int esp_audio_hw_auto_mute_set_cfg(esp_codec_dev_handle_t dev, const esp_audio_hw_auto_mute_cfg_t *cfg);

/**
 * @brief  Enable or disable auto mute
 *
 * @param[in]  dev     Codec device handle
 * @param[in]  enable  True to enable auto mute, false to disable it
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  dev is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 *       - ESP_CODEC_DEV_NO_MEM       hw_proc lock creation failed
 *       - ESP_CODEC_DEV_TIMEOUT      hw_proc lock acquisition timed out
 */
int esp_audio_hw_auto_mute_enable(esp_codec_dev_handle_t dev, bool enable);

/**
 * @brief  Set soft mute configuration
 *
 * @param[in]  dev  Codec device handle
 * @param[in]  cfg  Soft mute configuration; caller retains ownership
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  dev or cfg is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 *       - ESP_CODEC_DEV_NO_MEM       hw_proc lock creation failed
 *       - ESP_CODEC_DEV_TIMEOUT      hw_proc lock acquisition timed out
 */
int esp_audio_hw_soft_mute_set_cfg(esp_codec_dev_handle_t dev, const esp_audio_hw_soft_mute_cfg_t *cfg);

/**
 * @brief  Enable or disable soft mute
 *
 * @param[in]  dev     Codec device handle
 * @param[in]  enable  True to enable soft mute, false to disable it
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  dev is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 *       - ESP_CODEC_DEV_NO_MEM       hw_proc lock creation failed
 *       - ESP_CODEC_DEV_TIMEOUT      hw_proc lock acquisition timed out
 */
int esp_audio_hw_soft_mute_enable(esp_codec_dev_handle_t dev, bool enable);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
