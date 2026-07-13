/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_codec_dev.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  USB UAC manager event type
 */
typedef enum {
    ESP_CODEC_DEV_UAC_EVENT_CONNECTED = 0,  /*!< A UAC device is enumerated and available */
    ESP_CODEC_DEV_UAC_EVENT_DISCONNECTED,   /*!< A UAC device is no longer available */
} esp_codec_dev_uac_event_type_t;

/**
 * @brief  USB UAC device selection mode
 */
typedef enum {
    ESP_CODEC_DEV_UAC_SELECT_FIRST = 0,  /*!< Bind to the first connected device matching the wanted direction */
    ESP_CODEC_DEV_UAC_SELECT_BY_ADDR,    /*!< Bind to the device with a specific USB address */
} esp_codec_dev_uac_select_mode_t;

/**
 * @brief  USB UAC manager event payload
 */
typedef struct {
    esp_codec_dev_uac_event_type_t  event;     /*!< Event type */
    uint8_t                         addr;      /*!< USB device address */
    esp_codec_dev_type_t            dev_type;  /*!< Connected directions: IN / OUT / IN_OUT */
} esp_codec_dev_uac_event_info_t;

/**
 * @brief  USB UAC device selection
 */
typedef struct {
    esp_codec_dev_uac_select_mode_t  mode;      /*!< Selection strategy */
    uint8_t                          addr;      /*!< USB device address, used when mode is ESP_CODEC_DEV_UAC_SELECT_BY_ADDR */
    esp_codec_dev_type_t             dev_type;  /*!< Wanted direction: IN (mic) / OUT (speaker) / IN_OUT */
} esp_codec_dev_uac_select_t;

/**
 * @brief  Connected UAC device snapshot
 */
typedef struct {
    uint8_t               addr;      /*!< USB device address */
    esp_codec_dev_type_t  dev_type;  /*!< Connected directions: IN / OUT / IN_OUT */
} esp_codec_dev_uac_info_t;

/**
 * @brief  USB UAC manager event callback
 *
 * @note  Invoked from the UAC host driver task context. Keep the callback non-blocking;
 *        defer heavy work to an application task.
 *
 * @param[in]  info  Event information; valid only for the duration of the callback
 * @param[in]  ctx   User context from esp_codec_dev_uac_cfg_t::event_ctx
 */
typedef void (*esp_codec_dev_uac_event_cb_t)(const esp_codec_dev_uac_event_info_t *info, void *ctx);

/**
 * @brief  USB UAC manager configuration
 *
 * @note  The USB host stack (usb_host_install) must be installed by the application
 *        before opening any derived device. The manager only owns the UAC host
 *        class driver lifecycle, not the USB host library itself.
 */
typedef struct {
    uint32_t                      connect_timeout_ms;  /*!< Timeout waiting for UAC device connection. 0 uses default */
    uint32_t                      host_buffer_size;    /*!< UAC host ring buffer size. 0 uses default */
    uint32_t                      buffer_threshold;    /*!< UAC host ring buffer threshold. 0 uses default */
    uint8_t                       max_devices;         /*!< Maximum concurrent UAC devices. 0 uses CONFIG_CODEC_UAC_MAX_DEVICES */
    esp_codec_dev_uac_event_cb_t  event_cb;            /*!< Optional connect/disconnect callback; NULL disables events */
    void                         *event_ctx;           /*!< User context passed to event_cb */
} esp_codec_dev_uac_cfg_t;

/**
 * @brief  Install the USB UAC manager
 *
 *         Installs the UAC host class driver and starts device enumeration when the USB
 *         host library is already available. The UAC service is global; call this once.
 *         The USB host library must already be installed by the application.
 *
 * @note  Usage model: call esp_codec_dev_uac_install() once, optionally register event_cb for
 *        connect/disconnect notifications, or use esp_codec_dev_uac_get_num() and
 *        esp_codec_dev_uac_get_info() to poll connected devices, then esp_codec_dev_uac_new_dev()
 *        with ESP_CODEC_DEV_UAC_SELECT_FIRST or ESP_CODEC_DEV_UAC_SELECT_BY_ADDR. Release each derived
 *        handle with esp_codec_dev_uac_del_dev() before esp_codec_dev_uac_uninstall().
 *
 * @note  Disconnect events are reported when a device stream was opened by the manager.
 *        Devices that were enumerated but never opened may not raise a disconnect callback
 *        until the underlying USB host stack supports device-removed monitoring.
 *
 * @param[in]  cfg  Manager configuration, NULL uses defaults
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_WRONG_STATE  UAC manager already installed
 *       - ESP_CODEC_DEV_NO_MEM       Internal allocation failed
 *       - ESP_CODEC_DEV_DRV_ERR      UAC host initialization failed
 */
int esp_codec_dev_uac_install(const esp_codec_dev_uac_cfg_t *cfg);

/**
 * @brief  Derive a standard esp_codec_dev handle from a UAC device
 *
 *         Binding to a physical device is resolved lazily when the handle is opened or when
 *         capabilities are queried, so the handle can be created before the device is plugged in.
 *         Release the returned handle with esp_codec_dev_uac_del_dev().
 *
 * @param[in]  sel  Device selection
 *
 * @return
 *       - NULL    Failed to create device
 *       - Others  esp_codec_dev handle, used with esp_codec_dev_* APIs
 */
esp_codec_dev_handle_t esp_codec_dev_uac_new_dev(const esp_codec_dev_uac_select_t *sel);

/**
 * @brief  Release a derived UAC esp_codec_dev handle
 *
 *         Closes and deletes the handle and releases the underlying codec/data interfaces
 *         and binding owned by the UAC manager for this handle.
 *
 * @param[in]  dev  Handle returned by esp_codec_dev_uac_new_dev()
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  Handle is NULL or not a UAC-derived handle
 *       - ESP_CODEC_DEV_WRONG_STATE  UAC manager is not installed
 */
int esp_codec_dev_uac_del_dev(esp_codec_dev_handle_t dev);

/**
 * @brief  Get the number of currently connected UAC devices
 *
 *         Returns a snapshot of devices already enumerated by the UAC host driver.
 *         Enumeration is asynchronous; poll after install, on event_cb, or after hot-plug.
 *
 * @param[out]  num  Number of connected UAC devices
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  num is NULL
 *       - ESP_CODEC_DEV_WRONG_STATE  UAC manager is not installed
 */
int esp_codec_dev_uac_get_num(uint8_t *num);

/**
 * @brief  Get basic information for a connected UAC device
 *
 *         The index is the ordinal among currently connected devices (0 .. num-1 from
 *         esp_codec_dev_uac_get_num()). Use the returned addr with ESP_CODEC_DEV_UAC_SELECT_BY_ADDR.
 *
 * @param[in]   index  Device index in the connected-device list
 * @param[out]  info   Device information to fill
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  info is NULL
 *       - ESP_CODEC_DEV_WRONG_STATE  UAC manager is not installed
 *       - ESP_CODEC_DEV_NOT_FOUND    index is out of range
 */
int esp_codec_dev_uac_get_info(uint8_t index, esp_codec_dev_uac_info_t *info);

/**
 * @brief  Uninstall the USB UAC manager
 *
 *         Releases the UAC host class driver. All derived handles must be released with
 *         esp_codec_dev_uac_del_dev() before calling this function. Does not uninstall the
 *         USB host library.
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_WRONG_STATE  UAC manager not installed or derived handles remain
 */
int esp_codec_dev_uac_uninstall(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
