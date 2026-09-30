/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_codec_dev_types.h"
#include "codec_dev_priv.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define ESP_CODEC_DEV_STD_4CH_MAP  (ESP_CODEC_DEV_CHANNEL_MAP_4CH(3, 1, 4, 2))

/**
 * @brief  Result of map-query registration during open
 *
 *         `in_query` / `out_query` are true only when that direction registered a TDM map query.
 *         They are not the same as `input_opened` / `output_opened`.
 */
typedef struct {
    bool                         in_query;          /*!< IN registered a TDM map query */
    bool                         out_query;         /*!< OUT registered a TDM map query */
    esp_codec_dev_channel_map_t  expected_in_map;   /*!< Expected IN memory map; 0 when in_query is false */
    esp_codec_dev_channel_map_t  expected_out_map;  /*!< Expected OUT memory map; 0 when out_query is false */
} codec_dev_layout_plan_t;

/**
 * @brief  Return whether map-query planning can be used for one direction
 *
 * @param[in]  dev   Codec device instance
 * @param[in]  dir   IN or OUT
 * @param[in]  mode  Current I2S mode for that direction
 *
 * @return
 *       - true   TDM map query is available (TDM Philips, callbacks, and a 2-slot order-table row)
 *       - false  Use the legacy path
 */
bool codec_dev_layout_can_use_map_query(codec_dev_t *dev, esp_codec_dev_type_t dir,
                                        esp_codec_dev_i2s_mode_t mode);

/**
 * @brief  Clear the device-map query for one direction
 *
 *         The matching registration is done inside codec_dev_layout_prepare_open();
 *         there is no standalone register entry point.
 *
 * @note  Idempotent: does nothing when this direction has no registered query. Called on
 *        close and on the open failure path.
 *
 * @param[in]  dev  Codec device instance
 * @param[in]  dir  IN or OUT
 */
void codec_dev_layout_clear_map_query(codec_dev_t *dev, esp_codec_dev_type_t dir);

/**
 * @brief  Register map queries and resolve expected memory map for open
 *
 *         Call after `dev->input_opened` / `dev->output_opened` are set. Directions that cannot
 *         use map query stay on the legacy path (`*_query` remains false).
 *
 * @note  On error the helper clears any query it registered. `open` cleanup may clear
 *        again; codec_dev_layout_clear_map_query() is idempotent.
 *
 * @param[in]   dev     Codec device instance
 * @param[in]   app_fs  Application sample format (not mutated)
 * @param[out]  plan    Query flags and expected memory maps; zeroed on entry
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Ready for set_fmt; plan is populated
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid argument
 *       - ESP_CODEC_DEV_NOT_SUPPORT  IN and OUT map-query maps differ, or register/resolve failed
 */
int codec_dev_layout_prepare_open(codec_dev_t *dev, const esp_codec_dev_sample_info_t *app_fs,
                                  codec_dev_layout_plan_t *plan);

/**
 * @brief  Commit the expected map-query memory map after hardware open succeeds
 *
 *         IN query takes precedence on a shared-format handle. When neither query ran, `cur_map`
 *         is left unchanged so the caller can fall back to `resolve_map_from_fs`.
 *
 * @param[in]  dev   Codec device instance
 * @param[in]  plan  Result of codec_dev_layout_prepare_open()
 */
void codec_dev_layout_commit_open_map(codec_dev_t *dev, const codec_dev_layout_plan_t *plan);

/**
 * @brief  Resolve memory map from the application sample format and I2S mode
 *
 * @param[in]   dev   Codec device instance
 * @param[in]   fs    Application sample format
 * @param[in]   mode  Current I2S mode
 * @param[out]  map   Memory map
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Map resolved
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid argument
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Map cannot be derived
 */
int codec_dev_layout_resolve_map_from_fs(codec_dev_t *dev, const esp_codec_dev_sample_info_t *fs,
                                         esp_codec_dev_i2s_mode_t mode, esp_codec_dev_channel_map_t *map);

/**
 * @brief  Resolve memory map from the live bus configuration
 *
 * @param[in]   dev         Codec device instance
 * @param[in]   dir         IN or OUT
 * @param[out]  memory_map  Memory map
 * @param[out]  bus_info    Optional live bus info
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Map resolved
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Bus or order table is unavailable
 */
int codec_dev_layout_resolve_map_from_bus(codec_dev_t *dev, esp_codec_dev_type_t dir,
                                          esp_codec_dev_channel_map_t *memory_map,
                                          esp_codec_dev_bus_info_t *bus_info);

