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
    int (*set_map_query)(const audio_codec_data_if_t *h, esp_codec_dev_type_t dev_type,
                         const esp_codec_dev_map_query_t *query);  /*!< Optional: set device-map query for bus planning; NULL if unsupported */
    int (*set_fmt)(const audio_codec_data_if_t *h, esp_codec_dev_type_t dev_type,
                   esp_codec_dev_sample_info_t *fs);                                            /*!< Set and return the committed audio format */
    int (*enable)(const audio_codec_data_if_t *h, esp_codec_dev_type_t dev_type, bool enable);  /*!< Enable input or output channel */
    int (*get_mode)(const audio_codec_data_if_t *h,
                    esp_codec_dev_i2s_mode_t *in_mode, esp_codec_dev_i2s_mode_t *out_mode);  /*!< Get I2S mode */
    int (*get_fmt)(const audio_codec_data_if_t *h,
                   esp_codec_dev_type_t dev_type, esp_codec_dev_sample_info_t *fs);  /*!< Get sample format from data interface or underlying bus */
    int (*get_bus_info)(const audio_codec_data_if_t *h, esp_codec_dev_type_t dev_type,
                        esp_codec_dev_bus_info_t *bus_info);                /*!< Query one direction's current bus state */
    int (*read)(const audio_codec_data_if_t *h, uint8_t *data, int size);   /*!< Read data from data interface */
    int (*write)(const audio_codec_data_if_t *h, uint8_t *data, int size);  /*!< Write data to data interface */
    int (*close)(const audio_codec_data_if_t *h);                           /*!< Close data interface */
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
