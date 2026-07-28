/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "esp_log.h"

#include "codec_dev_order.h"

#define CODEC_DEV_ORDER_MAX_LABEL_COUNT  (8)
#define CODEC_DEV_ORDER_MAX_LABEL_LEN    (16)

static const char *TAG = "ADEV_LAYOUT";

typedef struct {
    char     label[CODEC_DEV_ORDER_MAX_LABEL_COUNT][CODEC_DEV_ORDER_MAX_LABEL_LEN];
    uint8_t  count;
} codec_label_list_t;

static inline bool label_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool parse_label_list(const char *label, codec_label_list_t *list)
{
    if (label == NULL || label[0] == '\0' || list == NULL) {
        return false;
    }
    memset(list, 0, sizeof(*list));
    const char *token_start = label;
    while (true) {
        if (list->count >= CODEC_DEV_ORDER_MAX_LABEL_COUNT) {
            return false;
        }
        const char *segment_end = token_start;
        while (*segment_end != '\0' && *segment_end != ',') {
            segment_end++;
        }
        const char *token_end = segment_end;
        while (token_start < token_end && label_is_space(*token_start)) {
            token_start++;
        }
        while (token_end > token_start && label_is_space(*(token_end - 1))) {
            token_end--;
        }
        int token_len = token_end - token_start;
        if (token_len <= 0 || token_len >= CODEC_DEV_ORDER_MAX_LABEL_LEN) {
            return false;
        }
        for (int i = 0; i < list->count; i++) {
            if (strlen(list->label[i]) == token_len && strncmp(list->label[i], token_start, token_len) == 0) {
                return false;
            }
        }
        memcpy(list->label[list->count], token_start, token_len);
        list->label[list->count][token_len] = '\0';
        list->count++;
        if (*segment_end == '\0') {
            break;
        }
        token_start = segment_end + 1;
    }
    return list->count > 0;
}

static int find_label_index(const codec_label_list_t *list, const char *label)
{
    if (list == NULL || label == NULL) {
        return -1;
    }
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->label[i], label) == 0) {
            return i;
        }
    }
    return -1;
}

bool codec_dev_order_is_valid(const esp_codec_dev_channel_map_t *map)
{
    if (map == NULL) {
        ESP_LOGE(TAG, "Channel map is NULL");
        return false;
    }
    if (codec_dev_channel_map_has_dense_prefix(map) == false) {
        ESP_LOGE(TAG, "Channel map must be a dense memory-position prefix from ch1");
        return false;
    }
    uint16_t used_ids = 0;
    for (uint8_t mem_pos = 1; mem_pos <= 8; mem_pos++) {
        uint8_t slot = codec_dev_channel_map_get_slot(map, mem_pos);
        if (slot == 0) {
            break;
        }
        if (slot > 8) {
            ESP_LOGE(TAG, "Slot / channel ID is out of range: %u", slot);
            return false;
        }
        uint16_t bit = (uint16_t)(1U << slot);
        if (used_ids & bit) {
            ESP_LOGE(TAG, "Channel map has a duplicated slot / channel ID: %u", slot);
            return false;
        }
        used_ids |= bit;
    }
    return true;
}

bool codec_dev_order_contains(const esp_codec_dev_channel_map_t *superset_map,
                              const esp_codec_dev_channel_map_t *subset_map)
{
    if (subset_map == NULL) {
        return true;
    }
    if (superset_map == NULL) {
        return false;
    }
    for (uint8_t mem_pos = 1; mem_pos <= 8; mem_pos++) {
        uint8_t slot = codec_dev_channel_map_get_slot(subset_map, mem_pos);
        if (slot == 0) {
            break;
        }
        if (codec_dev_channel_map_find_pos(superset_map, slot) == 0) {
            return false;
        }
    }
    return true;
}

