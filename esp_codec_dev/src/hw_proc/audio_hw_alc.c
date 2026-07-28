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
#include "audio_hw_alc.h"

static const char *TAG = "AUDIO_HW_ALC";

int audio_hw_alc_new(const audio_codec_if_t *codec_if, audio_hw_alc_handle_t *alc)
{
    ESP_RETURN_ON_FALSE(codec_if && alc, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(codec_if->hw_proc && codec_if->hw_proc->alc_new,
                        ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return codec_if->hw_proc->alc_new(&codec_if->hw_base, alc);
}

int audio_hw_alc_init(const audio_hw_alc_handle_t h, const audio_alc_cfg_t *cfg)
{
    audio_hw_alc_t *alc = (audio_hw_alc_t *)h;
    ESP_RETURN_ON_FALSE(alc, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(cfg, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid cfg");
    ESP_RETURN_ON_FALSE(alc->init, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return alc->init(alc, cfg);
}

int audio_hw_alc_set_gain(const audio_hw_alc_handle_t h, float target_gain)
{
    audio_hw_alc_t *alc = (audio_hw_alc_t *)h;
    ESP_RETURN_ON_FALSE(alc, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(alc->set_gain, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return alc->set_gain(alc, target_gain);
}

int audio_hw_alc_set_channel_mask(const audio_hw_alc_handle_t h, int channel_mask)
{
    audio_hw_alc_t *alc = (audio_hw_alc_t *)h;
    ESP_RETURN_ON_FALSE(alc, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(alc->set_channel, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return alc->set_channel(alc, channel_mask);
}

int audio_hw_alc_set_noise_gate(const audio_hw_alc_handle_t h, float threshold)
{
    audio_hw_alc_t *alc = (audio_hw_alc_t *)h;
    ESP_RETURN_ON_FALSE(alc, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(alc->set_noise_gate, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return alc->set_noise_gate(alc, threshold);
}

int audio_hw_alc_delete(audio_hw_alc_handle_t alc)
{
    ESP_RETURN_ON_FALSE(alc, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    free((void *)alc);
    return ESP_CODEC_DEV_OK;
}
