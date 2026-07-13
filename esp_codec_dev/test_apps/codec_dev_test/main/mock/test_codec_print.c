/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "esp_codec_dev_types.h"
#include "test_codec_print.h"

static const char *TAG = "CODEC_DEV_TEST_PCM";

void test_print_pcm_s16_head(const uint8_t *data, int num_samples)
{
    if (data == NULL || num_samples <= 0) {
        printf("\n");
        return;
    }
    int n = num_samples;
    const int16_t *s = (const int16_t *)data;
    for (int i = 0; i < n; i++) {
        printf("%6d, ", (int)s[i]);
    }
    printf("\n");
}

static bool buffer_is_identical(const uint8_t *buf, int len)
{
    if (buf == NULL || len <= 1) {
        return true;
    }
    uint8_t first = buf[0];
    for (int i = 1; i < len; i++) {
        if (buf[i] != first) {
            return false;
        }
    }
    return true;
}

void codec_max_sample(uint8_t *data, int size, int *max_value, int *min_value)
{
    int16_t *s = (int16_t *)data;
    size >>= 1;
    int i = 1, max, min;
    max = min = s[0];
    while (i < size) {
        if (s[i] > max) {
            max = s[i];
        } else if (s[i] < min) {
            min = s[i];
        }
        i++;
    }
    *max_value = max;
    *min_value = min;
}

int test_analyze_recorded_pcm_s16(const uint8_t *buf, int len, int chunk_bytes)
{
    if (buf == NULL || len <= 1 || ((len % 2) != 0)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const int16_t *pcm = (const int16_t *)buf;
    int sample_num = len >> 1;
    int16_t min_sample = INT16_MAX;
    int16_t max_sample = INT16_MIN;
    int64_t sum = 0;
    int64_t sum_abs = 0;
    int clip_count = 0;
    int identical_pairs = 0;

    for (int i = 0; i < sample_num; i++) {
        int16_t sample = pcm[i];
        if (sample < min_sample) {
            min_sample = sample;
        }
        if (sample > max_sample) {
            max_sample = sample;
        }
        sum += sample;
        sum_abs += sample >= 0 ? sample : -(int32_t)sample;
        if (sample >= 32760 || sample <= -32760) {
            clip_count++;
        }
        if (i > 0 && sample == pcm[i - 1]) {
            identical_pairs++;
        }
    }

    int repeated_chunks = 0;
    int chunk_count = 0;
    if (chunk_bytes > 0 && len >= chunk_bytes) {
        chunk_count = len / chunk_bytes;
        for (int i = 1; i < chunk_count; i++) {
            const uint8_t *prev = buf + (i - 1) * chunk_bytes;
            const uint8_t *cur = buf + i * chunk_bytes;
            if (memcmp(prev, cur, chunk_bytes) == 0) {
                repeated_chunks++;
            }
        }
    }

    int64_t mean = sum / sample_num;
    int64_t mean_abs = sum_abs / sample_num;
    ESP_LOGI(TAG,
             "record stats: samples=%d min=%d max=%d mean=%d mean_abs=%d",
             sample_num, min_sample, max_sample, (int)mean, (int)mean_abs);
    ESP_LOGI(TAG, "clip=%d identical_pairs=%d repeated_chunks=%d, chunk_count=%d",
             clip_count, identical_pairs, repeated_chunks, chunk_count);

    if (buffer_is_identical(buf, len)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (max_sample <= min_sample) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (clip_count * 100 >= sample_num * 20) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (llabs(mean) >= 24000) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (chunk_count > 1 && repeated_chunks * 100 >= chunk_count * 10) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    return ESP_CODEC_DEV_OK;
}

int test_analyze_recorded_pcm_s32(const uint8_t *buf, int len, int chunk_bytes)
{
    if (buf == NULL || len <= 3 || ((len % 4) != 0)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const int32_t *pcm = (const int32_t *)buf;
    int sample_num = len >> 2;
    int32_t min_sample = INT32_MAX;
    int32_t max_sample = INT32_MIN;
    int64_t sum = 0;
    int64_t sum_abs = 0;
    int identical_pairs = 0;

    for (int i = 0; i < sample_num; i++) {
        int32_t sample = pcm[i];
        if (sample < min_sample) {
            min_sample = sample;
        }
        if (sample > max_sample) {
            max_sample = sample;
        }
        sum += sample;
        sum_abs += sample >= 0 ? sample : -(int64_t)sample;
        if (i > 0 && sample == pcm[i - 1]) {
            identical_pairs++;
        }
    }

    int repeated_chunks = 0;
    int chunk_count = 0;
    if (chunk_bytes > 0 && len >= chunk_bytes) {
        chunk_count = len / chunk_bytes;
        for (int i = 1; i < chunk_count; i++) {
            const uint8_t *prev = buf + (i - 1) * chunk_bytes;
            const uint8_t *cur = buf + i * chunk_bytes;
            if (memcmp(prev, cur, chunk_bytes) == 0) {
                repeated_chunks++;
            }
        }
    }

    int64_t mean = sum / sample_num;
    int64_t mean_abs = sum_abs / sample_num;
    ESP_LOGI(TAG,
             "record 32bit stats: samples=%d min=%ld max=%ld mean=%lld mean_abs=%lld",
             sample_num, (long)min_sample, (long)max_sample, (long long)mean, (long long)mean_abs);
    ESP_LOGI(TAG, "32bit identical_pairs=%d repeated_chunks=%d, chunk_count=%d",
             identical_pairs, repeated_chunks, chunk_count);

    if (buffer_is_identical(buf, len)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (max_sample <= min_sample) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (mean_abs == 0) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (chunk_count > 1 && repeated_chunks * 100 >= chunk_count * 10) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    return ESP_CODEC_DEV_OK;
}