int codec_dev_order_from_labels(const char *board_labels, const char *requested_labels,
                                esp_codec_dev_channel_map_t *map)
{
    codec_label_list_t board = {0};
    codec_label_list_t requested = {0};
    if (parse_label_list(board_labels, &board) == false ||
        parse_label_list(requested_labels, &requested) == false || map == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    map->value = 0;
    for (int mem_pos = 0; mem_pos < requested.count; mem_pos++) {
        int label_idx = find_label_index(&board, requested.label[mem_pos]);
        if (label_idx < 0) {
            ESP_LOGE(TAG, "Invalid label: %s, board count: %d", requested.label[mem_pos], board.count);
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        codec_dev_channel_map_set_slot(map, (uint8_t)(mem_pos + 1), (uint8_t)(label_idx + 1));
    }
    return codec_dev_order_is_valid(map) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_INVALID_ARG;
}

int codec_dev_order_to_labels(const char *board_labels, const esp_codec_dev_channel_map_t *map,
                              char *label_buf, int label_buf_size)
{
    codec_label_list_t board = {0};
    if (map == NULL || parse_label_list(board_labels, &board) == false ||
        label_buf == NULL || label_buf_size <= 0 || codec_dev_order_is_valid(map) == false) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int channel_count = codec_dev_channel_map_count_channels(map);
    int used = 0;
    label_buf[0] = '\0';
    for (int mem_pos = 1; mem_pos <= channel_count; mem_pos++) {
        uint8_t channel_id = codec_dev_channel_map_get_slot(map, (uint8_t)mem_pos);
        if (channel_id == 0 || channel_id > board.count) {
            ESP_LOGE(TAG, "Invalid channel ID %u at memory position %d, label count: %d",
                     channel_id, mem_pos, board.count);
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        const char *token = board.label[channel_id - 1];
        int token_len = strlen(token);
        int need_len = token_len + (mem_pos == 1 ? 0 : 1);
        if (used + need_len >= label_buf_size) {
            ESP_LOGE(TAG, "Label buffer is too small, used: %d, need: %d, buffer size: %d",
                     used, need_len, label_buf_size);
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        if (mem_pos != 1) {
            label_buf[used++] = ',';
        }
        memcpy(label_buf + used, token, token_len);
        used += token_len;
        label_buf[used] = '\0';
    }
    return ESP_CODEC_DEV_OK;
}

int codec_dev_order_resolve_memory_map(const esp_codec_dev_channel_map_t *data_map,
                                       const esp_codec_dev_channel_map_t *device_map,
                                       esp_codec_dev_channel_map_t *memory_map)
{
    if (data_map == NULL || device_map == NULL || memory_map == NULL ||
        codec_dev_order_is_valid(data_map) == false || codec_dev_order_is_valid(device_map) == false) {
        ESP_LOGE(TAG, "Data map, device map, or memory map pointer is invalid");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    memory_map->value = 0;
    int data_count = codec_dev_channel_map_count_channels(data_map);
    for (int mem_pos = 1; mem_pos <= data_count; mem_pos++) {
        uint8_t bus_slot = codec_dev_channel_map_get_slot(data_map, (uint8_t)mem_pos);
        uint8_t channel_id = codec_dev_channel_map_get_slot(device_map, bus_slot);
        if (channel_id == 0) {
            return ESP_CODEC_DEV_NOT_SUPPORT;
        }
        codec_dev_channel_map_set_slot(memory_map, (uint8_t)mem_pos, channel_id);
    }
    return codec_dev_order_is_valid(memory_map) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_NOT_SUPPORT;
}

int codec_dev_order_resolve_data_map(const esp_codec_dev_channel_map_t *memory_map,
                                     const esp_codec_dev_channel_map_t *device_map,
                                     esp_codec_dev_channel_map_t *data_map)
{
    if (memory_map == NULL || device_map == NULL || data_map == NULL ||
        codec_dev_order_is_valid(memory_map) == false || codec_dev_order_is_valid(device_map) == false) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    int mem_count = codec_dev_channel_map_count_channels(memory_map);
    data_map->value = 0;
    for (int mem_pos = 1; mem_pos <= mem_count; mem_pos++) {
        uint8_t channel_id = codec_dev_channel_map_get_slot(memory_map, (uint8_t)mem_pos);
        uint8_t bus_slot = codec_dev_channel_map_find_pos(device_map, channel_id);
        if (bus_slot == 0) {
            return ESP_CODEC_DEV_NOT_SUPPORT;
        }
        codec_dev_channel_map_set_slot(data_map, (uint8_t)mem_pos, bus_slot);
    }
    return codec_dev_order_is_valid(data_map) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_NOT_SUPPORT;
}
