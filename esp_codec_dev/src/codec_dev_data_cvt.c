/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"

#include "codec_dev_order.h"
#include "codec_dev_data_cvt.h"

static const char *TAG = "ESP_DATA_CONVERT";

static inline bool _cvt_is_supported_bits(int bits)
{
    return (bits == 8 || bits == 16 || bits == 24 || bits == 32);
}

static inline bool _cvt_is_dense_memory_map(const esp_codec_dev_channel_map_t *map, int ch_num)
{
    if (map == NULL || ch_num <= 0 || ch_num > 8) {
        return false;
    }
    if (codec_dev_order_is_valid(map) == false) {
        return false;
    }
    return codec_dev_channel_map_count_channels(map) == ch_num;
}

static int32_t _cvt_read_sample(const uint8_t *ptr, int bits)
{
    if (bits <= 8) {
        int8_t v = (int8_t)ptr[0];
        return (int32_t)v;
    }
    if (bits <= 16) {
        int16_t v;
        memcpy(&v, ptr, 2);
        return (int32_t)v;
    }
    if (bits <= 24) {
        int32_t v = (int32_t)(ptr[0] | (ptr[1] << 8) | (ptr[2] << 16));
        if (v & 0x800000) {
            v |= 0xFF000000;
        }
        return v;
    }
    int32_t v;
    memcpy(&v, ptr, 4);
    return v;
}

static void _cvt_write_sample(uint8_t *ptr, int bits, int32_t val)
{
    if (bits <= 8) {
        if (val > 127) {
            val = 127;
        } else if (val < -128) {
            val = -128;
        }
        ptr[0] = (uint8_t)((int8_t)val);
        return;
    }
    if (bits <= 16) {
        if (val > 32767) {
            val = 32767;
        } else if (val < -32768) {
            val = -32768;
        }
        int16_t v = (int16_t)val;
        memcpy(ptr, &v, 2);
        return;
    }
    if (bits <= 24) {
        if (val > 0x7FFFFF) {
            val = 0x7FFFFF;
        } else if (val < -0x800000) {
            val = -0x800000;
        }
        ptr[0] = (uint8_t)(val & 0xFF);
        ptr[1] = (uint8_t)((val >> 8) & 0xFF);
        ptr[2] = (uint8_t)((val >> 16) & 0xFF);
        return;
    }
    memcpy(ptr, &val, 4);
}

static void _mono_swap_adjacent_u16(uint8_t *buf, int len)
{
    if (buf == NULL || len < 4) {
        return;
    }
    int16_t *p = (int16_t *)buf;
    int n = len >> 1;
    for (int i = 0; i + 1 < n; i += 2) {
        int16_t t = p[i];
        p[i] = p[i + 1];
        p[i + 1] = t;
    }
}

static void _pack_8_from_16(uint8_t *dst, int dst_len, const uint8_t *src16, int src_len)
{
    if (dst == NULL || src16 == NULL || dst_len <= 0 || src_len <= 1) {
        return;
    }
    int n = src_len >> 1;
    if (dst_len < n) {
        n = dst_len;
    }
    const uint16_t *s = (const uint16_t *)src16;
    for (int i = 0; i < n; i++) {
        dst[i] = (uint8_t)(s[i] >> 8);
    }
}

static void _pack_24_from_32(uint8_t *dst, int dst_len, const uint8_t *src32, int src_len)
{
    if (dst == NULL || src32 == NULL || dst_len < 3 || src_len < 4) {
        return;
    }
    int n = src_len >> 2;
    int max_n = dst_len / 3;
    if (n > max_n) {
        n = max_n;
    }
    const uint32_t *s = (const uint32_t *)src32;
    for (int i = 0; i < n; i++) {
        uint32_t v = s[i];
        dst[i * 3 + 0] = (uint8_t)((v >> 8) & 0xFF);
        dst[i * 3 + 1] = (uint8_t)((v >> 16) & 0xFF);
        dst[i * 3 + 2] = (uint8_t)((v >> 24) & 0xFF);
    }
}

