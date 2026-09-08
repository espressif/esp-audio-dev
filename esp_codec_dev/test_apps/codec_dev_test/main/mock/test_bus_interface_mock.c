/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "unity.h"

#include "audio_codec_ctrl_if.h"
#include "audio_codec_data_if.h"
#include "audio_codec_if.h"
#include "audio_hw_base.h"
#include "es7210_adc.h"
#include "es8311_codec.h"
#include "es8389_codec.h"

#define TEST_CTRL_REG_COUNT  (256)

typedef struct {
    audio_codec_ctrl_if_t    base;
    bool                     is_open;
    audio_codec_ctrl_info_t  info;
    uint8_t                  reg[TEST_CTRL_REG_COUNT];
} test_codec_ctrl_t;

typedef struct {
    int                          call_count;
    esp_codec_dev_type_t         last_dev_type;
    uint8_t                      last_total_slot;
    esp_codec_dev_channel_map_t  mapping;
    int                          ret;
} test_bus_map_query_ctx_t;

typedef struct {
    audio_codec_data_if_t      base;
    esp_codec_dev_map_query_t  in_map_query;
    esp_codec_dev_map_query_t  out_map_query;
    esp_codec_dev_bus_info_t   in_bus;
    esp_codec_dev_bus_info_t   out_bus;
} test_bus_data_if_t;

static bool test_bus_is_direction(esp_codec_dev_type_t dev_type)
{
    return dev_type == ESP_CODEC_DEV_TYPE_IN || dev_type == ESP_CODEC_DEV_TYPE_OUT;
}

