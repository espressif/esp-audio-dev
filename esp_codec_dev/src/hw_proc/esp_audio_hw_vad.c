/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_log.h"

#include "esp_audio_hw_vad.h"
#include "audio_codec_hw_proc.h"

static const char *TAG = "AUDIO_HW_VAD";

static inline const void *_get_vad_ops(const esp_audio_hw_proc_ops_t *hw_proc)
{
    return hw_proc ? (const void *)hw_proc->vad : NULL;
}

static int esp_audio_hw_vad_resolve(esp_codec_dev_handle_t dev,
                                    const audio_codec_if_t **codec_if,
                                    const esp_audio_hw_vad_t **vad)
{
    int ret = esp_audio_hw_proc_resolve(dev, _get_vad_ops, codec_if, (const void **)vad);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to resolve VAD ops: ret 0x%x", ret);
        return ret;
    }
    ret = esp_audio_hw_proc_check_open(*codec_if);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Codec is not open");
    }
    return ret;
}

int esp_audio_hw_vad_init(esp_codec_dev_handle_t dev, const esp_audio_hw_vad_cfg_t *cfg)
{
    if (dev == NULL || cfg == NULL) {
        ESP_LOGE(TAG, "Invalid handle or config");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *codec_if = NULL;
    const esp_audio_hw_vad_t *vad = NULL;
    int ret = esp_audio_hw_vad_resolve(dev, &codec_if, &vad);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    if (vad->init == NULL) {
        ESP_LOGE(TAG, "VAD init is not supported");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }

    ret = esp_audio_hw_proc_lock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to lock hw_proc: ret 0x%x", ret);
        return ret;
    }
    ret = vad->init(&codec_if->hw_base, cfg);
    esp_audio_hw_proc_unlock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "VAD init failed: ret 0x%x", ret);
    }
    return ret;
}

int esp_audio_hw_vad_enable(esp_codec_dev_handle_t dev, bool enable, bool output_enable)
{
    if (dev == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *codec_if = NULL;
    const esp_audio_hw_vad_t *vad = NULL;
    int ret = esp_audio_hw_vad_resolve(dev, &codec_if, &vad);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    if (vad->enable == NULL) {
        ESP_LOGE(TAG, "VAD enable is not supported");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }

    ret = esp_audio_hw_proc_lock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to lock hw_proc: ret 0x%x", ret);
        return ret;
    }
    ret = vad->enable(&codec_if->hw_base, enable, output_enable);
    esp_audio_hw_proc_unlock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "VAD enable failed: ret 0x%x", ret);
    }
    return ret;
}

int esp_audio_hw_vad_reset(esp_codec_dev_handle_t dev)
{
    if (dev == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *codec_if = NULL;
    const esp_audio_hw_vad_t *vad = NULL;
    int ret = esp_audio_hw_vad_resolve(dev, &codec_if, &vad);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    if (vad->reset == NULL) {
        ESP_LOGE(TAG, "VAD reset is not supported");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }

    ret = esp_audio_hw_proc_lock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to lock hw_proc: ret 0x%x", ret);
        return ret;
    }
    ret = vad->reset(&codec_if->hw_base);
    esp_audio_hw_proc_unlock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "VAD reset failed: ret 0x%x", ret);
    }
    return ret;
}

int esp_audio_hw_vad_get_status(esp_codec_dev_handle_t dev, esp_audio_hw_vad_status_t *status)
{
    if (dev == NULL || status == NULL) {
        ESP_LOGE(TAG, "Invalid handle or status");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *codec_if = NULL;
    const esp_audio_hw_vad_t *vad = NULL;
    int ret = esp_audio_hw_vad_resolve(dev, &codec_if, &vad);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    if (vad->get_status == NULL) {
        ESP_LOGE(TAG, "VAD get_status is not supported");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }

    ret = esp_audio_hw_proc_lock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to lock hw_proc: ret 0x%x", ret);
        return ret;
    }
    ret = vad->get_status(&codec_if->hw_base, status);
    esp_audio_hw_proc_unlock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "VAD get_status failed: ret 0x%x", ret);
    }
    return ret;
}

int esp_audio_hw_vad_set_event_cb(esp_codec_dev_handle_t dev, esp_audio_hw_vad_event_cb_t cb, void *arg)
{
    if (dev == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *codec_if = NULL;
    const esp_audio_hw_vad_t *vad = NULL;
    int ret = esp_audio_hw_vad_resolve(dev, &codec_if, &vad);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    if (vad->set_event_cb == NULL) {
        ESP_LOGE(TAG, "VAD set_event_cb is not supported");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }

    ret = esp_audio_hw_proc_lock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to lock hw_proc: ret 0x%x", ret);
        return ret;
    }
    ret = vad->set_event_cb(&codec_if->hw_base, cb, arg);
    esp_audio_hw_proc_unlock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "VAD set_event_cb failed: ret 0x%x", ret);
    }
    return ret;
}

int esp_audio_hw_vad_get_frame_info(esp_codec_dev_handle_t dev, esp_audio_hw_vad_frame_info_t *info)
{
    if (dev == NULL || info == NULL) {
        ESP_LOGE(TAG, "Invalid handle or frame info");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *codec_if = NULL;
    const esp_audio_hw_vad_t *vad = NULL;
    int ret = esp_audio_hw_vad_resolve(dev, &codec_if, &vad);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    if (vad->get_frame_info == NULL) {
        ESP_LOGE(TAG, "VAD get_frame_info is not supported");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }

    ret = esp_audio_hw_proc_lock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to lock hw_proc: ret 0x%x", ret);
        return ret;
    }
    ret = vad->get_frame_info(&codec_if->hw_base, info);
    esp_audio_hw_proc_unlock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "VAD get_frame_info failed: ret 0x%x", ret);
    }
    return ret;
}

int esp_audio_hw_vad_read_frame(esp_codec_dev_handle_t dev, uint8_t *buf, size_t len, size_t *read_len)
{
    /* Clear first so the "no bytes reported on failure" contract holds on every error path */
    if (read_len != NULL) {
        *read_len = 0;
    }
    if (dev == NULL || buf == NULL || read_len == NULL) {
        ESP_LOGE(TAG, "Invalid handle or buffer");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *codec_if = NULL;
    const esp_audio_hw_vad_t *vad = NULL;
    int ret = esp_audio_hw_vad_resolve(dev, &codec_if, &vad);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    if (vad->read_frame == NULL) {
        ESP_LOGE(TAG, "VAD read_frame is not supported");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }

    ret = esp_audio_hw_proc_lock();
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to lock hw_proc: ret 0x%x", ret);
        return ret;
    }
    ret = vad->read_frame(&codec_if->hw_base, buf, len, read_len);
    esp_audio_hw_proc_unlock();
    if (ret != ESP_CODEC_DEV_OK) {
        /* Keep the documented contract even if a driver left a count behind on failure */
        *read_len = 0;
        ESP_LOGE(TAG, "VAD read_frame failed: ret 0x%x", ret);
    }
    return ret;
}
