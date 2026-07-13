/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Codec device mutex handle
 */
typedef void *esp_codec_dev_mutex_handle_t;

/**
 * @brief  Create mutex for codec device
 *
 * @return
 *       - NULL    No available resource
 *       - Others  Mutex handle
 */
esp_codec_dev_mutex_handle_t esp_codec_dev_mutex_create(void);

/**
 * @brief  Lock mutex for codec device
 *
 * @param[in]  mutex       Mutex handle
 * @param[in]  timeout_ms  Wait timeout for mutex (unit ms)
 *
 * @return
 *       - 0       On success
 *       - Others  Failed to lock
 */
int esp_codec_dev_mutex_lock(esp_codec_dev_mutex_handle_t mutex, int timeout_ms);

/**
 * @brief  Unlock mutex for codec device
 *
 * @param[in]  mutex  Mutex handle
 *
 * @return
 *       - 0       On success
 *       - Others  Failed to unlock
 */
int esp_codec_dev_mutex_unlock(esp_codec_dev_mutex_handle_t mutex);

/**
 * @brief  Destroy mutex for codec device
 *
 * @param[in]  mutex  Mutex handle
 */
void esp_codec_dev_mutex_destroy(esp_codec_dev_mutex_handle_t mutex);

/**
 * @brief  Sleep in milliseconds
 *
 * @param[in]  ms  Sleep time (unit ms)
 */
void esp_codec_dev_sleep(int ms);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
