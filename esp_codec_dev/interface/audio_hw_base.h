/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_codec_dev_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define AUDIO_HW_ADC_LABEL_MAX_LEN  (64)

typedef struct audio_hw_base_t audio_hw_base_t;

/**
 * @brief  Base hardware interface for codec operations
 */
struct audio_hw_base_t {
    int (*open)(const audio_hw_base_t *h, void *cfg, int cfg_size);  /*!< Open codec */
    bool (*is_open)(const audio_hw_base_t *h);                       /*!< Check whether codec is opened */
    int (*set_fs)(const audio_hw_base_t *h,
                  esp_codec_dev_sample_info_t *fs, esp_codec_dev_type_t type);  /*!< Set audio format to codec */
    int (*set_reg)(const audio_hw_base_t *h, int reg, int value);               /*!< Set register value to codec */
    int (*get_reg)(const audio_hw_base_t *h, int reg, int *value);              /*!< Get register value from codec */
    void (*dump_reg)(const audio_hw_base_t *h);                                 /*!< Dump all register settings */
    int (*close)(const audio_hw_base_t *h);                                     /*!< Close codec */
    int (*get_adc_label)(const audio_hw_base_t *h, const char **label);         /*!< Get stored ADC channel labels */
    int (*get_order_list)(const audio_hw_base_t *h,
                          const esp_codec_dev_device_map_info_t **order_list, int *list_size);  /*!< Get logical-channel to physical-slot mappings */
    int (*get_caps)(const audio_hw_base_t *h, esp_codec_dev_type_t dev_type,
                    esp_codec_dev_capability_t *caps, int *count);  /*!< Get codec device capabilities */
};

#ifdef __cplusplus
}
#endif  /* __cplusplus */
