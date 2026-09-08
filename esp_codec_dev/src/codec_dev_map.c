/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "esp_log.h"

#include "audio_codec_adc_label.h"
#include "codec_dev_map.h"

#define CODEC_DEV_ORDER_MAX_LABEL_COUNT  (8)
#define CODEC_DEV_ORDER_MAX_LABEL_LEN    (16)
#define CODEC_DEV_ORDER_MAX_MAP_POS      ESP_CODEC_DEV_MAX_MAP_CHANNELS
#define CODEC_DEV_ADC_LABEL_UNUSED       "NA"

typedef struct {
    char     label[CODEC_DEV_ORDER_MAX_LABEL_COUNT][CODEC_DEV_ORDER_MAX_LABEL_LEN];
    uint8_t  count;
} codec_label_list_t;

static const char *const s_adc_label_tokens[] = {
    "FC", "RE", "FL", "FR", "SL", "SR", "BL", "BR", CODEC_DEV_ADC_LABEL_UNUSED,
};

static const char *TAG = "ADEV_MAP";

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

static bool adc_label_token_is_supported(const char *token)
{
    if (token == NULL) {
        return false;
    }
    for (size_t i = 0; i < sizeof(s_adc_label_tokens) / sizeof(s_adc_label_tokens[0]); i++) {
        if (strcmp(token, s_adc_label_tokens[i]) == 0) {
            return true;
        }
    }
    return false;
}

static int find_unused_label_index(const codec_label_list_t *list, const char *label, uint16_t used)
{
    if (list == NULL || label == NULL) {
        return -1;
    }
    for (int i = 0; i < list->count; i++) {
        if ((used & (uint16_t)(1U << i)) == 0 && strcmp(list->label[i], label) == 0) {
            return i;
        }
    }
    return -1;
}

static inline uint16_t codec_dev_map_make_low_mask(uint8_t bit_count)
{
    if (bit_count == 0) {
        return 0;
    }
    if (bit_count >= 16) {
        return UINT16_MAX;
    }
    return (uint16_t)((1U << bit_count) - 1U);
}

static bool codec_dev_map_has_dense_prefix(const esp_codec_dev_channel_map_t *map)
{
    if (map == NULL || codec_dev_map_get(map, 1) == 0) {
        return false;
    }
    bool seen_zero = false;
    for (uint8_t mem_pos = 1; mem_pos <= ESP_CODEC_DEV_MAX_MAP_CHANNELS; mem_pos++) {
        uint8_t id = codec_dev_map_get(map, mem_pos);
        if (id == 0) {
            seen_zero = true;
        } else if (seen_zero) {
            return false;
        }
    }
    return true;
}

