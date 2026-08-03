/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

/**
 * @file esp_audio_hw_proc_if.h
 *
 * @brief  Codec hardware audio processing ops
 *
 * @note  Codec driver authors include this header when wiring
 *        `audio_codec_if_t::hw_proc`. Application APIs and configuration
 *        types are defined in the corresponding `esp_audio_hw_*.h` headers.
 */

#include "audio_hw_base.h"
#include "esp_audio_hw_alc.h"
#include "esp_audio_hw_drc.h"
#include "esp_audio_hw_eq.h"
#include "esp_audio_hw_line.h"
#include "esp_audio_hw_mute.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  ALC implementation table (vtable)
 *
 * @note  Each driver provides one static const instance shared by all codec
 *        instances of that chip. Do not store instance-specific fields here;
 *        the chip handle is passed as parameter `h`.
 */
typedef struct esp_audio_hw_alc_t esp_audio_hw_alc_t;

struct esp_audio_hw_alc_t {
    int (*init)(const audio_hw_base_t *h, const esp_audio_hw_alc_cfg_t *cfg);  /*!< Initialize ALC */
    int (*set_gain)(const audio_hw_base_t *h, float target_gain);              /*!< Set ALC target gain */
    int (*set_channel)(const audio_hw_base_t *h, int channel_mask);            /*!< Set ALC channel mask */
    int (*set_noise_gate)(const audio_hw_base_t *h, float threshold);          /*!< Set noise gate threshold */
};

/**
 * @brief  DRC implementation table (vtable)
 *
 * @note  Each driver provides one static const instance shared by all codec
 *        instances of that chip. Do not store instance-specific fields here;
 *        the chip handle is passed as parameter `h`.
 */
typedef struct esp_audio_hw_drc_t esp_audio_hw_drc_t;

struct esp_audio_hw_drc_t {
    int (*init)(const audio_hw_base_t *h, const esp_audio_hw_drc_cfg_t *cfg);  /*!< Initialize DRC */
    int (*set_offset_gain)(const audio_hw_base_t *h, float gain);              /*!< Set DRC offset gain */
    int (*enable)(const audio_hw_base_t *h, bool enable);                      /*!< Enable or disable DRC */
};

/**
 * @brief  EQ implementation table (vtable)
 *
 * @note  Each driver provides one static const instance shared by all codec
 *        instances of that chip. Do not store instance-specific fields here;
 *        the chip handle is passed as parameter `h`.
 */
typedef struct esp_audio_hw_eq_t esp_audio_hw_eq_t;

struct esp_audio_hw_eq_t {
    int (*set_band_para)(const audio_hw_base_t *h, const esp_audio_hw_eq_para_t *para, int index);  /*!< Set EQ band */
    int (*set_cfg)(const audio_hw_base_t *h, const esp_audio_hw_eq_cfg_t *cfg);                     /*!< Set EQ config */
    int (*enable)(const audio_hw_base_t *h, bool enable);                                           /*!< Enable or disable EQ */
    int (*dump_info)(const audio_hw_base_t *h);                                                     /*!< Dump EQ information */
};

/**
 * @brief  Line implementation table (vtable)
 *
 * @note  Each driver provides one static const instance shared by all codec
 *        instances of that chip. Do not store instance-specific fields here;
 *        the chip handle is passed as parameter `h`.
 */
typedef struct esp_audio_hw_line_t esp_audio_hw_line_t;

struct esp_audio_hw_line_t {
    int (*enable_in)(const audio_hw_base_t *h, bool enable);   /*!< Enable or disable line-in mode */
    int (*enable_out)(const audio_hw_base_t *h, bool enable);  /*!< Enable or disable line-out mode */
};

/**
 * @brief  Mute implementation table (vtable)
 *
 * @note  Each driver provides one static const instance shared by all codec
 *        instances of that chip. Do not store instance-specific fields here;
 *        the chip handle is passed as parameter `h`.
 */
typedef struct esp_audio_hw_mute_t esp_audio_hw_mute_t;

struct esp_audio_hw_mute_t {
    int (*set_auto_mute_cfg)(const audio_hw_base_t *h, const esp_audio_hw_auto_mute_cfg_t *cfg);  /*!< Set auto mute config */
    int (*enable_auto_mute)(const audio_hw_base_t *h, bool enable);                               /*!< Enable or disable auto mute */
    int (*set_soft_mute_cfg)(const audio_hw_base_t *h, const esp_audio_hw_soft_mute_cfg_t *cfg);  /*!< Set soft mute config */
    int (*enable_soft_mute)(const audio_hw_base_t *h, bool enable);                               /*!< Enable or disable soft mute */
};

typedef struct esp_audio_hw_proc_ops_t esp_audio_hw_proc_ops_t;

/**
 * @brief  Codec hardware audio processing ops
 *
 * @note  Each field is a pointer to a static const implementation table.
 *        NULL means the codec does not support that processing type.
 */
struct esp_audio_hw_proc_ops_t {
    const esp_audio_hw_alc_t  *alc;   /*!< ALC ops, NULL if not supported */
    const esp_audio_hw_drc_t  *drc;   /*!< DRC ops, NULL if not supported */
    const esp_audio_hw_eq_t   *eq;    /*!< EQ ops, NULL if not supported */
    const esp_audio_hw_line_t *line;  /*!< Line ops, NULL if not supported */
    const esp_audio_hw_mute_t *mute;  /*!< Mute ops, NULL if not supported */
};

#ifdef __cplusplus
}
#endif  /* __cplusplus */
