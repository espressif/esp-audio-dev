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

#define TEST_LAYOUT_SYSCLK_HIST  (8)

typedef struct {
    audio_codec_if_t                       base;
    const esp_codec_dev_device_map_info_t *order_list;
    int                                    order_list_size;
    char                                   adc_label[AUDIO_HW_ADC_LABEL_MAX_LEN];
    int                                    set_adc_label_call_count;
    int                                    set_adc_label_ret;
    bool                                   set_adc_label_copy;
    esp_codec_dev_sample_info_t            last_fs;
    esp_codec_dev_type_t                   last_fs_type;
    esp_codec_dev_sample_info_t            fs_history[4];
    esp_codec_dev_type_t                   fs_type_history[4];
    int                                    set_fs_seq[4];
    int                                    set_fs_call_count;
    int                                    set_fs_ret;
    int                                    set_fs_fail_on_call;
    esp_codec_dev_sys_clk_info_t           last_clk_info;
    esp_codec_dev_sys_clk_info_t           clk_info_history[TEST_LAYOUT_SYSCLK_HIST];
    int                                    set_sysclk_seq[TEST_LAYOUT_SYSCLK_HIST];
    bool                                   clk_info_null_history[TEST_LAYOUT_SYSCLK_HIST];
    bool                                   last_clk_info_is_null;
    int                                    set_sysclk_call_count;
    int                                    set_sysclk_apply_count;
    int                                    set_sysclk_ret;
    audio_hw_adc_if_t                      adc_if;
    int                                    adc_enable_true_count;
    int                                    adc_enable_false_count;
    bool                                   adc_enabled;
    esp_codec_dev_device_map_info_t        map_query_order_list[4];
    int                                    get_order_list_call_count;
    bool                                   opened;
} test_layout_codec_if_t;

typedef struct {
    audio_codec_data_if_t        base;
    esp_codec_dev_sample_info_t  fs;
    esp_codec_dev_sample_info_t  last_set_fmt_fs;
    esp_codec_dev_i2s_mode_t     in_mode;
    esp_codec_dev_i2s_mode_t     out_mode;
    esp_codec_dev_map_query_t    in_map_query;
    esp_codec_dev_map_query_t    out_map_query;
    esp_codec_dev_bus_info_t     in_bus;
    esp_codec_dev_bus_info_t     out_bus;
    int                          set_fmt_call_count;
    int                          set_fmt_ret;
    int                          set_fmt_fail_on_call;
    int                          get_bus_info_call_count;
    int                          get_bus_info_ret;
    esp_codec_dev_type_t         last_get_bus_info_type;
    int                          set_map_query_call_count;
    int                          clear_map_query_call_count;
    int                          set_in_map_query_call_count;
    int                          set_out_map_query_call_count;
    int                          clear_in_map_query_call_count;
    int                          clear_out_map_query_call_count;
    int                          enable_call_count;
    esp_codec_dev_type_t         last_map_query_type;
    esp_codec_dev_type_t         last_enable_type;
    esp_codec_dev_type_t         last_set_fmt_type;
    bool                         last_enable_state;
    bool                         map_query_registered_during_set_fmt;
    int                          map_query_set_seq;
    int                          map_query_clear_seq;
    int                          set_fmt_seq;
    int                          enable_true_seq;
    int                          enable_false_seq;
    int                          op_seq;
    int                          enable_true_ret;
    int                          read_count;
    int                          last_read_size;
    int                          write_count;
    int                          last_write_size;
    uint8_t                      read_data[32];
    int                          read_data_size;
    uint8_t                      last_write_data[32];
    bool                         opened;
} test_layout_data_if_t;

static int s_test_layout_op_seq;

static int test_layout_next_seq(void)
{
    s_test_layout_op_seq++;
    return s_test_layout_op_seq;
}

static esp_codec_dev_map_query_t *test_layout_select_map_query(test_layout_data_if_t *data_if,
                                                               esp_codec_dev_type_t dev_type)
{
    if (dev_type == ESP_CODEC_DEV_TYPE_IN) {
        return &data_if->in_map_query;
    }
    if (dev_type == ESP_CODEC_DEV_TYPE_OUT) {
        return &data_if->out_map_query;
    }
    return NULL;
}

static esp_codec_dev_bus_info_t *test_layout_select_bus(test_layout_data_if_t *data_if, esp_codec_dev_type_t dev_type)
{
    if (dev_type == ESP_CODEC_DEV_TYPE_IN) {
        return &data_if->in_bus;
    }
    if (dev_type == ESP_CODEC_DEV_TYPE_OUT) {
        return &data_if->out_bus;
    }
    return NULL;
}

static esp_codec_dev_bus_info_t test_layout_make_bus(esp_codec_dev_i2s_mode_t mode, uint32_t sample_rate,
                                                     int mclk_multiple, uint8_t total_slot, uint8_t slot_bit,
                                                     uint8_t data_bit, uint16_t slot_mask)
{
    esp_codec_dev_bus_info_t bus = {
        .mode = mode,
        .sample_rate = sample_rate,
        .mclk_multiple = mclk_multiple,
        .total_slot = total_slot,
        .slot_bit = slot_bit,
        .data_bit = data_bit,
        .slot_mask = slot_mask,
        .total_frame_bits = (uint16_t)(total_slot * slot_bit),
    };
    return bus;
}

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
    test_layout_codec_if_t *codec = (test_layout_codec_if_t *)h;
    if (codec == NULL || fs == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    codec->set_fs_call_count++;
    if (codec->set_fs_call_count <= (int)(sizeof(codec->fs_history) / sizeof(codec->fs_history[0]))) {
        int idx = codec->set_fs_call_count - 1;
        codec->fs_history[idx] = *fs;
        codec->fs_type_history[idx] = type;
        codec->set_fs_seq[idx] = test_layout_next_seq();
    }
    codec->last_fs = *fs;
    codec->last_fs_type = type;
    if (codec->set_fs_ret != ESP_CODEC_DEV_OK &&
        (codec->set_fs_fail_on_call == 0 || codec->set_fs_call_count == codec->set_fs_fail_on_call)) {
        return codec->set_fs_ret;
    }
    return ESP_CODEC_DEV_OK;
}

