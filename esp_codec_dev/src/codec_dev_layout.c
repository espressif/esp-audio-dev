/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_bit_defs.h"
#include "esp_log.h"

#include "audio_codec_if.h"
#include "audio_codec_data_if.h"
#include "audio_hw_base_priv.h"
#include "codec_dev_data_cvt.h"
#include "codec_dev_layout.h"
#include "codec_dev_mirror.h"
#include "codec_dev_map.h"
#include "codec_dev_priv.h"

#define CODEC_DEV_MIN_TDM_TOTAL_SLOT  (2)

typedef struct {
    int  cur_ch_num;
    int  req_ch_num;
    int  bus_len;
} layout_frame_info_t;

static const char *TAG = "ADEV_CODEC";

static int _ensure_order_table(codec_dev_t *dev)
{
    if (dev == NULL) {
        ESP_LOGE(TAG, "Ensure order table failed: invalid argument");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (dev->order_row_count == CODEC_DEV_ORDER_TABLE_UNLOADED) {
        const esp_codec_dev_device_map_info_t *rows = NULL;
        int row_count = 0;
        int ret = ESP_CODEC_DEV_NOT_SUPPORT;
        if (dev->codec_if != NULL) {
            ret = audio_hw_get_order_list(&dev->codec_if->hw_base, &rows, &row_count);
        }
        if (ret != ESP_CODEC_DEV_OK || rows == NULL || row_count <= 0) {
            dev->order_rows = NULL;
            dev->order_row_count = 0;
        } else {
            dev->order_rows = rows;
            dev->order_row_count = row_count;
        }
    }
    if (dev->order_rows == NULL || dev->order_row_count <= 0) {
        ESP_LOGD(TAG, "Ensure order table: no codec order table is available");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    return ESP_CODEC_DEV_OK;
}

static int _get_dir_mode(codec_dev_t *dev, esp_codec_dev_type_t dir, esp_codec_dev_i2s_mode_t *mode)
{
    if (dev == NULL || mode == NULL || dev->data_if == NULL || dev->data_if->get_mode == NULL) {
        ESP_LOGW(TAG, "Get data direction mode: interface does not expose mode information");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    esp_codec_dev_i2s_mode_t in_mode = ESP_CODEC_DEV_I2S_MODE_NONE;
    esp_codec_dev_i2s_mode_t out_mode = ESP_CODEC_DEV_I2S_MODE_NONE;
    int ret = dev->data_if->get_mode(dev->data_if, &in_mode, &out_mode);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Get data direction mode: interface query failed, ret=%d", ret);
        return ret;
    }
    if (dir == ESP_CODEC_DEV_TYPE_IN) {
        *mode = in_mode;
        return ESP_CODEC_DEV_OK;
    }
    if (dir == ESP_CODEC_DEV_TYPE_OUT) {
        *mode = out_mode;
        return ESP_CODEC_DEV_OK;
    }
    ESP_LOGE(TAG, "Get data direction mode failed: direction %d is invalid", dir);
    return ESP_CODEC_DEV_INVALID_ARG;
}

/**
 * @brief  Read a codec's slot mapping for one frame width out of its order table
 *
 * @note  The table is the single source of truth for where a codec places its channels. Rows carry
 *         no direction because the placement does not depend on one; a handle only ever asks for a
 *         direction it opened, and an input-only part never has an output side to ask about.
 */
static int _get_slot_map(codec_dev_t *dev, esp_codec_dev_i2s_mode_t mode,
                         uint8_t total_slot, esp_codec_dev_channel_map_t *device_map)
{
    if (dev == NULL || dev->codec_if == NULL || device_map == NULL) {
        ESP_LOGE(TAG, "Get codec slot mapping failed: invalid argument");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (total_slot == 0 || total_slot > ESP_CODEC_DEV_MAX_MAP_CHANNELS) {
        ESP_LOGE(TAG, "Get codec slot mapping failed: slot count %u is invalid", total_slot);
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = _ensure_order_table(dev);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    const esp_codec_dev_device_map_info_t *rows = dev->order_rows;
    int row_count = dev->order_row_count;
    for (int i = 0; i < row_count; i++) {
        if (rows[i].mode == mode && rows[i].channels == total_slot) {
            if (codec_dev_map_validate_device(&rows[i].map) != ESP_CODEC_DEV_OK ||
                codec_dev_map_count(&rows[i].map) != total_slot) {
                ESP_LOGE(TAG, "Get codec slot mapping failed: invalid map for %u slots", total_slot);
                return ESP_CODEC_DEV_INVALID_ARG;
            }
            *device_map = rows[i].map;
            return ESP_CODEC_DEV_OK;
        }
    }
    ESP_LOGD(TAG, "Get codec slot mapping: no %u-slot entry for mode %d", total_slot, mode);
    return ESP_CODEC_DEV_NOT_SUPPORT;
}

static int _resolve_slot_map(void *ctx, esp_codec_dev_type_t dev_type,
                             uint8_t total_slot, esp_codec_dev_channel_map_t *slot_map)
{
    codec_dev_t *dev = (codec_dev_t *)ctx;
    if (dev == NULL || slot_map == NULL) {
        ESP_LOGE(TAG, "Resolve codec slot map failed: invalid argument");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    esp_codec_dev_i2s_mode_t mode = ESP_CODEC_DEV_I2S_MODE_NONE;
    int ret = _get_dir_mode(dev, dev_type, &mode);
    if (ret != ESP_CODEC_DEV_OK) {
        /* _get_dir_mode already logged */
        return ret;
    }
    return _get_slot_map(dev, mode, total_slot, slot_map);
}

static void _expand_mono_frame(const esp_codec_dev_sample_info_t *app_fs,
                               esp_codec_dev_sample_info_t *planner_fs)
{
    if (app_fs == NULL || planner_fs == NULL) {
        return;
    }
    *planner_fs = *app_fs;
    if (planner_fs->channel == 1) {
        planner_fs->channel = 2;
        planner_fs->channel_mask = BIT(0);
    }
}

static int _register_map_query(codec_dev_t *dev, esp_codec_dev_type_t dir)
{
    if (dev == NULL || dev->data_if == NULL || dev->data_if->set_map_query == NULL) {
        ESP_LOGW(TAG, "Register map query: data interface does not support map query");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    esp_codec_dev_map_query_t *query = NULL;
    if (dir == ESP_CODEC_DEV_TYPE_IN) {
        query = &dev->in_map_query;
    } else if (dir == ESP_CODEC_DEV_TYPE_OUT) {
        query = &dev->out_map_query;
    } else {
        ESP_LOGE(TAG, "Register map query failed: direction %d is invalid", dir);
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    query->ctx = dev;
    query->resolve_cb = _resolve_slot_map;
    int ret = dev->data_if->set_map_query(dev->data_if, dir, query);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Register map query failed: direction %d, ret=%d", dir, ret);
        memset(query, 0, sizeof(*query));
    }
    return ret;
}

static int _resolve_expected_map(codec_dev_t *dev, esp_codec_dev_type_t dir,
                                 const esp_codec_dev_sample_info_t *planner_fs,
                                 esp_codec_dev_channel_map_t *memory_map)
{
    if (dev == NULL || dev->codec_if == NULL || planner_fs == NULL || memory_map == NULL) {
        ESP_LOGE(TAG, "Resolve expected memory map failed: invalid argument");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    esp_codec_dev_i2s_mode_t mode = ESP_CODEC_DEV_I2S_MODE_NONE;
    int ret = _get_dir_mode(dev, dir, &mode);
    if (ret != ESP_CODEC_DEV_OK) {
        /* _get_dir_mode already logged */
        return ret;
    }
    esp_codec_dev_channel_map_t device_map = {0};
    ret = _get_slot_map(dev, mode, planner_fs->channel, &device_map);
    if (ret != ESP_CODEC_DEV_OK) {
        /* _get_slot_map already logged */
        return ret;
    }
    /* OUT: playback mask is already logical channel IDs; device_map is only used for frame width.
       IN:  channel_mask is a slot mask and must be projected through device_map. */
    if (dir == ESP_CODEC_DEV_TYPE_OUT) {
        return codec_dev_map_from_mask(planner_fs->channel_mask,
                                       (uint8_t)codec_dev_map_count(&device_map),
                                       memory_map);
    }
    return codec_dev_map_slots_to_memory(&device_map, planner_fs->channel_mask, memory_map);
}

static int _try_prepare_map_query(codec_dev_t *dev, esp_codec_dev_type_t dir,
                                  const esp_codec_dev_sample_info_t *app_fs, bool *query_active,
                                  esp_codec_dev_channel_map_t *expected)
{
    esp_codec_dev_i2s_mode_t mode = ESP_CODEC_DEV_I2S_MODE_NONE;
    if (_get_dir_mode(dev, dir, &mode) != ESP_CODEC_DEV_OK ||
        codec_dev_layout_can_use_map_query(dev, dir, mode) == false) {
        return ESP_CODEC_DEV_OK;
    }
    int ret = _register_map_query(dev, dir);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to register %s map query, ret=0x%x",
                 dir == ESP_CODEC_DEV_TYPE_IN ? "input" : "output", ret);
        return ret;
    }
    *query_active = true;
    esp_codec_dev_sample_info_t planner_fs = {0};
    _expand_mono_frame(app_fs, &planner_fs);
    ret = _resolve_expected_map(dev, dir, &planner_fs, expected);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to resolve %s logical selection, ret=0x%x",
                 dir == ESP_CODEC_DEV_TYPE_IN ? "input" : "output", ret);
    }
    return ret;
}

static int _match_order_row_to_map(const esp_codec_dev_channel_map_t *req_map,
                                   const esp_codec_dev_device_map_info_t *cand,
                                   uint16_t *ch_mask)
{
    if (req_map == NULL || cand == NULL || ch_mask == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (cand->channels > ESP_CODEC_DEV_MAX_MAP_CHANNELS) {
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }

    esp_codec_dev_channel_map_t data_map = {0};
    int ret = codec_dev_map_memory_to_data(req_map, &cand->map, &data_map);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }

    uint16_t cand_ch_mask = 0;
    ret = codec_dev_map_to_mask(&data_map, cand->channels, &cand_ch_mask);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }

    esp_codec_dev_channel_map_t verify_data_map = {0};
    ret = codec_dev_map_from_mask(cand_ch_mask, cand->channels, &verify_data_map);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }

    esp_codec_dev_channel_map_t verify_map = {0};
    ret = codec_dev_map_data_to_memory(&verify_data_map, &cand->map, &verify_map);
    if (ret != ESP_CODEC_DEV_OK || verify_map.value != req_map->value) {
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }

    *ch_mask = cand_ch_mask;
    return ESP_CODEC_DEV_OK;
}

static int _resolve_hw_fmt_from_map(codec_dev_t *dev, const esp_codec_dev_channel_map_t *req_map,
                                    esp_codec_dev_i2s_mode_t data_mode,
                                    uint8_t *ch_num, uint16_t *ch_mask)
{
    if (dev == NULL || req_map == NULL || req_map->value == 0 ||
        ch_num == NULL || ch_mask == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = _ensure_order_table(dev);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }

    const char *mode_name = codec_dev_i2s_mode_name(data_mode);

    for (int i = 0; i < dev->order_row_count; i++) {
        const esp_codec_dev_device_map_info_t *cand = &dev->order_rows[i];
        if (cand->mode != data_mode) {
            continue;
        }
        uint16_t cand_ch_mask = 0;
        ret = _match_order_row_to_map(req_map, cand, &cand_ch_mask);
        if (ret != ESP_CODEC_DEV_OK) {
            continue;
        }
        ESP_LOGI(TAG, "Resolved map 0x%lx to ch_num=%d, ch_mask=0x%x, data_mode=%s, dev_map=0x%lx",
                 (unsigned long)req_map->value, cand->channels, cand_ch_mask, mode_name,
                 (unsigned long)cand->map.value);
        *ch_num = cand->channels;
        *ch_mask = cand_ch_mask;
        return ESP_CODEC_DEV_OK;
    }

    ESP_LOGE(TAG, "Failed to resolve map 0x%lx for data mode %s", (unsigned long)req_map->value, mode_name);
    return ESP_CODEC_DEV_NOT_SUPPORT;
}

static int _get_layout_frame_info(codec_dev_t *dev, const esp_codec_dev_channel_map_t *req_map,
                                  const esp_codec_dev_channel_map_t *cur_map,
                                  int user_len, layout_frame_info_t *frame_info)
{
    frame_info->cur_ch_num = codec_dev_map_count(cur_map);
    frame_info->req_ch_num = codec_dev_map_count(req_map);
    if (frame_info->cur_ch_num <= 0 || frame_info->req_ch_num <= 0) {
        ESP_LOGE(TAG, "Invalid channel count for layout conversion");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    int bytes_per_sample = dev->fs.bits_per_sample / 8;
    int user_frame_size = frame_info->req_ch_num * bytes_per_sample;
    int bus_frame_size = frame_info->cur_ch_num * bytes_per_sample;
    if (bytes_per_sample <= 0 || user_frame_size <= 0 || bus_frame_size <= 0 ||
        (user_len % user_frame_size) != 0) {
        ESP_LOGE(TAG, "Invalid frame size or length alignment: bytes_per_sample=%d, "
                      "user_frame_size=%d, bus_frame_size=%d, user_len=%d",
                 bytes_per_sample, user_frame_size, bus_frame_size, user_len);
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    frame_info->bus_len = (user_len / user_frame_size) * bus_frame_size;
    return ESP_CODEC_DEV_OK;
}

static int _read_direct(codec_dev_t *dev, void *data, int len)
{
    const audio_codec_data_if_t *data_if = dev->data_if;
    int ret = data_if->read(data_if, (uint8_t *)data, len);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to read audio data, ret=0x%x", ret);
        return ret;
    }
    if (dev->mirror) {
        (void)codec_dev_mirror_write(dev->mirror, (const uint8_t *)data, len);
    }
    return ESP_CODEC_DEV_OK;
}

static int _read_with_convert(codec_dev_t *dev, void *data, int len,
                              const esp_codec_dev_channel_map_t *req_map,
                              const esp_codec_dev_channel_map_t *cur_map)
{
    const audio_codec_data_if_t *data_if = dev->data_if;
    layout_frame_info_t frame_info = {0};
    int ret = _get_layout_frame_info(dev, req_map, cur_map, len, &frame_info);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }

    uint8_t *recv_data = (uint8_t *)data;
    if (frame_info.cur_ch_num != frame_info.req_ch_num) {
        recv_data = (uint8_t *)malloc(frame_info.bus_len);
        if (recv_data == NULL) {
            return ESP_CODEC_DEV_NO_MEM;
        }
    }
    ret = data_if->read(data_if, recv_data, frame_info.bus_len);
    if (ret == ESP_CODEC_DEV_OK) {
        codec_dev_data_cvt_info_t src = {
            .data = recv_data,
            .len = frame_info.bus_len,
            .map = *cur_map,
            .bits = dev->fs.bits_per_sample,
            .ch_num = frame_info.cur_ch_num,
        };
        codec_dev_data_cvt_info_t dst = {
            .data = (uint8_t *)data,
            .len = len,
            .map = *req_map,
            .bits = dev->fs.bits_per_sample,
            .ch_num = frame_info.req_ch_num,
        };
        ret = codec_dev_data_cvt_layout(&src, &dst);
    }
    if (ret == ESP_CODEC_DEV_OK && dev->mirror) {
        (void)codec_dev_mirror_write(dev->mirror, (const uint8_t *)data, len);
    }
    if (recv_data != (uint8_t *)data) {
        free(recv_data);
    }
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to read audio data, ret=0x%x", ret);
    }
    return ret;
}