static int test_codec_ctrl_open(const audio_codec_ctrl_if_t *ctrl, void *cfg, int cfg_size)
{
    (void)cfg;
    (void)cfg_size;
    test_codec_ctrl_t *codec_ctrl = (test_codec_ctrl_t *)ctrl;
    if (codec_ctrl == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    codec_ctrl->is_open = true;
    return ESP_CODEC_DEV_OK;
}

static bool test_codec_ctrl_is_open(const audio_codec_ctrl_if_t *ctrl)
{
    test_codec_ctrl_t *codec_ctrl = (test_codec_ctrl_t *)ctrl;
    return codec_ctrl && codec_ctrl->is_open;
}

static int test_codec_ctrl_read_reg(const audio_codec_ctrl_if_t *ctrl, int reg, int reg_len, void *data, int data_len)
{
    (void)reg_len;
    test_codec_ctrl_t *codec_ctrl = (test_codec_ctrl_t *)ctrl;
    if (codec_ctrl == NULL || data == NULL || data_len != 1 || reg < 0 || reg >= TEST_CTRL_REG_COUNT) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *(uint8_t *)data = codec_ctrl->reg[reg];
    return ESP_CODEC_DEV_OK;
}

static int test_codec_ctrl_write_reg(const audio_codec_ctrl_if_t *ctrl, int reg, int reg_len, void *data, int data_len)
{
    (void)reg_len;
    test_codec_ctrl_t *codec_ctrl = (test_codec_ctrl_t *)ctrl;
    if (codec_ctrl == NULL || data == NULL || data_len != 1 || reg < 0 || reg >= TEST_CTRL_REG_COUNT) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    codec_ctrl->reg[reg] = *(uint8_t *)data;
    return ESP_CODEC_DEV_OK;
}

static int test_codec_ctrl_get_info(const audio_codec_ctrl_if_t *ctrl, audio_codec_ctrl_info_t *info)
{
    test_codec_ctrl_t *codec_ctrl = (test_codec_ctrl_t *)ctrl;
    if (codec_ctrl == NULL || info == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *info = codec_ctrl->info;
    return ESP_CODEC_DEV_OK;
}

static int test_codec_ctrl_close(const audio_codec_ctrl_if_t *ctrl)
{
    test_codec_ctrl_t *codec_ctrl = (test_codec_ctrl_t *)ctrl;
    if (codec_ctrl == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    codec_ctrl->is_open = false;
    return ESP_CODEC_DEV_OK;
}

static void test_codec_ctrl_init(test_codec_ctrl_t *ctrl, uint16_t addr)
{
    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->info.type = AUDIO_CODEC_CTRL_I2C;
    ctrl->info.i2c.addr = addr;
    ctrl->base.open = test_codec_ctrl_open;
    ctrl->base.is_open = test_codec_ctrl_is_open;
    ctrl->base.read_reg = test_codec_ctrl_read_reg;
    ctrl->base.write_reg = test_codec_ctrl_write_reg;
    ctrl->base.get_info = test_codec_ctrl_get_info;
    ctrl->base.close = test_codec_ctrl_close;
    TEST_ESP_OK(ctrl->base.open(&ctrl->base, NULL, 0));
}

static void assert_bus_info_equals(const esp_codec_dev_bus_info_t *actual,
                                   const esp_codec_dev_bus_info_t *expected)
{
    TEST_ASSERT_NOT_NULL(actual);
    TEST_ASSERT_NOT_NULL(expected);
    TEST_ASSERT_EQUAL(expected->mode, actual->mode);
    TEST_ASSERT_EQUAL_UINT32(expected->sample_rate, actual->sample_rate);
    TEST_ASSERT_EQUAL(expected->mclk_multiple, actual->mclk_multiple);
    TEST_ASSERT_EQUAL_UINT8(expected->total_slot, actual->total_slot);
    TEST_ASSERT_EQUAL_UINT8(expected->slot_bit, actual->slot_bit);
    TEST_ASSERT_EQUAL_UINT8(expected->data_bit, actual->data_bit);
    TEST_ASSERT_EQUAL_HEX16(expected->slot_mask, actual->slot_mask);
    TEST_ASSERT_EQUAL_HEX16(expected->total_frame_bits, actual->total_frame_bits);
}

static int test_bus_map_query_resolve(void *ctx, esp_codec_dev_type_t dev_type,
                                      uint8_t total_slot, esp_codec_dev_channel_map_t *mapping)
{
    test_bus_map_query_ctx_t *query_ctx = (test_bus_map_query_ctx_t *)ctx;
    if (query_ctx == NULL || mapping == NULL || !test_bus_is_direction(dev_type) || total_slot == 0) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    query_ctx->call_count++;
    query_ctx->last_dev_type = dev_type;
    query_ctx->last_total_slot = total_slot;
    *mapping = query_ctx->mapping;
    return query_ctx->ret;
}

static esp_codec_dev_map_query_t *test_bus_select_map_query(test_bus_data_if_t *data_if,
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

static esp_codec_dev_bus_info_t *test_bus_select_bus_info(test_bus_data_if_t *data_if,
                                                          esp_codec_dev_type_t dev_type)
{
    if (dev_type == ESP_CODEC_DEV_TYPE_IN) {
        return &data_if->in_bus;
    }
    if (dev_type == ESP_CODEC_DEV_TYPE_OUT) {
        return &data_if->out_bus;
    }
    return NULL;
}

static int test_bus_data_set_map_query(const audio_codec_data_if_t *h, esp_codec_dev_type_t dev_type,
                                       const esp_codec_dev_map_query_t *query)
{
    test_bus_data_if_t *data_if = (test_bus_data_if_t *)h;
    esp_codec_dev_map_query_t *stored_query = data_if ? test_bus_select_map_query(data_if, dev_type) : NULL;
    if (stored_query == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (query) {
        *stored_query = *query;
    } else {
        memset(stored_query, 0, sizeof(*stored_query));
    }
    return ESP_CODEC_DEV_OK;
}

static int test_bus_data_get_bus_info(const audio_codec_data_if_t *h, esp_codec_dev_type_t dev_type,
                                      esp_codec_dev_bus_info_t *bus_info)
{
    test_bus_data_if_t *data_if = (test_bus_data_if_t *)h;
    esp_codec_dev_bus_info_t *stored_bus = data_if ? test_bus_select_bus_info(data_if, dev_type) : NULL;
    if (stored_bus == NULL || bus_info == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *bus_info = *stored_bus;
    return ESP_CODEC_DEV_OK;
}

static void test_bus_init_data_if(test_bus_data_if_t *data_if)
{
    memset(data_if, 0, sizeof(*data_if));
    data_if->base.set_map_query = test_bus_data_set_map_query;
    data_if->base.get_bus_info = test_bus_data_get_bus_info;
    data_if->in_bus = (esp_codec_dev_bus_info_t) {
        .mode = ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS,
        .sample_rate = 16000,
        .mclk_multiple = 256,
        .total_slot = 6,
        .slot_bit = 16,
        .data_bit = 16,
        .slot_mask = 0x000B,
        .total_frame_bits = 96,
    };
    data_if->out_bus = (esp_codec_dev_bus_info_t) {
        .mode = ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS,
        .sample_rate = 48000,
        .mclk_multiple = 256,
        .total_slot = 2,
        .slot_bit = 32,
        .data_bit = 24,
        .slot_mask = 0x0003,
        .total_frame_bits = 64,
    };
}

static void assert_codec_tdm_order_rows(const audio_codec_if_t *codec_if)
{
    static const esp_codec_dev_device_map_info_t expected[] = {
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2)}},
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 4, {.value = ESP_CODEC_DEV_CHANNEL_MAP_4CH(1, 3, 2, 4)}},
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 6, {.value = ESP_CODEC_DEV_CHANNEL_MAP_6CH(1, 3, 5, 2, 4, 6)}},
        {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 8, {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 3, 5, 7, 2, 4, 6, 8)}},
    };
    const esp_codec_dev_device_map_info_t *rows = NULL;
    int row_count = 0;
    TEST_ASSERT_NOT_NULL(codec_if->hw_base.get_order_list);
    TEST_ESP_OK(codec_if->hw_base.get_order_list(&codec_if->hw_base, &rows, &row_count));
    TEST_ASSERT_NOT_NULL(rows);
    int tdm_row_count = 0;
    for (int row_idx = 0; row_idx < row_count; row_idx++) {
        if (rows[row_idx].mode == ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS) {
            tdm_row_count++;
        }
    }
    TEST_ASSERT_EQUAL_INT(4, tdm_row_count);
    for (size_t expected_idx = 0; expected_idx < sizeof(expected) / sizeof(expected[0]); expected_idx++) {
        const esp_codec_dev_device_map_info_t *match = NULL;
        for (int row_idx = 0; row_idx < row_count; row_idx++) {
            if (rows[row_idx].mode == expected[expected_idx].mode &&
                rows[row_idx].channels == expected[expected_idx].channels) {
                match = &rows[row_idx];
                break;
            }
        }
        TEST_ASSERT_NOT_NULL(match);
        TEST_ASSERT_EQUAL_HEX32(expected[expected_idx].map.value, match->map.value);
    }
}