static int test_layout_codec_adc_enable(const audio_codec_if_t *h, bool enable)
{
    test_layout_codec_if_t *codec = (test_layout_codec_if_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    codec->adc_enabled = enable;
    if (enable) {
        codec->adc_enable_true_count++;
    } else {
        codec->adc_enable_false_count++;
    }
    return ESP_CODEC_DEV_OK;
}

static int test_layout_codec_set_sysclk(const audio_hw_base_t *h, const esp_codec_dev_sys_clk_info_t *clk_info)
{
    test_layout_codec_if_t *codec = (test_layout_codec_if_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    codec->set_sysclk_call_count++;
    int idx = codec->set_sysclk_call_count - 1;
    if (idx < TEST_LAYOUT_SYSCLK_HIST) {
        codec->set_sysclk_seq[idx] = test_layout_next_seq();
        codec->clk_info_null_history[idx] = (clk_info == NULL);
        if (clk_info) {
            codec->clk_info_history[idx] = *clk_info;
        } else {
            memset(&codec->clk_info_history[idx], 0, sizeof(codec->clk_info_history[idx]));
        }
    }
    codec->last_clk_info_is_null = (clk_info == NULL);
    if (clk_info) {
        codec->last_clk_info = *clk_info;
        codec->set_sysclk_apply_count++;
        return codec->set_sysclk_ret;
    }
    memset(&codec->last_clk_info, 0, sizeof(codec->last_clk_info));
    return ESP_CODEC_DEV_OK;
}

static int test_layout_codec_get_order_list(const audio_hw_base_t *h, const esp_codec_dev_device_map_info_t **order_list,
                                            int *list_size)
{
    test_layout_codec_if_t *codec = (test_layout_codec_if_t *)h;
    if (codec == NULL || order_list == NULL || list_size == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    codec->get_order_list_call_count++;
    *order_list = codec->order_list;
    *list_size = codec->order_list_size;
    return ESP_CODEC_DEV_OK;
}

static int test_layout_codec_get_adc_label(const audio_hw_base_t *h, const char **label)
{
    test_layout_codec_if_t *codec = (test_layout_codec_if_t *)h;
    if (codec == NULL || label == NULL || codec->adc_label[0] == '\0') {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *label = codec->adc_label;
    return ESP_CODEC_DEV_OK;
}

static int test_layout_codec_set_adc_label(const audio_hw_base_t *h, const char *label)
{
    test_layout_codec_if_t *codec = (test_layout_codec_if_t *)h;
    if (codec == NULL || label == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    codec->set_adc_label_call_count++;
    if (codec->set_adc_label_ret != ESP_CODEC_DEV_OK) {
        return codec->set_adc_label_ret;
    }
    if (codec->set_adc_label_copy) {
        size_t label_len = strlen(label);
        if (label_len >= sizeof(codec->adc_label)) {
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        memcpy(codec->adc_label, label, label_len + 1);
    }
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
    test_layout_data_if_t *data_if = (test_layout_data_if_t *)h;
    if (data_if == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    data_if->enable_call_count++;
    data_if->last_enable_type = dev_type;
    data_if->last_enable_state = enable;
    int seq = test_layout_next_seq();
    data_if->op_seq = seq;
    if (enable) {
        data_if->enable_true_seq = seq;
        if (data_if->enable_true_ret != ESP_CODEC_DEV_OK) {
            return data_if->enable_true_ret;
        }
    } else {
        data_if->enable_false_seq = seq;
    }
    return ESP_CODEC_DEV_OK;
}

static int test_layout_data_set_fmt(const audio_codec_data_if_t *h, esp_codec_dev_type_t dev_type,
                                    esp_codec_dev_sample_info_t *fs)
{
    test_layout_data_if_t *data_if = (test_layout_data_if_t *)h;
    if (data_if == NULL || fs == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    data_if->set_fmt_call_count++;
    data_if->last_set_fmt_type = dev_type;
    data_if->last_set_fmt_fs = *fs;
    data_if->set_fmt_seq = test_layout_next_seq();
    data_if->op_seq = data_if->set_fmt_seq;
    if (dev_type == ESP_CODEC_DEV_TYPE_IN_OUT) {
        /* An IN_OUT handle may run TDM on one direction only, so a single live map query still proves
           that registration happened before set_fmt. */
        data_if->map_query_registered_during_set_fmt =
            data_if->in_map_query.resolve_cb != NULL || data_if->out_map_query.resolve_cb != NULL;
    } else {
        esp_codec_dev_map_query_t *query = test_layout_select_map_query(data_if, dev_type);
        data_if->map_query_registered_during_set_fmt = query && query->resolve_cb != NULL;
    }
    if (data_if->set_fmt_ret != ESP_CODEC_DEV_OK &&
        (data_if->set_fmt_fail_on_call == 0 ||
         data_if->set_fmt_call_count == data_if->set_fmt_fail_on_call)) {
        if (dev_type & ESP_CODEC_DEV_TYPE_IN) {
            memset(&data_if->in_bus, 0, sizeof(data_if->in_bus));
        }
        if (dev_type & ESP_CODEC_DEV_TYPE_OUT) {
            memset(&data_if->out_bus, 0, sizeof(data_if->out_bus));
        }
        return data_if->set_fmt_ret;
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

static int test_layout_data_set_map_query(const audio_codec_data_if_t *h, esp_codec_dev_type_t dev_type,
                                          const esp_codec_dev_map_query_t *query)
{
    test_layout_data_if_t *data_if = (test_layout_data_if_t *)h;
    esp_codec_dev_map_query_t *stored_query =
        data_if ? test_layout_select_map_query(data_if, dev_type) : NULL;
    if (stored_query == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    data_if->last_map_query_type = dev_type;
    data_if->op_seq = test_layout_next_seq();
    if (query != NULL) {
        *stored_query = *query;
        data_if->set_map_query_call_count++;
        data_if->map_query_set_seq = data_if->op_seq;
        if (dev_type == ESP_CODEC_DEV_TYPE_IN) {
            data_if->set_in_map_query_call_count++;
        } else if (dev_type == ESP_CODEC_DEV_TYPE_OUT) {
            data_if->set_out_map_query_call_count++;
        }
    } else {
        memset(stored_query, 0, sizeof(*stored_query));
        data_if->clear_map_query_call_count++;
        data_if->map_query_clear_seq = data_if->op_seq;
        if (dev_type == ESP_CODEC_DEV_TYPE_IN) {
            data_if->clear_in_map_query_call_count++;
        } else if (dev_type == ESP_CODEC_DEV_TYPE_OUT) {
            data_if->clear_out_map_query_call_count++;
        }
    }
    return ESP_CODEC_DEV_OK;
}

static int test_layout_data_get_bus_info(const audio_codec_data_if_t *h, esp_codec_dev_type_t dev_type,
                                         esp_codec_dev_bus_info_t *bus_info)
{
    test_layout_data_if_t *data_if = (test_layout_data_if_t *)h;
    esp_codec_dev_bus_info_t *stored_bus = data_if ? test_layout_select_bus(data_if, dev_type) : NULL;
    if (stored_bus == NULL || bus_info == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    data_if->get_bus_info_call_count++;
    data_if->last_get_bus_info_type = dev_type;
    if (data_if->get_bus_info_ret != ESP_CODEC_DEV_OK) {
        return data_if->get_bus_info_ret;
    }
    if (stored_bus->total_slot == 0) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    *bus_info = *stored_bus;
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
    if (size > (int)sizeof(data_if->read_data)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (data_if->read_data_size == size) {
        memcpy(data, data_if->read_data, size);
    } else {
        memset(data, 0, size);
    }
    return ESP_CODEC_DEV_OK;
}

static int test_layout_data_write(const audio_codec_data_if_t *h, uint8_t *data, int size)
{
    test_layout_data_if_t *data_if = (test_layout_data_if_t *)h;
    if (data_if == NULL || data == NULL || size < 0 || size > (int)sizeof(data_if->last_write_data)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    data_if->write_count++;
    data_if->last_write_size = size;
    memcpy(data_if->last_write_data, data, size);
    return ESP_CODEC_DEV_OK;
}

static void test_layout_init_codec(test_layout_codec_if_t *codec, const esp_codec_dev_device_map_info_t *order_list,
                                   int order_list_size, const char *adc_label)
{
    memset(codec, 0, sizeof(*codec));
    codec->order_list = order_list;
    codec->order_list_size = order_list_size;
    if (adc_label != NULL) {
        strncpy(codec->adc_label, adc_label, sizeof(codec->adc_label) - 1);
        codec->adc_label[sizeof(codec->adc_label) - 1] = '\0';
    }
    codec->set_adc_label_copy = true;
    codec->opened = true;
    codec->base.hw_base.open = test_layout_codec_open;
    codec->base.hw_base.is_open = test_layout_codec_is_open;
    codec->base.hw_base.set_fs = test_layout_codec_set_fs;
    codec->base.hw_base.set_sysclk = test_layout_codec_set_sysclk;
    codec->base.hw_base.get_order_list = test_layout_codec_get_order_list;
    codec->base.hw_base.get_adc_label = test_layout_codec_get_adc_label;
    codec->base.hw_base.set_adc_label = test_layout_codec_set_adc_label;
    codec->adc_if.ops.enable = test_layout_codec_adc_enable;
}

static void test_layout_enable_codec_order_rows(test_layout_codec_if_t *codec)
{
    codec->map_query_order_list[0] = (esp_codec_dev_device_map_info_t) {
        ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2)}};
    codec->map_query_order_list[1] = (esp_codec_dev_device_map_info_t) {
        ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 4, {.value = ESP_CODEC_DEV_CHANNEL_MAP_4CH(1, 3, 2, 4)}};
    codec->map_query_order_list[2] = (esp_codec_dev_device_map_info_t) {
        ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 6, {.value = ESP_CODEC_DEV_CHANNEL_MAP_6CH(1, 3, 5, 2, 4, 6)}};
    codec->map_query_order_list[3] = (esp_codec_dev_device_map_info_t) {
        ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 8, {.value = ESP_CODEC_DEV_CHANNEL_MAP_8CH(1, 3, 5, 7, 2, 4, 6, 8)}};
    codec->order_list = codec->map_query_order_list;
    codec->order_list_size = sizeof(codec->map_query_order_list) / sizeof(codec->map_query_order_list[0]);
}

static void test_layout_init_data(test_layout_data_if_t *data_if, const esp_codec_dev_sample_info_t *fs)
{
    memset(data_if, 0, sizeof(*data_if));
    s_test_layout_op_seq = 0;
    data_if->fs = *fs;
    data_if->in_mode = ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS;
    data_if->out_mode = ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS;
    data_if->in_bus = test_layout_make_bus(ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, fs->sample_rate, fs->mclk_multiple,
                                           fs->channel, fs->bits_per_sample, fs->bits_per_sample, fs->channel_mask);
    data_if->out_bus = test_layout_make_bus(ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS, fs->sample_rate, fs->mclk_multiple,
                                            2, fs->bits_per_sample, fs->bits_per_sample, BIT(0) | BIT(1));
    data_if->enable_true_ret = ESP_CODEC_DEV_OK;
    data_if->set_fmt_ret = ESP_CODEC_DEV_OK;
    data_if->get_bus_info_ret = ESP_CODEC_DEV_OK;
    data_if->opened = true;
    data_if->base.open = test_layout_data_open;
    data_if->base.is_open = test_layout_data_is_open;
    data_if->base.get_mode = test_layout_data_get_mode;
    data_if->base.get_fmt = test_layout_data_get_fmt;
    data_if->base.enable = test_layout_data_enable;
    data_if->base.set_fmt = test_layout_data_set_fmt;
    data_if->base.read = test_layout_data_read;
    data_if->base.write = test_layout_data_write;
    data_if->base.set_map_query = test_layout_data_set_map_query;
    data_if->base.get_bus_info = test_layout_data_get_bus_info;
}

static void test_layout_label_rejects_output_only_device(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2)}},
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
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);

    char label[16] = {0};
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_NOT_SUPPORT, esp_codec_dev_get_data_layout_label(dev, label, sizeof(label)));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_NOT_SUPPORT, esp_codec_dev_set_data_layout_label(dev, "FR,FL"));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_NOT_SUPPORT, esp_codec_dev_set_adc_label(dev, "FL,FR"));

    esp_codec_dev_delete(dev);
}

static void test_layout_query_without_data_compute_hooks(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2)}},
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
    TEST_ESP_OK(esp_codec_dev_get_data_layout(dev, &map));
    TEST_ASSERT_EQUAL_UINT32(ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2), map.value);

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
    test_layout_init_data(&data_if, &fs);

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
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 4, {.value = ESP_CODEC_DEV_CHANNEL_MAP_4CH(1, 3, 2, 4)}},
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
    test_layout_init_data(&data_if, &fs);

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
        .value = ESP_CODEC_DEV_CHANNEL_MAP_2CH(3, 1),
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

static void test_layout_query_uses_computed_data_order(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 4, {.value = ESP_CODEC_DEV_CHANNEL_MAP_4CH(1, 3, 2, 4)}},
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
    test_layout_init_data(&data_if, &fs);

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
    TEST_ASSERT_EQUAL_UINT32(ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2), map.value);

    TEST_ESP_OK(esp_codec_dev_close(dev));
    esp_codec_dev_delete(dev);
}

