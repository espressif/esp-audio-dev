/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>

#include "esp_check.h"

#include "audio_codec_if.h"
#include "audio_hw_proc_if.h"
#include "audio_hw_mute.h"

static const char *TAG = "AUDIO_HW_MUTE";

int audio_hw_mute_new(const audio_codec_if_t *codec_if, audio_hw_mute_handle_t *mute)
{
    ESP_RETURN_ON_FALSE(codec_if && mute, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(codec_if->hw_proc && codec_if->hw_proc->mute_new,
                        ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return codec_if->hw_proc->mute_new(&codec_if->hw_base, mute);
}

int audio_hw_auto_mute_set_cfg(const audio_hw_mute_handle_t h, const auto_mute_cfg_t *cfg)
{
    audio_hw_mute_t *mute = (audio_hw_mute_t *)h;
    ESP_RETURN_ON_FALSE(mute, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(cfg, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid cfg");
    ESP_RETURN_ON_FALSE(mute->set_auto_mute_cfg, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return mute->set_auto_mute_cfg(mute, cfg);
}

int audio_hw_auto_mute_enable(const audio_hw_mute_handle_t h, bool enable)
{
    audio_hw_mute_t *mute = (audio_hw_mute_t *)h;
    ESP_RETURN_ON_FALSE(mute, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(mute->enable_auto_mute, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return mute->enable_auto_mute(mute, enable);
}

int audio_hw_soft_mute_set_cfg(const audio_hw_mute_handle_t h, const soft_mute_cfg_t *cfg)
{
    audio_hw_mute_t *mute = (audio_hw_mute_t *)h;
    ESP_RETURN_ON_FALSE(mute, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(cfg, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid cfg");
    ESP_RETURN_ON_FALSE(mute->set_soft_mute_cfg, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return mute->set_soft_mute_cfg(mute, cfg);
}

int audio_hw_soft_mute_enable(const audio_hw_mute_handle_t h, bool enable)
{
    audio_hw_mute_t *mute = (audio_hw_mute_t *)h;
    ESP_RETURN_ON_FALSE(mute, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(mute->enable_soft_mute, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return mute->enable_soft_mute(mute, enable);
}

int audio_hw_mute_delete(audio_hw_mute_handle_t mute)
{
    ESP_RETURN_ON_FALSE(mute, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    free((void *)mute);
    return ESP_CODEC_DEV_OK;
}
