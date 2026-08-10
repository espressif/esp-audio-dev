/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_audio_hw_proc_if.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  ES8311 ALC hardware audio processing ops
 */
extern const esp_audio_hw_alc_t es8311_alc_ops;

/**
 * @brief  ES8311 DRC hardware audio processing ops
 */
extern const esp_audio_hw_drc_t es8311_drc_ops;

/**
 * @brief  ES8311 EQ hardware audio processing ops
 */
extern const esp_audio_hw_eq_t es8311_eq_ops;

/**
 * @brief  ES8311 mute hardware audio processing ops
 */
extern const esp_audio_hw_mute_t es8311_mute_ops;

#ifdef __cplusplus
}
#endif  /* __cplusplus */