static void test_layout_set_uses_computed_data_mask(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 4, {.value = ESP_CODEC_DEV_CHANNEL_MAP_4CH(1, 3, 2, 4)}},
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
    test_layout_init_data(&data_if, &fs);

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
        .value = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 3),
    };
    TEST_ESP_OK(esp_codec_dev_set_data_layout(dev, &map));
    TEST_ASSERT_EQUAL_UINT8(4, data_if.fs.channel);
    TEST_ASSERT_EQUAL_UINT16(0x03, data_if.fs.channel_mask);

    TEST_ESP_OK(esp_codec_dev_close(dev));
    esp_codec_dev_delete(dev);
}

static void test_layout_open_registers_map_query_and_uses_actual_bus_layout(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 4, {.value = ESP_CODEC_DEV_CHANNEL_MAP_4CH(1, 3, 2, 4)}},
    };
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 4,
        .bits_per_sample = 16,
        .channel_mask = 0x03,
        .mclk_multiple = 256,
    };
    test_layout_codec_if_t codec = {0};
    test_layout_data_if_t data_if = {0};
    test_layout_init_codec(&codec, codec_orders, sizeof(codec_orders) / sizeof(codec_orders[0]), "CH1,CH2,CH3,CH4");
    test_layout_enable_codec_order_rows(&codec);
    test_layout_init_data(&data_if, &fs);
    data_if.in_mode = ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS;
    data_if.in_bus = test_layout_make_bus(ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 16000, 256, 6, 16, 16, 0x03);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);

    TEST_ESP_OK(esp_codec_dev_open(dev, &fs));
    TEST_ASSERT_EQUAL_INT(1, data_if.set_map_query_call_count);
    TEST_ASSERT_TRUE(data_if.map_query_set_seq > 0);
    TEST_ASSERT_TRUE(data_if.map_query_set_seq < data_if.set_fmt_seq);
    TEST_ASSERT_TRUE(data_if.map_query_registered_during_set_fmt);
    TEST_ASSERT_TRUE(data_if.enable_true_seq > 0);
    TEST_ASSERT_TRUE(data_if.set_fmt_seq < data_if.enable_true_seq);
    TEST_ASSERT_EQUAL_INT(1, codec.set_fs_call_count);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_TYPE_IN, codec.last_fs_type);
    TEST_ASSERT_EQUAL_UINT8(4, codec.last_fs.channel);
    TEST_ASSERT_EQUAL_UINT16(0x03, codec.last_fs.channel_mask);
    TEST_ASSERT_TRUE(codec.set_fs_seq[0] > 0);
    TEST_ASSERT_TRUE(data_if.enable_true_seq < codec.set_fs_seq[0]);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_TYPE_IN, data_if.last_set_fmt_type);
    TEST_ASSERT_EQUAL_UINT8(4, data_if.last_set_fmt_fs.channel);
    TEST_ASSERT_EQUAL_UINT16(0x03, data_if.last_set_fmt_fs.channel_mask);
    TEST_ASSERT_EQUAL_UINT8(4, fs.channel);
    TEST_ASSERT_EQUAL_UINT16(0x03, fs.channel_mask);

    esp_codec_dev_channel_map_t map = {0};
    TEST_ESP_OK(esp_codec_dev_get_data_layout(dev, &map));
    TEST_ASSERT_EQUAL_UINT32(ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 3), map.value);

    char label[32] = {0};
    TEST_ESP_OK(esp_codec_dev_get_data_layout_label(dev, label, sizeof(label)));
    TEST_ASSERT_EQUAL_STRING("CH1,CH3", label);

    TEST_ESP_OK(esp_codec_dev_close(dev));
    TEST_ASSERT_EQUAL_INT(1, data_if.clear_map_query_call_count);
    TEST_ASSERT_TRUE(data_if.enable_false_seq > 0);
    TEST_ASSERT_TRUE(data_if.enable_false_seq < data_if.map_query_clear_seq);
    TEST_ASSERT_NULL(data_if.in_map_query.resolve_cb);
    esp_codec_dev_delete(dev);
}

