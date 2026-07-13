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

typedef struct audio_hw_line_t *audio_hw_line_handle_t;

/**
 * @brief  Line hardware audio processing implementation interface
 *
 * @note  Codec driver authors use this structure when implementing line processing handles.
 */
typedef struct audio_hw_line_t audio_hw_line_t;

struct audio_hw_line_t {
    const audio_hw_base_t *base;                               /*!< Parent codec hardware base interface */
    int (*enable_in)(const audio_hw_line_t *h, bool enable);   /*!< Enable or disable line-in mode */
    int (*enable_out)(const audio_hw_line_t *h, bool enable);  /*!< Enable or disable line-out mode */
};

/**
 * @brief  Create a line-in and line-out hardware audio processing handle
 *
 * @note  The returned handle is owned by the caller and must be released with
 *        audio_hw_line_delete() before deleting the parent codec interface.
 * @note  This API may allocate heap memory, may block on codec control access,
 *        and is not ISR-safe.
 *
 * @param[in]   codec_if  Codec interface returned by audio_codec_new()
 * @param[out]  line      Created line hardware audio processing handle
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  codec_if or line is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support line hardware audio processing
 *       - ESP_CODEC_DEV_NO_MEM       Memory allocation failed
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 */
int audio_hw_line_new(const audio_codec_if_t *codec_if, audio_hw_line_handle_t *line);

/**
 * @brief  Enable or disable line-in mode
 *
 * @param[in]  h       Line hardware audio processing handle
 * @param[in]  enable  True to enable line-in mode, false to disable it
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_line_enable_in(const audio_hw_line_handle_t h, bool enable);

/**
 * @brief  Enable or disable line-out mode
 *
 * @param[in]  h       Line hardware audio processing handle
 * @param[in]  enable  True to enable line-out mode, false to disable it
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_line_enable_out(const audio_hw_line_handle_t h, bool enable);

/**
 * @brief  Delete a line hardware audio processing handle
 *
 * @note  After this call succeeds, line is invalid and must not be used again.
 *        The caller may set line to NULL after deletion to avoid a dangling handle.
 *
 * @param[in]  line  Line hardware audio processing handle to delete
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  line is NULL
 */
int audio_hw_line_delete(audio_hw_line_handle_t line);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
