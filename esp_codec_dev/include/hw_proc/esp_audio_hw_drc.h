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
 * @brief  DRC hardware audio processing configuration
 */
typedef struct {
    float     max_gain;         /*!< Maximum gain in dB */
    float     min_gain;         /*!< Minimum gain in dB */
    float     noise_threshold;  /*!< Noise threshold in dB */
    float     offset_gain;      /*!< Offset gain in dB */
    bool      hard_soft_knee;   /*!< True to use soft knee, false to use hard knee */
    uint8_t   slope;            /*!< Compression slope, such as 20 for 1:20 */
    uint32_t  decay_time_us;    /*!< Decay time in microseconds */
    uint32_t  attack_time_us;   /*!< Attack time in microseconds */
} esp_audio_hw_drc_cfg_t;

#define ESP_AUDIO_HW_DRC_CFG_DEFAULT()  {  \
    .max_gain        = -6.0f,              \
    .min_gain        = -30.1f,             \
    .noise_threshold = -70.0f,             \
    .decay_time_us   = 10000,              \
    .attack_time_us  = 5000,               \
    .hard_soft_knee  = false,              \
    .slope           = 20,                 \
    .offset_gain     = 0.0f,               \
}

/**
 * @brief  Initialize DRC hardware audio processing
 *
 * @note  Operates on the codec chip bound to `dev`, independent of IN/OUT direction.
 *        `dev` must remain valid for the duration of the call; using it after
 *        esp_codec_dev_delete() is undefined behavior.
 * @note  This API is not ISR-safe and may block on the hw_proc lock.
 *
 * @param[in]  dev  Codec device handle
 * @param[in]  cfg  DRC configuration; caller retains ownership
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  dev or cfg is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 *       - ESP_CODEC_DEV_NO_MEM       hw_proc lock creation failed
 *       - ESP_CODEC_DEV_TIMEOUT      hw_proc lock acquisition timed out
 */
int esp_audio_hw_drc_init(esp_codec_dev_handle_t dev, const esp_audio_hw_drc_cfg_t *cfg);

/**
 * @brief  Set DRC offset gain
 *
 * @param[in]  dev          Codec device handle
 * @param[in]  offset_gain  Offset gain in dB
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  dev is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 *       - ESP_CODEC_DEV_NO_MEM       hw_proc lock creation failed
 *       - ESP_CODEC_DEV_TIMEOUT      hw_proc lock acquisition timed out
 */
int esp_audio_hw_drc_set_offset_gain(esp_codec_dev_handle_t dev, float offset_gain);

/**
 * @brief  Enable or disable DRC
 *
 * @param[in]  dev     Codec device handle
 * @param[in]  enable  True to enable DRC, false to disable it
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  dev is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 *       - ESP_CODEC_DEV_NO_MEM       hw_proc lock creation failed
 *       - ESP_CODEC_DEV_TIMEOUT      hw_proc lock acquisition timed out
 */
int esp_audio_hw_drc_enable(esp_codec_dev_handle_t dev, bool enable);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
