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
 * @brief  Acquire a reference for a codec control interface
 *
 * @param[in]  info  Control interface identity
 *
 * @return
 *       - -1      If info is NULL, or acquire failed
 *       - Others  Reference count after acquire (>= 1)
 */
int codec_ref_acquire(const audio_codec_ctrl_info_t *info);

/**
 * @brief  Release a reference for a codec control interface
 *
 * @param[in]  info  Control interface identity
 *
 * @return
 *       - -1      If info is NULL, or release failed
 *       - Others  Remaining reference count after release (0 means last instance)
 */
int codec_ref_release(const audio_codec_ctrl_info_t *info);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
