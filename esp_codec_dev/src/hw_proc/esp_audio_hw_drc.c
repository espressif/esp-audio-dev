/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_log.h"

#include "esp_audio_hw_drc.h"
#include "audio_codec_hw_proc.h"

static const char *TAG = "AUDIO_HW_DRC";

static inline const void *_get_drc_ops(const esp_audio_hw_proc_ops_t *hw_proc)
{
    return hw_proc ? (const void *)hw_proc->drc : NULL;
}

int esp_audio_hw_drc_init(esp_codec_dev_handle_t dev, const esp_audio_hw_drc_cfg_t *cfg)
{
    if (dev == NULL || cfg == NULL) {
        ESP_LOGE(TAG, "Invalid handle or config");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *codec_if = NULL;
    const esp_audio_hw_drc_t *drc = NULL;
    int ret = esp_audio_hw_proc_resolve(dev, _get_drc_ops, &codec_if, (const void **)&drc);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to resolve DRC ops: ret 0x%x", ret);
        return ret;
    }
    if (drc->init == NULL) {
        ESP_LOGE(TAG, "DRC init is not supported");
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
    ret = drc->init(&codec_if->hw_base, cfg);
    esp_audio_hw_proc_unlock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "DRC init failed: ret 0x%x", ret);
    }
    return ret;
}

int esp_audio_hw_drc_set_offset_gain(esp_codec_dev_handle_t dev, float offset_gain)
{
    if (dev == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *codec_if = NULL;
    const esp_audio_hw_drc_t *drc = NULL;
    int ret = esp_audio_hw_proc_resolve(dev, _get_drc_ops, &codec_if, (const void **)&drc);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to resolve DRC ops: ret 0x%x", ret);
        return ret;
    }
    if (drc->set_offset_gain == NULL) {
        ESP_LOGE(TAG, "DRC set_offset_gain is not supported");
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
    ret = drc->set_offset_gain(&codec_if->hw_base, offset_gain);
    esp_audio_hw_proc_unlock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "DRC set_offset_gain failed: ret 0x%x", ret);
    }
    return ret;
}

int esp_audio_hw_drc_enable(esp_codec_dev_handle_t dev, bool enable)
{
    if (dev == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *codec_if = NULL;
    const esp_audio_hw_drc_t *drc = NULL;
    int ret = esp_audio_hw_proc_resolve(dev, _get_drc_ops, &codec_if, (const void **)&drc);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to resolve DRC ops: ret 0x%x", ret);
        return ret;
    }
    if (drc->enable == NULL) {
        ESP_LOGE(TAG, "DRC enable is not supported");
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
    ret = drc->enable(&codec_if->hw_base, enable);
    esp_audio_hw_proc_unlock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "DRC enable failed: ret 0x%x", ret);
    }
    return ret;
}
