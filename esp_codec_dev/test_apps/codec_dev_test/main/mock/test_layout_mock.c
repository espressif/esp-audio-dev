/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdint.h>
#include <string.h>

#include "esp_bit_defs.h"
#include "unity.h"

#include "esp_codec_dev.h"
#include "audio_codec_data_if.h"
#include "audio_codec_if.h"
#include "audio_hw_base.h"

typedef struct {
    audio_codec_if_t                       base;
    const esp_codec_dev_device_map_info_t *order_list;
    int                                    order_list_size;
    const char                            *adc_label;
    bool                                   opened;
} test_layout_codec_if_t;

typedef struct {
    audio_codec_data_if_t        base;
    esp_codec_dev_sample_info_t  fs;
    esp_codec_dev_i2s_mode_t     in_mode;
    esp_codec_dev_i2s_mode_t     out_mode;
    int                          read_count;
    int                          last_read_size;
    bool                         opened;
} test_layout_data_if_t;

#define TEST_LAYOUT_DATA_MAX_CHANNELS  (8)

static bool test_layout_codec_is_open(const audio_hw_base_t *h)
{
    test_layout_codec_if_t *codec = (test_layout_codec_if_t *)h;
    return codec && codec->opened;
}

static int test_layout_codec_open(const audio_hw_base_t *h, void *cfg, int cfg_size)
{
    (void)cfg;
    (void)cfg_size;
    test_layout_codec_if_t *codec = (test_layout_codec_if_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    codec->opened = true;
    return ESP_CODEC_DEV_OK;
}

static int test_layout_codec_set_fs(const audio_hw_base_t *h, esp_codec_dev_sample_info_t *fs, esp_codec_dev_type_t type)
{
    (void)type;
    (void)h;
    return fs == NULL ? ESP_CODEC_DEV_INVALID_ARG : ESP_CODEC_DEV_OK;
}

static int test_layout_codec_get_order_list(const audio_hw_base_t *h, const esp_codec_dev_device_map_info_t **order_list,
                                            int *list_size)
{
    test_layout_codec_if_t *codec = (test_layout_codec_if_t *)h;
    if (codec == NULL || order_list == NULL || list_size == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *order_list = codec->order_list;
    *list_size = codec->order_list_size;
    return ESP_CODEC_DEV_OK;
}

static int test_layout_codec_get_adc_label(const audio_hw_base_t *h, const char **label)
{
    test_layout_codec_if_t *codec = (test_layout_codec_if_t *)h;
    if (codec == NULL || label == NULL || codec->adc_label == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *label = codec->adc_label;
    return ESP_CODEC_DEV_OK;
}

static bool test_layout_data_is_open(const audio_codec_data_if_t *h)
{
    test_layout_data_if_t *data_if = (test_layout_data_if_t *)h;
    return data_if && data_if->opened;
}

static int test_layout_data_open(const audio_codec_data_if_t *h, void *data_cfg, int cfg_size)
{
    (void)data_cfg;
    (void)cfg_size;
    test_layout_data_if_t *data_if = (test_layout_data_if_t *)h;
    if (data_if == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    data_if->opened = true;
    return ESP_CODEC_DEV_OK;
}

static int test_layout_data_enable(const audio_codec_data_if_t *h, esp_codec_dev_type_t dev_type, bool enable)
{
    (void)h;
    (void)dev_type;
    (void)enable;
    return ESP_CODEC_DEV_OK;
}

static int test_layout_data_set_fmt(const audio_codec_data_if_t *h, esp_codec_dev_type_t dev_type,
                                    esp_codec_dev_sample_info_t *fs)
{
    (void)dev_type;
    test_layout_data_if_t *data_if = (test_layout_data_if_t *)h;
    if (data_if == NULL || fs == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    data_if->fs = *fs;
    return ESP_CODEC_DEV_OK;
}

static int test_layout_data_get_fmt(const audio_codec_data_if_t *h, esp_codec_dev_type_t dev_type,
                                    esp_codec_dev_sample_info_t *fs)
{
    (void)dev_type;
    test_layout_data_if_t *data_if = (test_layout_data_if_t *)h;
    if (data_if == NULL || fs == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *fs = data_if->fs;
    return ESP_CODEC_DEV_OK;
}

static int test_layout_data_get_mode(const audio_codec_data_if_t *h, esp_codec_dev_i2s_mode_t *in_mode,
                                     esp_codec_dev_i2s_mode_t *out_mode)
{
    test_layout_data_if_t *data_if = (test_layout_data_if_t *)h;
    if (data_if == NULL || in_mode == NULL || out_mode == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *in_mode = data_if->in_mode;
    *out_mode = data_if->out_mode;
    return ESP_CODEC_DEV_OK;
}

static int test_layout_data_get_order(const audio_codec_data_if_t *h, uint8_t channel,
                                      uint16_t channel_mask, esp_codec_dev_channel_map_t *map)
{
    (void)h;
    if (map == NULL || channel == 0 || channel > TEST_LAYOUT_DATA_MAX_CHANNELS || channel_mask == 0) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    uint16_t valid_mask = (uint16_t)((1U << channel) - 1U);
    if ((channel_mask & ~valid_mask) != 0) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    map->value = 0;
    uint8_t logical_channel = 1;
    for (uint8_t slot = 0; slot < channel; slot++) {
        if (channel_mask & (1U << slot)) {
            map->value |= (uint32_t)(slot + 1) << (4 * (logical_channel - 1));
            logical_channel++;
        }
    }
    return ESP_CODEC_DEV_OK;
}

static int test_layout_data_get_channel_mask(const audio_codec_data_if_t *h, uint8_t channel,
                                             const esp_codec_dev_channel_map_t *map, uint16_t *channel_mask)
{
    (void)h;
    if (channel_mask == NULL || map == NULL || channel == 0 || channel > TEST_LAYOUT_DATA_MAX_CHANNELS ||
        map->value == 0) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    uint16_t mask = 0;
    for (uint8_t logical_channel = 1; logical_channel <= 8; logical_channel++) {
        uint8_t slot = (uint8_t)((map->value >> (4 * (logical_channel - 1))) & 0x0F);
        if (slot == 0) {
            continue;
        }
        if (slot > channel) {
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        uint16_t bit = (uint16_t)(1U << (slot - 1));
        if (mask & bit) {
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        mask |= bit;
    }

    *channel_mask = mask;
    return ESP_CODEC_DEV_OK;
}

static int test_layout_data_read(const audio_codec_data_if_t *h, uint8_t *data, int size)
{
    test_layout_data_if_t *data_if = (test_layout_data_if_t *)h;
    if (data_if == NULL || data == NULL || size < 0) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    data_if->read_count++;
    data_if->last_read_size = size;
    memset(data, 0, size);
    return ESP_CODEC_DEV_OK;
}

static void test_layout_init_codec(test_layout_codec_if_t *codec, const esp_codec_dev_device_map_info_t *order_list,
                                   int order_list_size, const char *adc_label)
{
    memset(codec, 0, sizeof(*codec));
    codec->order_list = order_list;
    codec->order_list_size = order_list_size;
    codec->adc_label = adc_label;
    codec->opened = true;
    codec->base.hw_base.open = test_layout_codec_open;
    codec->base.hw_base.is_open = test_layout_codec_is_open;
    codec->base.hw_base.set_fs = test_layout_codec_set_fs;
    codec->base.hw_base.get_order_list = test_layout_codec_get_order_list;
    codec->base.hw_base.get_adc_label = test_layout_codec_get_adc_label;
}

static void test_layout_init_data(test_layout_data_if_t *data_if, const esp_codec_dev_sample_info_t *fs)
{
    memset(data_if, 0, sizeof(*data_if));
    data_if->fs = *fs;
    data_if->in_mode = ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS;
    data_if->out_mode = ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS;
    data_if->opened = true;
    data_if->base.open = test_layout_data_open;
    data_if->base.is_open = test_layout_data_is_open;
    data_if->base.get_mode = test_layout_data_get_mode;
    data_if->base.get_fmt = test_layout_data_get_fmt;
    data_if->base.enable = test_layout_data_enable;
    data_if->base.set_fmt = test_layout_data_set_fmt;
    data_if->base.read = test_layout_data_read;
}

static void test_layout_init_data_with_compute(test_layout_data_if_t *data_if,
                                               const esp_codec_dev_sample_info_t *fs)
{
    test_layout_init_data(data_if, fs);
    data_if->base.get_order = test_layout_data_get_order;
    data_if->base.get_channel_mask = test_layout_data_get_channel_mask;
}

static void test_layout_label_rejects_output_only_device(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0)}},
    };
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = BIT(0) | BIT(1),
        .mclk_multiple = 256,
    };
    test_layout_codec_if_t codec = {0};
    test_layout_data_if_t data_if = {0};
    test_layout_init_codec(&codec, codec_orders, sizeof(codec_orders) / sizeof(codec_orders[0]), "FL,FR");
    test_layout_init_data_with_compute(&data_if, &fs);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);

    char label[16] = {0};
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_NOT_SUPPORT, esp_codec_dev_get_data_layout_label(dev, label, sizeof(label)));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_NOT_SUPPORT, esp_codec_dev_set_data_layout_label(dev, "FR,FL"));

    esp_codec_dev_delete(dev);
}

static void test_layout_query_missing_compute_callbacks_not_supported(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0)}},
    };
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = BIT(0) | BIT(1),
        .mclk_multiple = 256,
    };
    test_layout_codec_if_t codec = {0};
    test_layout_data_if_t data_if = {0};
    test_layout_init_codec(&codec, codec_orders, sizeof(codec_orders) / sizeof(codec_orders[0]), "FL,FR");
    test_layout_init_data(&data_if, &fs);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);

    esp_codec_dev_channel_map_t map = {0};
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_NOT_SUPPORT, esp_codec_dev_get_data_layout(dev, &map));

    esp_codec_dev_delete(dev);
}

