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

typedef struct audio_hw_eq_t *audio_hw_eq_handle_t;

/**
 * @brief  EQ band parameter
 */
typedef struct {
    float     gain;       /*!< Band gain in dB */
    uint16_t  frequency;  /*!< Band frequency in Hz */
} eq_para_t;

/**
 * @brief  EQ hardware audio processing configuration
 */
typedef struct {
    const eq_para_t *para;        /*!< Array pointer for EQ parameters */
    uint8_t          filter_num;  /*!< Number of EQ filters */
} audio_eq_cfg_t;

/**
 * @brief  EQ hardware audio processing implementation interface
 *
 * @note  Codec driver authors use this structure when implementing EQ processing handles.
 */
typedef struct audio_hw_eq_t audio_hw_eq_t;

struct audio_hw_eq_t {
    const audio_hw_base_t *base;                                                     /*!< Parent codec hardware base interface */
    int (*set_band_para)(const audio_hw_eq_t *h, const eq_para_t *para, int index);  /*!< Set EQ band */
    int (*set_cfg)(const audio_hw_eq_t *h, const audio_eq_cfg_t *cfg);               /*!< Set EQ config */
    int (*enable)(const audio_hw_eq_t *h, bool enable);                              /*!< Enable or disable EQ */
    int (*dump_info)(const audio_hw_eq_t *h);                                        /*!< Dump EQ information */
};

/**
 * @brief  Create an EQ hardware audio processing handle
 *
 * @note  The returned handle is owned by the caller and must be released with
 *        audio_hw_eq_delete() before deleting the parent codec interface.
 * @note  This API may allocate heap memory, may block on codec control access,
 *        and is not ISR-safe.
 *
 * @param[in]   codec_if  Codec interface returned by audio_codec_new()
 * @param[out]  eq        Created EQ hardware audio processing handle
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  codec_if or eq is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support EQ hardware audio processing
 *       - ESP_CODEC_DEV_NO_MEM       Memory allocation failed
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 */
int audio_hw_eq_new(const audio_codec_if_t *codec_if, audio_hw_eq_handle_t *eq);

/**
 * @brief  Set EQ hardware audio processing configuration
 *
 * @param[in]  h    EQ hardware audio processing handle
 * @param[in]  cfg  EQ configuration; caller retains ownership. cfg->para must not be NULL
 *                  and cfg->filter_num must be greater than 0. Some codecs may require
 *                  a codec-specific minimum number of filters.
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h or cfg is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_eq_set_cfg(const audio_hw_eq_handle_t h, const audio_eq_cfg_t *cfg);

/**
 * @brief  Set one EQ band parameter
 *
 * @param[in]  h      EQ hardware audio processing handle
 * @param[in]  para   EQ band parameter; caller retains ownership
 * @param[in]  index  EQ band index
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h or para is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_eq_set_band_para(const audio_hw_eq_handle_t h, const eq_para_t *para, int index);

/**
 * @brief  Enable or disable EQ
 *
 * @param[in]  h       EQ hardware audio processing handle
 * @param[in]  enable  True to enable EQ, false to disable it
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_eq_enable(const audio_hw_eq_handle_t h, bool enable);

/**
 * @brief  Dump EQ hardware audio processing information
 *
 * @param[in]  h  EQ hardware audio processing handle
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_eq_dump_info(const audio_hw_eq_handle_t h);

/**
 * @brief  Delete an EQ hardware audio processing handle
 *
 * @note  After this call succeeds, eq is invalid and must not be used again.
 *        The caller may set eq to NULL after deletion to avoid a dangling handle.
 *
 * @param[in]  eq  EQ hardware audio processing handle to delete
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  eq is NULL
 */
int audio_hw_eq_delete(audio_hw_eq_handle_t eq);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
