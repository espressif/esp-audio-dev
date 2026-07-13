/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "esp_codec_dev_types.h"
#include "audio_codec_if.h"
#include "audio_codec_data_if.h"
#include "esp_codec_dev_uac.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Binding between an esp_codec_dev handle and a physical UAC device
 *
 *         Shared by the codec interface and the data interface that make up a single
 *         derived esp_codec_dev. Carries the selection criteria, the requested sample
 *         format and the resolved USB address (cached after the first resolution).
 */
typedef struct {
    esp_codec_dev_type_t             dev_type;       /*!< Wanted direction */
    esp_codec_dev_uac_select_mode_t  mode;           /*!< Selection strategy */
    esp_codec_dev_sample_info_t      req_fs;         /*!< Requested sample format */
    uint8_t                          sel_addr;       /*!< Wanted USB address (ESP_CODEC_DEV_UAC_SELECT_BY_ADDR) */
    uint8_t                          resolved_addr;  /*!< Resolved USB address (0 = unresolved) */
    bool                             resolved;       /*!< Whether the binding has been resolved */
} uac_binding_t;

static inline int esp_err_to_codec_err(esp_err_t ret)
{
    switch (ret) {
        case ESP_OK:
            return ESP_CODEC_DEV_OK;
        case ESP_ERR_INVALID_ARG:
            return ESP_CODEC_DEV_INVALID_ARG;
        case ESP_ERR_INVALID_STATE:
            return ESP_CODEC_DEV_WRONG_STATE;
        case ESP_ERR_NOT_FOUND:
            return ESP_CODEC_DEV_NOT_FOUND;
        case ESP_ERR_TIMEOUT:
            return ESP_CODEC_DEV_TIMEOUT;
        case ESP_ERR_NO_MEM:
            return ESP_CODEC_DEV_NO_MEM;
        default:
            return ESP_CODEC_DEV_DRV_ERR;
    }
}

/**
 * @brief  Create a UAC-backed codec interface pair member for one binding
 *
 *         Called by the UAC manager when constructing a derived esp_codec_dev.
 *         The returned interface delegates control operations to the shared host layer.
 *
 * @param[in]  binding  Shared binding; must outlive the returned interface
 *
 * @return
 *       - Codec  interface on success, or NULL on failure
 */
const audio_codec_if_t *uac_codec_new(uac_binding_t *binding);

/**
 * @brief  Create a UAC-backed data interface pair member for one binding
 *
 *         Called by the UAC manager when constructing a derived esp_codec_dev.
 *         The returned interface delegates read/write to the shared host layer.
 *
 * @param[in]  binding  Shared binding; must outlive the returned interface
 *
 * @return
 *       - Data  interface on success, or NULL on failure
 */
const audio_codec_data_if_t *audio_codec_new_uac_data(uac_binding_t *binding);

/**
 * @brief  Initialize the shared UAC host context
 *
 *         Reference counted; paired with audio_codec_uac_host_deinit().
 *         cfg may be NULL to keep zero-initialized defaults.
 *
 * @param[in]  cfg  Host configuration snapshot
 *
 * @return
 *       - ESP_OK  On success
 */
esp_err_t audio_codec_uac_host_init(const esp_codec_dev_uac_cfg_t *cfg);

/**
 * @brief  Tear down the shared UAC host context
 *
 *         Decrements the reference count. The UAC driver and device slots are released
 *         only when the last reference is dropped.
 *
 * @return
 *       - ESP_CODEC_DEV_OK  On success
 */
esp_err_t audio_codec_uac_host_deinit(void);

/**
 * @brief  Query capabilities for a binding and direction
 *
 *         Resolves the binding if needed. When caps is NULL or *count is 0, only the
 *         required entry count is written to *count.
 *
 * @param[in]      b         Binding to query
 * @param[in]      dev_type  Direction mask (IN, OUT, or IN_OUT)
 * @param[out]     caps      Capability array; may be NULL for size query
 * @param[in,out]  count     Input capacity; output filled entry count
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_NOT_FOUND    Device not connected or selection failed
 *       - ESP_CODEC_DEV_NO_MEM       caps buffer too small
 */
esp_err_t audio_codec_uac_get_caps(uac_binding_t *b, esp_codec_dev_type_t dev_type,
                                   esp_codec_dev_capability_t *caps, int *count);