static void test_layout_label_not_supported_without_codec_if(void)
{
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = BIT(0) | BIT(1),
        .mclk_multiple = 256,
    };
    test_layout_data_if_t data_if = {0};
    test_layout_init_data_with_compute(&data_if, &fs);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = NULL,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);

    char label[16] = {0};
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_NOT_SUPPORT, esp_codec_dev_get_data_layout_label(dev, label, sizeof(label)));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_NOT_SUPPORT, esp_codec_dev_set_data_layout_label(dev, "FL,FR"));

    esp_codec_dev_delete(dev);
}

static void test_layout_conversion_rejects_incomplete_user_frame(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 4, {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 3, 2, 4, 0, 0, 0, 0)}},
    };
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 4,
        .bits_per_sample = 16,
        .channel_mask = BIT(0) | BIT(1) | BIT(2) | BIT(3),
        .mclk_multiple = 256,
    };
    test_layout_codec_if_t codec = {0};
    test_layout_data_if_t data_if = {0};
    test_layout_init_codec(&codec, codec_orders, sizeof(codec_orders) / sizeof(codec_orders[0]), "FL,FR,RE,NA");
    test_layout_init_data_with_compute(&data_if, &fs);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ESP_OK(esp_codec_dev_open(dev, &fs));

    // Memory wants: ch3 then ch1
    esp_codec_dev_channel_map_t map = {
        .value = ESP_CODEC_DEV_CHANNEL_MAP(3, 1, 0, 0, 0, 0, 0, 0),
    };
    TEST_ESP_OK(esp_codec_dev_set_data_layout(dev, &map));

    uint8_t data[5] = {0};
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, esp_codec_dev_read(dev, data, 0));
    TEST_ASSERT_EQUAL(0, data_if.read_count);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, esp_codec_dev_read(dev, data, sizeof(data)));
    TEST_ASSERT_EQUAL(0, data_if.read_count);

    TEST_ESP_OK(esp_codec_dev_close(dev));
    esp_codec_dev_delete(dev);
}