static void test_es7210_order_table_has_expected_tdm_rows(void)
{
    test_codec_ctrl_t ctrl = {0};
    test_codec_ctrl_init(&ctrl, ES7210_CODEC_DEFAULT_ADDR);
    es7210_codec_cfg_t cfg = {
        .ctrl_if = &ctrl.base,
    };
    const audio_codec_if_t *codec_if = es7210_codec_new(&cfg);
    TEST_ASSERT_NOT_NULL(codec_if);
    assert_codec_tdm_order_rows(codec_if);
    audio_codec_delete_codec_if(codec_if);
}

static void test_es8311_order_table_has_expected_tdm_rows(void)
{
    test_codec_ctrl_t ctrl = {0};
    test_codec_ctrl_init(&ctrl, ES8311_CODEC_DEFAULT_ADDR);
    es8311_codec_cfg_t cfg = {
        .ctrl_if = &ctrl.base,
        .pa_cfg = {
            .pa_pin = -1,
        },
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&cfg);
    TEST_ASSERT_NOT_NULL(codec_if);
    assert_codec_tdm_order_rows(codec_if);
    audio_codec_delete_codec_if(codec_if);
}

static void test_es8389_order_table_has_expected_tdm_rows(void)
{
    test_codec_ctrl_t ctrl = {0};
    test_codec_ctrl_init(&ctrl, ES8389_CODEC_DEFAULT_ADDR);
    es8389_codec_cfg_t cfg = {
        .ctrl_if = &ctrl.base,
        .pa_cfg = {
            .pa_pin = -1,
        },
    };
    const audio_codec_if_t *codec_if = es8389_codec_new(&cfg);
    TEST_ASSERT_NOT_NULL(codec_if);
    assert_codec_tdm_order_rows(codec_if);
    audio_codec_delete_codec_if(codec_if);
}

static void test_data_if_map_query_registration_and_unregistration_is_directional(void)
{
    test_bus_data_if_t data_if;
    test_bus_init_data_if(&data_if);

    test_bus_map_query_ctx_t in_ctx = {
        .mapping = {
            .value = ESP_CODEC_DEV_CHANNEL_MAP_6CH(1, 3, 5, 2, 4, 6),
        },
        .ret = ESP_CODEC_DEV_OK,
    };
    test_bus_map_query_ctx_t out_ctx = {
        .mapping = {
            .value = ESP_CODEC_DEV_CHANNEL_MAP_4CH(1, 3, 2, 4),
        },
        .ret = ESP_CODEC_DEV_NOT_SUPPORT,
    };
    const esp_codec_dev_map_query_t in_map_query = {
        .ctx = &in_ctx,
        .resolve_cb = test_bus_map_query_resolve,
    };
    const esp_codec_dev_map_query_t out_map_query = {
        .ctx = &out_ctx,
        .resolve_cb = test_bus_map_query_resolve,
    };

    TEST_ESP_OK(data_if.base.set_map_query(&data_if.base, ESP_CODEC_DEV_TYPE_IN, &in_map_query));
    TEST_ESP_OK(data_if.base.set_map_query(&data_if.base, ESP_CODEC_DEV_TYPE_OUT, &out_map_query));
    TEST_ASSERT_EQUAL_PTR(&in_ctx, data_if.in_map_query.ctx);
    TEST_ASSERT_EQUAL_PTR(test_bus_map_query_resolve, data_if.in_map_query.resolve_cb);
    TEST_ASSERT_EQUAL_PTR(&out_ctx, data_if.out_map_query.ctx);
    TEST_ASSERT_EQUAL_PTR(test_bus_map_query_resolve, data_if.out_map_query.resolve_cb);

    esp_codec_dev_channel_map_t mapping = {0};
    TEST_ESP_OK(data_if.in_map_query.resolve_cb(data_if.in_map_query.ctx, ESP_CODEC_DEV_TYPE_IN, 6, &mapping));
    TEST_ASSERT_EQUAL_INT(1, in_ctx.call_count);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_TYPE_IN, in_ctx.last_dev_type);
    TEST_ASSERT_EQUAL_UINT8(6, in_ctx.last_total_slot);
    TEST_ASSERT_EQUAL_HEX32(in_ctx.mapping.value, mapping.value);

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_NOT_SUPPORT,
                      data_if.out_map_query.resolve_cb(data_if.out_map_query.ctx, ESP_CODEC_DEV_TYPE_OUT, 4, &mapping));
    TEST_ASSERT_EQUAL_INT(1, out_ctx.call_count);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_TYPE_OUT, out_ctx.last_dev_type);
    TEST_ASSERT_EQUAL_UINT8(4, out_ctx.last_total_slot);

    TEST_ESP_OK(data_if.base.set_map_query(&data_if.base, ESP_CODEC_DEV_TYPE_IN, NULL));
    TEST_ASSERT_NULL(data_if.in_map_query.ctx);
    TEST_ASSERT_NULL(data_if.in_map_query.resolve_cb);
    TEST_ASSERT_EQUAL_PTR(&out_ctx, data_if.out_map_query.ctx);
    TEST_ASSERT_EQUAL_PTR(test_bus_map_query_resolve, data_if.out_map_query.resolve_cb);
}