static void test_layout_open_keeps_mono_app_fs(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2)}},
    };
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 1,
        .bits_per_sample = 16,
        .channel_mask = BIT(0),
        .mclk_multiple = 256,
    };
    test_layout_codec_if_t codec = {0};
    test_layout_data_if_t data_if = {0};
    test_layout_init_codec(&codec, codec_orders, sizeof(codec_orders) / sizeof(codec_orders[0]), "CH1");
    test_layout_enable_codec_order_rows(&codec);
    test_layout_init_data(&data_if, &fs);
    data_if.in_mode = ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS;
    data_if.in_bus = test_layout_make_bus(ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 16000, 256, 6, 16, 16, BIT(0));

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);

    TEST_ESP_OK(esp_codec_dev_open(dev, &fs));
    TEST_ASSERT_EQUAL_INT(1, codec.set_fs_call_count);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_TYPE_IN, codec.last_fs_type);
    TEST_ASSERT_EQUAL_UINT8(1, codec.last_fs.channel);
    TEST_ASSERT_EQUAL_UINT16(BIT(0), codec.last_fs.channel_mask);
    TEST_ASSERT_EQUAL_UINT8(1, data_if.last_set_fmt_fs.channel);
    TEST_ASSERT_EQUAL_UINT16(BIT(0), data_if.last_set_fmt_fs.channel_mask);
    TEST_ASSERT_EQUAL_UINT8(1, fs.channel);
    TEST_ASSERT_EQUAL_UINT16(BIT(0), fs.channel_mask);

    esp_codec_dev_channel_map_t map = {0};
    TEST_ESP_OK(esp_codec_dev_get_data_layout(dev, &map));
    TEST_ASSERT_EQUAL_UINT32(ESP_CODEC_DEV_CHANNEL_MAP_1CH(1), map.value);

    TEST_ESP_OK(esp_codec_dev_close(dev));
    esp_codec_dev_delete(dev);
}

static void test_layout_dynamic_query_tracks_peer_late_bus_widening(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2)}},
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 4, {.value = ESP_CODEC_DEV_CHANNEL_MAP_4CH(1, 3, 2, 4)}},
    };
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = 0x03,
        .mclk_multiple = 256,
    };
    test_layout_codec_if_t codec = {0};
    test_layout_data_if_t data_if = {0};
    test_layout_init_codec(&codec, codec_orders, sizeof(codec_orders) / sizeof(codec_orders[0]), "CH1,CH2,CH3,CH4");
    test_layout_enable_codec_order_rows(&codec);
    test_layout_init_data(&data_if, &fs);
    data_if.in_mode = ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS;
    data_if.in_bus = test_layout_make_bus(ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 16000, 256, 2, 16, 16, 0x03);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ESP_OK(esp_codec_dev_open(dev, &fs));

    int bus_info_calls = data_if.get_bus_info_call_count;
    int order_list_calls = codec.get_order_list_call_count;
    esp_codec_dev_channel_map_t map = {0};
    TEST_ESP_OK(esp_codec_dev_get_data_layout(dev, &map));
    TEST_ASSERT_EQUAL_UINT32(ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2), map.value);
    TEST_ASSERT_TRUE(data_if.get_bus_info_call_count > bus_info_calls);
    TEST_ASSERT_EQUAL_INT(order_list_calls, codec.get_order_list_call_count);

    bus_info_calls = data_if.get_bus_info_call_count;
    order_list_calls = codec.get_order_list_call_count;
    data_if.in_bus = test_layout_make_bus(ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 16000, 256, 4, 16, 16, 0x05);
    TEST_ESP_OK(esp_codec_dev_get_data_layout(dev, &map));
    TEST_ASSERT_EQUAL_UINT32(ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2), map.value);
    TEST_ASSERT_TRUE(data_if.get_bus_info_call_count > bus_info_calls);
    TEST_ASSERT_EQUAL_INT(order_list_calls, codec.get_order_list_call_count);

    bus_info_calls = data_if.get_bus_info_call_count;
    order_list_calls = codec.get_order_list_call_count;
    data_if.in_bus = test_layout_make_bus(ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 16000, 256, 6, 16, 16, 0x09);
    TEST_ESP_OK(esp_codec_dev_get_data_layout(dev, &map));
    TEST_ASSERT_EQUAL_UINT32(ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2), map.value);
    TEST_ASSERT_TRUE(data_if.get_bus_info_call_count > bus_info_calls);
    TEST_ASSERT_EQUAL_INT(order_list_calls, codec.get_order_list_call_count);

    TEST_ESP_OK(esp_codec_dev_close(dev));
    esp_codec_dev_delete(dev);
}

static void test_layout_dynamic_query_reports_selected_channel_from_widened_frame(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 4, {.value = ESP_CODEC_DEV_CHANNEL_MAP_4CH(1, 3, 2, 4)}},
    };
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 4,
        .bits_per_sample = 16,
        .channel_mask = 0x03,
        .mclk_multiple = 256,
    };
    test_layout_codec_if_t codec = {0};
    test_layout_data_if_t data_if = {0};
    test_layout_init_codec(&codec, codec_orders, sizeof(codec_orders) / sizeof(codec_orders[0]), "CH1,CH2,CH3,CH4");
    test_layout_enable_codec_order_rows(&codec);
    test_layout_init_data(&data_if, &fs);
    data_if.in_mode = ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS;
    data_if.in_bus = test_layout_make_bus(ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 16000, 256, 6, 16, 16, 0x03);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);

    TEST_ESP_OK(esp_codec_dev_open(dev, &fs));
    data_if.in_bus = test_layout_make_bus(ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 16000, 256, 6, 16, 16, 0x04);

    esp_codec_dev_channel_map_t map = {0};
    TEST_ESP_OK(esp_codec_dev_get_data_layout(dev, &map));
    TEST_ASSERT_EQUAL_UINT32(ESP_CODEC_DEV_CHANNEL_MAP_1CH(5), map.value);

    TEST_ESP_OK(esp_codec_dev_close(dev));
    esp_codec_dev_delete(dev);
}

static void test_layout_open_with_in_out_distinct_logical_masks_uses_app_codec_fs(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 4, {.value = ESP_CODEC_DEV_CHANNEL_MAP_4CH(1, 3, 2, 4)}},
    };
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 4,
        .bits_per_sample = 16,
        .channel_mask = 0x03,
        .mclk_multiple = 256,
    };
    test_layout_codec_if_t codec = {0};
    test_layout_data_if_t data_if = {0};
    test_layout_init_codec(&codec, codec_orders, sizeof(codec_orders) / sizeof(codec_orders[0]), "CH1,CH2,CH3,CH4");
    test_layout_enable_codec_order_rows(&codec);
    test_layout_init_data(&data_if, &fs);
    /* Only the input runs TDM, so the output keeps the default STD bus and stays without a map query.
       Codec set_fs still receives the application request once for the combined handle. */
    data_if.in_mode = ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS;
    data_if.in_bus = test_layout_make_bus(ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 16000, 256, 4, 16, 16, 0x03);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);

    TEST_ESP_OK(esp_codec_dev_open(dev, &fs));
    TEST_ASSERT_EQUAL_INT(1, data_if.set_map_query_call_count);
    TEST_ASSERT_EQUAL_INT(1, data_if.set_in_map_query_call_count);
    TEST_ASSERT_EQUAL_INT(0, data_if.set_out_map_query_call_count);
    TEST_ASSERT_TRUE(data_if.map_query_set_seq < data_if.set_fmt_seq);
    TEST_ASSERT_TRUE(data_if.map_query_registered_during_set_fmt);
    TEST_ASSERT_EQUAL_INT(1, codec.set_fs_call_count);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_TYPE_IN_OUT, codec.last_fs_type);
    TEST_ASSERT_EQUAL_UINT16(0x03, codec.last_fs.channel_mask);
    TEST_ASSERT_TRUE(data_if.set_fmt_seq < data_if.enable_true_seq);
    TEST_ASSERT_TRUE(data_if.enable_true_seq < codec.set_fs_seq[0]);

    TEST_ESP_OK(esp_codec_dev_close(dev));
    TEST_ASSERT_EQUAL_INT(1, data_if.clear_map_query_call_count);
    TEST_ASSERT_EQUAL_INT(1, data_if.clear_in_map_query_call_count);
    TEST_ASSERT_EQUAL_INT(0, data_if.clear_out_map_query_call_count);
    esp_codec_dev_delete(dev);
}