static void test_layout_data_order_compute_legacy_2_and_4ch(void)
{
    esp_codec_dev_sample_info_t fs = {0};
    test_layout_data_if_t data_if = {0};
    test_layout_init_data_with_compute(&data_if, &fs);

    esp_codec_dev_channel_map_t map = {0};
    TEST_ESP_OK(data_if.base.get_order(&data_if.base, 2, 0x03, &map));
    TEST_ASSERT_EQUAL_UINT32(ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0), map.value);
    TEST_ESP_OK(data_if.base.get_order(&data_if.base, 4, 0x05, &map));
    TEST_ASSERT_EQUAL_UINT32(ESP_CODEC_DEV_CHANNEL_MAP(1, 3, 0, 0, 0, 0, 0, 0), map.value);

    uint16_t mask = 0;
    TEST_ESP_OK(data_if.base.get_channel_mask(&data_if.base, 4, &map, &mask));
    TEST_ASSERT_EQUAL_UINT16(0x05, mask);
}

static void test_layout_data_order_compute_8ch_mask_and_order(void)
{
    esp_codec_dev_sample_info_t fs = {0};
    test_layout_data_if_t data_if = {0};
    test_layout_init_data_with_compute(&data_if, &fs);

    esp_codec_dev_channel_map_t map = {0};
    TEST_ESP_OK(data_if.base.get_order(&data_if.base, 8, 0x81, &map));
    TEST_ASSERT_EQUAL_UINT32(ESP_CODEC_DEV_CHANNEL_MAP(1, 8, 0, 0, 0, 0, 0, 0), map.value);

    uint16_t mask = 0;
    TEST_ESP_OK(data_if.base.get_channel_mask(&data_if.base, 8, &map, &mask));
    TEST_ASSERT_EQUAL_UINT16(0x81, mask);

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG,
                      data_if.base.get_order(&data_if.base, 8, 0x100, &map));
    esp_codec_dev_channel_map_t dup = {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 1, 0, 0, 0, 0, 0, 0)};
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG,
                      data_if.base.get_channel_mask(&data_if.base, 8, &dup, &mask));
}

