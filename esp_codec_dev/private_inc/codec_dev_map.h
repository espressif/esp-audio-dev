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
 * @brief  Get the ID at a memory position (1..8)
 *
 *         Packed nibble read. The ID may be a physical slot or a logical channel,
 *         depending on which layer `map` represents.
 */
static inline uint8_t codec_dev_map_get(const esp_codec_dev_channel_map_t *map, uint8_t mem_pos)
{
    if (map == NULL || mem_pos == 0 || mem_pos > ESP_CODEC_DEV_MAX_MAP_CHANNELS) {
        return 0;
    }
    return (uint8_t)((map->value >> (4 * (mem_pos - 1))) & 0xFu);
}

/**
 * @brief  Set the ID at a memory position (1..8)
 */
static inline void codec_dev_map_set(esp_codec_dev_channel_map_t *map, uint8_t mem_pos, uint8_t id)
{
    if (map == NULL || mem_pos == 0 || mem_pos > ESP_CODEC_DEV_MAX_MAP_CHANNELS) {
        return;
    }
    uint32_t shift = 4 * (mem_pos - 1);
    map->value &= ~(0xFu << shift);
    map->value |= ((uint32_t)(id & 0xFu) << shift);
}

/**
 * @brief  Count used memory positions from ch1 until the first unused entry
 */
static inline int codec_dev_map_count(const esp_codec_dev_channel_map_t *map)
{
    if (map == NULL) {
        return 0;
    }
    int count = 0;
    for (uint8_t mem_pos = 1; mem_pos <= ESP_CODEC_DEV_MAX_MAP_CHANNELS; mem_pos++) {
        if (codec_dev_map_get(map, mem_pos) == 0) {
            break;
        }
        count++;
    }
    return count;
}

/**
 * @brief  Find the memory position of a given ID
 */
static inline uint8_t codec_dev_map_find_pos(const esp_codec_dev_channel_map_t *map, uint8_t id)
{
    if (map == NULL || id == 0) {
        return 0;
    }
    for (uint8_t mem_pos = 1; mem_pos <= ESP_CODEC_DEV_MAX_MAP_CHANNELS; mem_pos++) {
        if (codec_dev_map_get(map, mem_pos) == id) {
            return mem_pos;
        }
    }
    return 0;
}

/**
 * @brief  Check whether a packed nibble map is valid
 *
 *         Layer-neutral: `map` may be a device, data, or memory map. Used positions
 *         must form a dense prefix from ch1 with unique IDs in 1..8. Subsets such
 *         as {1,3,4} are allowed. A device frame permutation is checked by
 *         codec_dev_map_validate_device() instead.
 *
 * @param[in]  map  Packed nibble map
 *
 * @return
 *       - true   The map is valid
 *       - false  map is NULL or contains invalid IDs
 */
bool codec_dev_map_is_valid(const esp_codec_dev_channel_map_t *map);

/**
 * @brief  Pack a bit mask into a dense map from memory position 1
 *
 *         Bit i of `mask` maps to ID i+1. Selected IDs are written in ascending
 *         ID order as a dense prefix. Domain-neutral: IDs may be slots or channels.
 *         Applied to a slot_mask this is the canonical data_map.
 *
 * @param[in]   mask      Selected IDs as bits 0..id_count-1; must be non-zero
 * @param[in]   id_count  ID range (1..id_count); must be 1..ESP_CODEC_DEV_MAX_MAP_CHANNELS
 * @param[out]  map       Dense packed map; caller retains ownership
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  map is NULL, mask is 0, id_count is out of range,
 *                                    or mask has bits beyond id_count
 */
int codec_dev_map_from_mask(uint16_t mask, uint8_t id_count, esp_codec_dev_channel_map_t *map);

/**
 * @brief  Collect IDs from a packed map into a bit mask
 *
 *         Does not apply a device mapping. Each non-zero nibble is treated as
 *         an ID in 1..id_count. Zero nibbles are skipped. Order is ignored.
 *
 * @param[in]   map       Packed map of IDs; caller retains ownership
 * @param[in]   id_count  Valid ID range (1..id_count)
 * @param[out]  mask      Bit i is set when ID i+1 appears in map
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  map or mask is NULL, id_count is out of range,
 *                                    map is empty, an ID is out of range, or an ID is duplicated
 */
int codec_dev_map_to_mask(const esp_codec_dev_channel_map_t *map, uint8_t id_count, uint16_t *mask);

/**
 * @brief  Validate a dense device frame map
 *
 *         The map must contain IDs 1..N exactly once in a dense prefix.
 *
 * @param[in]  device_map  Codec frame map (logical channel ID at each physical slot)
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid map
 */
int codec_dev_map_validate_device(const esp_codec_dev_channel_map_t *device_map);

/**
 * @brief  Resolve a physical slot index from a logical channel ID (1-based)
 *
 * @param[in]   device_map  Device frame map (logical channel ID at each slot)
 * @param[in]   channel_id  Logical channel ID to look up (1-based)
 * @param[out]  slot        Physical slot index (1-based)
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  slot is NULL, channel_id is 0, device_map is invalid,
 *                                    or channel_id exceeds the map slot count
 *       - ESP_CODEC_DEV_NOT_SUPPORT  channel_id is not present in device_map
 */
