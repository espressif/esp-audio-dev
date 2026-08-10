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
 * @brief  ES7210 ALC hardware audio processing ops
 */
extern const esp_audio_hw_alc_t es7210_alc_ops;

/**
 * @brief  ES7210 mute hardware audio processing ops
 */
extern const esp_audio_hw_mute_t es7210_mute_ops;

#ifdef __cplusplus
}
#endif  /* __cplusplus */
