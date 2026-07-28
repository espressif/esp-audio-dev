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

typedef struct audio_hw_drc_t *audio_hw_drc_handle_t;

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
} audio_drc_cfg_t;

#define DEFAULT_DRC_CONFIG()  {  \
    .max_gain        = -6.0f,    \
    .min_gain        = -30.1f,   \
    .noise_threshold = -70.0f,   \
    .decay_time_us   = 10000,    \
    .attack_time_us  = 5000,     \
    .hard_soft_knee  = false,    \
    .slope           = 20,       \
    .offset_gain     = 0.0f,     \
}

/**
 * @brief  DRC hardware audio processing implementation interface
 *
 * @note  Codec driver authors use this structure when implementing DRC processing handles.
 */
typedef struct audio_hw_drc_t audio_hw_drc_t;

struct audio_hw_drc_t {
    const audio_hw_base_t *base;                                       /*!< Parent codec hardware base interface */
    int (*init)(const audio_hw_drc_t *h, const audio_drc_cfg_t *cfg);  /*!< Initialize DRC */
    int (*set_offset_gain)(const audio_hw_drc_t *h, float gain);       /*!< Set DRC offset gain */
    int (*enable)(const audio_hw_drc_t *h, bool enable);               /*!< Enable or disable DRC */
};

/**
 * @brief  Create a DRC hardware audio processing handle
 *
 * @note  The returned handle is owned by the caller and must be released with
 *        audio_hw_drc_delete() before deleting the parent codec interface.
 * @note  This API may allocate heap memory, may block on codec control access,
 *        and is not ISR-safe.
 *
 * @param[in]   codec_if  Codec interface returned by audio_codec_new()
 * @param[out]  drc       Created DRC hardware audio processing handle
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  codec_if or drc is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support DRC hardware audio processing
 *       - ESP_CODEC_DEV_NO_MEM       Memory allocation failed
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 */
int audio_hw_drc_new(const audio_codec_if_t *codec_if, audio_hw_drc_handle_t *drc);

/**
 * @brief  Initialize DRC hardware audio processing
 *
 * @param[in]  h    DRC hardware audio processing handle
 * @param[in]  cfg  DRC configuration; caller retains ownership
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h or cfg is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_drc_init(const audio_hw_drc_handle_t h, const audio_drc_cfg_t *cfg);

/**
 * @brief  Set DRC offset gain
 *
 * @param[in]  h            DRC hardware audio processing handle
 * @param[in]  offset_gain  Offset gain in dB
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_drc_set_offset_gain(const audio_hw_drc_handle_t h, float offset_gain);

/**
 * @brief  Enable or disable DRC
 *
 * @param[in]  h       DRC hardware audio processing handle
 * @param[in]  enable  True to enable DRC, false to disable it
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_drc_enable(const audio_hw_drc_handle_t h, bool enable);

/**
 * @brief  Delete a DRC hardware audio processing handle
 *
 * @note  After this call succeeds, drc is invalid and must not be used again.
 *        The caller may set drc to NULL after deletion to avoid a dangling handle.
 *
 * @param[in]  drc  DRC hardware audio processing handle to delete
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  drc is NULL
 */
int audio_hw_drc_delete(audio_hw_drc_handle_t drc);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
