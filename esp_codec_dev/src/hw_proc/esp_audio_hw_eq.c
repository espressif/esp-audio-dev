/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_log.h"

#include "esp_audio_hw_eq.h"
#include "audio_codec_hw_proc.h"

static const char *TAG = "AUDIO_HW_EQ";

static inline const void *_get_eq_ops(const esp_audio_hw_proc_ops_t *hw_proc)
{
    return hw_proc ? (const void *)hw_proc->eq : NULL;
}

int esp_audio_hw_eq_set_cfg(esp_codec_dev_handle_t dev, const esp_audio_hw_eq_cfg_t *cfg)
{
    if (dev == NULL || cfg == NULL || cfg->para == NULL || cfg->filter_num == 0) {
        ESP_LOGE(TAG, "Invalid handle or config");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *codec_if = NULL;
    const esp_audio_hw_eq_t *eq = NULL;
    int ret = esp_audio_hw_proc_resolve(dev, _get_eq_ops, &codec_if, (const void **)&eq);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to resolve EQ ops: ret 0x%x", ret);
        return ret;
    }
    if (eq->set_cfg == NULL) {
        ESP_LOGE(TAG, "EQ set_cfg is not supported");
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
    ret = eq->set_cfg(&codec_if->hw_base, cfg);
    esp_audio_hw_proc_unlock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "EQ set_cfg failed: ret 0x%x", ret);
    }
    return ret;
}

int esp_audio_hw_eq_set_band_para(esp_codec_dev_handle_t dev, const esp_audio_hw_eq_para_t *para, int index)
{
    if (dev == NULL || para == NULL) {
        ESP_LOGE(TAG, "Invalid handle or band parameter");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *codec_if = NULL;
    const esp_audio_hw_eq_t *eq = NULL;
    int ret = esp_audio_hw_proc_resolve(dev, _get_eq_ops, &codec_if, (const void **)&eq);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to resolve EQ ops: ret 0x%x", ret);
        return ret;
    }
    if (eq->set_band_para == NULL) {
        ESP_LOGE(TAG, "EQ set_band_para is not supported");
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
    ret = eq->set_band_para(&codec_if->hw_base, para, index);
    esp_audio_hw_proc_unlock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "EQ set_band_para failed: ret 0x%x", ret);
    }
    return ret;
}

int esp_audio_hw_eq_enable(esp_codec_dev_handle_t dev, bool enable)
{
    if (dev == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *codec_if = NULL;
    const esp_audio_hw_eq_t *eq = NULL;
    int ret = esp_audio_hw_proc_resolve(dev, _get_eq_ops, &codec_if, (const void **)&eq);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to resolve EQ ops: ret 0x%x", ret);
        return ret;
    }
    if (eq->enable == NULL) {
        ESP_LOGE(TAG, "EQ enable is not supported");
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
    ret = eq->enable(&codec_if->hw_base, enable);
    esp_audio_hw_proc_unlock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "EQ enable failed: ret 0x%x", ret);
    }
    return ret;
}

int esp_audio_hw_eq_dump_info(esp_codec_dev_handle_t dev)
{
    if (dev == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *codec_if = NULL;
    const esp_audio_hw_eq_t *eq = NULL;
    int ret = esp_audio_hw_proc_resolve(dev, _get_eq_ops, &codec_if, (const void **)&eq);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to resolve EQ ops: ret 0x%x", ret);
        return ret;
    }
    if (eq->dump_info == NULL) {
        ESP_LOGE(TAG, "EQ dump_info is not supported");
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
    ret = eq->dump_info(&codec_if->hw_base);
    esp_audio_hw_proc_unlock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "EQ dump_info failed: ret 0x%x", ret);
    }
    return ret;
}