static void _unpack_16_from_8(uint8_t *dst16, int dst16_len, const uint8_t *src8, int src8_len)
{
    if (dst16 == NULL || src8 == NULL || dst16_len < 2 || src8_len <= 0) {
        return;
    }
    int n = src8_len;
    int max_n = dst16_len >> 1;
    if (n > max_n) {
        n = max_n;
    }
    uint16_t *d = (uint16_t *)dst16;
    for (int i = 0; i < n; i++) {
        d[i] = (uint16_t)((uint16_t)src8[i] << 8);
    }
}

static void _unpack_32_from_24(uint8_t *dst32, int dst32_len, const uint8_t *src24, int src24_len)
{
    if (dst32 == NULL || src24 == NULL || dst32_len < 4 || src24_len < 3) {
        return;
    }
    int n = src24_len / 3;
    int max_n = dst32_len >> 2;
    if (n > max_n) {
        n = max_n;
    }
    uint32_t *d = (uint32_t *)dst32;
    for (int i = 0; i < n; i++) {
        uint32_t b0 = (uint32_t)src24[i * 3 + 0];
        uint32_t b1 = (uint32_t)src24[i * 3 + 1];
        uint32_t b2 = (uint32_t)src24[i * 3 + 2];
        d[i] = (b0 << 8) | (b1 << 16) | (b2 << 24);
    }
}

