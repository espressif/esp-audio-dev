/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  GPIO drive mode
 */
typedef enum {
    AUDIO_GPIO_MODE_FLOAT,                 /*!< Float */
    AUDIO_GPIO_MODE_PULL_UP   = (1 << 0),  /*!< Internally pullup */
    AUDIO_GPIO_MODE_PULL_DOWN = (1 << 1),  /*!< Internally pulldown */
} audio_gpio_mode_t;

/**
 * @brief  GPIO direction type
 */
typedef enum {
    AUDIO_GPIO_DIR_OUT,  /*!< Output GPIO */
    AUDIO_GPIO_DIR_IN,   /*!< Input GPIO */
} audio_gpio_dir_t;

/**
 * @brief  Interrupt callback for a logical GPIO line managed by `audio_codec_gpio_if_t`.
 *         May run in ISR context or a deferred context, depending on the implementation
 *         (native GPIO, GPIO expander, another SoC, etc.).
 */
typedef void (*audio_codec_gpio_isr_t)(void *arg);

/**
 * @brief  Codec GPIO interface structure
 */
typedef struct {
    int (*setup)(int16_t gpio, audio_gpio_dir_t dir, audio_gpio_mode_t mode);  /*!< Setup GPIO */
    int (*set)(int16_t gpio, bool high);                                       /*!< Set GPIO level */
    bool (*get)(int16_t gpio);                                                 /*!< Get GPIO level */
    int (*add_isr)(int16_t gpio, audio_codec_gpio_isr_t isr, void *arg);       /*!< Register interrupt callback for a logical GPIO */
    int (*remove_isr)(int16_t gpio);                                           /*!< Unregister interrupt callback for a logical GPIO */
} audio_codec_gpio_if_t;

/**
 * @brief  Delete GPIO interface instance
 *
 * @param[in]  gpio_if  GPIO interface
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Delete success
 *       - ESP_CODEC_DEV_INVALID_ARG  Input is NULL pointer
 */
int audio_codec_delete_gpio_if(const audio_codec_gpio_if_t *gpio_if);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