static void test_layout_open_with_in_out_compatible_logical_masks_uses_combined_codec_fs(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 4, {.value = ESP_CODEC_DEV_CHANNEL_MAP_4CH(1, 3, 2, 4)}},
    };
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 4,
        .bits_per_sample = 16,
        .channel_mask = 0x01,
        .mclk_multiple = 256,
    };
    test_layout_codec_if_t codec = {0};
    test_layout_data_if_t data_if = {0};
    test_layout_init_codec(&codec, codec_orders, sizeof(codec_orders) / sizeof(codec_orders[0]), "CH1,CH2,CH3,CH4");
    test_layout_enable_codec_order_rows(&codec);
    test_layout_init_data(&data_if, &fs);
    data_if.in_mode = ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS;
    data_if.out_mode = ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS;
    data_if.in_bus = test_layout_make_bus(ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 16000, 256, 4, 16, 16, 0x01);
    data_if.out_bus = test_layout_make_bus(ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 16000, 256, 4, 16, 16, 0x01);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);

    TEST_ESP_OK(esp_codec_dev_open(dev, &fs));
    TEST_ASSERT_EQUAL_INT(2, data_if.set_map_query_call_count);
    TEST_ASSERT_EQUAL_INT(1, codec.set_fs_call_count);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_TYPE_IN_OUT, codec.last_fs_type);
    TEST_ASSERT_EQUAL_UINT16(0x01, codec.last_fs.channel_mask);
    TEST_ASSERT_TRUE(data_if.set_fmt_seq < data_if.enable_true_seq);
    TEST_ASSERT_TRUE(data_if.enable_true_seq < codec.set_fs_seq[0]);

    TEST_ESP_OK(esp_codec_dev_close(dev));
    esp_codec_dev_delete(dev);
}

static void test_layout_in_out_rejects_direction_dependent_layout(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 4, {.value = ESP_CODEC_DEV_CHANNEL_MAP_4CH(1, 3, 2, 4)}},
    };
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 4,
        .bits_per_sample = 16,
        .channel_mask = 0x03,
        .mclk_multiple = 256,
    };
    test_layout_codec_if_t codec = {0};
    test_layout_data_if_t data_if = {0};
    test_layout_init_codec(&codec, codec_orders, sizeof(codec_orders) / sizeof(codec_orders[0]), "CH1,CH2,CH3,CH4");
    test_layout_enable_codec_order_rows(&codec);
    test_layout_init_data(&data_if, &fs);
    data_if.in_mode = ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS;
    data_if.out_mode = ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS;
    data_if.in_bus = test_layout_make_bus(ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 16000, 256, 4, 16, 16, 0x03);
    data_if.out_bus = test_layout_make_bus(ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 16000, 256, 4, 16, 16, 0x05);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);

    /* Input reads mask 0x03 as physical slots and lands on CH1,CH3 while output reads the same mask as
       logical channels CH1,CH2. A handle that shares one sample format cannot carry both layouts, so
       the open must be refused before any bus or codec register is touched. */
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_NOT_SUPPORT, esp_codec_dev_open(dev, &fs));
    TEST_ASSERT_EQUAL_INT(0, data_if.set_fmt_call_count);
    TEST_ASSERT_EQUAL_INT(0, codec.set_fs_call_count);
    TEST_ASSERT_EQUAL_INT(2, data_if.clear_map_query_call_count);
    TEST_ASSERT_NULL(data_if.in_map_query.resolve_cb);
    TEST_ASSERT_NULL(data_if.out_map_query.resolve_cb);

    esp_codec_dev_delete(dev);
}

static void test_layout_codec_set_fs_failure_happens_after_bus_is_configured(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 4, {.value = ESP_CODEC_DEV_CHANNEL_MAP_4CH(1, 3, 2, 4)}},
    };
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 4,
        .bits_per_sample = 16,
        .channel_mask = 0x03,
        .mclk_multiple = 256,
    };
    test_layout_codec_if_t codec = {0};
    test_layout_data_if_t data_if = {0};
    test_layout_init_codec(&codec, codec_orders, sizeof(codec_orders) / sizeof(codec_orders[0]), "CH1,CH2,CH3,CH4");
    test_layout_enable_codec_order_rows(&codec);
    codec.set_fs_ret = ESP_CODEC_DEV_NOT_SUPPORT;
    test_layout_init_data(&data_if, &fs);
    data_if.in_mode = ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS;
    data_if.in_bus = test_layout_make_bus(ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 16000, 256, 6, 16, 16, 0x03);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_NOT_SUPPORT, esp_codec_dev_open(dev, &fs));
    TEST_ASSERT_EQUAL_INT(1, codec.set_fs_call_count);
    TEST_ASSERT_EQUAL_INT(2, codec.set_sysclk_call_count);
    TEST_ASSERT_EQUAL_INT(1, codec.set_sysclk_apply_count);
    TEST_ASSERT_TRUE(codec.clk_info_null_history[0]);
    TEST_ASSERT_FALSE(codec.clk_info_null_history[1]);
    TEST_ASSERT_EQUAL_INT(1, data_if.set_fmt_call_count);
    TEST_ASSERT_TRUE(data_if.set_fmt_seq > 0);
    TEST_ASSERT_TRUE(data_if.enable_true_seq > 0);
    TEST_ASSERT_TRUE(data_if.set_fmt_seq < data_if.enable_true_seq);
    TEST_ASSERT_TRUE(codec.set_sysclk_seq[0] > data_if.enable_true_seq);
    TEST_ASSERT_TRUE(codec.set_sysclk_seq[1] > codec.set_sysclk_seq[0]);
    TEST_ASSERT_TRUE(codec.set_fs_seq[0] > codec.set_sysclk_seq[1]);
    TEST_ASSERT_EQUAL_INT(1, data_if.clear_map_query_call_count);
    TEST_ASSERT_TRUE(data_if.enable_false_seq > 0);
    TEST_ASSERT_TRUE(data_if.enable_false_seq < data_if.map_query_clear_seq);
    TEST_ASSERT_NULL(data_if.in_map_query.resolve_cb);
    esp_codec_dev_delete(dev);
}

static void test_layout_open_converts_widened_bus_into_sysclk(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2)}},
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 6, {.value = ESP_CODEC_DEV_CHANNEL_MAP_6CH(1, 3, 5, 2, 4, 6)}},
    };
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = 0x03,
        .mclk_multiple = 256,
    };
    test_layout_codec_if_t codec = {0};
    test_layout_data_if_t data_if = {0};
    test_layout_init_codec(&codec, codec_orders, sizeof(codec_orders) / sizeof(codec_orders[0]), "CH1,CH2,CH3,CH4");
    test_layout_enable_codec_order_rows(&codec);
    test_layout_init_data(&data_if, &fs);
    data_if.in_mode = ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS;
    data_if.in_bus = test_layout_make_bus(ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 16000, 256, 6, 16, 16, 0x03);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ESP_OK(esp_codec_dev_open(dev, &fs));
    TEST_ASSERT_EQUAL_UINT8(2, codec.last_fs.channel);
    TEST_ASSERT_EQUAL_UINT16(0x03, codec.last_fs.channel_mask);
    TEST_ASSERT_FALSE(codec.last_clk_info_is_null);
    TEST_ASSERT_EQUAL_UINT32(16000, codec.last_clk_info.sample_rate);
    TEST_ASSERT_EQUAL_UINT32(4096000, codec.last_clk_info.mclk_hz);
    TEST_ASSERT_EQUAL_UINT32(1536000, codec.last_clk_info.bclk_hz);
    TEST_ASSERT_EQUAL_UINT8(6, codec.last_clk_info.total_slot);
    TEST_ESP_OK(esp_codec_dev_close(dev));
    esp_codec_dev_delete(dev);
}

