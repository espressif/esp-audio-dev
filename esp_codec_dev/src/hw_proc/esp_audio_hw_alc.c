/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_log.h"

#include "esp_audio_hw_alc.h"
#include "audio_codec_hw_proc.h"

static const char *TAG = "AUDIO_HW_ALC";

static inline const void *_get_alc_ops(const esp_audio_hw_proc_ops_t *hw_proc)
{
    return hw_proc ? (const void *)hw_proc->alc : NULL;
}

int esp_audio_hw_alc_init(esp_codec_dev_handle_t dev, const esp_audio_hw_alc_cfg_t *cfg)
{
    if (dev == NULL || cfg == NULL) {
        ESP_LOGE(TAG, "Invalid handle or config");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *codec_if = NULL;
    const esp_audio_hw_alc_t *alc = NULL;
    int ret = esp_audio_hw_proc_resolve(dev, _get_alc_ops, &codec_if, (const void **)&alc);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to resolve ALC ops: ret 0x%x", ret);
        return ret;
    }
    if (alc->init == NULL) {
        ESP_LOGE(TAG, "ALC init is not supported");
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
    ret = alc->init(&codec_if->hw_base, cfg);
    esp_audio_hw_proc_unlock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "ALC init failed: ret 0x%x", ret);
    }
    return ret;
}

int esp_audio_hw_alc_set_gain(esp_codec_dev_handle_t dev, float target_gain)
{
    if (dev == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *codec_if = NULL;
    const esp_audio_hw_alc_t *alc = NULL;
    int ret = esp_audio_hw_proc_resolve(dev, _get_alc_ops, &codec_if, (const void **)&alc);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to resolve ALC ops: ret 0x%x", ret);
        return ret;
    }
    if (alc->set_gain == NULL) {
        ESP_LOGE(TAG, "ALC set_gain is not supported");
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
    ret = alc->set_gain(&codec_if->hw_base, target_gain);
    esp_audio_hw_proc_unlock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "ALC set_gain failed: ret 0x%x", ret);
    }
    return ret;
}

int esp_audio_hw_alc_set_channel_mask(esp_codec_dev_handle_t dev, int channel_mask)
{
    if (dev == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *codec_if = NULL;
    const esp_audio_hw_alc_t *alc = NULL;
    int ret = esp_audio_hw_proc_resolve(dev, _get_alc_ops, &codec_if, (const void **)&alc);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to resolve ALC ops: ret 0x%x", ret);
        return ret;
    }
    if (alc->set_channel == NULL) {
        ESP_LOGE(TAG, "ALC set_channel is not supported");
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
    ret = alc->set_channel(&codec_if->hw_base, channel_mask);
    esp_audio_hw_proc_unlock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "ALC set_channel failed: ret 0x%x", ret);
    }
    return ret;
}

int esp_audio_hw_alc_set_noise_gate(esp_codec_dev_handle_t dev, float threshold)
{
    if (dev == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *codec_if = NULL;
    const esp_audio_hw_alc_t *alc = NULL;
    int ret = esp_audio_hw_proc_resolve(dev, _get_alc_ops, &codec_if, (const void **)&alc);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to resolve ALC ops: ret 0x%x", ret);
        return ret;
    }
    if (alc->set_noise_gate == NULL) {
        ESP_LOGE(TAG, "ALC set_noise_gate is not supported");
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
    ret = alc->set_noise_gate(&codec_if->hw_base, threshold);
    esp_audio_hw_proc_unlock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "ALC set_noise_gate failed: ret 0x%x", ret);
    }
    return ret;
}
