/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#ifndef ESP_CODEC_DEV_VERSION
#error "ESP_CODEC_DEV_VERSION is not defined"
#endif

/**
 * @brief  Define error number of codec device module
 *
 *         Inherit from `esp_err_t`
 */
#define ESP_CODEC_DEV_OK           (0)
#define ESP_CODEC_DEV_DRV_ERR      (ESP_FAIL)
#define ESP_CODEC_DEV_NO_MEM       (ESP_ERR_NO_MEM)
#define ESP_CODEC_DEV_INVALID_ARG  (ESP_ERR_INVALID_ARG)
#define ESP_CODEC_DEV_WRONG_STATE  (ESP_ERR_INVALID_STATE)
#define ESP_CODEC_DEV_NOT_FOUND    (ESP_ERR_NOT_FOUND)
#define ESP_CODEC_DEV_NOT_SUPPORT  (ESP_ERR_NOT_SUPPORTED)
#define ESP_CODEC_DEV_TIMEOUT      (ESP_ERR_TIMEOUT)
#define ESP_CODEC_DEV_WRITE_FAIL   (0x10D)
#define ESP_CODEC_DEV_READ_FAIL    (0x10E)

#define ESP_CODEC_DEV_MAKE_CHANNEL_MASK(channel)  ((uint16_t)1 << (channel))

/**
 * @brief  Codec Device type
 */
typedef enum {
    ESP_CODEC_DEV_TYPE_NONE,                                                       /*!< No direction configured */
    ESP_CODEC_DEV_TYPE_IN     = (1 << 0),                                          /*!< Codec input device like ADC (capture data from microphone) */
    ESP_CODEC_DEV_TYPE_OUT    = (1 << 1),                                          /*!< Codec output device like DAC (output analog signal to speaker) */
    ESP_CODEC_DEV_TYPE_IN_OUT = (ESP_CODEC_DEV_TYPE_IN | ESP_CODEC_DEV_TYPE_OUT),  /*!< Codec input and output device */
} esp_codec_dev_type_t;

/**
 * @brief  I2S mode used for channel order mapping
 */
typedef enum {
    ESP_CODEC_DEV_I2S_MODE_NONE    = -1,  /*!< No I2S mode */
    ESP_CODEC_DEV_I2S_MODE_DEFAULT = 0,   /*!< Default I2S mode */
    ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS,   /*!< I2S standard Philips mode */
    ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS,   /*!< I2S TDM Philips mode */
    ESP_CODEC_DEV_I2S_MODE_PDM_TX,        /*!< I2S PDM transmit mode */
    ESP_CODEC_DEV_I2S_MODE_PDM_RX,        /*!< I2S PDM receive mode */
    ESP_CODEC_DEV_I2S_MODE_MAX,           /*!< Sentinel value for mode count */
} esp_codec_dev_i2s_mode_t;

/**
 * @brief  Memory-position to slot / channel-ID mapping
 *
 *         `chN` denotes the N-th sample position within one interleaved PCM frame in memory,
 *         not the logical channel ID itself. The field value is the physical slot or device
 *         channel ID stored at that position.
 *
 *         Packed encoding:
 *         - bits[3:0]    : Slot / channel ID at memory position 1
 *         - bits[7:4]    : Slot / channel ID at memory position 2
 *         - bits[11:8]   : Slot / channel ID at memory position 3
 *         - bits[15:12]  : Slot / channel ID at memory position 4
 *         - bits[19:16]  : Slot / channel ID at memory position 5
 *         - bits[23:20]  : Slot / channel ID at memory position 6
 *         - bits[27:24]  : Slot / channel ID at memory position 7
 *         - bits[31:28]  : Slot / channel ID at memory position 8
 *         Example: `MAP(1,3,2,4)=0x4231` means each PCM frame is ordered as ch1, ch3, ch2, ch4.
 *
 * @note  A value of 0 means that memory position is unused. Used positions must form
 *        a dense prefix from `ch1` (no holes). Identity stereo is `MAP(1,2)=0x21`.
 */
typedef struct {
    union {
        struct {
            uint32_t  ch1 : 4;  /*!< Slot / channel ID at memory position 1 */
            uint32_t  ch2 : 4;  /*!< Slot / channel ID at memory position 2 */
            uint32_t  ch3 : 4;  /*!< Slot / channel ID at memory position 3 */
            uint32_t  ch4 : 4;  /*!< Slot / channel ID at memory position 4 */
            uint32_t  ch5 : 4;  /*!< Slot / channel ID at memory position 5 */
            uint32_t  ch6 : 4;  /*!< Slot / channel ID at memory position 6 */
            uint32_t  ch7 : 4;  /*!< Slot / channel ID at memory position 7 */
            uint32_t  ch8 : 4;  /*!< Slot / channel ID at memory position 8 */
        };
        uint32_t  value;  /*!< Packed channel map value */
    };
} esp_codec_dev_channel_map_t;

