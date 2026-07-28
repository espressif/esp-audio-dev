/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

/**
 * @file audio_hw_proc_if.h
 *
 * @brief  Codec hardware audio processing ops
 *
 * @note  Codec driver authors include this header when wiring
 *        `audio_codec_if_t::hw_proc`. Processing handle implementation
 *        structures are defined in the corresponding `audio_hw_*.h` headers.
 */

#include "audio_hw_alc.h"
#include "audio_hw_drc.h"
#include "audio_hw_eq.h"
#include "audio_hw_line.h"
#include "audio_hw_mute.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef struct audio_codec_hw_proc_ops_t audio_codec_hw_proc_ops_t;

/**
 * @brief  Codec hardware audio processing ops
 */
struct audio_codec_hw_proc_ops_t {
    int (*alc_new)(const audio_hw_base_t *h, audio_hw_alc_handle_t *alc);     /*!< Create ALC processing handle */
    int (*drc_new)(const audio_hw_base_t *h, audio_hw_drc_handle_t *drc);     /*!< Create DRC processing handle */
    int (*eq_new)(const audio_hw_base_t *h, audio_hw_eq_handle_t *eq);        /*!< Create EQ processing handle */
    int (*line_new)(const audio_hw_base_t *h, audio_hw_line_handle_t *line);  /*!< Create line processing handle */
    int (*mute_new)(const audio_hw_base_t *h, audio_hw_mute_handle_t *mute);  /*!< Create mute processing handle */
};

#ifdef __cplusplus
}
#endif  /* __cplusplus */