int codec_dev_map_channel_id_to_slot(const esp_codec_dev_channel_map_t *device_map,
                                     uint8_t channel_id, uint8_t *slot);

/**
 * @brief  Compose a memory map from a data map and a device map
 *
 *         memory[i] = device_map[ data_map[i] ]
 *
 * @param[in]   data_map    DMA position to physical slot
 * @param[in]   device_map  Physical slot to logical channel ID
 * @param[out]  memory_map  DMA position to logical channel ID
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  data_map, device_map, or memory_map is invalid
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Data map is not supported by the device map
 */
int codec_dev_map_data_to_memory(const esp_codec_dev_channel_map_t *data_map,
                                 const esp_codec_dev_channel_map_t *device_map,
                                 esp_codec_dev_channel_map_t *memory_map);

/**
 * @brief  Resolve a data map from a memory map and a device map
 *
 *         data[i] = slot of memory[i] in device_map
 *
 * @param[in]   memory_map  DMA position to logical channel ID
 * @param[in]   device_map  Physical slot to logical channel ID
 * @param[out]  data_map    DMA position to physical slot
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid input
 *       - ESP_CODEC_DEV_NOT_SUPPORT  memory_map is not reachable from device_map
 */
int codec_dev_map_memory_to_data(const esp_codec_dev_channel_map_t *memory_map,
                                 const esp_codec_dev_channel_map_t *device_map,
                                 esp_codec_dev_channel_map_t *data_map);

/**
 * @brief  Convert a physical slot mask into a memory map
 *
 *         Shortcut for data_map = from_mask(slot_mask): selected logical channel IDs
 *         are packed in ascending slot order. A zero slot_mask returns an empty map.
 *         The selected channel set is the IDs in memory_map; use codec_dev_map_to_mask()
 *         if a bit mask is needed.
 *
 * @param[in]   device_map  Device frame map (logical channel ID at each slot)
 * @param[in]   slot_mask   Enabled physical slots as bits 0..N-1
 * @param[out]  memory_map  Dense packed map of selected logical channel IDs in slot order
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  memory_map is NULL, device_map is invalid, or slot_mask
 *                                    has bits beyond the map slot count
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Selected channel count exceeds map capacity
 */
int codec_dev_map_slots_to_memory(const esp_codec_dev_channel_map_t *device_map,
                                  uint16_t slot_mask,
                                  esp_codec_dev_channel_map_t *memory_map);

/**
 * @brief  Convert a memory map into a physical slot mask
 *
 *         Each logical channel ID in memory_map is translated to a physical slot
 *         through device_map. memory_map must keep slot-scan order; do not rebuild
 *         it with from_mask. An empty memory_map returns slot_mask 0.
 *
 * @param[in]   device_map  Device frame map (logical channel ID at each slot)
 * @param[in]   memory_map  Dense packed map of selected logical channel IDs
 * @param[out]  slot_mask   Physical slots as bits 0..N-1
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  slot_mask or memory_map is NULL, or device_map is invalid
 *       - ESP_CODEC_DEV_NOT_SUPPORT  A logical channel in memory_map is not present in device_map
 */
int codec_dev_map_memory_to_slots(const esp_codec_dev_channel_map_t *device_map,
                                  const esp_codec_dev_channel_map_t *memory_map,
                                  uint16_t *slot_mask);

/**
 * @brief  Check whether one memory map contains all logical channels from another map
 *
 * @param[in]  superset_memory  Map expected to contain the channels
 * @param[in]  subset_memory    Map whose channels are checked
 *
 * @return
 *       - true   superset_memory contains every logical channel in subset_memory
 *       - false  One or more logical channels from subset_memory are missing
 */
bool codec_dev_map_contains(const esp_codec_dev_channel_map_t *superset_memory,
                            const esp_codec_dev_channel_map_t *subset_memory);

/**
 * @brief  Convert requested channel labels to a memory map
 *
 * @note  Duplicate names are allowed. Each requested token binds the next unused
 *        matching board channel (left to right).
 *
 * @param[in]   board_labels      Board channel label list
 * @param[in]   requested_labels  Requested channel label list
 * @param[out]  memory_map        Converted memory map
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid label list, missing label, or memory_map is NULL
 */
int codec_dev_map_from_labels(const char *board_labels, const char *requested_labels,
                              esp_codec_dev_channel_map_t *memory_map);

/**
 * @brief  Convert a memory map to channel labels
 *
 * @param[in]   board_labels    Board channel label list
 * @param[in]   memory_map      Memory map to convert
 * @param[out]  label_buf       Output label buffer
 * @param[in]   label_buf_size  Output label buffer size
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid input, output buffer too small, or label is not found
 */
int codec_dev_map_to_labels(const char *board_labels, const esp_codec_dev_channel_map_t *memory_map,
                            char *label_buf, int label_buf_size);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