/**
 * @brief  Build a packed channel map
 */
#define ESP_CODEC_DEV_CHANNEL_MAP(ch1, ch2, ch3, ch4, ch5, ch6, ch7, ch8) \
    ((uint32_t)(ch1)        | \
    ((uint32_t)(ch2) <<  4) | \
    ((uint32_t)(ch3) <<  8) | \
    ((uint32_t)(ch4) << 12) | \
    ((uint32_t)(ch5) << 16) | \
    ((uint32_t)(ch6) << 20) | \
    ((uint32_t)(ch7) << 24) | \
    ((uint32_t)(ch8) << 28))

/**
 * @brief  Device-side channel map info (mode + channels → map)
 */
typedef struct {
    esp_codec_dev_i2s_mode_t     mode;      /*!< I2S working mode */
    uint8_t                      channels;  /*!< Number of channels on the bus */
    esp_codec_dev_channel_map_t  map;       /*!< Bus-position to device channel ID mapping */
} esp_codec_dev_device_map_info_t;

/**
 * @brief  Data-side channel map info (channels + slot_mask → map)
 */
typedef struct {
    uint8_t                      channels;   /*!< Number of channels */
    uint16_t                     slot_mask;  /*!< Enabled physical slot mask */
    esp_codec_dev_channel_map_t  map;        /*!< Memory-position to physical slot mapping */
} esp_codec_dev_data_map_info_t;

/**
 * @brief  Codec capability combination mode
 */
typedef enum {
    ESP_CODEC_DEV_CAPS_MODE_FIXED = 0,  /*!< Fixed channel/bits/sample_rate tuple */
    ESP_CODEC_DEV_CAPS_MODE_FLEXIBLE,   /*!< Bits and sample rates can be combined freely up to max_channels */
} esp_codec_dev_caps_mode_t;

/**
 * @brief  Codec device capability information
 *
 *         One capability entry describes either:
 *         - a single fixed format tuple when `mode == ESP_CODEC_DEV_CAPS_MODE_FIXED`, or
 *         - a freely-combinable capability set when `mode == ESP_CODEC_DEV_CAPS_MODE_FLEXIBLE`.
 *
 *         Fixed mode:
 *         - `fixed.channel`, `fixed.bits_per_sample`, and `fixed.sample_rate` together form one
 *           exact supported format.
 *
 *         Flexible mode:
 *         - `flexible.bits_per_sample[]` lists supported bit widths.
 *         - `flexible.sample_rates[]` lists supported sample rates.
 *         - `flexible.max_channels` is the maximum supported channel count for this direction.
 *         - Any combination of the listed bit widths and sample rates is valid, with channel
 *           count in the range `[1, max_channels]`, unless noted otherwise by the codec driver.
 *
 *         If a device exposes multiple disjoint fixed tuples or multiple independent flexible
 *         ranges, return multiple capability entries.
 */
typedef struct {
    esp_codec_dev_type_t       dev_type;  /*!< Capability direction */
    esp_codec_dev_caps_mode_t  mode;      /*!< Capability combination mode */
    union {
        struct {
            uint8_t   channel;          /*!< Channel count */
            uint8_t   bits_per_sample;  /*!< Bit lengths of one channel data */
            uint32_t  sample_rate;      /*!< Sample rate in Hz */
        } fixed;                        /*!< Fixed capability tuple */
        struct {
            const uint8_t  *bits_per_sample;  /*!< Supported bits per sample */
            const uint32_t *sample_rates;     /*!< Supported sample rates in Hz */
            uint8_t         bits_num;         /*!< Number of supported bits per sample */
            uint8_t         sample_rate_num;  /*!< Number of supported sample rates */
            uint8_t         max_channels;     /*!< Maximum channel count on this direction */
        } flexible;                           /*!< Flexible capability lists */
    };
} esp_codec_dev_capability_t;

/**
 * @brief  Codec audio sample information
 *
 *         channel_mask filters wanted channels in the driver. When set to 0, all channels are
 *         selected by default. When channel is 2, set bit 0 or bit 1 to select one channel.
 *         When channel is 4, the mask can select one or more channels.
 */
typedef struct {
    uint8_t   bits_per_sample;  /*!< Bit lengths of one channel data */
    uint8_t   channel;          /*!< Channels of sample */
    uint16_t  channel_mask;     /*!< Channel mask indicate which channel to be selected */
    uint32_t  sample_rate;      /*!< Sample rate of sample */
    int       mclk_multiple;    /*!< The multiple of MCLK to the sample rate
                                     If value is 0, mclk = sample_rate * 256
                                     If bits_per_sample is 24bit, mclk_multiple should be the multiple of 3
                                */
} esp_codec_dev_sample_info_t;

#ifdef __cplusplus
}
#endif  /* __cplusplus */
