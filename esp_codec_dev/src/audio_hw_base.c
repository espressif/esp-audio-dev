/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_check.h"

#include "audio_hw_base_priv.h"

static const char *TAG = "AUDIO_HW_BASE";

int audio_hw_open(const audio_hw_base_t *h)
{
    ESP_RETURN_ON_FALSE(h, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(h->open, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return h->open(h, NULL, 0);
}

bool audio_hw_is_open(const audio_hw_base_t *h)
{
    if (h == NULL || h->is_open == NULL) {
        return false;
    }
    return h->is_open(h);
}

int audio_hw_set_fs(const audio_hw_base_t *h, esp_codec_dev_sample_info_t *fs, esp_codec_dev_type_t type)
{
    ESP_RETURN_ON_FALSE(h && fs, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle or fs");
    ESP_RETURN_ON_FALSE(h->set_fs, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return h->set_fs(h, fs, type);
}

int audio_hw_set_reg(const audio_hw_base_t *h, int reg, int value)
{
    ESP_RETURN_ON_FALSE(h, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(h->set_reg, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return h->set_reg(h, reg, value);
}

int audio_hw_get_reg(const audio_hw_base_t *h, int reg, int *value)
{
    ESP_RETURN_ON_FALSE(h && value, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle or value");
    ESP_RETURN_ON_FALSE(h->get_reg, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return h->get_reg(h, reg, value);
}

int audio_hw_dump_reg(const audio_hw_base_t *h)
{
    ESP_RETURN_ON_FALSE(h, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(h->dump_reg, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    h->dump_reg(h);
    return ESP_CODEC_DEV_OK;
}

int audio_hw_close(const audio_hw_base_t *h)
{
    ESP_RETURN_ON_FALSE(h, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(h->close, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return h->close(h);
}

int audio_hw_get_order_list(const audio_hw_base_t *h, const esp_codec_dev_device_map_info_t **order_list, int *list_size)
{
    ESP_RETURN_ON_FALSE(h && order_list && list_size, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle or output");
    ESP_RETURN_ON_FALSE(h->get_order_list, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return h->get_order_list(h, order_list, list_size);
}

int audio_hw_get_adc_label(const audio_hw_base_t *h, const char **label)
{
    ESP_RETURN_ON_FALSE(h && label, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle or label");
    ESP_RETURN_ON_FALSE(h->get_adc_label, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return h->get_adc_label(h, label);
}

int audio_hw_get_caps(const audio_hw_base_t *h, esp_codec_dev_type_t dev_type,
                      esp_codec_dev_capability_t *caps, int *count)
{
    ESP_RETURN_ON_FALSE(h && count && *count >= 0, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle or count");
    ESP_RETURN_ON_FALSE(h->get_caps, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return h->get_caps(h, dev_type, caps, count);
}
