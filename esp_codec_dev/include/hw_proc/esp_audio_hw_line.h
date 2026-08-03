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
 * @brief  Enable or disable line-in mode
 *
 * @note  Operates on the codec chip bound to `dev`, independent of IN/OUT direction.
 *        `dev` must remain valid for the duration of the call; using it after
 *        esp_codec_dev_delete() is undefined behavior.
 * @note  This API is not ISR-safe and may block on the hw_proc lock.
 *
 * @param[in]  dev     Codec device handle
 * @param[in]  enable  True to enable line-in mode, false to disable it
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  dev is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 *       - ESP_CODEC_DEV_NO_MEM       hw_proc lock creation failed
 *       - ESP_CODEC_DEV_TIMEOUT      hw_proc lock acquisition timed out
 */
int esp_audio_hw_line_enable_in(esp_codec_dev_handle_t dev, bool enable);

/**
 * @brief  Enable or disable line-out mode
 *
 * @param[in]  dev     Codec device handle
 * @param[in]  enable  True to enable line-out mode, false to disable it
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  dev is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 *       - ESP_CODEC_DEV_NO_MEM       hw_proc lock creation failed
 *       - ESP_CODEC_DEV_TIMEOUT      hw_proc lock acquisition timed out
 */
int esp_audio_hw_line_enable_out(esp_codec_dev_handle_t dev, bool enable);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
