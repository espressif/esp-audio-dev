/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stddef.h>

#include "unity.h"

#include "esp_bit_defs.h"

#include "audio_codec_adc_label.h"
#include "esp_codec_dev_types.h"

static void test_adc_label_parse_examples(void)
{
    uint16_t mic_mask = 0xFFFF;
    uint8_t channel_num = 0xFF;

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_codec_adc_label_parse("FL,FL,RE", &mic_mask, &channel_num));
    TEST_ASSERT_EQUAL_UINT16(BIT(0) | BIT(1) | BIT(2), mic_mask);
    TEST_ASSERT_EQUAL_UINT8(3, channel_num);

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_codec_adc_label_parse("FL,NA,RE", &mic_mask, &channel_num));
    TEST_ASSERT_EQUAL_UINT16(BIT(0) | BIT(2), mic_mask);
    TEST_ASSERT_EQUAL_UINT8(3, channel_num);

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_codec_adc_label_parse("FL,FR,SL,SR", &mic_mask, &channel_num));
    TEST_ASSERT_EQUAL_UINT16(BIT(0) | BIT(1) | BIT(2) | BIT(3), mic_mask);
    TEST_ASSERT_EQUAL_UINT8(4, channel_num);

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_codec_adc_label_parse("FL,FL,NA,FL", &mic_mask, &channel_num));
    TEST_ASSERT_EQUAL_UINT16(BIT(0) | BIT(1) | BIT(3), mic_mask);
    TEST_ASSERT_EQUAL_UINT8(4, channel_num);

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_codec_adc_label_parse("RE,FL", &mic_mask, &channel_num));
    TEST_ASSERT_EQUAL_UINT16(BIT(0) | BIT(1), mic_mask);
    TEST_ASSERT_EQUAL_UINT8(2, channel_num);

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_codec_adc_label_parse("FL,FR,RE,NA", &mic_mask, &channel_num));
    TEST_ASSERT_EQUAL_UINT16(BIT(0) | BIT(1) | BIT(2), mic_mask);
    TEST_ASSERT_EQUAL_UINT8(4, channel_num);

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_codec_adc_label_parse("FL,FR,SL,SR,RE,NA", &mic_mask, &channel_num));
    TEST_ASSERT_EQUAL_UINT16(BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4), mic_mask);
    TEST_ASSERT_EQUAL_UINT8(6, channel_num);

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_codec_adc_label_parse("FL, NA, RE", &mic_mask, &channel_num));
    TEST_ASSERT_EQUAL_UINT16(BIT(0) | BIT(2), mic_mask);
    TEST_ASSERT_EQUAL_UINT8(3, channel_num);

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_codec_adc_label_parse("FC,BL,BR", &mic_mask, &channel_num));
    TEST_ASSERT_EQUAL_UINT16(BIT(0) | BIT(1) | BIT(2), mic_mask);
    TEST_ASSERT_EQUAL_UINT8(3, channel_num);

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK,
                      audio_codec_adc_label_parse("FL,FR,SL,SR,BL,BR,RE,NA", &mic_mask, &channel_num));
    TEST_ASSERT_EQUAL_UINT16(BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) | BIT(6), mic_mask);
    TEST_ASSERT_EQUAL_UINT8(8, channel_num);
}

static void test_adc_label_parse_empty_and_unused(void)
{
    uint16_t mic_mask = 0xFFFF;
    uint8_t channel_num = 0xFF;

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_codec_adc_label_parse(NULL, &mic_mask, &channel_num));
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, mic_mask);
    TEST_ASSERT_EQUAL_UINT8(0, channel_num);

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_codec_adc_label_parse("", &mic_mask, &channel_num));
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, mic_mask);
    TEST_ASSERT_EQUAL_UINT8(0, channel_num);

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_codec_adc_label_parse("NA,NA", &mic_mask, &channel_num));
    TEST_ASSERT_EQUAL_UINT16(0, mic_mask);
    TEST_ASSERT_EQUAL_UINT8(2, channel_num);

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_codec_adc_label_parse("FL,FR", &mic_mask, NULL));
    TEST_ASSERT_EQUAL_UINT16(BIT(0) | BIT(1), mic_mask);

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, audio_codec_adc_label_parse("FL", NULL, &channel_num));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, audio_codec_adc_label_parse("FL,,RE", &mic_mask, &channel_num));
}

static void test_adc_label_parse_rejects_unsupported_and_malformed(void)
{
    static const char *const invalid_labels[] = {
        "NF",
        "N/A",
        "na",
        "Na",
        "fl",
        "MIC1",
        "FL,na,RE",
        "FL, ,RE",
        "FL,",
        "FL,FR,SL,SR,BL,BR,RE,NA,FC",
        "ABCDEFGHIJKLMNOP",
    };
    uint16_t mic_mask = 0xFFFF;
    uint8_t channel_num = 0xFF;

    for (size_t i = 0; i < sizeof(invalid_labels) / sizeof(invalid_labels[0]); i++) {
        mic_mask = 0xFFFF;
        channel_num = 0xFF;
        TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG,
                          audio_codec_adc_label_parse(invalid_labels[i], &mic_mask, &channel_num));
        TEST_ASSERT_EQUAL_UINT16(0, mic_mask);
        TEST_ASSERT_EQUAL_UINT8(0, channel_num);
    }
}

TEST_CASE("adc label converts to hardware mic select mask", "[mock][layout]")
{
    test_adc_label_parse_examples();
}

TEST_CASE("adc label mask treats empty as all selected and NA as unused", "[mock][layout]")
{
    test_adc_label_parse_empty_and_unused();
}

TEST_CASE("adc label parse rejects unsupported tokens and malformed lists", "[mock][layout]")
{
    test_adc_label_parse_rejects_unsupported_and_malformed();
}
