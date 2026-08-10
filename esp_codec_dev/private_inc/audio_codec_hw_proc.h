/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_codec_dev.h"
#include "audio_codec_if.h"
#include "esp_audio_hw_proc_if.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Internal helpers for hardware audio processing (not a public API)
 */

typedef const void *(*esp_audio_hw_proc_get_ops_fn)(const esp_audio_hw_proc_ops_t *hw_proc);

/**
 * @brief  Get codec_if from device handle (internal)
 */
const audio_codec_if_t *esp_audio_hw_proc_get_codec_if(esp_codec_dev_handle_t dev);

/**
 * @brief  Resolve hw_proc path: validate args, get codec_if and processing ops table (no lock, no open check)
 */
int esp_audio_hw_proc_resolve(esp_codec_dev_handle_t dev, esp_audio_hw_proc_get_ops_fn get_ops,
                              const audio_codec_if_t **codec_if, const void **ops);

/**
 * @brief  Chip-level open check; treat as ready when is_open is not implemented
 */
int esp_audio_hw_proc_check_open(const audio_codec_if_t *codec_if);

/**
 * @brief  Acquire the global hw_proc mutex
 */
int esp_audio_hw_proc_lock(void);

/**
 * @brief  Release the global hw_proc mutex
 */
void esp_audio_hw_proc_unlock(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
