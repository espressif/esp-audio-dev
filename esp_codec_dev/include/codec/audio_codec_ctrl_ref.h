/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "audio_codec_ctrl_if.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Acquire a reference for a codec control interface identity
 *
 *         Codec drivers use this to share one physical chip across multiple
 *         `audio_codec_if_t` instances (typically IN and OUT). Identity comes
 *         from `audio_codec_ctrl_if_t.get_info()`.
 *
 * @note  Not thread-safe with respect to callers omitting the lock: the
 *        implementation serializes acquire/release internally. Not ISR-safe.
 *        May allocate on the first acquire of a new identity.
 * @note  A return value of 1 means this is the first reference; the driver
 *        should open the hardware. After destroy/close of an instance, that
 *        instance must not reuse a stale count.
 *
 * @param[in]  info  Control interface identity from get_info(); caller retains ownership
 *
 * @return
 *       - -1      info is NULL, lock failed, or allocation failed
 *       - Others  Reference count after acquire (>= 1)
 */
int audio_codec_ctrl_ref_acquire(const audio_codec_ctrl_info_t *info);

/**
 * @brief  Release a reference for a codec control interface identity
 *
 * @note  Not ISR-safe. When the remaining count is 0, the last instance should
 *        close the hardware. Do not call release without a matching acquire.
 *
 * @param[in]  info  Control interface identity from get_info(); caller retains ownership
 *
 * @return
 *       - -1      info is NULL, list is not initialized, lock failed, or identity not found
 *       - Others  Remaining reference count after release (0 means last instance)
 */
int audio_codec_ctrl_ref_release(const audio_codec_ctrl_info_t *info);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
