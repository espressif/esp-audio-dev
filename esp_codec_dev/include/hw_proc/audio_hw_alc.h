/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>

#include "audio_codec_if.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef struct audio_hw_alc_t *audio_hw_alc_handle_t;

/**
 * @brief  ALC noise gate behavior
 */
typedef enum {
    ALC_NOISE_GATE_DISABLE     = 0,  /*!< Disable noise gate */
    ALC_NOISE_GATE_PGA_HOLD    = 1,  /*!< Hold PGA gain */
    ALC_NOISE_GATE_MUTE_ADC    = 2,  /*!< Mute ADC */
    ALC_NOISE_GATE_ANALOG_FADE = 3,  /*!< Apply analog fade */
    ALC_NOISE_GATE_FADE_MUTE   = 4,  /*!< Apply fade mute */
} alc_noise_gate_mode_t;

/**
 * @brief  ALC hardware audio processing configuration
 */
typedef struct {
    float                  target_gain;           /*!< Target gain in dB */
    float                  max_gain;              /*!< Maximum gain in dB */
    float                  min_gain;              /*!< Minimum gain in dB */
    float                  hold_time_ms;          /*!< Hold time in milliseconds */
    float                  decay_time_us;         /*!< Decay time in microseconds */
    float                  attack_time_us;        /*!< Attack time in microseconds */
    float                  noise_gate_threshold;  /*!< Noise gate threshold in dB */
    alc_noise_gate_mode_t  noise_gate_mode;       /*!< Noise gate behavior */
} audio_alc_cfg_t;

#define DEFAULT_ALC_CONFIG()  {                       \
    .target_gain          = -15.0f,                   \
    .max_gain             = 40.0f,                    \
    .min_gain             = -30.0f,                   \
    .hold_time_ms         = 150.0f,                   \
    .decay_time_us        = 10000.0f,                 \
    .attack_time_us       = 5000.0f,                  \
    .noise_gate_threshold = -50.0f,                   \
    .noise_gate_mode      = ALC_NOISE_GATE_MUTE_ADC,  \
}

/**
 * @brief  ALC hardware audio processing implementation interface
 *
 * @note  Codec driver authors use this structure when implementing ALC processing handles.
 */
typedef struct audio_hw_alc_t audio_hw_alc_t;

struct audio_hw_alc_t {
    const audio_hw_base_t *base;                                       /*!< Parent codec hardware base interface */
    int (*init)(const audio_hw_alc_t *h, const audio_alc_cfg_t *cfg);  /*!< Initialize ALC */
    int (*set_gain)(const audio_hw_alc_t *h, float target_gain);       /*!< Set ALC target gain */
    int (*set_channel)(const audio_hw_alc_t *h, int channel_mask);     /*!< Set ALC channel mask */
    int (*set_noise_gate)(const audio_hw_alc_t *h, float threshold);   /*!< Set noise gate threshold */
};

/**
 * @brief  Create an ALC hardware audio processing handle
 *
 * @note  The returned handle is owned by the caller and must be released with
 *        audio_hw_alc_delete() before deleting the parent codec interface.
 * @note  This API may allocate heap memory, may block on codec control access,
 *        and is not ISR-safe.
 *
 * @param[in]   codec_if  Codec interface returned by audio_codec_new()
 * @param[out]  alc       Created ALC hardware audio processing handle
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  codec_if or alc is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support ALC hardware audio processing
 *       - ESP_CODEC_DEV_NO_MEM       Memory allocation failed
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 */
int audio_hw_alc_new(const audio_codec_if_t *codec_if, audio_hw_alc_handle_t *alc);

/**
 * @brief  Initialize ALC hardware audio processing
 *
 * @param[in]  h    ALC hardware audio processing handle
 * @param[in]  cfg  ALC configuration; caller retains ownership
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h or cfg is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_alc_init(const audio_hw_alc_handle_t h, const audio_alc_cfg_t *cfg);

/**
 * @brief  Set ALC target gain
 *
 * @param[in]  h            ALC hardware audio processing handle
 * @param[in]  target_gain  Target gain in dB
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_alc_set_gain(const audio_hw_alc_handle_t h, float target_gain);

/**
 * @brief  Set ALC channel mask
 *
 * @param[in]  h             ALC hardware audio processing handle
 * @param[in]  channel_mask  Channel mask to enable; 0 disables ALC
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_alc_set_channel_mask(const audio_hw_alc_handle_t h, int channel_mask);

/**
 * @brief  Set ALC noise gate threshold
 *
 * @param[in]  h          ALC hardware audio processing handle
 * @param[in]  threshold  Noise gate threshold in dB
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_alc_set_noise_gate(const audio_hw_alc_handle_t h, float threshold);

/**
 * @brief  Delete an ALC hardware audio processing handle
 *
 * @note  After this call succeeds, alc is invalid and must not be used again.
 *        The caller may set alc to NULL after deletion to avoid a dangling handle.
 *
 * @param[in]  alc  ALC hardware audio processing handle to delete
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  alc is NULL
 */
int audio_hw_alc_delete(audio_hw_alc_handle_t alc);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
