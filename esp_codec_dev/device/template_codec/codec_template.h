/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "audio_codec_if.h"
#include "audio_codec_hw_cfg.h"
#include "audio_codec_gpio_if.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * Template only:
 * 1. This folder is a scaffold for creating a new codec driver.
 * 2. Keep it in template_codec while implementing the driver.
 * 3. Copy/rename this folder (for example to device/my_codec/) when ready.
 * 4. Replace file names, include guards, symbols and TAG in the copied version.
 *    chip_ stands for the target codec (ES8311, ES8389, ES7210, TAS5805M, ...).
 *    Global replace examples: chip_ -> es8311_, audio_codec_chip_t -> audio_codec_es8311_t,
 *    chip_cfg_t -> es8311_codec_cfg_t, chip_codec_new -> es8311_codec_new,
 *    chip_order_info -> es8311_order_info, CHIP -> ES8311.
 * 5. Move the final public header to device/include/ with the real codec name.
 * 6. Add the .c source file to CMakeLists.txt only after implementation is complete.
 */

// The address(8bits) of the codec, if use 7bits, need to shift left 1 bit.
#define CHIP_DEFAULT_ADDR  (0x00)

/**
 * @brief  Template codec configuration
 *
 *         This template starts from a full-duplex codec model like ES8311/ES8389.
 *         Remove the unsupported parts if the target chip is ADC-only or DAC-only.
 */
typedef struct {
    const audio_codec_ctrl_if_t *ctrl_if;    /*!< Codec control interface */
    const audio_codec_gpio_if_t *gpio_if;    /*!< Optional GPIO interface */
    audio_hw_sys_cfg_t           sys_cfg;    /*!< Clock/master-slave settings */
    audio_hw_adc_cfg_t           adc_cfg;    /*!< ADC / microphone settings */
    audio_hw_dac_cfg_t           dac_cfg;    /*!< DAC reference / loopback settings */
    audio_hw_pa_cfg_t            pa_cfg;     /*!< PA pin and hardware gain settings */
    audio_hw_reset_cfg_t         reset_cfg;  /*!< Optional reset settings */
} chip_cfg_t;

/**
 * @brief  Create a new template codec instance
 *
 *         Replace this declaration with the real codec name when copying the template.
 *
 * @param[in]  codec_cfg  Template codec configuration
 *
 * @return
 *       - NULL    Fail to create template codec interface
 *       - Others  Template codec interface
 */
const audio_codec_if_t *chip_codec_new(chip_cfg_t *codec_cfg);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
