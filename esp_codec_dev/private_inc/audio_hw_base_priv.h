/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "audio_hw_base.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Open a codec hardware base interface
 *
 * @param[in]  h  Codec hardware base interface
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_open(const audio_hw_base_t *h);

/**
 * @brief  Check whether a codec hardware base interface is open
 *
 * @param[in]  h  Codec hardware base interface
 *
 * @return
 *       - true   The codec hardware base interface is open
 *       - false  h is NULL, the operation is unsupported, or the codec is closed
 */
bool audio_hw_is_open(const audio_hw_base_t *h);

/**
 * @brief  Set sample information on a codec hardware base interface
 *
 * @param[in]  h     Codec hardware base interface
 * @param[in]  fs    Sample information; caller retains ownership
 * @param[in]  type  Direction mask: IN / OUT / IN_OUT
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h or fs is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_set_fs(const audio_hw_base_t *h, esp_codec_dev_sample_info_t *fs, esp_codec_dev_type_t type);

/**
 * @brief  Write one codec register
 *
 * @param[in]  h      Codec hardware base interface
 * @param[in]  reg    Register address
 * @param[in]  value  Register value
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_set_reg(const audio_hw_base_t *h, int reg, int value);

/**
 * @brief  Read one codec register
 *
 * @param[in]   h      Codec hardware base interface
 * @param[in]   reg    Register address
 * @param[out]  value  Register value
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h or value is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_get_reg(const audio_hw_base_t *h, int reg, int *value);

/**
 * @brief  Dump codec registers
 *
 * @param[in]  h  Codec hardware base interface
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_dump_reg(const audio_hw_base_t *h);

/**
 * @brief  Close a codec hardware base interface
 *
 * @param[in]  h  Codec hardware base interface
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_close(const audio_hw_base_t *h);

/**
 * @brief  Get supported data order information
 *
 * @param[in]   h           Codec hardware base interface
 * @param[out]  order_list  Supported data order information list
 * @param[out]  list_size   Number of entries in order_list
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h, order_list, or list_size is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_get_order_list(const audio_hw_base_t *h, const esp_codec_dev_device_map_info_t **order_list, int *list_size);

/**
 * @brief  Get ADC channel labels
 *
 * @param[in]   h      Codec hardware base interface
 * @param[out]  label  ADC label string owned by the codec driver
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h or label is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_get_adc_label(const audio_hw_base_t *h, const char **label);

/**
 * @brief  Get codec device capabilities
 *
 * @param[in]      h         Codec hardware base interface
 * @param[in]      dev_type  Codec device type
 * @param[out]     caps      Capability array to fill, or NULL to query count only
 * @param[in,out]  count     Input capacity of caps; output capability count
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  h or count is NULL, or *count is negative
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support this operation
 */
int audio_hw_get_caps(const audio_hw_base_t *h, esp_codec_dev_type_t dev_type,
                      esp_codec_dev_capability_t *caps, int *count);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
