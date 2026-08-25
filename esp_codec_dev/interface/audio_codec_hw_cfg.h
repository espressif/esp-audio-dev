/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include "esp_codec_dev_vol.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Codec system clock configuration
 */
typedef struct {
    bool  is_master;  /*!< Whether codec works as I2S master or not, default work as slave */
    bool  no_mclk;    /*!< Whether use external MCLK clock, default need use external MCLK clock */
} audio_hw_sys_cfg_t;

/**
 * @brief  Codec ADC and microphone configuration
 *
 *         `label` is a comma-separated list of logical channel names in
 *         codec_dev 2.0 channel order (LSB to MSB). Duplicate names are allowed.
 *         Only the following tokens are supported:
 *         - FC: Front Center
 *         - RE: Reference signal
 *         - FL / FR: Front Left / Right
 *         - SL / SR: Side Left / Right
 *         - BL / BR: Back Left / Right
 *         - NA: Not available / not enabled
 *
 *         RE and NA have special meaning: RE is the reference channel, NA leaves
 *         that physical channel unused (not selected for hardware mic enable).
 *         Any other token is invalid (exact, case-sensitive match only).
 *         A NULL or empty label selects all channels (`audio_codec_adc_label_parse`
 *         yields 0xFFFF; the codec applies bits for its physical ADC channels).
 *         Codec drivers parse `label` with `audio_codec_adc_label_parse()`.
 */
typedef struct {
    bool        digital_mic;  /*!< Whether use digital microphone */
    const char *label;        /*!< ADC channel logical labels, such as "FL,FR,RE" */
} audio_hw_adc_cfg_t;

/**
 * @brief  Codec PA configuration
 */
typedef struct {
    int16_t                  pa_pin;         /*!< PA chip power pin, -1 means not used */
    bool                     pa_active_low;  /*!< false: enable PA when pin set to 1, true: enable PA when pin set to 0 */
    esp_codec_dev_hw_gain_t  hw_gain;        /*!< Hardware gain */
} audio_hw_pa_cfg_t;

/**
 * @brief  Codec DAC loopback configuration
 */
typedef struct {
    bool    ref_enable;        /*!< Whether codec internal DAC reference loopback is enabled */
    int8_t  ref_dac_ch;        /*!< DAC reference channel, 1 for ch1, 2 for ch2, -1 means not used */
    int8_t  real_adc_data_ch;  /*!< Real ADC data channel, 1 for ch1, 2 for ch2, -1 means not used */
} audio_hw_dac_cfg_t;

/**
 * @brief  Codec reset pin configuration
 */
typedef struct {
    int16_t  reset_pin;         /*!< Reset pin, -1 means not used */
    bool     reset_active_low;  /*!< false: reset active high, true: reset active low */
} audio_hw_reset_cfg_t;

#ifdef __cplusplus
}
#endif  /* __cplusplus */