static void test_layout_query_uses_computed_data_order(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 4, {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 3, 2, 4, 0, 0, 0, 0)}},
    };
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 4,
        .bits_per_sample = 16,
        .channel_mask = 0x05,
        .mclk_multiple = 256,
    };
    test_layout_codec_if_t codec = {0};
    test_layout_data_if_t data_if = {0};
    test_layout_init_codec(&codec, codec_orders, sizeof(codec_orders) / sizeof(codec_orders[0]), "FL,FR,RE,NA");
    test_layout_init_data_with_compute(&data_if, &fs);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ESP_OK(esp_codec_dev_open(dev, &fs));

    esp_codec_dev_channel_map_t map = {0};
    TEST_ESP_OK(esp_codec_dev_get_data_layout(dev, &map));
    TEST_ASSERT_EQUAL_UINT32(ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0), map.value);

    TEST_ESP_OK(esp_codec_dev_close(dev));
    esp_codec_dev_delete(dev);
}

static void test_layout_set_uses_computed_data_mask(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 4, {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 3, 2, 4, 0, 0, 0, 0)}},
    };
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 4,
        .bits_per_sample = 16,
        .channel_mask = 0x0F,
        .mclk_multiple = 256,
    };
    test_layout_codec_if_t codec = {0};
    test_layout_data_if_t data_if = {0};
    test_layout_init_codec(&codec, codec_orders, sizeof(codec_orders) / sizeof(codec_orders[0]), "FL,FR,RE,NA");
    test_layout_init_data_with_compute(&data_if, &fs);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ESP_OK(esp_codec_dev_open(dev, &fs));

    // Memory wants: ch1 then ch3
    esp_codec_dev_channel_map_t map = {
        .value = ESP_CODEC_DEV_CHANNEL_MAP(1, 3, 0, 0, 0, 0, 0, 0),
    };
    TEST_ESP_OK(esp_codec_dev_set_data_layout(dev, &map));
    TEST_ASSERT_EQUAL_UINT8(4, data_if.fs.channel);
    TEST_ASSERT_EQUAL_UINT16(0x03, data_if.fs.channel_mask);

    TEST_ESP_OK(esp_codec_dev_close(dev));
    esp_codec_dev_delete(dev);
}

TEST_CASE("layout label API rejects output only device", "[mock][layout]")
{
    test_layout_label_rejects_output_only_device();
}

TEST_CASE("layout query treats missing computed order callbacks as not supported", "[mock][layout]")
{
    test_layout_query_missing_compute_callbacks_not_supported();
}

TEST_CASE("layout label API returns not supported without codec interface", "[mock][layout]")
{
    test_layout_label_not_supported_without_codec_if();
}

TEST_CASE("layout conversion rejects incomplete user frame before reading bus data", "[mock][layout]")
{
    test_layout_conversion_rejects_incomplete_user_frame();
}

TEST_CASE("layout data order compute matches legacy table for 2 and 4 channels", "[mock][layout]")
{
    test_layout_data_order_compute_legacy_2_and_4ch();
}

TEST_CASE("layout data order compute supports 8 channel mask and order", "[mock][layout]")
{
    test_layout_data_order_compute_8ch_mask_and_order();
}

TEST_CASE("layout query uses computed data order without order table", "[mock][layout]")
{
    test_layout_query_uses_computed_data_order();
}

TEST_CASE("layout set uses computed data mask without order table", "[mock][layout]")
{
    test_layout_set_uses_computed_data_mask();
}