static int codec_dev_map_validate_mask_ids(uint16_t mask, const esp_codec_dev_channel_map_t *map,
                                           uint8_t id_count)
{
    if (map == NULL || id_count == 0 || id_count > ESP_CODEC_DEV_MAX_MAP_CHANNELS) {
        ESP_LOGE(TAG, "Validate packed map failed: invalid argument");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    uint16_t valid_mask = codec_dev_map_make_low_mask(id_count);
    if ((mask & ~valid_mask) != 0) {
        ESP_LOGE(TAG, "Validate packed map failed: mask 0x%04x exceeds ID count %u", mask, id_count);
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (map->value != 0 && codec_dev_map_has_dense_prefix(map) == false) {
        ESP_LOGE(TAG, "Validate packed map failed: map is not a dense prefix");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    uint16_t order_mask = 0;
    for (uint8_t mem_pos = 1; mem_pos <= CODEC_DEV_ORDER_MAX_MAP_POS; mem_pos++) {
        uint8_t channel_id = codec_dev_map_get(map, mem_pos);
        if (channel_id == 0) {
            break;
        }
        if (channel_id > id_count) {
            ESP_LOGE(TAG, "Validate packed map failed: ID %u exceeds count %u", channel_id, id_count);
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        uint16_t bit = (uint16_t)(1U << (channel_id - 1));
        if ((mask & bit) == 0 || (order_mask & bit) != 0) {
            ESP_LOGE(TAG, "Validate packed map failed: ID %u is missing or duplicated", channel_id);
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        order_mask |= bit;
    }
    if (order_mask != mask) {
        ESP_LOGE(TAG, "Validate packed map failed: map does not match mask 0x%04x", mask);
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    return ESP_CODEC_DEV_OK;
}

int audio_codec_adc_label_parse(const char *label, uint16_t *mic_mask, uint8_t *channel_num)
{
    if (mic_mask == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *mic_mask = 0;
    if (channel_num != NULL) {
        *channel_num = 0;
    }
    if (label == NULL || label[0] == '\0') {
        *mic_mask = UINT16_MAX;
        return ESP_CODEC_DEV_OK;
    }
    codec_label_list_t list = {0};
    if (parse_label_list(label, &list) == false) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    for (int i = 0; i < list.count; i++) {
        if (adc_label_token_is_supported(list.label[i]) == false) {
            ESP_LOGE(TAG, "Unsupported ADC label token: %s", list.label[i]);
            *mic_mask = 0;
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        if (strcmp(list.label[i], CODEC_DEV_ADC_LABEL_UNUSED) != 0) {
            *mic_mask |= (uint16_t)(1U << i);
        }
    }
    if (channel_num != NULL) {
        *channel_num = list.count;
    }
    return ESP_CODEC_DEV_OK;
}

bool codec_dev_map_is_valid(const esp_codec_dev_channel_map_t *map)
{
    if (map == NULL) {
        ESP_LOGE(TAG, "Packed map is NULL");
        return false;
    }
    if (codec_dev_map_has_dense_prefix(map) == false) {
        ESP_LOGE(TAG, "Packed map must be a dense memory-position prefix from ch1");
        return false;
    }
    uint16_t used_ids = 0;
    for (uint8_t mem_pos = 1; mem_pos <= ESP_CODEC_DEV_MAX_MAP_CHANNELS; mem_pos++) {
        uint8_t id = codec_dev_map_get(map, mem_pos);
        if (id == 0) {
            break;
        }
        if (id > ESP_CODEC_DEV_MAX_MAP_CHANNELS) {
            ESP_LOGE(TAG, "Packed map ID is out of range: %u", id);
            return false;
        }
        uint16_t bit = (uint16_t)(1U << id);
        if (used_ids & bit) {
            ESP_LOGE(TAG, "Packed map has a duplicated ID: %u", id);
            return false;
        }
        used_ids |= bit;
    }
    return true;
}

int codec_dev_map_from_mask(uint16_t mask, uint8_t id_count, esp_codec_dev_channel_map_t *map)
{
    if (map == NULL || mask == 0 || id_count == 0 ||
        id_count > ESP_CODEC_DEV_MAX_MAP_CHANNELS ||
        (mask & ~codec_dev_map_make_low_mask(id_count)) != 0) {
        ESP_LOGE(TAG, "Build map from mask failed: invalid mask or ID count");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    map->value = 0;
    uint8_t mem_pos = 1;
    for (uint8_t id = 1; id <= id_count; id++) {
        if ((mask & (uint16_t)(1U << (id - 1))) == 0) {
            continue;
        }
        codec_dev_map_set(map, mem_pos, id);
        mem_pos++;
    }
    return codec_dev_map_validate_mask_ids(mask, map, id_count);
}

int codec_dev_map_to_mask(const esp_codec_dev_channel_map_t *map, uint8_t id_count, uint16_t *mask)
{
    if (map == NULL || mask == NULL || id_count == 0 ||
        id_count > ESP_CODEC_DEV_MAX_MAP_CHANNELS || map->value == 0) {
        ESP_LOGE(TAG, "Build mask from map failed: invalid argument");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    uint16_t collected = 0;
    for (uint8_t mem_pos = 1; mem_pos <= ESP_CODEC_DEV_MAX_MAP_CHANNELS; mem_pos++) {
        uint8_t id = codec_dev_map_get(map, mem_pos);
        if (id == 0) {
            continue;
        }
        if (id > id_count) {
            ESP_LOGE(TAG, "Build mask from map failed: ID %u exceeds count %u", id, id_count);
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        uint16_t bit = (uint16_t)(1U << (id - 1));
        if (collected & bit) {
            ESP_LOGE(TAG, "Build mask from map failed: duplicated ID %u", id);
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        collected |= bit;
    }

    *mask = collected;
    return ESP_CODEC_DEV_OK;
}

int codec_dev_map_validate_device(const esp_codec_dev_channel_map_t *device_map)
{
    if (device_map == NULL || codec_dev_map_has_dense_prefix(device_map) == false) {
        ESP_LOGE(TAG, "Validate device map failed: map is NULL or not a dense prefix");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int total_slot = codec_dev_map_count(device_map);
    uint16_t used_mask = 0;
    for (uint8_t slot = 1; slot <= total_slot; slot++) {
        uint8_t channel_id = codec_dev_map_get(device_map, slot);
        if (channel_id == 0 || channel_id > total_slot) {
            ESP_LOGE(TAG, "Validate device map failed: channel %u is invalid for %d slots",
                     channel_id, total_slot);
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        uint16_t bit = (uint16_t)(1U << (channel_id - 1));
        if ((used_mask & bit) != 0) {
            ESP_LOGE(TAG, "Validate device map failed: channel %u is duplicated", channel_id);
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        used_mask |= bit;
    }
    if (used_mask != codec_dev_map_make_low_mask((uint8_t)total_slot)) {
        ESP_LOGE(TAG, "Validate device map failed: channel set is incomplete");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    return ESP_CODEC_DEV_OK;
}

int codec_dev_map_channel_id_to_slot(const esp_codec_dev_channel_map_t *device_map,
                                     uint8_t channel_id, uint8_t *slot)
{
    if (slot == NULL || channel_id == 0) {
        ESP_LOGE(TAG, "Resolve channel slot failed: invalid argument");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec_dev_map_validate_device(device_map) != ESP_CODEC_DEV_OK) {
        /* codec_dev_map_validate_device already logged */
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int total_slot = codec_dev_map_count(device_map);
    if (channel_id > total_slot) {
        ESP_LOGE(TAG, "Resolve channel slot failed: channel %u exceeds slot count %d", channel_id, total_slot);
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *slot = codec_dev_map_find_pos(device_map, channel_id);
    return *slot != 0 ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_NOT_SUPPORT;
}

int codec_dev_map_slots_to_memory(const esp_codec_dev_channel_map_t *device_map,
                                  uint16_t slot_mask,
                                  esp_codec_dev_channel_map_t *memory_map)
{
    if (memory_map == NULL) {
        ESP_LOGE(TAG, "Convert slot mask failed: memory map is NULL");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec_dev_map_validate_device(device_map) != ESP_CODEC_DEV_OK) {
        /* codec_dev_map_validate_device already logged */
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int total_slot = codec_dev_map_count(device_map);
    if ((slot_mask & ~codec_dev_map_make_low_mask((uint8_t)total_slot)) != 0) {
        ESP_LOGE(TAG, "Convert slot mask failed: mask 0x%04x exceeds slot count %d", slot_mask, total_slot);
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    memory_map->value = 0;
    uint8_t mem_pos = 1;
    for (uint8_t slot = 1; slot <= total_slot; slot++) {
        if ((slot_mask & (uint16_t)(1U << (slot - 1))) == 0) {
            continue;
        }
        uint8_t channel_id = codec_dev_map_get(device_map, slot);
        if (mem_pos > CODEC_DEV_ORDER_MAX_MAP_POS) {
            ESP_LOGW(TAG, "Convert slot mask: channel count exceeds map capacity");
            return ESP_CODEC_DEV_NOT_SUPPORT;
        }
        codec_dev_map_set(memory_map, mem_pos, channel_id);
        mem_pos++;
    }
    return ESP_CODEC_DEV_OK;
}

int codec_dev_map_memory_to_slots(const esp_codec_dev_channel_map_t *device_map,
                                  const esp_codec_dev_channel_map_t *memory_map,
                                  uint16_t *slot_mask)
{
    if (slot_mask == NULL || memory_map == NULL) {
        ESP_LOGE(TAG, "Convert memory map failed: output pointer is NULL");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec_dev_map_validate_device(device_map) != ESP_CODEC_DEV_OK) {
        /* codec_dev_map_validate_device already logged */
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *slot_mask = 0;
    if (memory_map->value == 0) {
        return ESP_CODEC_DEV_OK;
    }
    if (codec_dev_map_is_valid(memory_map) == false) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    for (uint8_t mem_pos = 1; mem_pos <= CODEC_DEV_ORDER_MAX_MAP_POS; mem_pos++) {
        uint8_t channel_id = codec_dev_map_get(memory_map, mem_pos);
        if (channel_id == 0) {
            break;
        }
        uint8_t slot = 0;
        int ret = codec_dev_map_channel_id_to_slot(device_map, channel_id, &slot);
        if (ret != ESP_CODEC_DEV_OK) {
            /* codec_dev_map_channel_id_to_slot already logged */
            return ret;
        }
        *slot_mask |= (uint16_t)(1U << (slot - 1));
    }
    return ESP_CODEC_DEV_OK;
}

int codec_dev_map_data_to_memory(const esp_codec_dev_channel_map_t *data_map,
                                 const esp_codec_dev_channel_map_t *device_map,
                                 esp_codec_dev_channel_map_t *memory_map)
{
    if (data_map == NULL || device_map == NULL || memory_map == NULL ||
        codec_dev_map_is_valid(data_map) == false || codec_dev_map_is_valid(device_map) == false) {
        ESP_LOGE(TAG, "Data map, device map, or memory map pointer is invalid");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    memory_map->value = 0;
    int data_count = codec_dev_map_count(data_map);
    for (int mem_pos = 1; mem_pos <= data_count; mem_pos++) {
        uint8_t bus_slot = codec_dev_map_get(data_map, (uint8_t)mem_pos);
        uint8_t channel_id = codec_dev_map_get(device_map, bus_slot);
        if (channel_id == 0) {
            return ESP_CODEC_DEV_NOT_SUPPORT;
        }
        codec_dev_map_set(memory_map, (uint8_t)mem_pos, channel_id);
    }
    return codec_dev_map_is_valid(memory_map) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_NOT_SUPPORT;
}

int codec_dev_map_memory_to_data(const esp_codec_dev_channel_map_t *memory_map,
                                 const esp_codec_dev_channel_map_t *device_map,
                                 esp_codec_dev_channel_map_t *data_map)
{
    if (memory_map == NULL || device_map == NULL || data_map == NULL ||
        codec_dev_map_is_valid(memory_map) == false || codec_dev_map_is_valid(device_map) == false) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    int mem_count = codec_dev_map_count(memory_map);
    data_map->value = 0;
    for (int mem_pos = 1; mem_pos <= mem_count; mem_pos++) {
        uint8_t channel_id = codec_dev_map_get(memory_map, (uint8_t)mem_pos);
        uint8_t bus_slot = codec_dev_map_find_pos(device_map, channel_id);
        if (bus_slot == 0) {
            return ESP_CODEC_DEV_NOT_SUPPORT;
        }
        codec_dev_map_set(data_map, (uint8_t)mem_pos, bus_slot);
    }
    return codec_dev_map_is_valid(data_map) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_NOT_SUPPORT;
}

bool codec_dev_map_contains(const esp_codec_dev_channel_map_t *superset_memory,
                            const esp_codec_dev_channel_map_t *subset_memory)
{
    if (subset_memory == NULL) {
        return true;
    }
    if (superset_memory == NULL) {
        return false;
    }
    for (uint8_t mem_pos = 1; mem_pos <= ESP_CODEC_DEV_MAX_MAP_CHANNELS; mem_pos++) {
        uint8_t channel_id = codec_dev_map_get(subset_memory, mem_pos);
        if (channel_id == 0) {
            break;
        }
        if (codec_dev_map_find_pos(superset_memory, channel_id) == 0) {
            return false;
        }
    }
    return true;
}

int codec_dev_map_from_labels(const char *board_labels, const char *requested_labels,
                              esp_codec_dev_channel_map_t *memory_map)
{
    codec_label_list_t board = {0};
    codec_label_list_t requested = {0};
    if (parse_label_list(board_labels, &board) == false ||
        parse_label_list(requested_labels, &requested) == false || memory_map == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    memory_map->value = 0;
    uint16_t used = 0;
    for (int mem_pos = 0; mem_pos < requested.count; mem_pos++) {
        int label_idx = find_unused_label_index(&board, requested.label[mem_pos], used);
        if (label_idx < 0) {
            ESP_LOGE(TAG, "Invalid label: %s, board count: %d", requested.label[mem_pos], board.count);
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        used |= (uint16_t)(1U << label_idx);
        codec_dev_map_set(memory_map, (uint8_t)(mem_pos + 1), (uint8_t)(label_idx + 1));
    }
    return codec_dev_map_is_valid(memory_map) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_INVALID_ARG;
}

int codec_dev_map_to_labels(const char *board_labels, const esp_codec_dev_channel_map_t *memory_map,
                            char *label_buf, int label_buf_size)
{
    codec_label_list_t board = {0};
    if (memory_map == NULL || parse_label_list(board_labels, &board) == false ||
        label_buf == NULL || label_buf_size <= 0 || codec_dev_map_is_valid(memory_map) == false) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int channel_count = codec_dev_map_count(memory_map);
    int used = 0;
    label_buf[0] = '\0';
    for (int mem_pos = 1; mem_pos <= channel_count; mem_pos++) {
        uint8_t channel_id = codec_dev_map_get(memory_map, (uint8_t)mem_pos);
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
