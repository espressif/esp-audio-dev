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
 *         Fails when mean absolute level is below 50 (covers 0/-1 toggling dead ADC).
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
 * @brief  Analyze interleaved captured PCM with health and structure checks
 *
 * @note  Caller should drop warmup samples first. Channels below the silence
 *        threshold skip the structure checks, so quiet noise is not failed.
 *
 * @param[in]  buf              Interleaved PCM; caller retains ownership
 * @param[in]  len              Byte length; must be a multiple of one frame
 * @param[in]  chunk_bytes      One read size for stuck-chunk detection; 0 skips it
 * @param[in]  channels         Interleaved channel count, 1..16
 * @param[in]  bits_per_sample  16 or 32
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Buffer looks like live, advancing PCM
 *       - ESP_CODEC_DEV_INVALID_ARG  Argument or a health/structure check failed
 */
int test_analyze_recorded_pcm(const uint8_t *buf, int len, int chunk_bytes,
                              int channels, int bits_per_sample);

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