/**
 * @brief  Read requested and current memory maps from the handle
 *
 * @param[in]   dev            Codec device instance
 * @param[out]  requested_map  Optional requested map
 * @param[out]  current_map    Optional current map
 * @param[out]  need_convert   Optional true when software conversion is required
 */
void codec_dev_layout_get_maps(codec_dev_t *dev, esp_codec_dev_channel_map_t *requested_map,
                               esp_codec_dev_channel_map_t *current_map, bool *need_convert);

/**
 * @brief  Return the map that read and write should present to the application
 *
 *         When software conversion is active this is the requested map; otherwise the current map.
 *
 * @param[in]   dev  Codec device instance
 * @param[out]  map  Application-visible memory map
 *
 * @return
 *       - ESP_CODEC_DEV_OK         Map populated
 *       - ESP_CODEC_DEV_NOT_FOUND  No current layout is known
 */
int codec_dev_layout_get_app_map(codec_dev_t *dev, esp_codec_dev_channel_map_t *map);

/**
 * @brief  Resolve the hardware sample format that expresses a requested memory map
 *
 *         Pure query: no hardware is touched, so a failure only means the map cannot be expressed
 *         by a bus format and the caller may still fall back to software layout conversion.
 *
 * @param[in]   dev    Codec device instance
 * @param[in]   map    Requested memory map
 * @param[out]  hw_fs  Current format with channel count and mask replaced; undefined on error
 *
 * @return
 *       - ESP_CODEC_DEV_OK           hw_fs is populated
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid argument
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Device is not open, mode query failed, or no order-table row
 *                                    can express the map
 */
int codec_dev_layout_resolve_hw_fs(codec_dev_t *dev, const esp_codec_dev_channel_map_t *map,
                                   esp_codec_dev_sample_info_t *hw_fs);

/**
 * @brief  Apply a resolved hardware sample format so the bus matches a requested memory map
 *
 * @note  Call only with an `hw_fs` from codec_dev_layout_resolve_hw_fs(). This touches the audio
 *        path, so any error must be reported to the application instead of being treated as a
 *        reason to fall back to software conversion. Restore on failure is best effort; when it
 *        cannot bring the bus back the error is logged and the device must be closed and reopened.
 *
 * @param[in]  dev    Codec device instance
 * @param[in]  map    Requested memory map
 * @param[in]  hw_fs  Format resolved by codec_dev_layout_resolve_hw_fs()
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Hardware matches the map
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid argument
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Codec rejected the format
 *       - ESP_CODEC_DEV_WRONG_STATE  Data interface is not open
 *       - ESP_CODEC_DEV_DRV_ERR      Codec or data interface enable or format failed
 */
int codec_dev_layout_reconfigure_hw(codec_dev_t *dev, const esp_codec_dev_channel_map_t *map,
                                    const esp_codec_dev_sample_info_t *hw_fs);

/**
 * @brief  Read PCM, applying software layout conversion when needed
 *
 * @note  Caller has already validated handle, buffer, length, and that input is open.
 *
 * @param[in]   dev   Codec device instance
 * @param[out]  data  Application buffer
 * @param[in]   len   Buffer length in bytes
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Read succeeded
 *       - ESP_CODEC_DEV_INVALID_ARG  Length is not frame-aligned or maps are invalid
 *       - ESP_CODEC_DEV_NO_MEM       Conversion buffer allocation failed
 *       - ESP_CODEC_DEV_WRONG_STATE  Data interface is not open
 *       - ESP_CODEC_DEV_TIMEOUT      Data interface read timed out
 *       - ESP_CODEC_DEV_DRV_ERR      Data interface read failed
 */
int codec_dev_layout_read(codec_dev_t *dev, void *data, int len);

/**
 * @brief  Write PCM, applying software layout conversion when needed
 *
 * @note  Caller has already validated handle, buffer, length, and that output is open.
 *
 * @param[in]  dev   Codec device instance
 * @param[in]  data  Application buffer
 * @param[in]  len   Buffer length in bytes
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Write succeeded
 *       - ESP_CODEC_DEV_INVALID_ARG  Length is not frame-aligned or maps are invalid
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Write callback is missing
 *       - ESP_CODEC_DEV_NO_MEM       Conversion buffer allocation failed
 *       - ESP_CODEC_DEV_WRONG_STATE  Data interface is not open
 *       - ESP_CODEC_DEV_TIMEOUT      Data interface write timed out
 *       - ESP_CODEC_DEV_DRV_ERR      Data interface write failed
 */
int codec_dev_layout_write(codec_dev_t *dev, void *data, int len);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
