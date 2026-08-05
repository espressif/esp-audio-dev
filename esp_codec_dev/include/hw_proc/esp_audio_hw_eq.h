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
 * @brief  EQ band parameter
 */
typedef struct {
    float     gain;       /*!< Band gain in dB */
    uint16_t  frequency;  /*!< Band frequency in Hz */
} esp_audio_hw_eq_para_t;

/**
 * @brief  EQ hardware audio processing configuration
 *
 * @note  No default config macro: `para` and `filter_num` are required.
 */
typedef struct {
    const esp_audio_hw_eq_para_t *para;        /*!< Array pointer for EQ parameters */
    uint8_t                       filter_num;  /*!< Number of EQ filters */
} esp_audio_hw_eq_cfg_t;

/**
 * @brief  Set EQ hardware audio processing configuration
 *
 * @note  Operates on the codec chip bound to `dev`, independent of IN/OUT direction.
 *        `dev` must remain valid for the duration of the call; using it after
 *        esp_codec_dev_delete() is undefined behavior.
 * @note  This API is not ISR-safe and may block on the hw_proc lock.
 *
 * @param[in]  dev  Codec device handle
 * @param[in]  cfg  EQ configuration; caller retains ownership. cfg->para must not be NULL
 *                  and cfg->filter_num must be greater than 0. Some codecs may require
 *                  a codec-specific minimum number of filters.
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  dev or cfg is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 *       - ESP_CODEC_DEV_NO_MEM       hw_proc lock creation failed
 *       - ESP_CODEC_DEV_TIMEOUT      hw_proc lock acquisition timed out
 */
int esp_audio_hw_eq_set_cfg(esp_codec_dev_handle_t dev, const esp_audio_hw_eq_cfg_t *cfg);

/**
 * @brief  Set one EQ band parameter
 *
 * @param[in]  dev    Codec device handle
 * @param[in]  para   EQ band parameter; caller retains ownership
 * @param[in]  index  EQ band index
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  dev or para is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 *       - ESP_CODEC_DEV_NO_MEM       hw_proc lock creation failed
 *       - ESP_CODEC_DEV_TIMEOUT      hw_proc lock acquisition timed out
 */
int esp_audio_hw_eq_set_band_para(esp_codec_dev_handle_t dev, const esp_audio_hw_eq_para_t *para, int index);

/**
 * @brief  Enable or disable EQ
 *
 * @param[in]  dev     Codec device handle
 * @param[in]  enable  True to enable EQ, false to disable it
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  dev is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 *       - ESP_CODEC_DEV_NO_MEM       hw_proc lock creation failed
 *       - ESP_CODEC_DEV_TIMEOUT      hw_proc lock acquisition timed out
 */
int esp_audio_hw_eq_enable(esp_codec_dev_handle_t dev, bool enable);

/**
 * @brief  Dump EQ hardware audio processing information
 *
 * @param[in]  dev  Codec device handle
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  dev is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 *       - ESP_CODEC_DEV_NO_MEM       hw_proc lock creation failed
 *       - ESP_CODEC_DEV_TIMEOUT      hw_proc lock acquisition timed out
 */
int esp_audio_hw_eq_dump_info(esp_codec_dev_handle_t dev);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