static void test_layout_open_skips_sysclk_without_get_bus_info(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2)}},
    };
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = 0x03,
        .mclk_multiple = 256,
    };
    test_layout_codec_if_t codec = {0};
    test_layout_data_if_t data_if = {0};
    test_layout_init_codec(&codec, codec_orders, sizeof(codec_orders) / sizeof(codec_orders[0]), "CH1,CH2");
    test_layout_init_data(&data_if, &fs);
    data_if.in_mode = ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS;
    data_if.base.get_bus_info = NULL;

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ESP_OK(esp_codec_dev_open(dev, &fs));
    TEST_ASSERT_EQUAL_INT(0, codec.set_sysclk_apply_count);
    TEST_ASSERT_EQUAL_INT(1, codec.set_sysclk_call_count);
    TEST_ASSERT_TRUE(codec.last_clk_info_is_null);
    TEST_ASSERT_EQUAL_INT(1, codec.set_fs_call_count);
    TEST_ESP_OK(esp_codec_dev_close(dev));
    esp_codec_dev_delete(dev);
}

static void test_layout_open_skips_sysclk_on_wrong_state(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2)}},
    };
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = 0x03,
        .mclk_multiple = 256,
    };
    test_layout_codec_if_t codec = {0};
    test_layout_data_if_t data_if = {0};
    test_layout_init_codec(&codec, codec_orders, sizeof(codec_orders) / sizeof(codec_orders[0]), "CH1,CH2");
    test_layout_init_data(&data_if, &fs);
    data_if.in_mode = ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS;
    data_if.get_bus_info_ret = ESP_CODEC_DEV_WRONG_STATE;

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ESP_OK(esp_codec_dev_open(dev, &fs));
    TEST_ASSERT_EQUAL_INT(0, codec.set_sysclk_apply_count);
    TEST_ASSERT_TRUE(codec.last_clk_info_is_null);
    TEST_ASSERT_EQUAL_INT(1, codec.set_fs_call_count);
    TEST_ESP_OK(esp_codec_dev_close(dev));
    esp_codec_dev_delete(dev);
}

static void test_layout_open_skips_sysclk_when_callback_absent(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2)}},
    };
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = 0x03,
        .mclk_multiple = 256,
    };
    test_layout_codec_if_t codec = {0};
    test_layout_data_if_t data_if = {0};
    test_layout_init_codec(&codec, codec_orders, sizeof(codec_orders) / sizeof(codec_orders[0]), "CH1,CH2");
    test_layout_init_data(&data_if, &fs);
    data_if.in_mode = ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS;
    codec.base.hw_base.set_sysclk = NULL;

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ESP_OK(esp_codec_dev_open(dev, &fs));
    TEST_ASSERT_EQUAL_INT(0, codec.set_sysclk_call_count);
    TEST_ASSERT_EQUAL_INT(1, codec.set_fs_call_count);
    TEST_ESP_OK(esp_codec_dev_close(dev));
    esp_codec_dev_delete(dev);
}

static void test_layout_open_fails_when_get_bus_info_errors(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2)}},
    };
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = 0x03,
        .mclk_multiple = 256,
    };
    test_layout_codec_if_t codec = {0};
    test_layout_data_if_t data_if = {0};
    test_layout_init_codec(&codec, codec_orders, sizeof(codec_orders) / sizeof(codec_orders[0]), "CH1,CH2");
    test_layout_init_data(&data_if, &fs);
    data_if.in_mode = ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS;
    data_if.get_bus_info_ret = ESP_CODEC_DEV_DRV_ERR;

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_DRV_ERR, esp_codec_dev_open(dev, &fs));
    TEST_ASSERT_EQUAL_INT(0, codec.set_sysclk_apply_count);
    TEST_ASSERT_EQUAL_INT(0, codec.set_fs_call_count);
    esp_codec_dev_delete(dev);
}

static void test_layout_open_fails_when_set_sysclk_errors(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2)}},
    };
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = 0x03,
        .mclk_multiple = 256,
    };
    test_layout_codec_if_t codec = {0};
    test_layout_data_if_t data_if = {0};
    test_layout_init_codec(&codec, codec_orders, sizeof(codec_orders) / sizeof(codec_orders[0]), "CH1,CH2");
    test_layout_init_data(&data_if, &fs);
    data_if.in_mode = ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS;
    codec.set_sysclk_ret = ESP_CODEC_DEV_INVALID_ARG;

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, esp_codec_dev_open(dev, &fs));
    TEST_ASSERT_EQUAL_INT(0, codec.set_fs_call_count);
    TEST_ASSERT_TRUE(data_if.enable_false_seq > 0);
    esp_codec_dev_delete(dev);
}

static void test_layout_open_clears_sysclk_cache_when_bus_is_unavailable(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2)}},
    };
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = 0x03,
        .mclk_multiple = 256,
    };
    test_layout_codec_if_t codec = {0};
    test_layout_data_if_t data_if = {0};
    test_layout_init_codec(&codec, codec_orders, sizeof(codec_orders) / sizeof(codec_orders[0]), "CH1,CH2");
    test_layout_init_data(&data_if, &fs);
    data_if.in_mode = ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS;

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ESP_OK(esp_codec_dev_open(dev, &fs));
    TEST_ASSERT_EQUAL_INT(1, codec.set_sysclk_apply_count);
    TEST_ASSERT_FALSE(codec.last_clk_info_is_null);
    TEST_ESP_OK(esp_codec_dev_close(dev));

    data_if.get_bus_info_ret = ESP_CODEC_DEV_WRONG_STATE;
    TEST_ESP_OK(esp_codec_dev_open(dev, &fs));
    TEST_ASSERT_EQUAL_INT(1, codec.set_sysclk_apply_count);
    TEST_ASSERT_TRUE(codec.last_clk_info_is_null);
    TEST_ESP_OK(esp_codec_dev_close(dev));
    esp_codec_dev_delete(dev);
}

static void test_layout_rollback_keeps_codec_disabled_when_bus_restore_fails(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2)}},
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 4, {.value = ESP_CODEC_DEV_CHANNEL_MAP_4CH(1, 3, 2, 4)}},
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
    test_layout_init_codec(&codec, codec_orders, sizeof(codec_orders) / sizeof(codec_orders[0]),
                           "CH1,CH2,CH3,CH4");
    codec.base.adc_if = &codec.adc_if;
    test_layout_init_data(&data_if, &fs);
    data_if.in_bus = test_layout_make_bus(ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 16000, 256, 4, 32, 16, 0x0F);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ESP_OK(esp_codec_dev_open(dev, &fs));
    TEST_ASSERT_EQUAL_INT(1, codec.adc_enable_true_count);

    codec.set_fs_ret = ESP_CODEC_DEV_NOT_SUPPORT;
    codec.set_fs_fail_on_call = 2;
    data_if.set_fmt_ret = ESP_CODEC_DEV_DRV_ERR;
    data_if.set_fmt_fail_on_call = 3;
    esp_codec_dev_channel_map_t map = {
        .value = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2),
    };
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_NOT_SUPPORT, esp_codec_dev_set_data_layout(dev, &map));

    TEST_ASSERT_EQUAL_INT(3, data_if.set_fmt_call_count);
    TEST_ASSERT_EQUAL_INT(2, codec.set_fs_call_count);
    TEST_ASSERT_TRUE(codec.last_clk_info_is_null);
    TEST_ASSERT_EQUAL_INT(1, codec.adc_enable_true_count);
    TEST_ASSERT_FALSE(codec.adc_enabled);

    esp_codec_dev_delete(dev);
}

static void test_layout_rollback_restores_codec_from_committed_bus(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2)}},
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 4, {.value = ESP_CODEC_DEV_CHANNEL_MAP_4CH(1, 3, 2, 4)}},
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
    test_layout_init_codec(&codec, codec_orders, sizeof(codec_orders) / sizeof(codec_orders[0]),
                           "CH1,CH2,CH3,CH4");
    codec.base.adc_if = &codec.adc_if;
    test_layout_init_data(&data_if, &fs);
    data_if.in_bus = test_layout_make_bus(ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 16000, 256, 4, 32, 16, 0x0F);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ESP_OK(esp_codec_dev_open(dev, &fs));

    codec.set_fs_ret = ESP_CODEC_DEV_NOT_SUPPORT;
    codec.set_fs_fail_on_call = 2;
    esp_codec_dev_channel_map_t map = {
        .value = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2),
    };
    /* Hardware could express the layout but failed to apply it, so no software fallback happens. */
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_NOT_SUPPORT, esp_codec_dev_set_data_layout(dev, &map));

    TEST_ASSERT_EQUAL_INT(3, codec.set_fs_call_count);
    TEST_ASSERT_FALSE(codec.last_clk_info_is_null);
    TEST_ASSERT_EQUAL_UINT32(16000U * 4U * 32U, codec.last_clk_info.bclk_hz);
    TEST_ASSERT_EQUAL_INT(2, codec.adc_enable_true_count);
    TEST_ASSERT_TRUE(codec.adc_enabled);

    TEST_ESP_OK(esp_codec_dev_close(dev));
    esp_codec_dev_delete(dev);
}

