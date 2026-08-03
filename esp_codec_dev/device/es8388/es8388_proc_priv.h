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
 * @brief  ES8388 ALC hardware audio processing ops
 */
extern const esp_audio_hw_alc_t es8388_alc_ops;

/**
 * @brief  ES8388 line hardware audio processing ops
 */
extern const esp_audio_hw_line_t es8388_line_ops;

#ifdef __cplusplus
}
#endif  /* __cplusplus */
