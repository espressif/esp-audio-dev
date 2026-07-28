/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdint.h>
#include <stdlib.h>

#include "unity.h"

#include "esp_codec_dev_types.h"
#include "codec_dev_data_cvt.h"

static void write_s24le(uint8_t *p, int32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
}

static void test_data_convert_in_place_expand_should_keep_all_frames(void)
{
    uint8_t buf[16] = {0};
    int16_t *s = (int16_t *)buf;
    // src: 2 frames, 2ch, 16bit, order 0x12
    // frame0: ch1=100 ch2=200, frame1: ch1=300 ch2=400
    s[0] = 100;
    s[1] = 200;
    s[2] = 300;
    s[3] = 400;

    codec_dev_data_cvt_info_t src = {
        .data = buf,
        .len = 8,
        .map = {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0)},
        .bits = 16,
        .ch_num = 2,
    };
    codec_dev_data_cvt_info_t dst = {
        .data = buf,
        .len = 16,
        .map = {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 3, 4, 0, 0, 0, 0)},
        .bits = 16,
        .ch_num = 4,
    };
    int ret = codec_dev_data_cvt_layout(&src, &dst);
    TEST_ESP_OK(ret);

    int16_t *d = (int16_t *)buf;
    // frame0 -> [100, 200, 0, 0]
    TEST_ASSERT_EQUAL_INT16(100, d[0]);
    TEST_ASSERT_EQUAL_INT16(200, d[1]);
    TEST_ASSERT_EQUAL_INT16(0, d[2]);
    TEST_ASSERT_EQUAL_INT16(0, d[3]);
    // frame1 -> [300, 400, 0, 0]
    TEST_ASSERT_EQUAL_INT16(300, d[4]);
    TEST_ASSERT_EQUAL_INT16(400, d[5]);
    TEST_ASSERT_EQUAL_INT16(0, d[6]);
    TEST_ASSERT_EQUAL_INT16(0, d[7]);
}

static void test_data_convert_should_reject_non_frame_aligned_src_length(void)
{
    uint8_t src_buf[8] = {0};
    uint8_t dst_buf[16] = {0};
    codec_dev_data_cvt_info_t src = {
        .data = src_buf,
        .len = 6,  // not multiple of frame_size(2ch*2B=4)
        .map = {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0)},
        .bits = 16,
        .ch_num = 2,
    };
    codec_dev_data_cvt_info_t dst = {
        .data = dst_buf,
        .len = 16,
        .map = {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0)},
        .bits = 16,
        .ch_num = 2,
    };
    int ret = codec_dev_data_cvt_layout(&src, &dst);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, ret);
}

static void test_data_convert_should_reject_duplicate_order_digits(void)
{
    uint8_t src_buf[8] = {0};
    uint8_t dst_buf[8] = {0};
    codec_dev_data_cvt_info_t src = {
        .data = src_buf,
        .len = 8,
        .map = {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 1, 0, 0, 0, 0, 0, 0)},  // duplicate channel ID
        .bits = 16,
        .ch_num = 2,
    };
    codec_dev_data_cvt_info_t dst = {
        .data = dst_buf,
        .len = 8,
        .map = {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0)},
        .bits = 16,
        .ch_num = 2,
    };
    int ret = codec_dev_data_cvt_layout(&src, &dst);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, ret);
}

static void test_data_convert_should_reject_unsupported_bit_depth(void)
{
    uint8_t src_buf[8] = {0};
    uint8_t dst_buf[8] = {0};
    codec_dev_data_cvt_info_t src = {
        .data = src_buf,
        .len = 8,
        .map = {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0)},
        .bits = 20,  // unsupported
        .ch_num = 2,
    };
    codec_dev_data_cvt_info_t dst = {
        .data = dst_buf,
        .len = 8,
        .map = {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0)},
        .bits = 16,
        .ch_num = 2,
    };
    int ret = codec_dev_data_cvt_layout(&src, &dst);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, ret);
}