static int _write_direct(codec_dev_t *dev, void *data, int len)
{
    const audio_codec_data_if_t *data_if = dev->data_if;
    if (data_if->write == NULL) {
        ESP_LOGE(TAG, "Data interface write is not supported");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    if (dev->sw_vol) {
        dev->sw_vol->process(dev->sw_vol, (uint8_t *)data, len, (uint8_t *)data, len);
    }
    int ret = data_if->write(data_if, (uint8_t *)data, len);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to write audio data, ret=0x%x", ret);
    }
    return ret;
}

static int _write_with_convert(codec_dev_t *dev, void *data, int len,
                               const esp_codec_dev_channel_map_t *req_map,
                               const esp_codec_dev_channel_map_t *cur_map)
{
    const audio_codec_data_if_t *data_if = dev->data_if;
    if (data_if->write == NULL) {
        ESP_LOGE(TAG, "Data interface write is not supported");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    layout_frame_info_t frame_info = {0};
    int ret = _get_layout_frame_info(dev, req_map, cur_map, len, &frame_info);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    uint8_t *send_data = (uint8_t *)data;
    if (frame_info.cur_ch_num != frame_info.req_ch_num) {
        send_data = (uint8_t *)malloc(frame_info.bus_len);
        if (send_data == NULL) {
            return ESP_CODEC_DEV_NO_MEM;
        }
    }
    codec_dev_data_cvt_info_t src = {
        .data = (uint8_t *)data,
        .len = len,
        .map = *req_map,
        .bits = dev->fs.bits_per_sample,
        .ch_num = frame_info.req_ch_num,
    };
    codec_dev_data_cvt_info_t dst = {
        .data = send_data,
        .len = frame_info.bus_len,
        .map = *cur_map,
        .bits = dev->fs.bits_per_sample,
        .ch_num = frame_info.cur_ch_num,
    };
    ret = codec_dev_data_cvt_layout(&src, &dst);
    if (ret == ESP_CODEC_DEV_OK) {
        if (dev->sw_vol) {
            dev->sw_vol->process(dev->sw_vol, send_data, frame_info.bus_len, send_data, frame_info.bus_len);
        }
        ret = data_if->write(data_if, send_data, frame_info.bus_len);
    }
    if (send_data != (uint8_t *)data) {
        free(send_data);
    }
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to write audio data, ret=0x%x", ret);
    }
    return ret;
}

