/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "audio_codec_vol_if.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  New software volume processor interface
 *         Notes: currently only support 16bits input
 *
 * @return
 *       - NULL    Memory not enough
 *       - Others  Software volume interface handle
 */
const audio_codec_vol_if_t *audio_codec_new_sw_vol(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
