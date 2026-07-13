/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
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
 * @brief  Print up to four leading int16 PCM samples (decimal, comma-separated) for UART trace.
 *
 * @param  data         Interleaved PCM buffer, 16-bit per sample slot (one int16 per slot).
 * @param  num_samples  Number of valid int16 samples in @a data; at most 4 are printed. Caller must ensure
 *                      the buffer holds at least `min(num_samples, 4) * sizeof(int16_t)` bytes.
 */
void test_print_pcm_s16_head(const uint8_t *data, int num_samples);

/**
 * @brief  Sanity-check interleaved 16-bit captured PCM (stats + heuristics for silence/clip/stuck chunks).
 *
 * @param  buf          PCM buffer.
 * @param  len          Byte length (must be even, >= 2).
 * @param  chunk_bytes  If > 0 and len >= chunk_bytes, detect repeated fixed-size chunks (e.g. one read size).
 * @return
 *       - ESP_CODEC_DEV_OK  or ESP_CODEC_DEV_INVALID_ARG.
 */
int test_analyze_recorded_pcm_s16(const uint8_t *buf, int len, int chunk_bytes);

/**
 * @brief  Same as @ref test_analyze_recorded_pcm_s16 for 32-bit samples (len multiple of 4).
 */
int test_analyze_recorded_pcm_s32(const uint8_t *buf, int len, int chunk_bytes);

/**
 * @brief  Find the maximum and minimum sample values in an interleaved 16-bit PCM buffer.
 *
 * @param  data       Interleaved PCM buffer, 16-bit per sample slot (one int16 per slot).
 * @param  size       Byte length (must be even, >= 2).
 * @param  max_value  Pointer to store the maximum sample value.
 * @param  min_value  Pointer to store the minimum sample value.
 */
void codec_max_sample(uint8_t *data, int size, int *max_value, int *min_value);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
