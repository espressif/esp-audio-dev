/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "esp_codec_dev_types.h"
#include "test_codec_print.h"

static const char *TAG = "CODEC_DEV_TEST_PCM";

/* Dead ADC often toggles 0/-1 (mean_abs < 10). Real capture is far above this. */
#define TEST_PCM_S16_MIN_MEAN_ABS         (5)
/* Smoothness is meaningless on quiet noise; hold still runs so AABB is caught. */
#define TEST_PCM_S16_STRUCT_MIN_MEAN_ABS  (200)
#define TEST_PCM_S16_DC_ABS_MAX           (24000)
#define TEST_PCM_S16_CLIP_ABS             (32760)
#define TEST_PCM_S16_NEAR_ZERO_ABS        (16)
#define TEST_PCM_CLIP_PERCENT             (20)
#define TEST_PCM_REPEAT_CHUNK_PERCENT     (10)
#define TEST_PCM_HOLD_PERCENT             (25)
#define TEST_PCM_ZERO_ALT_PERCENT         (35)
#define TEST_PCM_LR_COPY_PERCENT          (80)
#define TEST_PCM_DELTA_ABS_PERCENT        (15)
#define TEST_PCM_MAX_CHANNELS             (16)
#define TEST_PCM_S32_FROM_S16_SHIFT       (16)

/**
 * @brief  Per-channel accumulators for captured PCM analysis
 */