static void test_data_if_map_query_registration_rejects_invalid_arguments(void)
{
    test_bus_data_if_t data_if;
    test_bus_init_data_if(&data_if);
    const esp_codec_dev_map_query_t query = {0};

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG,
                      data_if.base.set_map_query(NULL, ESP_CODEC_DEV_TYPE_IN, &query));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG,
                      data_if.base.set_map_query(&data_if.base, ESP_CODEC_DEV_TYPE_NONE, &query));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG,
                      data_if.base.set_map_query(&data_if.base, ESP_CODEC_DEV_TYPE_IN_OUT, &query));
}

static void test_data_if_get_bus_info_is_directional(void)
{
    test_bus_data_if_t data_if;
    test_bus_init_data_if(&data_if);

    esp_codec_dev_bus_info_t bus_info = {0};
    TEST_ESP_OK(data_if.base.get_bus_info(&data_if.base, ESP_CODEC_DEV_TYPE_IN, &bus_info));
    assert_bus_info_equals(&bus_info, &data_if.in_bus);

    TEST_ESP_OK(data_if.base.get_bus_info(&data_if.base, ESP_CODEC_DEV_TYPE_OUT, &bus_info));
    assert_bus_info_equals(&bus_info, &data_if.out_bus);
}

static void test_data_if_get_bus_info_rejects_invalid_arguments(void)
{
    test_bus_data_if_t data_if;
    test_bus_init_data_if(&data_if);
    esp_codec_dev_bus_info_t bus_info = {0};

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG,
                      data_if.base.get_bus_info(NULL, ESP_CODEC_DEV_TYPE_IN, &bus_info));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG,
                      data_if.base.get_bus_info(&data_if.base, ESP_CODEC_DEV_TYPE_NONE, &bus_info));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG,
                      data_if.base.get_bus_info(&data_if.base, ESP_CODEC_DEV_TYPE_IN_OUT, &bus_info));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG,
                      data_if.base.get_bus_info(&data_if.base, ESP_CODEC_DEV_TYPE_OUT, NULL));
}

TEST_CASE("es7210 order table has expected tdm rows", "[mock][bus_if][codec]")
{
    test_es7210_order_table_has_expected_tdm_rows();
}

TEST_CASE("es8311 order table has expected tdm rows", "[mock][bus_if][codec]")
{
    test_es8311_order_table_has_expected_tdm_rows();
}

TEST_CASE("es8389 order table has expected tdm rows", "[mock][bus_if][codec]")
{
    test_es8389_order_table_has_expected_tdm_rows();
}

TEST_CASE("data if map query registration is directional", "[mock][bus_if]")
{
    test_data_if_map_query_registration_and_unregistration_is_directional();
}

TEST_CASE("data if map query registration rejects invalid arguments", "[mock][bus_if]")
{
    test_data_if_map_query_registration_rejects_invalid_arguments();
}

TEST_CASE("data if bus info query is directional", "[mock][bus_if]")
{
    test_data_if_get_bus_info_is_directional();
}

TEST_CASE("data if bus info query rejects invalid arguments", "[mock][bus_if]")
{
    test_data_if_get_bus_info_rejects_invalid_arguments();
}