bool codec_dev_layout_can_use_map_query(codec_dev_t *dev, esp_codec_dev_type_t dir, esp_codec_dev_i2s_mode_t mode)
{
    if (dev == NULL || dev->codec_if == NULL || dev->data_if == NULL) {
        return false;
    }
    if (mode != ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS) {
        return false;
    }
    if (dev->data_if->set_map_query == NULL || dev->data_if->get_bus_info == NULL) {
        return false;
    }
    if (dir != ESP_CODEC_DEV_TYPE_IN && dir != ESP_CODEC_DEV_TYPE_OUT) {
        return false;
    }
    /* The planner needs a slot mapping for at least the narrowest frame it may pick, and the codec
       describes that in its order table. A codec with no TDM row there stays on the legacy path. */
    esp_codec_dev_channel_map_t slot_map = {0};
    return _get_slot_map(dev, mode, CODEC_DEV_MIN_TDM_TOTAL_SLOT, &slot_map) == ESP_CODEC_DEV_OK;
}

void codec_dev_layout_clear_map_query(codec_dev_t *dev, esp_codec_dev_type_t dir)
{
    if (dev == NULL || dev->data_if == NULL || dev->data_if->set_map_query == NULL) {
        return;
    }
    esp_codec_dev_map_query_t *query = NULL;
    if (dir == ESP_CODEC_DEV_TYPE_IN) {
        query = &dev->in_map_query;
    } else if (dir == ESP_CODEC_DEV_TYPE_OUT) {
        query = &dev->out_map_query;
    }
    if (query == NULL || query->resolve_cb == NULL) {
        return;
    }
    dev->data_if->set_map_query(dev->data_if, dir, NULL);
    memset(query, 0, sizeof(*query));
}