/**
 * @brief  Store the requested sample format on a binding
 *
 *         Applied when the corresponding stream is opened or re-enabled.
 *
 * @param[in]  b         Binding to update
 * @param[in]  dev_type  Direction (stored for later open; not validated here)
 * @param[in]  fs        Requested sample format
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 */
esp_err_t audio_codec_uac_set_fmt(uac_binding_t *b, esp_codec_dev_type_t dev_type,
                                  const esp_codec_dev_sample_info_t *fs);

/**
 * @brief  Open or close UAC streams for a binding
 *
 *         On enable, resolves the binding, opens the UAC device, and starts the
 *         requested direction(s). On disable, closes the matching role(s).
 *
 * @param[in]  b         Binding to control
 * @param[in]  dev_type  Direction mask to enable or disable
 * @param[in]  enable    true to open/start, false to close
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_NOT_FOUND    Device not connected or selection failed
 */
esp_err_t audio_codec_uac_enable(uac_binding_t *b, esp_codec_dev_type_t dev_type, bool enable);

/**
 * @brief  Check whether a binding has an opened UAC stream
 *
 * @param[in]  b  Binding to query
 *
 * @return
 *       - true  if RX or TX role is opened on the resolved device
 */
bool audio_codec_uac_is_open(uac_binding_t *b);

/**
 * @brief  Read PCM data from the UAC capture stream of a binding
 *
 * @param[in]   b     Binding to read from
 * @param[out]  data  Output buffer
 * @param[in]   size  Number of bytes to read
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Requested size read
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_WRONG_STATE  RX stream not opened
 *       - ESP_CODEC_DEV_DRV_ERR      Short read or driver failure
 */
esp_err_t audio_codec_uac_read(uac_binding_t *b, uint8_t *data, int size);

/**
 * @brief  Write PCM data to the UAC playback stream of a binding
 *
 * @param[in]  b     Binding to write to
 * @param[in]  data  Input buffer
 * @param[in]  size  Number of bytes to write
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_WRONG_STATE  TX stream not opened
 */
esp_err_t audio_codec_uac_write(uac_binding_t *b, uint8_t *data, int size);

/**
 * @brief  Set hardware mute on a UAC stream
 *
 * @param[in]  b         Binding to control
 * @param[in]  dev_type  Direction (maps to RX for IN, TX for OUT)
 * @param[in]  mute      true to mute, false to unmute
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_WRONG_STATE  Stream not opened
 */
esp_err_t audio_codec_uac_set_mute(uac_binding_t *b, esp_codec_dev_type_t dev_type, bool mute);

/**
 * @brief  Set playback volume on a UAC stream in dB
 *
 *         Converts dB to the UAC host volume range before applying.
 *
 * @param[in]  b         Binding to control
 * @param[in]  dev_type  Direction (maps to RX for IN, TX for OUT)
 * @param[in]  db        Target volume in dB
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_WRONG_STATE  Stream not opened
 */
esp_err_t audio_codec_uac_set_volume_db(uac_binding_t *b, esp_codec_dev_type_t dev_type, float db);

/**
 * @brief  Get the number of connected UAC devices in the host layer
 *
 * @param[out]  num  Number of connected devices
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  num is NULL
 */
esp_err_t audio_codec_uac_get_dev_num(uint8_t *num);

/**
 * @brief  Get basic information for a connected UAC device by list index
 *
 * @param[in]   index  Ordinal among connected devices (0-based)
 * @param[out]  info   Device information to fill
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  info is NULL
 *       - ESP_ERR_NOT_FOUND    index is out of range
 */
esp_err_t audio_codec_uac_get_dev_info(uint8_t index, esp_codec_dev_uac_info_t *info);

/**
 * @brief  Platform-internal UAC host lifecycle event callback
 *
 * @param[in]  info  Event information; valid only for the duration of the callback
 * @param[in]  ctx   User context registered with audio_codec_uac_host_set_event_cb()
 */
typedef void (*audio_codec_uac_event_cb_t)(const esp_codec_dev_uac_event_info_t *info, void *ctx);

/**
 * @brief  Register a platform-internal UAC host lifecycle event callback
 *
 *         Used by the UAC manager to observe connect/disconnect independently of the
 *         application callback in esp_codec_dev_uac_cfg_t. Only one callback may be registered
 *         at a time. Pass NULL to clear.
 *
 * @param[in]  cb   Internal event callback, or NULL
 * @param[in]  ctx  User context passed to cb
 *
 * @return
 *       - ESP_OK  On success
 */
esp_err_t audio_codec_uac_host_set_event_cb(audio_codec_uac_event_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
