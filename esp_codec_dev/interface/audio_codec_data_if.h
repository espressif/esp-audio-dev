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

typedef struct audio_codec_data_if_t audio_codec_data_if_t;

/**
 * @brief  Audio codec data interface structure
 */
struct audio_codec_data_if_t {
    int (*open)(const audio_codec_data_if_t *h, void *data_cfg, int cfg_size);  /*!< Open data interface */
    bool (*is_open)(const audio_codec_data_if_t *h);                            /*!< Check whether data interface is opened */
    int (*get_mode)(const audio_codec_data_if_t *h,
                    esp_codec_dev_i2s_mode_t *in_mode, esp_codec_dev_i2s_mode_t *out_mode);  /*!< Get I2S mode */
    int (*get_fmt)(const audio_codec_data_if_t *h,
                   esp_codec_dev_type_t dev_type, esp_codec_dev_sample_info_t *fs);  /*!< Get sample format from data interface or underlying bus */
    int (*get_order)(const audio_codec_data_if_t *h,
                     uint8_t channel, uint16_t channel_mask, esp_codec_dev_channel_map_t *map);  /*!< Resolve data-interface logical-channel to physical-slot map from total channels and selected slot mask */
    int (*get_channel_mask)(const audio_codec_data_if_t *h,
                            uint8_t channel, const esp_codec_dev_channel_map_t *map, uint16_t *channel_mask);  /*!< Resolve selected slot mask from total data channels and data-interface map */
    int (*enable)(const audio_codec_data_if_t *h, esp_codec_dev_type_t dev_type, bool enable);                 /*!< Enable input or output channel */
    int (*set_fmt)(const audio_codec_data_if_t *h,
                   esp_codec_dev_type_t dev_type, esp_codec_dev_sample_info_t *fs);  /*!< Set audio format to data interface */
    int (*read)(const audio_codec_data_if_t *h, uint8_t *data, int size);            /*!< Read data from data interface */
    int (*write)(const audio_codec_data_if_t *h, uint8_t *data, int size);           /*!< Write data to data interface */
    int (*close)(const audio_codec_data_if_t *h);                                    /*!< Close data interface */
};

/**
 * @brief  Delete codec data interface instance
 *
 * @param[in]  data_if  Codec data interface
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Delete success
 *       - ESP_CODEC_DEV_INVALID_ARG  Input is NULL pointer
 */
int audio_codec_delete_data_if(const audio_codec_data_if_t *data_if);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