static void test_layout_in_out_open_queries_output_bus_for_sysclk(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 4, {.value = ESP_CODEC_DEV_CHANNEL_MAP_4CH(1, 3, 2, 4)}},
    };
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 4,
        .bits_per_sample = 16,
        .channel_mask = 0x01,
        .mclk_multiple = 256,
    };
    test_layout_codec_if_t codec = {0};
    test_layout_data_if_t data_if = {0};
    test_layout_init_codec(&codec, codec_orders, sizeof(codec_orders) / sizeof(codec_orders[0]), "CH1,CH2,CH3,CH4");
    test_layout_enable_codec_order_rows(&codec);
    test_layout_init_data(&data_if, &fs);
    data_if.in_mode = ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS;
    data_if.out_mode = ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS;
    data_if.in_bus = test_layout_make_bus(ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 16000, 256, 4, 16, 16, 0x01);
    data_if.out_bus = test_layout_make_bus(ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 16000, 256, 4, 32, 16, 0x01);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ESP_OK(esp_codec_dev_open(dev, &fs));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_TYPE_OUT, data_if.last_get_bus_info_type);
    TEST_ASSERT_EQUAL_UINT8(4, codec.last_clk_info.total_slot);
    TEST_ASSERT_EQUAL_UINT32(2048000, codec.last_clk_info.bclk_hz);
    TEST_ESP_OK(esp_codec_dev_close(dev));
    esp_codec_dev_delete(dev);
}

static void test_layout_map_query_absent_and_std_path_keep_legacy_behavior(void)
{
    {
        esp_codec_dev_device_map_info_t codec_orders[] = {
            {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 4, {.value = ESP_CODEC_DEV_CHANNEL_MAP_4CH(1, 3, 2, 4)}},
        };
        esp_codec_dev_sample_info_t fs = {
            .sample_rate = 16000,
            .channel = 4,
            .bits_per_sample = 16,
            .channel_mask = 0x03,
            .mclk_multiple = 256,
        };
        test_layout_codec_if_t codec = {0};
        test_layout_data_if_t data_if = {0};
        test_layout_init_codec(&codec, codec_orders, sizeof(codec_orders) / sizeof(codec_orders[0]), "CH1,CH2,CH3,CH4");
        test_layout_init_data(&data_if, &fs);
        data_if.in_mode = ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS;
        data_if.in_bus = test_layout_make_bus(ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 16000, 256, 4, 16, 16, 0x03);

        esp_codec_dev_cfg_t dev_cfg = {
            .codec_if = &codec.base,
            .data_if = &data_if.base,
            .dev_type = ESP_CODEC_DEV_TYPE_IN,
        };
        esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
        TEST_ASSERT_NOT_NULL(dev);
        TEST_ESP_OK(esp_codec_dev_open(dev, &fs));
        TEST_ASSERT_EQUAL_INT(0, data_if.set_map_query_call_count);

        esp_codec_dev_channel_map_t map = {0};
        TEST_ESP_OK(esp_codec_dev_get_data_layout(dev, &map));
        TEST_ASSERT_EQUAL_UINT32(ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 3), map.value);
        TEST_ESP_OK(esp_codec_dev_close(dev));
        esp_codec_dev_delete(dev);
    }

    {
        esp_codec_dev_sample_info_t fs = {
            .sample_rate = 16000,
            .channel = 4,
            .bits_per_sample = 16,
            .channel_mask = 0x0F,
            .mclk_multiple = 256,
        };
        test_layout_codec_if_t codec = {0};
        test_layout_data_if_t data_if = {0};
        test_layout_init_codec(&codec, NULL, 0, "CH1,CH2,CH3,CH4");
        test_layout_enable_codec_order_rows(&codec);
        test_layout_init_data(&data_if, &fs);
        data_if.in_mode = ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS;
        data_if.in_bus = test_layout_make_bus(ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS, 16000, 256, 2, 32, 16, 0x03);

        esp_codec_dev_cfg_t dev_cfg = {
            .codec_if = &codec.base,
            .data_if = &data_if.base,
            .dev_type = ESP_CODEC_DEV_TYPE_IN,
        };
        esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
        TEST_ASSERT_NOT_NULL(dev);
        TEST_ESP_OK(esp_codec_dev_open(dev, &fs));
        TEST_ASSERT_EQUAL_INT(0, data_if.set_map_query_call_count);
        TEST_ASSERT_EQUAL_INT(0, codec.get_order_list_call_count);

        esp_codec_dev_channel_map_t map = {0};
        TEST_ESP_OK(esp_codec_dev_get_data_layout(dev, &map));
        TEST_ASSERT_EQUAL_UINT32(ESP_CODEC_DEV_CHANNEL_MAP_4CH(3, 1, 4, 2), map.value);
        TEST_ESP_OK(esp_codec_dev_close(dev));
        esp_codec_dev_delete(dev);
    }
}

static void test_layout_legacy_tdm_open_accepts_16_slot_without_map_query(void)
{
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 16,
        .bits_per_sample = 16,
        .channel_mask = 0xFFFF,
        .mclk_multiple = 256,
    };
    test_layout_codec_if_t codec = {0};
    test_layout_data_if_t data_if = {0};
    test_layout_init_codec(&codec, NULL, 0, NULL);
    codec.base.hw_base.get_order_list = NULL;
    test_layout_init_data(&data_if, &fs);
    data_if.in_mode = ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS;
    data_if.in_bus = test_layout_make_bus(ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 16000, 256, 16, 16, 16, 0xFFFF);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);

    TEST_ESP_OK(esp_codec_dev_open(dev, &fs));
    TEST_ASSERT_EQUAL_INT(0, data_if.set_map_query_call_count);
    TEST_ASSERT_EQUAL_INT(0, codec.get_order_list_call_count);
    TEST_ASSERT_EQUAL_INT(1, data_if.set_fmt_call_count);
    TEST_ASSERT_EQUAL_UINT8(16, data_if.last_set_fmt_fs.channel);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, data_if.last_set_fmt_fs.channel_mask);
    TEST_ASSERT_EQUAL_UINT8(16, codec.last_fs.channel);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, codec.last_fs.channel_mask);

    TEST_ESP_OK(esp_codec_dev_close(dev));
    esp_codec_dev_delete(dev);
}

static void test_set_adc_label_updates_copied_label_and_layout_translation(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2)}},
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

    TEST_ESP_OK(esp_codec_dev_set_adc_label(dev, "FR,FL"));
    TEST_ASSERT_EQUAL_INT(1, codec.set_adc_label_call_count);
    TEST_ASSERT_EQUAL_STRING("FR,FL", codec.adc_label);

    TEST_ESP_OK(esp_codec_dev_open(dev, &fs));
    TEST_ESP_OK(esp_codec_dev_set_data_layout_label(dev, "FL,FR"));

    char label[16] = {0};
    TEST_ESP_OK(esp_codec_dev_get_data_layout_label(dev, label, sizeof(label)));
    TEST_ASSERT_EQUAL_STRING("FL,FR", label);

    esp_codec_dev_channel_map_t map = {0};
    TEST_ESP_OK(esp_codec_dev_get_data_layout(dev, &map));
    TEST_ASSERT_EQUAL_UINT32(ESP_CODEC_DEV_CHANNEL_MAP_2CH(2, 1), map.value);

    TEST_ESP_OK(esp_codec_dev_close(dev));
    esp_codec_dev_delete(dev);
}

