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
#include "audio_hw_eq.h"

static const char *TAG = "AUDIO_HW_EQ";

int audio_hw_eq_new(const audio_codec_if_t *codec_if, audio_hw_eq_handle_t *eq)
{
    ESP_RETURN_ON_FALSE(codec_if && eq, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(codec_if->hw_proc && codec_if->hw_proc->eq_new,
                        ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return codec_if->hw_proc->eq_new(&codec_if->hw_base, eq);
}

int audio_hw_eq_set_cfg(const audio_hw_eq_handle_t h, const audio_eq_cfg_t *cfg)
{
    audio_hw_eq_t *eq = (audio_hw_eq_t *)h;
    ESP_RETURN_ON_FALSE(eq, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(cfg, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid cfg");
    ESP_RETURN_ON_FALSE(cfg->para, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid EQ parameters");
    ESP_RETURN_ON_FALSE(cfg->filter_num > 0, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid filter number");
    ESP_RETURN_ON_FALSE(eq->set_cfg, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return eq->set_cfg(eq, cfg);
}

int audio_hw_eq_set_band_para(const audio_hw_eq_handle_t h, const eq_para_t *para, int index)
{
    audio_hw_eq_t *eq = (audio_hw_eq_t *)h;
    ESP_RETURN_ON_FALSE(eq, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(para, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid para");
    ESP_RETURN_ON_FALSE(eq->set_band_para, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return eq->set_band_para(eq, para, index);
}

int audio_hw_eq_enable(const audio_hw_eq_handle_t h, bool enable)
{
    audio_hw_eq_t *eq = (audio_hw_eq_t *)h;
    ESP_RETURN_ON_FALSE(eq, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(eq->enable, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return eq->enable(eq, enable);
}

int audio_hw_eq_dump_info(const audio_hw_eq_handle_t h)
{
    audio_hw_eq_t *eq = (audio_hw_eq_t *)h;
    ESP_RETURN_ON_FALSE(eq, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(eq->dump_info, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return eq->dump_info(eq);
}

int audio_hw_eq_delete(audio_hw_eq_handle_t eq)
{
    ESP_RETURN_ON_FALSE(eq, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    free((void *)eq);
    return ESP_CODEC_DEV_OK;
}