static void test_data_convert_should_select_channels_and_reorder_from_4ch_to_2ch(void)
{
    // src order 0x1324 means src slots are [ch1, ch3, ch2, ch4]
    // dst order 0x21 asks output [ch2, ch1]
    int16_t src_pcm[8] = {
        11, 33, 22, 44,     // frame0: ch1=11,ch3=33,ch2=22,ch4=44
        111, 333, 222, 444  // frame1
    };
    int16_t dst_pcm[4] = {0};

    codec_dev_data_cvt_info_t src = {
        .data = (uint8_t *)src_pcm,
        .len = sizeof(src_pcm),
        .map = {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 3, 2, 4, 0, 0, 0, 0)},
        .bits = 16,
        .ch_num = 4,
    };
    codec_dev_data_cvt_info_t dst = {
        .data = (uint8_t *)dst_pcm,
        .len = sizeof(dst_pcm),
        .map = {.value = ESP_CODEC_DEV_CHANNEL_MAP(2, 1, 0, 0, 0, 0, 0, 0)},
        .bits = 16,
        .ch_num = 2,
    };
    int ret = codec_dev_data_cvt_layout(&src, &dst);
    TEST_ESP_OK(ret);

    // expect frame0 [ch2, ch1] => [22, 11], frame1 => [222, 111]
    TEST_ASSERT_EQUAL_INT16(22, dst_pcm[0]);
    TEST_ASSERT_EQUAL_INT16(11, dst_pcm[1]);
    TEST_ASSERT_EQUAL_INT16(222, dst_pcm[2]);
    TEST_ASSERT_EQUAL_INT16(111, dst_pcm[3]);
}

static void test_data_convert_should_downmix_channels_and_bit_depth_24_to_16(void)
{
    // one frame, src 4ch/24bit order 0x1234 -> dst 2ch/16bit order 0x41
    uint8_t src_buf[12] = {0};
    int16_t dst_pcm[2] = {0};
    write_s24le(&src_buf[0], 100000);   // ch1
    write_s24le(&src_buf[3], 200000);   // ch2
    write_s24le(&src_buf[6], 300000);   // ch3
    write_s24le(&src_buf[9], -400000);  // ch4

    codec_dev_data_cvt_info_t src = {
        .data = src_buf,
        .len = sizeof(src_buf),
        .map = {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 3, 4, 0, 0, 0, 0)},
        .bits = 24,
        .ch_num = 4,
    };
    codec_dev_data_cvt_info_t dst = {
        .data = (uint8_t *)dst_pcm,
        .len = sizeof(dst_pcm),
        .map = {.value = ESP_CODEC_DEV_CHANNEL_MAP(4, 1, 0, 0, 0, 0, 0, 0)},  // output ch4 then ch1
        .bits = 16,
        .ch_num = 2,
    };
    int ret = codec_dev_data_cvt_layout(&src, &dst);
    TEST_ESP_OK(ret);

    // 24->16 uses saturation by value range in current implementation
    TEST_ASSERT_EQUAL_INT16(-32768, dst_pcm[0]);  // ch4 clipped
    TEST_ASSERT_EQUAL_INT16(32767, dst_pcm[1]);   // ch1 clipped
}

static void test_data_convert_should_fill_zero_for_missing_destination_channels(void)
{
    // src has ch1/ch2 only, dst asks ch1/ch2/ch3/ch4
    int16_t src_pcm[2] = {123, -456};
    int16_t dst_pcm[4] = {0};

    codec_dev_data_cvt_info_t src = {
        .data = (uint8_t *)src_pcm,
        .len = sizeof(src_pcm),
        .map = {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0)},
        .bits = 16,
        .ch_num = 2,
    };
    codec_dev_data_cvt_info_t dst = {
        .data = (uint8_t *)dst_pcm,
        .len = sizeof(dst_pcm),
        .map = {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 3, 4, 0, 0, 0, 0)},
        .bits = 16,
        .ch_num = 4,
    };
    int ret = codec_dev_data_cvt_layout(&src, &dst);
    TEST_ESP_OK(ret);

    TEST_ASSERT_EQUAL_INT16(123, dst_pcm[0]);
    TEST_ASSERT_EQUAL_INT16(-456, dst_pcm[1]);
    TEST_ASSERT_EQUAL_INT16(0, dst_pcm[2]);
    TEST_ASSERT_EQUAL_INT16(0, dst_pcm[3]);
}

