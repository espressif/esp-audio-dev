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
#include "audio_hw_line.h"

static const char *TAG = "AUDIO_HW_LINE";

int audio_hw_line_new(const audio_codec_if_t *codec_if, audio_hw_line_handle_t *line)
{
    ESP_RETURN_ON_FALSE(codec_if && line, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(codec_if->hw_proc && codec_if->hw_proc->line_new,
                        ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return codec_if->hw_proc->line_new(&codec_if->hw_base, line);
}

int audio_hw_line_enable_in(const audio_hw_line_handle_t h, bool enable)
{
    audio_hw_line_t *line = (audio_hw_line_t *)h;
    ESP_RETURN_ON_FALSE(line, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(line->enable_in, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return line->enable_in(line, enable);
}

int audio_hw_line_enable_out(const audio_hw_line_handle_t h, bool enable)
{
    audio_hw_line_t *line = (audio_hw_line_t *)h;
    ESP_RETURN_ON_FALSE(line, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(line->enable_out, ESP_CODEC_DEV_NOT_SUPPORT, TAG, "Not supported");
    return line->enable_out(line, enable);
}

int audio_hw_line_delete(audio_hw_line_handle_t line)
{
    ESP_RETURN_ON_FALSE(line, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    free((void *)line);
    return ESP_CODEC_DEV_OK;
}
