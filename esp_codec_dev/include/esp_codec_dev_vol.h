/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Codec volume map to decibel
 */
typedef struct {
    int    vol;       /*!< Volume value */
    float  db_value;  /*!< Volume decibel value */
} esp_codec_dev_vol_map_t;

/**
 * @brief  Codec volume range setting
 */
typedef struct {
    esp_codec_dev_vol_map_t  min_vol;  /*!< Minimum volume setting */
    esp_codec_dev_vol_map_t  max_vol;  /*!< Maximum volume setting */
} esp_codec_dev_vol_range_t;

/**
 * @brief  Codec volume curve configuration
 */
typedef struct {
    esp_codec_dev_vol_map_t *vol_map;  /*!< Point of volume curve */
    uint16_t                 count;    /*!< Curve point number */
} esp_codec_dev_vol_curve_t;

/**
 * @brief  Codec hardware gain setting
 *
 * @note  Audio gain overview:
 *        |----------------Software Gain--------------|--Hardware Gain--|
 *        Digital Audio Data -> Audio Process Gain -> Codec DAC Volume -> PA Gain -> Speaker Output
 *
 *        Final speaker loudness is affected by both software gain and hardware gain.
 *        Software gain includes audio post-processor gain and codec DAC volume.
 *        Hardware gain includes PA gain determined by the circuit.
 *
 *        MAX_GAIN = 20 * log(Vpa / Vdac), where Vpa is PA supply and Vdac is codec DAC supply.
 *        For example, Vpa = 5 V and Vdac = 3.3 V gives MAX_GAIN = 3.6 dB.
 *
 *        Hardware gain generally consists of two parts: codec DAC and PA supply voltages for
 *        MAX_GAIN, and PA gain from connected resistors.
 */
typedef struct {
    float  pa_voltage;         /*!< PA voltage: typical 5.0 V */
    float  codec_dac_voltage;  /*!< Codec chip DAC voltage: typical 3.3 V */
    float  pa_gain;            /*!< PA amplify coefficient in decibel unit */
} esp_codec_dev_hw_gain_t;

/**
 * @brief  Convert decibel value to register settings
 *
 * @param[in]  vol_range  Volume range
 * @param[in]  db         Volume decibel
 *
 * @return
 *       - Codec  register value
 */
int esp_codec_dev_vol_calc_reg(const esp_codec_dev_vol_range_t *vol_range, float db);

/**
 * @brief  Convert codec register setting to decibel value
 *
 * @param[in]  vol_range  Volume range
 * @param[in]  vol        Volume register setting
 *
 * @return
 *       - Codec  volume in decibel unit
 */
float esp_codec_dev_vol_calc_db(const esp_codec_dev_vol_range_t *vol_range, int vol);

/**
 * @brief  Calculate codec hardware gain value
 *
 * @param[in]  hw_gain  Hardware gain settings
 *
 * @return
 *       - Codec  hardware gain in decibel unit
 */
float esp_codec_dev_vol_calc_hw_gain(esp_codec_dev_hw_gain_t *hw_gain);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
