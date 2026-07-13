/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "audio_codec_ctrl_if.h"
#include "audio_hw_base.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef struct audio_codec_if_t audio_codec_if_t;
typedef struct audio_codec_hw_proc_ops_t audio_codec_hw_proc_ops_t;

/**
 * @brief  Codec ADC/DAC operation callbacks
 */
typedef struct {
    int (*enable)(const audio_codec_if_t *h, bool enable);             /*!< Enable or disable path */
    int (*mute)(const audio_codec_if_t *h, int ch_mask, bool mute);    /*!< Set channel mute */
    int (*set_vol)(const audio_codec_if_t *h, int ch_mask, float db);  /*!< Set channel volume or gain */
} audio_codec_ops_t;

typedef struct {
    int (*enable)(const audio_codec_if_t *h, bool enable);  /*!< Enable or disable PA */
} audio_hw_pa_t;

typedef struct {
    audio_codec_ops_t  ops;  /*!< ADC operation callbacks */
} audio_hw_adc_if_t;

/**
 * @brief  Codec DAC path including PA control
 */
typedef struct {
    audio_codec_ops_t  ops;  /*!< DAC operation callbacks */
    audio_hw_pa_t      pa;   /*!< PA control callbacks */
} audio_hw_dac_if_t;

/**
 * @brief  Unified codec interface structure
 */
struct audio_codec_if_t {
    audio_hw_base_t                  hw_base;  /*!< Base hardware interface */
    const audio_codec_ctrl_if_t     *ctrl_if;  /*!< Control interface */
    const audio_hw_adc_if_t         *adc_if;   /*!< ADC interface, NULL if not supported */
    const audio_hw_dac_if_t         *dac_if;   /*!< DAC interface, NULL if not supported */
    const audio_codec_hw_proc_ops_t *hw_proc;  /*!< Optional hardware audio processing ops */
};

/**
 * @brief  Delete a codec interface instance
 *
 * @note  All hardware audio processing handles created from this codec interface must be deleted
 *        before calling this function. Any processing handle that still references this codec
 *        interface becomes invalid after this function returns.
 *
 * @param[in]  codec_if  Codec interface returned by audio_codec_new()
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  codec_if is NULL
 */
int audio_codec_delete_codec_if(const audio_codec_if_t *codec_if);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
