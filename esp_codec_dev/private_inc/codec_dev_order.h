/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_codec_dev_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Get the slot / channel ID at a memory position (1..8)
 */
static inline uint8_t codec_dev_channel_map_get_slot(const esp_codec_dev_channel_map_t *map, uint8_t mem_pos)
{
    if (map == NULL || mem_pos == 0 || mem_pos > 8) {
        return 0;
    }
    return (uint8_t)((map->value >> (4 * (mem_pos - 1))) & 0xFu);
}

/**
 * @brief  Set the slot / channel ID at a memory position (1..8)
 */
static inline void codec_dev_channel_map_set_slot(esp_codec_dev_channel_map_t *map, uint8_t mem_pos, uint8_t slot)
{
    if (map == NULL || mem_pos == 0 || mem_pos > 8) {
        return;
    }
    uint32_t shift = 4 * (mem_pos - 1);
    map->value &= ~(0xFu << shift);
    map->value |= ((uint32_t)(slot & 0xFu) << shift);
}

/**
 * @brief  Count used memory positions from ch1 until the first unused entry
 */
static inline int codec_dev_channel_map_count_channels(const esp_codec_dev_channel_map_t *map)
{
    if (map == NULL) {
        return 0;
    }
    int count = 0;
    for (uint8_t mem_pos = 1; mem_pos <= 8; mem_pos++) {
        if (codec_dev_channel_map_get_slot(map, mem_pos) == 0) {
            break;
        }
        count++;
    }
    return count;
}

/**
 * @brief  Find the memory position of a given slot / channel ID
 */
static inline uint8_t codec_dev_channel_map_find_pos(const esp_codec_dev_channel_map_t *map, uint8_t slot)
{
    if (map == NULL || slot == 0) {
        return 0;
    }
    for (uint8_t mem_pos = 1; mem_pos <= 8; mem_pos++) {
        if (codec_dev_channel_map_get_slot(map, mem_pos) == slot) {
            return mem_pos;
        }
    }
    return 0;
}

/**
 * @brief  Check whether used memory positions form a dense prefix from ch1
 */
static inline bool codec_dev_channel_map_has_dense_prefix(const esp_codec_dev_channel_map_t *map)
{
    if (map == NULL || codec_dev_channel_map_get_slot(map, 1) == 0) {
        return false;
    }
    bool seen_zero = false;
    for (uint8_t mem_pos = 1; mem_pos <= 8; mem_pos++) {
        uint8_t slot = codec_dev_channel_map_get_slot(map, mem_pos);
        if (slot == 0) {
            seen_zero = true;
        } else if (seen_zero) {
            return false;
        }
    }
    return true;
}

/**
 * @brief  Check whether a channel map is valid
 *
 * @param[in]  map  Channel map to validate
 *
 * @return
 *       - true   The map is valid
 *       - false  map is NULL or contains invalid slot indexes
 */
bool codec_dev_order_is_valid(const esp_codec_dev_channel_map_t *map);

/**
 * @brief  Check whether one channel map contains all logical channels from another map
 *
 * @param[in]  superset_map  Map expected to contain the channels
 * @param[in]  subset_map    Map whose channels are checked
 *
 * @return
 *       - true   superset_map contains every logical channel in subset_map
 *       - false  One or more logical channels from subset_map are missing
 */
bool codec_dev_order_contains(const esp_codec_dev_channel_map_t *superset_map,
                              const esp_codec_dev_channel_map_t *subset_map);

/**
 * @brief  Convert requested channel labels to a memory channel map
 *
 * @param[in]   board_labels      Board channel label list
 * @param[in]   requested_labels  Requested channel label list
 * @param[out]  map               Converted channel map
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid label list, missing label, or map is NULL
 */
int codec_dev_order_from_labels(const char *board_labels, const char *requested_labels,
                                esp_codec_dev_channel_map_t *map);

/**
 * @brief  Convert a memory channel map to channel labels
 *
 * @param[in]   board_labels    Board channel label list
 * @param[in]   map             Channel map to convert
 * @param[out]  label_buf       Output label buffer
 * @param[in]   label_buf_size  Output label buffer size
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid input, output buffer too small, or label is not found
 */
int codec_dev_order_to_labels(const char *board_labels, const esp_codec_dev_channel_map_t *map,
                              char *label_buf, int label_buf_size);

/**
 * @brief  Resolve memory channel map from data-interface and device channel maps
 *
 * @param[in]   data_map    Data-interface logical-channel to slot map
 * @param[in]   device_map  Codec logical-channel to slot map
 * @param[out]  memory_map  Resolved memory logical-channel to slot map
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  data_map, device_map, or memory_map is invalid
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Data-interface map is not supported by the codec device map
 */
int codec_dev_order_resolve_memory_map(const esp_codec_dev_channel_map_t *data_map,
                                       const esp_codec_dev_channel_map_t *device_map,
                                       esp_codec_dev_channel_map_t *memory_map);

/**
 * @brief  Resolve data-interface channel map from memory and device channel maps
 *
 * @param[in]   memory_map  Desired memory logical-channel to slot map
 * @param[in]   device_map  Codec logical-channel to slot map
 * @param[out]  data_map    Data-interface map that produces memory_map through device_map
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid input
 *       - ESP_CODEC_DEV_NOT_SUPPORT  memory_map is not reachable from device_map
 */
int codec_dev_order_resolve_data_map(const esp_codec_dev_channel_map_t *memory_map,
                                     const esp_codec_dev_channel_map_t *device_map,
                                     esp_codec_dev_channel_map_t *data_map);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