int codec_dev_layout_prepare_open(codec_dev_t *dev, const esp_codec_dev_sample_info_t *app_fs,
                                  codec_dev_layout_plan_t *plan)
{
    if (dev == NULL || app_fs == NULL || plan == NULL) {
        ESP_LOGE(TAG, "Prepare open layout failed: invalid argument");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    memset(plan, 0, sizeof(*plan));
    int ret = ESP_CODEC_DEV_OK;
    if (dev->input_opened) {
        ret = _try_prepare_map_query(dev, ESP_CODEC_DEV_TYPE_IN, app_fs, &plan->in_query, &plan->expected_in_map);
    }
    if (ret == ESP_CODEC_DEV_OK && dev->output_opened) {
        ret = _try_prepare_map_query(dev, ESP_CODEC_DEV_TYPE_OUT, app_fs,
                                     &plan->out_query, &plan->expected_out_map);
    }
    if (ret == ESP_CODEC_DEV_OK && plan->in_query && plan->out_query &&
        plan->expected_in_map.value != plan->expected_out_map.value) {
        ESP_LOGE(TAG, "Input layout 0x%lX and output layout 0x%lX differ on a shared-format handle",
                 (unsigned long)plan->expected_in_map.value, (unsigned long)plan->expected_out_map.value);
        ret = ESP_CODEC_DEV_NOT_SUPPORT;
    }
    if (ret != ESP_CODEC_DEV_OK) {
        codec_dev_layout_clear_map_query(dev, ESP_CODEC_DEV_TYPE_OUT);
        codec_dev_layout_clear_map_query(dev, ESP_CODEC_DEV_TYPE_IN);
        memset(plan, 0, sizeof(*plan));
    }
    return ret;
}

void codec_dev_layout_commit_open_map(codec_dev_t *dev, const codec_dev_layout_plan_t *plan)
{
    if (dev == NULL || plan == NULL) {
        return;
    }
    /* The planner validates the map-query-backed mapping before hardware apply and proves the
       selected DMA logical order already matches the original expected map. Commit that
       expected map here instead of adding a second post-transaction failure boundary. */
    if (plan->in_query) {
        dev->cur_map = plan->expected_in_map;
    } else if (plan->out_query) {
        dev->cur_map = plan->expected_out_map;
    }
}

int codec_dev_layout_resolve_map_from_bus(codec_dev_t *dev, esp_codec_dev_type_t dir,
                                          esp_codec_dev_channel_map_t *memory_map, esp_codec_dev_bus_info_t *bus_info)
{
    if (dev == NULL || dev->codec_if == NULL || dev->data_if == NULL || memory_map == NULL ||
        dev->data_if->get_bus_info == NULL) {
        ESP_LOGW(TAG, "Resolve current bus layout: required interface operation is unavailable");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    esp_codec_dev_bus_info_t bus = {0};
    int ret = dev->data_if->get_bus_info(dev->data_if, dir, &bus);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Resolve current bus layout: bus query failed, ret=%d", ret);
        return ret;
    }
    if (bus.total_slot == 0) {
        ESP_LOGW(TAG, "Resolve current bus layout: bus is not configured");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    esp_codec_dev_channel_map_t device_map = {0};
    ret = _get_slot_map(dev, bus.mode, bus.total_slot, &device_map);
    if (ret != ESP_CODEC_DEV_OK) {
        /* _get_slot_map already logged */
        return ret;
    }
    ret = codec_dev_map_slots_to_memory(&device_map, bus.slot_mask, memory_map);
    if (ret != ESP_CODEC_DEV_OK) {
        /* codec_dev_map_slots_to_memory already logged */
        return ret;
    }
    if (bus_info) {
        *bus_info = bus;
    }
    return ESP_CODEC_DEV_OK;
}

int codec_dev_layout_resolve_map_from_fs(codec_dev_t *dev, const esp_codec_dev_sample_info_t *fs,
                                         esp_codec_dev_i2s_mode_t mode, esp_codec_dev_channel_map_t *map)
{
    if (dev == NULL || fs == NULL || map == NULL) {
        ESP_LOGE(TAG, "Resolve map from sample format failed: invalid argument");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if ((dev->dev_caps & ESP_CODEC_DEV_TYPE_IN) && fs->channel == 4 && mode == ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS) {
        /* When use 2ch 32bit to get 4ch 16bit, memory holds channel IDs 3,1,4,2. */
        map->value = ESP_CODEC_DEV_STD_4CH_MAP;
        return ESP_CODEC_DEV_OK;
    }
    if (dev->codec_if == NULL || dev->data_if == NULL) {
        ESP_LOGW(TAG, "Resolve map from sample format: codec or data interface is unavailable");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    esp_codec_dev_channel_map_t device_map = {0};
    int ret = _get_slot_map(dev, mode, fs->channel, &device_map);
    if (ret != ESP_CODEC_DEV_OK) {
        /* _get_slot_map already logged */
        return ret;
    }
    esp_codec_dev_channel_map_t data_map = {0};
    ret = codec_dev_map_from_mask(fs->channel_mask, fs->channel, &data_map);
    if (ret != ESP_CODEC_DEV_OK) {
        /* codec_dev_map_from_mask already logged */
        return ret;
    }
    return codec_dev_map_data_to_memory(&data_map, &device_map, map);
}

void codec_dev_layout_get_maps(codec_dev_t *dev,
                               esp_codec_dev_channel_map_t *requested_map,
                               esp_codec_dev_channel_map_t *current_map,
                               bool *need_convert)
{
    esp_codec_dev_channel_map_t requested = dev ? dev->set_map : (esp_codec_dev_channel_map_t) {0};
    esp_codec_dev_channel_map_t current = dev ? dev->cur_map : (esp_codec_dev_channel_map_t) {0};
    if (requested_map != NULL) {
        *requested_map = requested;
    }
    if (current_map != NULL) {
        *current_map = current;
    }
    if (need_convert != NULL) {
        *need_convert = (requested.value != 0 && current.value != 0 && requested.value != current.value);
    }
}

int codec_dev_layout_get_app_map(codec_dev_t *dev, esp_codec_dev_channel_map_t *map)
{
    if (dev == NULL || map == NULL) {
        ESP_LOGE(TAG, "Get application map failed: invalid argument");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    bool need_convert = false;
    codec_dev_layout_get_maps(dev, NULL, NULL, &need_convert);
    if (need_convert) {
        *map = dev->set_map;
        return ESP_CODEC_DEV_OK;
    }
    if (dev->cur_map.value != 0) {
        *map = dev->cur_map;
        return ESP_CODEC_DEV_OK;
    }
    ESP_LOGD(TAG, "Get application map: no current layout is known");
    return ESP_CODEC_DEV_NOT_FOUND;
}

int codec_dev_layout_resolve_hw_fs(codec_dev_t *dev, const esp_codec_dev_channel_map_t *map,
                                   esp_codec_dev_sample_info_t *hw_fs)
{
    if (dev == NULL || map == NULL || hw_fs == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (dev->input_opened == false && dev->output_opened == false) {
        ESP_LOGE(TAG, "Codec device is not open");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    if (dev->data_if == NULL || dev->data_if->get_mode == NULL) {
        ESP_LOGE(TAG, "Data interface does not support mode query");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }

    esp_codec_dev_i2s_mode_t in_mode, out_mode;
    if (dev->data_if->get_mode(dev->data_if, &in_mode, &out_mode) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to get data interface mode");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }

    esp_codec_dev_type_t dir =
        (dev->dev_caps == ESP_CODEC_DEV_TYPE_OUT) ? ESP_CODEC_DEV_TYPE_OUT : ESP_CODEC_DEV_TYPE_IN;
    esp_codec_dev_i2s_mode_t data_mode = (dir == ESP_CODEC_DEV_TYPE_OUT) ? out_mode : in_mode;
    uint16_t new_ch_mask = 0;
    uint8_t new_ch_num = 0;
    int ret = _resolve_hw_fmt_from_map(dev, map, data_mode, &new_ch_num, &new_ch_mask);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    *hw_fs = dev->fs;
    hw_fs->channel_mask = new_ch_mask;
    hw_fs->channel = new_ch_num;
    return ESP_CODEC_DEV_OK;
}

int codec_dev_layout_reconfigure_hw(codec_dev_t *dev, const esp_codec_dev_channel_map_t *map,
                                    const esp_codec_dev_sample_info_t *hw_fs)
{
    if (dev == NULL || map == NULL || hw_fs == NULL || dev->data_if == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (hw_fs->channel_mask == dev->fs.channel_mask && hw_fs->channel == dev->fs.channel) {
        dev->set_map = *map;
        dev->cur_map = *map;
        return ESP_CODEC_DEV_OK;
    }

    const audio_codec_if_t *codec = dev->codec_if;
    const audio_codec_data_if_t *data_if = dev->data_if;
    esp_codec_dev_sample_info_t old_fs = dev->fs;
    esp_codec_dev_sample_info_t new_fs = *hw_fs;
    bool adc_enabled = false;
    bool dac_enabled = false;
    bool data_if_enabled = false;
    int ret = ESP_CODEC_DEV_OK;
    ESP_LOGI(TAG, "Reconfigure hardware to ch_num=%d and ch_mask=0x%x", new_fs.channel, new_fs.channel_mask);

    /* Logical close: disable codec and data_if */
    if (codec) {
        if (dev->input_opened && codec->adc_if && codec->adc_if->ops.enable) {
            if (codec->adc_if->ops.enable(codec, false) != ESP_CODEC_DEV_OK) {
                return ESP_CODEC_DEV_DRV_ERR;
            }
        }
        if (dev->output_opened && codec->dac_if && codec->dac_if->ops.enable) {
            if (codec->dac_if->ops.enable(codec, false) != ESP_CODEC_DEV_OK) {
                return ESP_CODEC_DEV_DRV_ERR;
            }
        }
    }
    if (data_if->enable) {
        if (data_if->enable(data_if, dev->dev_caps, false) != ESP_CODEC_DEV_OK) {
            return ESP_CODEC_DEV_DRV_ERR;
        }
    }
    /* Logical open: configure and enable bus clocks before configuring and enabling the codec */
    if (data_if->set_fmt) {
        ret = data_if->set_fmt(data_if, dev->dev_caps, &new_fs);
        if (ret != ESP_CODEC_DEV_OK) {
            goto reconfig_rollback;
        }
    }
    if (data_if->enable) {
        ret = data_if->enable(data_if, dev->dev_caps, true);
        if (ret != ESP_CODEC_DEV_OK) {
            goto reconfig_rollback;
        }
        data_if_enabled = true;
    }
    ret = codec_dev_apply_sysclk(dev);
    if (ret != ESP_CODEC_DEV_OK) {
        goto reconfig_rollback;
    }
    if (codec && codec->hw_base.set_fs) {
        ret = codec->hw_base.set_fs(&codec->hw_base, &new_fs, dev->dev_caps);
        if (ret != 0) {
            ret = ESP_CODEC_DEV_NOT_SUPPORT;
            goto reconfig_rollback;
        }
    }
    if (codec) {
        if (dev->input_opened && codec->adc_if && codec->adc_if->ops.enable) {
            ret = codec->adc_if->ops.enable(codec, true);
            if (ret != ESP_CODEC_DEV_OK) {
                goto reconfig_rollback;
            }
            adc_enabled = true;
        }
        if (dev->output_opened && codec->dac_if && codec->dac_if->ops.enable) {
            ret = codec->dac_if->ops.enable(codec, true);
            if (ret != ESP_CODEC_DEV_OK) {
                goto reconfig_rollback;
            }
            dac_enabled = true;
        }
    }

    dev->fs = new_fs;
    codec_dev_apply_vol_mute(dev);
    dev->set_map = *map;
    dev->cur_map = *map;
    ESP_LOGI(TAG, "Applied map 0x%lX with ch_num=%d, ch_mask=0x%x",
             (unsigned long)map->value, new_fs.channel, (unsigned)new_fs.channel_mask);
    return ESP_CODEC_DEV_OK;

reconfig_rollback:
    if (codec) {
        if (dac_enabled && codec->dac_if && codec->dac_if->ops.enable) {
            codec->dac_if->ops.enable(codec, false);
        }
        if (adc_enabled && codec->adc_if && codec->adc_if->ops.enable) {
            codec->adc_if->ops.enable(codec, false);
        }
    }
    if (data_if_enabled && data_if->enable) {
        data_if->enable(data_if, dev->dev_caps, false);
    }

    /* Restore is best effort: the request already failed, so the return value is `ret` either way.
       Track the bus only to avoid programming the codec on a bus that stayed broken. */
    bool bus_restored = true;
    if (data_if->set_fmt) {
        int fmt_ret = data_if->set_fmt(data_if, dev->dev_caps, &old_fs);
        if (fmt_ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Reconfigure rollback failed: restore format ret=%d", fmt_ret);
            bus_restored = false;
        }
    }
    if (bus_restored && data_if->enable) {
        int enable_ret = data_if->enable(data_if, dev->dev_caps, true);
        if (enable_ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Reconfigure rollback failed: restore enable ret=%d", enable_ret);
            bus_restored = false;
        }
    }
    if (codec) {
        /* Refresh from the live bus even when restore failed, so the codec never keeps a cached
           frame length that no longer matches the bus. */
        codec_dev_apply_sysclk(dev);
        if (bus_restored) {
            if (codec->hw_base.set_fs) {
                codec->hw_base.set_fs(&codec->hw_base, &old_fs, dev->dev_caps);
            }
            if (dev->input_opened && codec->adc_if && codec->adc_if->ops.enable) {
                codec->adc_if->ops.enable(codec, true);
            }
            if (dev->output_opened && codec->dac_if && codec->dac_if->ops.enable) {
                codec->dac_if->ops.enable(codec, true);
            }
        }
    }
    if (bus_restored == false) {
        ESP_LOGE(TAG, "Reconfigure rollback left the audio path down, close and reopen the device");
    }
    return ret;
}

int codec_dev_layout_read(codec_dev_t *dev, void *data, int len)
{
    esp_codec_dev_channel_map_t requested_map = {0};
    esp_codec_dev_channel_map_t current_map = {0};
    bool need_convert = false;
    codec_dev_layout_get_maps(dev, &requested_map, &current_map, &need_convert);
    if (requested_map.value != 0 && current_map.value == 0) {
        ESP_LOGW(TAG, "Current data layout is unknown, skip software layout conversion");
        need_convert = false;
    }
    if (need_convert) {
        return _read_with_convert(dev, data, len, &requested_map, &current_map);
    }
    return _read_direct(dev, data, len);
}

int codec_dev_layout_write(codec_dev_t *dev, void *data, int len)
{
    esp_codec_dev_channel_map_t requested_map = {0};
    esp_codec_dev_channel_map_t current_map = {0};
    bool need_convert = false;
    codec_dev_layout_get_maps(dev, &requested_map, &current_map, &need_convert);
    if (requested_map.value != 0 && current_map.value == 0) {
        ESP_LOGW(TAG, "Current data layout is unknown, skip software layout conversion");
        need_convert = false;
    }
    if (need_convert) {
        return _write_with_convert(dev, data, len, &requested_map, &current_map);
    }
    return _write_direct(dev, data, len);
}
