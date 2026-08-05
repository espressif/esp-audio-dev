/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>

#include "esp_codec_dev.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  ALC noise gate behavior
 */
typedef enum {
    ESP_AUDIO_HW_ALC_NOISE_GATE_DISABLE     = 0,  /*!< Disable noise gate */
    ESP_AUDIO_HW_ALC_NOISE_GATE_PGA_HOLD    = 1,  /*!< Hold PGA gain */
    ESP_AUDIO_HW_ALC_NOISE_GATE_MUTE_ADC    = 2,  /*!< Mute ADC */
    ESP_AUDIO_HW_ALC_NOISE_GATE_ANALOG_FADE = 3,  /*!< Apply analog fade */
    ESP_AUDIO_HW_ALC_NOISE_GATE_FADE_MUTE   = 4,  /*!< Apply fade mute */
} esp_audio_hw_alc_noise_gate_mode_t;

/**
 * @brief  ALC hardware audio processing configuration
 */
typedef struct {
    float                               target_gain;           /*!< Target gain in dB */
    float                               max_gain;              /*!< Maximum gain in dB */
    float                               min_gain;              /*!< Minimum gain in dB */
    float                               hold_time_ms;          /*!< Hold time in milliseconds */
    float                               decay_time_us;         /*!< Decay time in microseconds */
    float                               attack_time_us;        /*!< Attack time in microseconds */
    float                               noise_gate_threshold;  /*!< Noise gate threshold in dB */
    esp_audio_hw_alc_noise_gate_mode_t  noise_gate_mode;       /*!< Noise gate behavior */
} esp_audio_hw_alc_cfg_t;

#define ESP_AUDIO_HW_ALC_CFG_DEFAULT()  {                          \
    .target_gain          = -15.0f,                                \
    .max_gain             = 40.0f,                                 \
    .min_gain             = -30.0f,                                \
    .hold_time_ms         = 150.0f,                                \
    .decay_time_us        = 10000.0f,                              \
    .attack_time_us       = 5000.0f,                               \
    .noise_gate_threshold = -50.0f,                                \
    .noise_gate_mode      = ESP_AUDIO_HW_ALC_NOISE_GATE_MUTE_ADC,  \
}

/**
 * @brief  Initialize ALC hardware audio processing
 *
 * @note  Operates on the codec chip bound to `dev`, independent of IN/OUT direction.
 *        `dev` must remain valid for the duration of the call; using it after
 *        esp_codec_dev_delete() is undefined behavior.
 * @note  This API is not ISR-safe and may block on the hw_proc lock.
 *
 * @param[in]  dev  Codec device handle
 * @param[in]  cfg  ALC configuration; caller retains ownership
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  dev or cfg is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 *       - ESP_CODEC_DEV_NO_MEM       hw_proc lock creation failed
 *       - ESP_CODEC_DEV_TIMEOUT      hw_proc lock acquisition timed out
 */
int esp_audio_hw_alc_init(esp_codec_dev_handle_t dev, const esp_audio_hw_alc_cfg_t *cfg);

/**
 * @brief  Set ALC target gain
 *
 * @param[in]  dev          Codec device handle
 * @param[in]  target_gain  Target gain in dB
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  dev is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 *       - ESP_CODEC_DEV_NO_MEM       hw_proc lock creation failed
 *       - ESP_CODEC_DEV_TIMEOUT      hw_proc lock acquisition timed out
 */
int esp_audio_hw_alc_set_gain(esp_codec_dev_handle_t dev, float target_gain);

/**
 * @brief  Set ALC channel mask
 *
 * @param[in]  dev           Codec device handle
 * @param[in]  channel_mask  Channel mask to enable; 0 disables ALC
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  dev is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 *       - ESP_CODEC_DEV_NO_MEM       hw_proc lock creation failed
 *       - ESP_CODEC_DEV_TIMEOUT      hw_proc lock acquisition timed out
 */
int esp_audio_hw_alc_set_channel_mask(esp_codec_dev_handle_t dev, int channel_mask);

/**
 * @brief  Set ALC noise gate threshold
 *
 * @param[in]  dev        Codec device handle
 * @param[in]  threshold  Noise gate threshold in dB
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  dev is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 *       - ESP_CODEC_DEV_NO_MEM       hw_proc lock creation failed
 *       - ESP_CODEC_DEV_TIMEOUT      hw_proc lock acquisition timed out
 */
int esp_audio_hw_alc_set_noise_gate(esp_codec_dev_handle_t dev, float threshold);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