static void test_data_convert_should_support_8bit_reorder_and_zero_fill(void)
{
    int8_t src_pcm[4] = {11, 22, 33, 44};
    int8_t dst_pcm[6] = {0};

    codec_dev_data_cvt_info_t src = {
        .data = (uint8_t *)src_pcm,
        .len = sizeof(src_pcm),
        .map = {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0)},
        .bits = 8,
        .ch_num = 2,
    };
    codec_dev_data_cvt_info_t dst = {
        .data = (uint8_t *)dst_pcm,
        .len = sizeof(dst_pcm),
        .map = {.value = ESP_CODEC_DEV_CHANNEL_MAP(3, 2, 1, 0, 0, 0, 0, 0)},
        .bits = 8,
        .ch_num = 3,
    };

    int ret = codec_dev_data_cvt_layout(&src, &dst);
    TEST_ESP_OK(ret);

    TEST_ASSERT_EQUAL_INT8(0, dst_pcm[0]);
    TEST_ASSERT_EQUAL_INT8(22, dst_pcm[1]);
    TEST_ASSERT_EQUAL_INT8(11, dst_pcm[2]);
    TEST_ASSERT_EQUAL_INT8(0, dst_pcm[3]);
    TEST_ASSERT_EQUAL_INT8(44, dst_pcm[4]);
    TEST_ASSERT_EQUAL_INT8(33, dst_pcm[5]);
}

static void test_esp32_read_mono_fix_16bit_should_swap_adjacent_samples(void)
{
    int16_t read_pcm[4] = {10, 20, 30, 40};
    int16_t out_pcm[4] = {0};
    int ret = esp32_read_mono_fix((uint8_t *)read_pcm, sizeof(read_pcm), 16, (uint8_t *)out_pcm, sizeof(out_pcm));
    TEST_ESP_OK(ret);
    TEST_ASSERT_EQUAL_INT16(20, out_pcm[0]);
    TEST_ASSERT_EQUAL_INT16(10, out_pcm[1]);
    TEST_ASSERT_EQUAL_INT16(40, out_pcm[2]);
    TEST_ASSERT_EQUAL_INT16(30, out_pcm[3]);
}

static void test_esp32_read_mono_fix_8bit_should_swap_and_pack_from_16bit_container(void)
{
    uint16_t read_16[4] = {0x1100, 0x2200, 0x3300, 0x4400};
    uint8_t out_8[4] = {0};
    int ret = esp32_read_mono_fix((uint8_t *)read_16, sizeof(read_16), 8, out_8, sizeof(out_8));
    TEST_ESP_OK(ret);
    // after swap: 2200,1100,4400,3300 -> pack high bytes
    TEST_ASSERT_EQUAL_HEX8(0x22, out_8[0]);
    TEST_ASSERT_EQUAL_HEX8(0x11, out_8[1]);
    TEST_ASSERT_EQUAL_HEX8(0x44, out_8[2]);
    TEST_ASSERT_EQUAL_HEX8(0x33, out_8[3]);
}

static void test_esp32_write_mono_fix_16bit_should_allocate_cache_and_swap(void)
{
    int16_t in_pcm[4] = {10, 20, 30, 40};
    uint8_t *cache = NULL;
    int cache_len = 0;
    int ret = esp32_write_mono_fix((uint8_t *)in_pcm, sizeof(in_pcm), 16, &cache, &cache_len);
    TEST_ESP_OK(ret);
    TEST_ASSERT_NOT_NULL(cache);
    TEST_ASSERT_NOT_EQUAL((void *)in_pcm, (void *)cache);
    TEST_ASSERT_EQUAL(sizeof(in_pcm), cache_len);
    int16_t *out = (int16_t *)cache;
    TEST_ASSERT_EQUAL_INT16(20, out[0]);
    TEST_ASSERT_EQUAL_INT16(10, out[1]);
    TEST_ASSERT_EQUAL_INT16(40, out[2]);
    TEST_ASSERT_EQUAL_INT16(30, out[3]);
    free(cache);
}

