/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "audio_codec_if.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef struct audio_hw_mute_t *audio_hw_mute_handle_t;

/**
 * @brief  Auto mute hardware audio processing configuration
 */
typedef struct {
    float     noise_gate;   /*!< Noise gate threshold in dB */
    float     mute_vol;     /*!< Mute volume in dB */
    uint16_t  window_size;  /*!< Number of samples in the mute window */
} auto_mute_cfg_t;

#define DEFAULT_AUTO_MUTE_CONFIG()  {  \
    .noise_gate  = -60.0f,             \
    .window_size = 4096,               \
    .mute_vol    = -10.0f,             \
}

/**
 * @brief  Soft mute hardware audio processing configuration
 */
typedef struct {
    float  ramp_rate;  /*!< Ramp rate in dB/s */
} soft_mute_cfg_t;

#define DEFAULT_SOFT_MUTE_CONFIG()  {  \
    .ramp_rate = -6.0f,                \
}

/**
 * @brief  Mute hardware audio processing implementation interface
 *
 * @note  Codec driver authors use this structure when implementing mute processing handles.
 */
typedef struct audio_hw_mute_t audio_hw_mute_t;

struct audio_hw_mute_t {
    const audio_hw_base_t *base;                                                     /*!< Parent codec hardware base interface */
    int (*set_auto_mute_cfg)(const audio_hw_mute_t *h, const auto_mute_cfg_t *cfg);  /*!< Set auto mute config */
    int (*enable_auto_mute)(const audio_hw_mute_t *h, bool enable);                  /*!< Enable or disable auto mute */
    int (*set_soft_mute_cfg)(const audio_hw_mute_t *h, const soft_mute_cfg_t *cfg);  /*!< Set soft mute config */
    int (*enable_soft_mute)(const audio_hw_mute_t *h, bool enable);                  /*!< Enable or disable soft mute */
};

/**
 * @brief  Create a mute hardware audio processing handle
 *
 * @note  The returned handle is owned by the caller and must be released with
 *        audio_hw_mute_delete() before deleting the parent codec interface.
 * @note  This API may allocate heap memory, may block on codec control access,
 *        and is not ISR-safe.
 *
 * @param[in]   codec_if  Codec interface returned by audio_codec_new()
 * @param[out]  mute      Created mute hardware audio processing handle
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  codec_if or mute is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support mute hardware audio processing
 *       - ESP_CODEC_DEV_NO_MEM       Memory allocation failed
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 */
int audio_hw_mute_new(const audio_codec_if_t *codec_if, audio_hw_mute_handle_t *mute);

/**
 * @brief  Set auto mute configuration
 *
 * @param[in]  h    Mute hardware audio processing handle
 * @param[in]  cfg  Auto mute configuration; caller retains ownership
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h or cfg is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_auto_mute_set_cfg(const audio_hw_mute_handle_t h, const auto_mute_cfg_t *cfg);

/**
 * @brief  Enable or disable auto mute
 *
 * @param[in]  h       Mute hardware audio processing handle
 * @param[in]  enable  True to enable auto mute, false to disable it
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_auto_mute_enable(const audio_hw_mute_handle_t h, bool enable);

/**
 * @brief  Set soft mute configuration
 *
 * @param[in]  h    Mute hardware audio processing handle
 * @param[in]  cfg  Soft mute configuration; caller retains ownership
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h or cfg is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_soft_mute_set_cfg(const audio_hw_mute_handle_t h, const soft_mute_cfg_t *cfg);

/**
 * @brief  Enable or disable soft mute
 *
 * @param[in]  h       Mute hardware audio processing handle
 * @param[in]  enable  True to enable soft mute, false to disable it
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_soft_mute_enable(const audio_hw_mute_handle_t h, bool enable);

/**
 * @brief  Delete a mute hardware audio processing handle
 *
 * @note  After this call succeeds, mute is invalid and must not be used again.
 *        The caller may set mute to NULL after deletion to avoid a dangling handle.
 *
 * @param[in]  mute  Mute hardware audio processing handle to delete
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  mute is NULL
 */
int audio_hw_mute_delete(audio_hw_mute_handle_t mute);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
