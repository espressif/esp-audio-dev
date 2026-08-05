/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_log.h"

#include "esp_audio_hw_line.h"
#include "audio_codec_hw_proc.h"

static const char *TAG = "AUDIO_HW_LINE";

static inline const void *_get_line_ops(const esp_audio_hw_proc_ops_t *hw_proc)
{
    return hw_proc ? (const void *)hw_proc->line : NULL;
}

int esp_audio_hw_line_enable_in(esp_codec_dev_handle_t dev, bool enable)
{
    if (dev == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *codec_if = NULL;
    const esp_audio_hw_line_t *line = NULL;
    int ret = esp_audio_hw_proc_resolve(dev, _get_line_ops, &codec_if, (const void **)&line);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to resolve line ops: ret 0x%x", ret);
        return ret;
    }
    if (line->enable_in == NULL) {
        ESP_LOGE(TAG, "Line enable_in is not supported");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    ret = esp_audio_hw_proc_check_open(codec_if);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Codec is not open");
        return ret;
    }

    ret = esp_audio_hw_proc_lock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to lock hw_proc: ret 0x%x", ret);
        return ret;
    }
    ret = line->enable_in(&codec_if->hw_base, enable);
    esp_audio_hw_proc_unlock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Line enable_in failed: ret 0x%x", ret);
    }
    return ret;
}

int esp_audio_hw_line_enable_out(esp_codec_dev_handle_t dev, bool enable)
{
    if (dev == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *codec_if = NULL;
    const esp_audio_hw_line_t *line = NULL;
    int ret = esp_audio_hw_proc_resolve(dev, _get_line_ops, &codec_if, (const void **)&line);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to resolve line ops: ret 0x%x", ret);
        return ret;
    }
    if (line->enable_out == NULL) {
        ESP_LOGE(TAG, "Line enable_out is not supported");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    ret = esp_audio_hw_proc_check_open(codec_if);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Codec is not open");
        return ret;
    }

    ret = esp_audio_hw_proc_lock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to lock hw_proc: ret 0x%x", ret);
        return ret;
    }
    ret = line->enable_out(&codec_if->hw_base, enable);
    esp_audio_hw_proc_unlock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Line enable_out failed: ret 0x%x", ret);
    }
    return ret;
}
