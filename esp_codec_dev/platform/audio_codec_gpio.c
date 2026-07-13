/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "esp_check.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "audio_codec_gpio_if.h"
#include "esp_codec_dev_types.h"

static const char *TAG = "GPIO_IF";

static int _gpio_cfg(int16_t gpio, audio_gpio_dir_t dir, audio_gpio_mode_t mode)
{
    ESP_RETURN_ON_FALSE(gpio != -1, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid gpio");
    gpio_config_t io_conf;
    memset(&io_conf, 0, sizeof(io_conf));
    io_conf.mode = (dir == AUDIO_GPIO_DIR_OUT ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT);
    io_conf.pin_bit_mask = BIT64(gpio);
    io_conf.pull_down_en = ((mode & AUDIO_GPIO_MODE_PULL_DOWN) != 0);
    io_conf.pull_up_en = ((mode & AUDIO_GPIO_MODE_PULL_UP) != 0);
    esp_err_t ret = 0;
    ret |= gpio_config(&io_conf);
    return ret == 0 ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}

static int _gpio_set(int16_t gpio, bool high)
{
    ESP_RETURN_ON_FALSE(gpio != -1, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid gpio");
    int ret = gpio_set_level((gpio_num_t)gpio, high ? 1 : 0);
    return ret == 0 ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}

static bool _gpio_get(int16_t gpio)
{
    if (gpio == -1) {
        return false;
    }
    return (bool)gpio_get_level((gpio_num_t)gpio);
}

static int _gpio_add_isr(int16_t gpio, audio_codec_gpio_isr_t isr, void *arg)
{
    ESP_RETURN_ON_FALSE(gpio != -1 && isr != NULL, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid gpio or isr");
    esp_err_t ret = gpio_isr_handler_add((gpio_num_t)gpio, (gpio_isr_t)isr, arg);
    return ret == ESP_OK ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}

static int _gpio_remove_isr(int16_t gpio)
{
    ESP_RETURN_ON_FALSE(gpio != -1, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid gpio");
    esp_err_t ret = gpio_isr_handler_remove((gpio_num_t)gpio);
    return ret == ESP_OK ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}

const audio_codec_gpio_if_t *audio_codec_new_gpio(void)
{
    audio_codec_gpio_if_t *gpio_if = (audio_codec_gpio_if_t *)calloc(1, sizeof(audio_codec_gpio_if_t));
    if (gpio_if == NULL) {
        ESP_LOGE(TAG, "No memory for instance");
        return NULL;
    }
    gpio_if->setup = _gpio_cfg;
    gpio_if->set = _gpio_set;
    gpio_if->get = _gpio_get;
    gpio_if->add_isr = _gpio_add_isr;
    gpio_if->remove_isr = _gpio_remove_isr;
    return gpio_if;
}
