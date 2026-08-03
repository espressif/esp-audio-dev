/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_log.h"

#include "esp_audio_hw_mute.h"
#include "audio_codec_hw_proc.h"

static const char *TAG = "AUDIO_HW_MUTE";

static inline const void *_get_mute_ops(const esp_audio_hw_proc_ops_t *hw_proc)
{
    return hw_proc ? (const void *)hw_proc->mute : NULL;
}

int esp_audio_hw_auto_mute_set_cfg(esp_codec_dev_handle_t dev, const esp_audio_hw_auto_mute_cfg_t *cfg)
{
    if (dev == NULL || cfg == NULL) {
        ESP_LOGE(TAG, "Invalid handle or config");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *codec_if = NULL;
    const esp_audio_hw_mute_t *mute = NULL;
    int ret = esp_audio_hw_proc_resolve(dev, _get_mute_ops, &codec_if, (const void **)&mute);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to resolve mute ops: ret 0x%x", ret);
        return ret;
    }
    if (mute->set_auto_mute_cfg == NULL) {
        ESP_LOGE(TAG, "Auto mute set_cfg is not supported");
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
    ret = mute->set_auto_mute_cfg(&codec_if->hw_base, cfg);
    esp_audio_hw_proc_unlock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Auto mute set_cfg failed: ret 0x%x", ret);
    }
    return ret;
}

int esp_audio_hw_auto_mute_enable(esp_codec_dev_handle_t dev, bool enable)
{
    if (dev == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *codec_if = NULL;
    const esp_audio_hw_mute_t *mute = NULL;
    int ret = esp_audio_hw_proc_resolve(dev, _get_mute_ops, &codec_if, (const void **)&mute);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to resolve mute ops: ret 0x%x", ret);
        return ret;
    }
    if (mute->enable_auto_mute == NULL) {
        ESP_LOGE(TAG, "Auto mute enable is not supported");
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
    ret = mute->enable_auto_mute(&codec_if->hw_base, enable);
    esp_audio_hw_proc_unlock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Auto mute enable failed: ret 0x%x", ret);
    }
    return ret;
}

int esp_audio_hw_soft_mute_set_cfg(esp_codec_dev_handle_t dev, const esp_audio_hw_soft_mute_cfg_t *cfg)
{
    if (dev == NULL || cfg == NULL) {
        ESP_LOGE(TAG, "Invalid handle or config");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *codec_if = NULL;
    const esp_audio_hw_mute_t *mute = NULL;
    int ret = esp_audio_hw_proc_resolve(dev, _get_mute_ops, &codec_if, (const void **)&mute);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to resolve mute ops: ret 0x%x", ret);
        return ret;
    }
    if (mute->set_soft_mute_cfg == NULL) {
        ESP_LOGE(TAG, "Soft mute set_cfg is not supported");
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
    ret = mute->set_soft_mute_cfg(&codec_if->hw_base, cfg);
    esp_audio_hw_proc_unlock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Soft mute set_cfg failed: ret 0x%x", ret);
    }
    return ret;
}

int esp_audio_hw_soft_mute_enable(esp_codec_dev_handle_t dev, bool enable)
{
    if (dev == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *codec_if = NULL;
    const esp_audio_hw_mute_t *mute = NULL;
    int ret = esp_audio_hw_proc_resolve(dev, _get_mute_ops, &codec_if, (const void **)&mute);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to resolve mute ops: ret 0x%x", ret);
        return ret;
    }
    if (mute->enable_soft_mute == NULL) {
        ESP_LOGE(TAG, "Soft mute enable is not supported");
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
    ret = mute->enable_soft_mute(&codec_if->hw_base, enable);
    esp_audio_hw_proc_unlock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Soft mute enable failed: ret 0x%x", ret);
    }
    return ret;
}