int codec_dev_data_cvt_layout(const codec_dev_data_cvt_info_t *src, const codec_dev_data_cvt_info_t *dst)
{
    if (src == NULL || dst == NULL || src->data == NULL || dst->data == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    /* One-shot trace: set log level for TAG to DEBUG, e.g. esp_log_level_set("ESP_DATA_CONVERT", ESP_LOG_DEBUG) */
    static bool s_logged_pcm_layout_args_once;
    if (!s_logged_pcm_layout_args_once) {
        s_logged_pcm_layout_args_once = true;
        ESP_LOGD(TAG,
                 "pcm_convert_layout (once) src=%p len=%d map=0x%lx bits=%d ch=%d | dst=%p len=%d map=0x%lx bits=%d ch=%d",
                 (void *)src->data, src->len, (unsigned long)src->map.value, src->bits, src->ch_num,
                 (void *)dst->data, dst->len, (unsigned long)dst->map.value, dst->bits, dst->ch_num);
    }
    if (src->ch_num <= 0 || src->ch_num > 8 || dst->ch_num <= 0 || dst->ch_num > 8) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (_cvt_is_supported_bits(src->bits) == false || _cvt_is_supported_bits(dst->bits) == false) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    int src_bps = src->bits / 8;
    int dst_bps = dst->bits / 8;
    int src_frame_size = src->ch_num * src_bps;
    int dst_frame_size = dst->ch_num * dst_bps;
    if (src_frame_size <= 0 || dst_frame_size <= 0) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if ((src->len % src_frame_size) != 0) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int n_frame = src->len / src_frame_size;
    if (n_frame <= 0) {
        return ESP_CODEC_DEV_OK;
    }
    int dst_need = n_frame * dst_frame_size;
    if (dst->len < dst_need) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    if (_cvt_is_dense_memory_map(&src->map, src->ch_num) == false ||
        _cvt_is_dense_memory_map(&dst->map, dst->ch_num) == false) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    bool in_place = (src->data == dst->data);
    uint8_t *frame_tmp = NULL;
    if (in_place) {
        frame_tmp = (uint8_t *)malloc((size_t)src_frame_size);
        if (frame_tmp == NULL) {
            return ESP_CODEC_DEV_NO_MEM;
        }
    }

    int start = 0;
    int end = n_frame;
    int step = 1;
    if (in_place && dst_frame_size > src_frame_size) {
        // For in-place expansion, walk backward to avoid overwriting unread source frames.
        start = n_frame - 1;
        end = -1;
        step = -1;
    }

    for (int f = start; f != end; f += step) {
        const uint8_t *src_frame;
        uint8_t *dst_frame = dst->data + f * dst_frame_size;
        if (in_place) {
            memcpy(frame_tmp, src->data + f * src_frame_size, (size_t)src_frame_size);
            src_frame = frame_tmp;
        } else {
            src_frame = src->data + f * src_frame_size;
        }
        for (int dst_pos = 1; dst_pos <= dst->ch_num; dst_pos++) {
            uint8_t channel_id = codec_dev_channel_map_get_slot(&dst->map, (uint8_t)dst_pos);
            int32_t val = 0;
            uint8_t src_pos = codec_dev_channel_map_find_pos(&src->map, channel_id);
            if (src_pos != 0) {
                const uint8_t *s = src_frame + (src_pos - 1) * src_bps;
                val = _cvt_read_sample(s, src->bits);
            }
            _cvt_write_sample(dst_frame + (dst_pos - 1) * dst_bps, dst->bits, val);
        }
    }

    if (frame_tmp) {
        free(frame_tmp);
    }
    return ESP_CODEC_DEV_OK;
}

int esp32_read_mono_fix(uint8_t *read_data, int read_len, int bits, uint8_t *user_data, int user_len)
{
    if (read_data == NULL || user_data == NULL || read_len <= 0 || user_len <= 0) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (bits == 16) {
        if ((read_len & 0x1) || (user_len & 0x1) || read_len < user_len) {
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        _mono_swap_adjacent_u16(read_data, read_len);
        memcpy(user_data, read_data, user_len);
        return ESP_CODEC_DEV_OK;
    }

    if (bits == 8) {
        if ((read_len & 0x1) || read_len < user_len * 2) {
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        _mono_swap_adjacent_u16(read_data, read_len);
        _pack_8_from_16(user_data, user_len, read_data, read_len);
        return ESP_CODEC_DEV_OK;
    }

    if (bits == 24) {
        if ((user_len % 3) != 0) {
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        int n_sample = user_len / 3;
        if (read_len < n_sample * 4) {
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        _pack_24_from_32(user_data, user_len, read_data, read_len);
        return ESP_CODEC_DEV_OK;
    }
    return ESP_CODEC_DEV_NOT_SUPPORT;
}

int esp32_write_mono_fix(uint8_t *user_data, int len, int bits, uint8_t **ret_cache, int *ret_cache_len)
{
    if (user_data == NULL || ret_cache == NULL || ret_cache_len == NULL || len <= 0) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *ret_cache = NULL;
    *ret_cache_len = 0;

    if (bits == 16) {
        if (len & 0x1) {
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        // Avoid in-place modification for read-only input buffer.
        uint8_t *cache = (uint8_t *)malloc((size_t)len);
        if (cache == NULL) {
            ESP_LOGE(TAG, "Operation failed: memory allocation failed");
            return ESP_CODEC_DEV_NO_MEM;
        }
        memcpy(cache, user_data, (size_t)len);
        _mono_swap_adjacent_u16(cache, len);
        *ret_cache = cache;
        *ret_cache_len = len;
        return ESP_CODEC_DEV_OK;
    }

    if (bits == 8) {
        int cache_len = len * 2;
        uint8_t *cache = (uint8_t *)malloc((size_t)cache_len);
        if (cache == NULL) {
            ESP_LOGE(TAG, "Operation failed: memory allocation failed");
            return ESP_CODEC_DEV_NO_MEM;
        }
        _unpack_16_from_8(cache, cache_len, user_data, len);
        _mono_swap_adjacent_u16(cache, cache_len);
        *ret_cache = cache;
        *ret_cache_len = cache_len;
        return ESP_CODEC_DEV_OK;
    }

    if (bits == 24) {
        if ((len % 3) != 0) {
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        int n_sample = len / 3;
        int cache_len = n_sample * 4;
        uint8_t *cache = (uint8_t *)malloc((size_t)cache_len);
        if (cache == NULL) {
            ESP_LOGE(TAG, "Operation failed: memory allocation failed");
            return ESP_CODEC_DEV_NO_MEM;
        }
        _unpack_32_from_24(cache, cache_len, user_data, len);
        *ret_cache = cache;
        *ret_cache_len = cache_len;
        return ESP_CODEC_DEV_OK;
    }
    return ESP_CODEC_DEV_NOT_SUPPORT;
}