static void test_set_adc_label_rejects_open_input_and_invalid_args(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2)}},
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

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, esp_codec_dev_set_adc_label(NULL, "FL,FR"));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, esp_codec_dev_set_adc_label(dev, NULL));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, esp_codec_dev_set_adc_label(dev, ""));
    TEST_ASSERT_EQUAL_INT(0, codec.set_adc_label_call_count);

    TEST_ESP_OK(esp_codec_dev_open(dev, &fs));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_WRONG_STATE, esp_codec_dev_set_adc_label(dev, "FR,FL"));
    TEST_ASSERT_EQUAL_INT(0, codec.set_adc_label_call_count);
    TEST_ASSERT_EQUAL_STRING("FL,FR", codec.adc_label);

    TEST_ESP_OK(esp_codec_dev_close(dev));
    esp_codec_dev_delete(dev);
}

static void test_set_adc_label_missing_callback_and_driver_failure(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2)}},
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
    codec.base.hw_base.set_adc_label = NULL;

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = &codec.base,
        .data_if = &data_if.base,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_NOT_SUPPORT, esp_codec_dev_set_adc_label(dev, "FR,FL"));
    TEST_ASSERT_EQUAL_STRING("FL,FR", codec.adc_label);

    codec.base.hw_base.set_adc_label = test_layout_codec_set_adc_label;
    codec.set_adc_label_ret = ESP_CODEC_DEV_INVALID_ARG;
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, esp_codec_dev_set_adc_label(dev, "FR,FL"));
    TEST_ASSERT_EQUAL_INT(1, codec.set_adc_label_call_count);
    TEST_ASSERT_EQUAL_STRING("FL,FR", codec.adc_label);

    esp_codec_dev_delete(dev);
}

static void test_set_adc_label_clears_layout_maps_only_on_success(void)
{
    esp_codec_dev_device_map_info_t codec_orders[] = {
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2)}},
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

    codec.set_adc_label_ret = ESP_CODEC_DEV_INVALID_ARG;
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, esp_codec_dev_set_adc_label(dev, "FL,RE"));
    TEST_ASSERT_EQUAL_STRING("FL,FR", codec.adc_label);

    codec.set_adc_label_ret = ESP_CODEC_DEV_OK;
    TEST_ESP_OK(esp_codec_dev_set_adc_label(dev, "FL,RE"));
    TEST_ASSERT_EQUAL_STRING("FL,RE", codec.adc_label);

    TEST_ESP_OK(esp_codec_dev_open(dev, &fs));
    TEST_ESP_OK(esp_codec_dev_set_data_layout_label(dev, "RE,FL"));

    esp_codec_dev_channel_map_t map = {0};
    TEST_ESP_OK(esp_codec_dev_get_data_layout(dev, &map));
    TEST_ASSERT_EQUAL_UINT32(ESP_CODEC_DEV_CHANNEL_MAP_2CH(2, 1), map.value);

    char label[16] = {0};
    TEST_ESP_OK(esp_codec_dev_get_data_layout_label(dev, label, sizeof(label)));
    TEST_ASSERT_EQUAL_STRING("RE,FL", label);

    TEST_ESP_OK(esp_codec_dev_close(dev));
    esp_codec_dev_delete(dev);
}

TEST_CASE("layout label API rejects output only device", "[mock][layout]")
{
    test_layout_label_rejects_output_only_device();
}

TEST_CASE("layout query resolves without data interface compute hooks", "[mock][layout]")
{
    test_layout_query_without_data_compute_hooks();
}

TEST_CASE("layout label API returns not supported without codec interface", "[mock][layout]")
{
    test_layout_label_not_supported_without_codec_if();
}

TEST_CASE("layout conversion rejects incomplete user frame before reading bus data", "[mock][layout]")
{
    test_layout_conversion_rejects_incomplete_user_frame();
}

TEST_CASE("layout query uses computed data order without order table", "[mock][layout]")
{
    test_layout_query_uses_computed_data_order();
}

TEST_CASE("layout set uses computed data mask without order table", "[mock][layout]")
{
    test_layout_set_uses_computed_data_mask();
}

TEST_CASE("layout open registers map query and uses actual bus layout", "[mock][layout]")
{
    test_layout_open_registers_map_query_and_uses_actual_bus_layout();
}

TEST_CASE("layout open normalizes mono tdm without mutating app fs", "[mock][layout]")
{
    test_layout_open_keeps_mono_app_fs();
}

TEST_CASE("layout dynamically tracks peer-late bus widening", "[mock][layout]")
{
    test_layout_dynamic_query_tracks_peer_late_bus_widening();
}

TEST_CASE("layout dynamic query reports selected channel from widened frame", "[mock][layout]")
{
    test_layout_dynamic_query_reports_selected_channel_from_widened_frame();
}

TEST_CASE("layout open with in-out distinct masks uses app codec fs", "[mock][layout]")
{
    test_layout_open_with_in_out_distinct_logical_masks_uses_app_codec_fs();
}

TEST_CASE("layout open with in-out compatible masks uses combined codec fs", "[mock][layout]")
{
    test_layout_open_with_in_out_compatible_logical_masks_uses_combined_codec_fs();
}

TEST_CASE("layout in-out rejects direction dependent layout", "[mock][layout]")
{
    test_layout_in_out_rejects_direction_dependent_layout();
}

TEST_CASE("layout configures and enables bus before codec set-fs", "[mock][layout]")
{
    test_layout_codec_set_fs_failure_happens_after_bus_is_configured();
}

TEST_CASE("layout open converts widened bus into sysclk", "[mock][layout]")
{
    test_layout_open_converts_widened_bus_into_sysclk();
}

TEST_CASE("layout open skips sysclk without get_bus_info", "[mock][layout]")
{
    test_layout_open_skips_sysclk_without_get_bus_info();
}

TEST_CASE("layout open skips sysclk on wrong state", "[mock][layout]")
{
    test_layout_open_skips_sysclk_on_wrong_state();
}

TEST_CASE("layout open skips sysclk when callback absent", "[mock][layout]")
{
    test_layout_open_skips_sysclk_when_callback_absent();
}

TEST_CASE("layout open fails when get_bus_info errors", "[mock][layout]")
{
    test_layout_open_fails_when_get_bus_info_errors();
}

TEST_CASE("layout open fails when set_sysclk errors", "[mock][layout]")
{
    test_layout_open_fails_when_set_sysclk_errors();
}

TEST_CASE("layout open clears sysclk cache when bus is unavailable", "[mock][layout]")
{
    test_layout_open_clears_sysclk_cache_when_bus_is_unavailable();
}

TEST_CASE("layout rollback keeps codec disabled when bus restore fails", "[mock][layout]")
{
    test_layout_rollback_keeps_codec_disabled_when_bus_restore_fails();
}

TEST_CASE("layout rollback restores codec from committed bus", "[mock][layout]")
{
    test_layout_rollback_restores_codec_from_committed_bus();
}

TEST_CASE("layout in-out open queries output bus for sysclk", "[mock][layout]")
{
    test_layout_in_out_open_queries_output_bus_for_sysclk();
}

TEST_CASE("layout map-query-absent and std paths keep legacy behavior", "[mock][layout]")
{
    test_layout_map_query_absent_and_std_path_keep_legacy_behavior();
}

TEST_CASE("layout legacy tdm open accepts 16 slots without map query", "[mock][layout]")
{
    test_layout_legacy_tdm_open_accepts_16_slot_without_map_query();
}

TEST_CASE("set_adc_label updates copied label and layout translation", "[mock][layout][adc_label]")
{
    test_set_adc_label_updates_copied_label_and_layout_translation();
}

TEST_CASE("set_adc_label rejects open input and invalid args", "[mock][layout][adc_label]")
{
    test_set_adc_label_rejects_open_input_and_invalid_args();
}

TEST_CASE("set_adc_label missing callback and driver failure", "[mock][layout][adc_label]")
{
    test_set_adc_label_missing_callback_and_driver_failure();
}

TEST_CASE("set_adc_label clears layout maps only on success", "[mock][layout][adc_label]")
{
    test_set_adc_label_clears_layout_maps_only_on_success();
}