static void test_esp32_write_mono_fix_24bit_should_expand_to_32bit_container(void)
{
    uint8_t in_24[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t *cache = NULL;
    int cache_len = 0;
    int ret = esp32_write_mono_fix(in_24, sizeof(in_24), 24, &cache, &cache_len);
    TEST_ESP_OK(ret);
    TEST_ASSERT_NOT_NULL(cache);
    TEST_ASSERT_EQUAL(8, cache_len);
    uint32_t *w = (uint32_t *)cache;
    TEST_ASSERT_EQUAL_HEX32(0x33221100, w[0]);
    TEST_ASSERT_EQUAL_HEX32(0x66554400, w[1]);
    free(cache);
}

static void test_esp32_mono_fix_should_reject_invalid_arguments(void)
{
    uint8_t b[8] = {0};
    uint8_t out[8] = {0};
    uint8_t *cache = NULL;
    int cache_len = 0;

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, esp32_read_mono_fix(NULL, 8, 16, out, 8));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, esp32_read_mono_fix(b, 8, 24, out, 5));  // non-3-aligned user_len
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, esp32_write_mono_fix(NULL, 8, 16, &cache, &cache_len));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, esp32_write_mono_fix(b, 5, 24, &cache, &cache_len));  // non-3-aligned len
}

TEST_CASE("data_convert in-place expand should keep all frames", "[mock][data_convert]")
{
    test_data_convert_in_place_expand_should_keep_all_frames();
}

TEST_CASE("data_convert should reject non-frame-aligned src length", "[mock][data_convert]")
{
    test_data_convert_should_reject_non_frame_aligned_src_length();
}

TEST_CASE("data_convert should reject duplicate order digits", "[mock][data_convert]")
{
    test_data_convert_should_reject_duplicate_order_digits();
}

TEST_CASE("data_convert should reject unsupported bit depth", "[mock][data_convert]")
{
    test_data_convert_should_reject_unsupported_bit_depth();
}

TEST_CASE("data_convert should select channels and reorder from 4ch to 2ch", "[mock][data_convert]")
{
    test_data_convert_should_select_channels_and_reorder_from_4ch_to_2ch();
}

TEST_CASE("data_convert should downmix channels and bit depth 24 to 16", "[mock][data_convert]")
{
    test_data_convert_should_downmix_channels_and_bit_depth_24_to_16();
}

TEST_CASE("data_convert should fill zero for missing destination channels", "[mock][data_convert]")
{
    test_data_convert_should_fill_zero_for_missing_destination_channels();
}

TEST_CASE("data_convert should support 8bit reorder and zero fill", "[mock][data_convert]")
{
    test_data_convert_should_support_8bit_reorder_and_zero_fill();
}

TEST_CASE("esp32_read_mono_fix 16bit should swap adjacent samples", "[mock][data_convert][mono_fix]")
{
    test_esp32_read_mono_fix_16bit_should_swap_adjacent_samples();
}

TEST_CASE("esp32_read_mono_fix 8bit should swap and pack from 16bit container", "[mock][data_convert][mono_fix]")
{
    test_esp32_read_mono_fix_8bit_should_swap_and_pack_from_16bit_container();
}

TEST_CASE("esp32_write_mono_fix 16bit should allocate cache and swap", "[mock][data_convert][mono_fix]")
{
    test_esp32_write_mono_fix_16bit_should_allocate_cache_and_swap();
}

TEST_CASE("esp32_write_mono_fix 24bit should expand to 32bit container", "[mock][data_convert][mono_fix]")
{
    test_esp32_write_mono_fix_24bit_should_expand_to_32bit_container();
}

TEST_CASE("esp32 mono_fix should reject invalid arguments", "[mock][data_convert][mono_fix]")
{
    test_esp32_mono_fix_should_reject_invalid_arguments();
}
