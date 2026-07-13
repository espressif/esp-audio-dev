/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>

#include "esp_codec_dev_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  PCM data conversion context
 */
typedef struct {
    uint8_t                     *data;    /*!< PCM buffer pointer */
    int                          len;     /*!< Buffer length in bytes */
    esp_codec_dev_channel_map_t  map;     /*!< Logical-channel to buffer-slot mapping */
    int                          bits;    /*!< Bits per sample: 8/16/24/32 */
    int                          ch_num;  /*!< Channel count represented by @ref map */
} codec_dev_data_cvt_info_t;

/**
 * @brief  Convert PCM data order, channel count, or bit depth
 *
 * @param[in]  src  Source conversion context
 * @param[in]  dst  Destination conversion context
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid input or unsupported layout
 *       - ESP_CODEC_DEV_NO_MEM       Temporary buffer allocation failed
 */
int codec_dev_data_cvt_layout(const codec_dev_data_cvt_info_t *src, const codec_dev_data_cvt_info_t *dst);

/**
 * @brief  Fix ESP32 mono read data layout
 *
 * @note  On read, I2S returns more bytes than the user buffer needs. The caller allocates
 *        @p read_data, fills it via i2s_channel_read, then this function writes the converted
 *        PCM into @p user_data.
 *        Call sequence: malloc(read_data) -> i2s_channel_read -> esp32_read_mono_fix -> free(read_data)
 *
 * @param[in]   read_data  Raw data read from I2S; caller-allocated scratch buffer
 * @param[in]   read_len   Length of raw data in bytes
 * @param[in]   bits       Destination bits per sample
 * @param[out]  user_data  Destination user buffer after conversion
 * @param[in]   user_len   Length of destination user buffer in bytes
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid input
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Unsupported bit depth
 */
int esp32_read_mono_fix(uint8_t *read_data, int read_len, int bits, uint8_t *user_data, int user_len);

/**
 * @brief  Fix ESP32 mono write data layout
 *
 * @note  On write, I2S expects more bytes than the user supplies. This function allocates
 *        @p ret_cache for the expanded buffer; the caller passes it to i2s_channel_write and
 *        then frees it.
 *        Call sequence: esp32_write_mono_fix -> i2s_channel_write -> free(ret_cache)
 *
 * @param[in]   user_data      User data to be written
 * @param[in]   len            Length of user data in bytes
 * @param[in]   bits           Source bits per sample
 * @param[out]  ret_cache      Temporary buffer allocated by this function for I2S write; caller frees
 * @param[out]  ret_cache_len  Returned buffer length in bytes
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid input
 *       - ESP_CODEC_DEV_NO_MEM       Temporary buffer allocation failed
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Unsupported bit depth
 */
int esp32_write_mono_fix(uint8_t *user_data, int len, int bits, uint8_t **ret_cache, int *ret_cache_len);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