typedef struct {
    int32_t  min_sample;    /*!< Minimum sample on this channel */
    int32_t  max_sample;    /*!< Maximum sample on this channel */
    int64_t  sum;           /*!< Signed sum for mean */
    int64_t  sum_abs;       /*!< Absolute-value sum for mean_abs */
    int64_t  delta_abs;     /*!< Sum of |x[n]-x[n-1]| */
    int      clip_count;    /*!< Samples at or beyond the clip threshold */
    int      hold_count;    /*!< Time-adjacent equal pairs */
    int      pair_count;    /*!< Number of time-adjacent pairs */
    int      a0b0_count;    /*!< Even-near-zero and odd-active pairs */
    int      b0a0_count;    /*!< Even-active and odd-near-zero pairs */
    int      alt_pairs;     /*!< Number of even/odd pairs */
    int      sample_count;  /*!< Frames accumulated on this channel */
    int32_t  prev_sample;   /*!< Previous sample for hold and delta */
    bool     has_prev;      /*!< True after the first sample */
} test_pcm_ch_stat_t;

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
    if (mean_abs < TEST_PCM_S16_MIN_MEAN_ABS) {
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

static int32_t test_pcm_load_sample(const uint8_t *buf, int index, int bits_per_sample)
{
    if (bits_per_sample == 16) {
        const int16_t *pcm = (const int16_t *)buf;
        return pcm[index];
    }
    const int32_t *pcm = (const int32_t *)buf;
    return pcm[index];
}

static int32_t test_pcm_scaled_s16_limit(int32_t s16_limit, int bits_per_sample)
{
    if (bits_per_sample == 16) {
        return s16_limit;
    }
    return (int32_t)((int64_t)s16_limit << TEST_PCM_S32_FROM_S16_SHIFT);
}

static bool test_pcm_near_zero(int32_t sample, int32_t zero_abs)
{
    int64_t abs_sample = sample >= 0 ? sample : -(int64_t)sample;
    return abs_sample < zero_abs;
}

static void test_pcm_ch_stat_init(test_pcm_ch_stat_t *stat)
{
    memset(stat, 0, sizeof(*stat));
    stat->min_sample = INT32_MAX;
    stat->max_sample = INT32_MIN;
}

static int test_pcm_count_repeated_chunks(const uint8_t *buf, int len, int chunk_bytes)
{
    if (chunk_bytes <= 0 || len < chunk_bytes) {
        return 0;
    }
    int chunk_count = len / chunk_bytes;
    int repeated_chunks = 0;
    for (int i = 1; i < chunk_count; i++) {
        const uint8_t *prev = buf + (i - 1) * chunk_bytes;
        const uint8_t *cur = buf + i * chunk_bytes;
        if (memcmp(prev, cur, chunk_bytes) == 0) {
            repeated_chunks++;
        }
    }
    return repeated_chunks;
}

int test_analyze_recorded_pcm(const uint8_t *buf, int len, int chunk_bytes,
                              int channels, int bits_per_sample)
{
    if (buf == NULL || len <= 0) {
        ESP_LOGE(TAG, "Analyze recorded PCM failed: buffer is NULL or empty");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (channels <= 0 || channels > TEST_PCM_MAX_CHANNELS) {
        ESP_LOGE(TAG, "Analyze recorded PCM failed: invalid channel count %d", channels);
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (bits_per_sample != 16 && bits_per_sample != 32) {
        ESP_LOGE(TAG, "Analyze recorded PCM failed: bits_per_sample %d is not 16 or 32",
                 bits_per_sample);
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    int bytes_per_sample = bits_per_sample >> 3;
    int frame_bytes = channels * bytes_per_sample;
    if ((len % frame_bytes) != 0) {
        ESP_LOGE(TAG, "Analyze recorded PCM failed: length %d is not a multiple of frame %d",
                 len, frame_bytes);
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    int frame_count = len / frame_bytes;
    if (frame_count < 2) {
        ESP_LOGE(TAG, "Analyze recorded PCM failed: need at least two frames");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    int32_t min_mean_abs = test_pcm_scaled_s16_limit(TEST_PCM_S16_MIN_MEAN_ABS, bits_per_sample);
    int32_t struct_min_mean_abs = test_pcm_scaled_s16_limit(TEST_PCM_S16_STRUCT_MIN_MEAN_ABS,
                                                            bits_per_sample);
    int32_t dc_abs_max = test_pcm_scaled_s16_limit(TEST_PCM_S16_DC_ABS_MAX, bits_per_sample);
    int32_t clip_abs = test_pcm_scaled_s16_limit(TEST_PCM_S16_CLIP_ABS, bits_per_sample);
    int32_t zero_abs = test_pcm_scaled_s16_limit(TEST_PCM_S16_NEAR_ZERO_ABS, bits_per_sample);

    test_pcm_ch_stat_t ch_stat[TEST_PCM_MAX_CHANNELS];
    for (int ch = 0; ch < channels; ch++) {
        test_pcm_ch_stat_init(&ch_stat[ch]);
    }

    int64_t all_sum = 0;
    int total_samples = frame_count * channels;
    int total_clips = 0;
    int lr_copy_count = 0;

    for (int frame = 0; frame < frame_count; frame++) {
        int32_t left_sample = 0;
        for (int ch = 0; ch < channels; ch++) {
            int32_t sample = test_pcm_load_sample(buf, frame * channels + ch, bits_per_sample);
            test_pcm_ch_stat_t *stat = &ch_stat[ch];
            if (sample < stat->min_sample) {
                stat->min_sample = sample;
            }
            if (sample > stat->max_sample) {
                stat->max_sample = sample;
            }
            stat->sum += sample;
            stat->sum_abs += sample >= 0 ? sample : -(int64_t)sample;
            all_sum += sample;
            if (sample >= clip_abs || sample <= -clip_abs) {
                stat->clip_count++;
                total_clips++;
            }
            if (stat->has_prev) {
                stat->pair_count++;
                if (sample == stat->prev_sample) {
                    stat->hold_count++;
                }
                int64_t delta = (int64_t)sample - stat->prev_sample;
                stat->delta_abs += delta >= 0 ? delta : -delta;
            } else {
                stat->has_prev = true;
            }
            stat->prev_sample = sample;
            stat->sample_count++;
            if (ch == 0) {
                left_sample = sample;
            } else if (ch == 1 && sample == left_sample) {
                lr_copy_count++;
            }
        }
        if (((frame & 1) != 0) || ((frame + 1) >= frame_count)) {
            continue;
        }
        for (int ch = 0; ch < channels; ch++) {
            int32_t even_s = test_pcm_load_sample(buf, frame * channels + ch, bits_per_sample);
            int32_t odd_s = test_pcm_load_sample(buf, (frame + 1) * channels + ch, bits_per_sample);
            bool even_z = test_pcm_near_zero(even_s, zero_abs);
            bool odd_z = test_pcm_near_zero(odd_s, zero_abs);
            ch_stat[ch].alt_pairs++;
            if (even_z && (odd_z == false)) {
                ch_stat[ch].a0b0_count++;
            }
            if ((even_z == false) && odd_z) {
                ch_stat[ch].b0a0_count++;
            }
        }
    }

    int chunk_count = (chunk_bytes > 0 && len >= chunk_bytes) ? (len / chunk_bytes) : 0;
    int repeated_chunks = test_pcm_count_repeated_chunks(buf, len, chunk_bytes);
    int64_t all_mean = all_sum / total_samples;
    int active_channels = 0;

    ESP_LOGI(TAG, "Record PCM stats: frames=%d channels=%d bits=%d",
             frame_count, channels, bits_per_sample);
    for (int ch = 0; ch < channels; ch++) {
        const test_pcm_ch_stat_t *stat = &ch_stat[ch];
        int64_t mean = stat->sum / stat->sample_count;
        int64_t mean_abs = stat->sum_abs / stat->sample_count;
        int64_t delta_mean = (stat->pair_count > 0) ? (stat->delta_abs / stat->pair_count) : 0;
        if (mean_abs >= min_mean_abs) {
            active_channels++;
        }
        ESP_LOGI(TAG,
                 "Channel %d: min=%d max=%d mean=%d mean_abs=%d hold=%d/%d delta_abs=%d a0b0=%d b0a0=%d alt=%d",
                 ch, (int)stat->min_sample, (int)stat->max_sample, (int)mean, (int)mean_abs,
                 stat->hold_count, stat->pair_count, (int)delta_mean,
                 stat->a0b0_count, stat->b0a0_count, stat->alt_pairs);
    }
    ESP_LOGI(TAG, "clip=%d/%d lr_copy=%d/%d repeated_chunks=%d/%d",
             total_clips, total_samples, lr_copy_count, frame_count,
             repeated_chunks, chunk_count);

    if (buffer_is_identical(buf, len)) {
        ESP_LOGE(TAG, "Analyze recorded PCM failed: buffer is constant");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (total_clips * 100 >= total_samples * TEST_PCM_CLIP_PERCENT) {
        ESP_LOGE(TAG, "Analyze recorded PCM failed: clip %d/%d exceeds %d percent",
                 total_clips, total_samples, TEST_PCM_CLIP_PERCENT);
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (llabs(all_mean) >= dc_abs_max) {
        ESP_LOGE(TAG, "Analyze recorded PCM failed: DC mean %d exceeds %d",
                 (int)all_mean, (int)dc_abs_max);
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (active_channels == 0) {
        ESP_LOGE(TAG, "Analyze recorded PCM failed: all channels are below silence threshold");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    for (int ch = 0; ch < channels; ch++) {
        const test_pcm_ch_stat_t *stat = &ch_stat[ch];
        int64_t mean_abs = stat->sum_abs / stat->sample_count;
        if (mean_abs < min_mean_abs) {
            continue;
        }
        if (stat->max_sample <= stat->min_sample) {
            ESP_LOGE(TAG, "Analyze recorded PCM failed: channel %d has no amplitude range", ch);
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        if (stat->pair_count > 0 &&
            stat->hold_count * 100 >= stat->pair_count * TEST_PCM_HOLD_PERCENT) {
            ESP_LOGE(TAG, "Analyze recorded PCM failed: channel %d hold %d/%d exceeds %d percent",
                     ch, stat->hold_count, stat->pair_count, TEST_PCM_HOLD_PERCENT);
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        if (stat->pair_count > 0 && mean_abs >= struct_min_mean_abs) {
            int64_t delta_mean = stat->delta_abs / stat->pair_count;
            if (delta_mean * 100 < mean_abs * TEST_PCM_DELTA_ABS_PERCENT) {
                ESP_LOGE(TAG,
                         "Analyze recorded PCM failed: channel %d delta_abs %d is below %d percent of mean_abs %d",
                         ch, (int)delta_mean, TEST_PCM_DELTA_ABS_PERCENT, (int)mean_abs);
                return ESP_CODEC_DEV_INVALID_ARG;
            }
        }
        if (stat->alt_pairs > 0) {
            int alt_hits = (stat->a0b0_count > stat->b0a0_count) ? stat->a0b0_count : stat->b0a0_count;
            if (alt_hits * 100 >= stat->alt_pairs * TEST_PCM_ZERO_ALT_PERCENT) {
                ESP_LOGE(TAG,
                         "Analyze recorded PCM failed: channel %d zero-alternate %d/%d exceeds %d percent",
                         ch, alt_hits, stat->alt_pairs, TEST_PCM_ZERO_ALT_PERCENT);
                return ESP_CODEC_DEV_INVALID_ARG;
            }
        }
    }

    if (channels >= 2) {
        int64_t left_abs = ch_stat[0].sum_abs / ch_stat[0].sample_count;
        int64_t right_abs = ch_stat[1].sum_abs / ch_stat[1].sample_count;
        if (left_abs >= min_mean_abs && right_abs >= min_mean_abs &&
            lr_copy_count * 100 >= frame_count * TEST_PCM_LR_COPY_PERCENT) {
            ESP_LOGE(TAG, "Analyze recorded PCM failed: L/R copy %d/%d exceeds %d percent",
                     lr_copy_count, frame_count, TEST_PCM_LR_COPY_PERCENT);
            return ESP_CODEC_DEV_INVALID_ARG;
        }
    }

    if (chunk_count > 1 &&
        repeated_chunks * 100 >= chunk_count * TEST_PCM_REPEAT_CHUNK_PERCENT) {
        ESP_LOGE(TAG, "Analyze recorded PCM failed: repeated chunks %d/%d exceed %d percent",
                 repeated_chunks, chunk_count, TEST_PCM_REPEAT_CHUNK_PERCENT);
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    return ESP_CODEC_DEV_OK;
}
