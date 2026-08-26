/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "audio_codec_if.h"
#include "audio_codec_data_if.h"
#include "audio_codec_vol_if.h"
#include "esp_codec_dev_types.h"
#include "esp_codec_dev_vol.h"
#include "codec_dev_mirror.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define CODEC_DEV_ORDER_TABLE_UNLOADED  (-1)

/**
 * @brief  Internal codec device instance
 *
 *         An IN_OUT handle shares one sample format, and playback is at most 2 channels, so its frame is
 *         always 2 slots where the codec slot mapping is identity and both directions resolve to the same
 *         memory layout. One order pair therefore covers every handle type; open() asserts the premise.
 */
typedef struct codec_dev_s {
    const audio_codec_if_t      *codec_if;             /*!< Codec interface */
    const audio_codec_data_if_t *data_if;              /*!< Data interface */
    const audio_codec_vol_if_t  *sw_vol;               /*!< Software volume interface */
    esp_codec_dev_type_t         dev_caps;             /*!< Opened device directions */
    bool                         input_opened;         /*!< True when input is open */
    bool                         output_opened;        /*!< True when output is open */
    int                          volume;               /*!< Cached output volume */
    float                        mic_gain;             /*!< Cached input gain */
    bool                         muted;                /*!< Cached output mute */
    bool                         mic_muted;            /*!< Cached input mute */
    bool                         sw_vol_alloced;       /*!< True when sw_vol was allocated by this handle */
    esp_codec_dev_vol_curve_t    vol_curve;            /*!< Volume curve */
    bool                         disable_when_closed;  /*!< Disable hardware when close() is called */
    esp_codec_dev_channel_map_t  set_map;              /*!< Requested memory map */
    esp_codec_dev_channel_map_t  cur_map;              /*!< Current hardware/DMA memory map */
    esp_codec_dev_sample_info_t  fs;                   /*!< Application sample format */
    codec_dev_mirror_handle_t    mirror;               /*!< Optional input mirror */

    /* A registered map query always has a resolve callback, so it carries its own validity.
       Its ctx is the device itself; the registered direction is encoded by the callback. */
    esp_codec_dev_map_query_t  in_map_query;   /*!< Input map query */
    esp_codec_dev_map_query_t  out_map_query;  /*!< Output map query */

    /* order_row_count is CODEC_DEV_ORDER_TABLE_UNLOADED until queried, then 0 for an empty table. */
    const esp_codec_dev_device_map_info_t *order_rows;       /*!< Cached codec order table; not owned */
    int                                    order_row_count;  /*!< Cached order-table row count */
} codec_dev_t;

/**
 * @brief  Convert an I2S mode to a log string
 */
static inline const char *codec_dev_i2s_mode_name(esp_codec_dev_i2s_mode_t mode)
{
    switch (mode) {
        case ESP_CODEC_DEV_I2S_MODE_NONE:
            return "NONE";
        case ESP_CODEC_DEV_I2S_MODE_DEFAULT:
            return "DEFAULT";
        case ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS:
            return "STD_PHILIPS";
        case ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS:
            return "TDM_PHILIPS";
        case ESP_CODEC_DEV_I2S_MODE_PDM_TX:
            return "PDM_TX";
        case ESP_CODEC_DEV_I2S_MODE_PDM_RX:
            return "PDM_RX";
        case ESP_CODEC_DEV_I2S_MODE_MAX:
            return "MAX";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief  Re-apply cached volume, gain, and mute after a format change
 *
 * @param[in]  dev  Codec device instance
 */
void codec_dev_apply_vol_mute(codec_dev_t *dev);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
