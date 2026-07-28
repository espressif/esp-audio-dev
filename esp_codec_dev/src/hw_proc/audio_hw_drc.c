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
#include "audio_hw_drc.h"

static const char *TAG = "AUDIO_HW_DRC";

int audio_hw_drc_new(const audio_codec_if_t *codec_if, audio_hw_drc_handle_t *drc)
{
    ESP_RETURN_ON_FALSE(codec_if && drc, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(codec_if->hw_proc && codec_if->hw_proc->drc_new,
                        ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return codec_if->hw_proc->drc_new(&codec_if->hw_base, drc);
}

int audio_hw_drc_set_offset_gain(const audio_hw_drc_handle_t h, float offset_gain)
{
    audio_hw_drc_t *drc = (audio_hw_drc_t *)h;
    ESP_RETURN_ON_FALSE(drc, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(drc->set_offset_gain, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return drc->set_offset_gain(drc, offset_gain);
}

int audio_hw_drc_init(const audio_hw_drc_handle_t h, const audio_drc_cfg_t *cfg)
{
    audio_hw_drc_t *drc = (audio_hw_drc_t *)h;
    ESP_RETURN_ON_FALSE(drc, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(cfg, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid cfg");
    ESP_RETURN_ON_FALSE(drc->init, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return drc->init(drc, cfg);
}

int audio_hw_drc_enable(const audio_hw_drc_handle_t h, bool enable)
{
    audio_hw_drc_t *drc = (audio_hw_drc_t *)h;
    ESP_RETURN_ON_FALSE(drc, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(drc->enable, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return drc->enable(drc, enable);
}

int audio_hw_drc_delete(audio_hw_drc_handle_t drc)
{
    ESP_RETURN_ON_FALSE(drc, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    free((void *)drc);
    return ESP_CODEC_DEV_OK;
}
