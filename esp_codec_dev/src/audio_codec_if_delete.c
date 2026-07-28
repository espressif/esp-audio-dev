/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>

#include "esp_log.h"

#include "audio_codec_if.h"
#include "audio_codec_ctrl_if.h"
#include "audio_codec_data_if.h"
#include "audio_codec_gpio_if.h"
#include "audio_codec_vol_if.h"

static const char *TAG = "ADEV_CODEC_IF";

int audio_codec_delete_codec_if(const audio_codec_if_t *h)
{
    if (h) {
        int ret = 0;
        if (h->hw_base.close) {
            ret = h->hw_base.close(&h->hw_base);
        }
        free((void *)h);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Fail to close codec interface, ret=%d", ret);
        }
        return ret;
    }
    ESP_LOGE(TAG, "Invalid codec interface");
    return ESP_CODEC_DEV_INVALID_ARG;
}

int audio_codec_delete_ctrl_if(const audio_codec_ctrl_if_t *h)
{
    if (h) {
        int ret = 0;
        if (h->close) {
            ret = h->close(h);
        }
        free((void *)h);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Fail to close control interface, ret=%d", ret);
        }
        return ret;
    }
    ESP_LOGE(TAG, "Invalid control interface");
    return ESP_CODEC_DEV_INVALID_ARG;
}

int audio_codec_delete_data_if(const audio_codec_data_if_t *h)
{
    if (h) {
        int ret = 0;
        if (h->close) {
            ret = h->close(h);
        }
        free((void *)h);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Fail to close data interface, ret=%d", ret);
        }
        return ret;
    }
    ESP_LOGE(TAG, "Invalid data interface");
    return ESP_CODEC_DEV_INVALID_ARG;
}

int audio_codec_delete_gpio_if(const audio_codec_gpio_if_t *gpio_if)
{
    if (gpio_if) {
        free((void *)gpio_if);
        return ESP_CODEC_DEV_OK;
    }
    ESP_LOGE(TAG, "Invalid GPIO interface");
    return ESP_CODEC_DEV_INVALID_ARG;
}

int audio_codec_delete_vol_if(const audio_codec_vol_if_t *h)
{
    if (h) {
        int ret = 0;
        if (h->close) {
            ret = h->close(h);
        }
        free((void *)h);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Fail to close volume interface, ret=%d", ret);
        }
        return ret;
    }
    ESP_LOGE(TAG, "Invalid volume interface");
    return ESP_CODEC_DEV_INVALID_ARG;
}
