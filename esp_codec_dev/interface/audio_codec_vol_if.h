/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_codec_dev_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef struct audio_codec_vol_if_t audio_codec_vol_if_t;

/**
 * @brief  Structure for volume interface
 */
struct audio_codec_vol_if_t {
    int (*open)(const audio_codec_vol_if_t *h,
                esp_codec_dev_sample_info_t *fs, int fade_time);    /*!< Open for software volume processor */
    int (*set_vol)(const audio_codec_vol_if_t *h, float db_value);  /*!< Set volume in decibel unit */
    int (*process)(const audio_codec_vol_if_t *h,
                   uint8_t *in, int len, uint8_t *out, int out_len);  /*!< Process data */
    int (*close)(const audio_codec_vol_if_t *h);                      /*!< Close volume processor */
};

/**
 * @brief  Delete volume interface instance
 *
 * @param[in]  vol_if  Volume interface
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Delete success
 *       - ESP_CODEC_DEV_INVALID_ARG  Input is NULL pointer
 */
int audio_codec_delete_vol_if(const audio_codec_vol_if_t *vol_if);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
