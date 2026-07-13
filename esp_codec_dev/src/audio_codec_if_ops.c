/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_check.h"

#include "audio_codec_if_ops.h"

static const char *TAG = "AUDIO_CODEC_IF";

int audio_codec_adc_enable(const audio_codec_if_t *h, bool enable)
{
    ESP_RETURN_ON_FALSE(h && h->adc_if && h->adc_if->ops.enable, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    return h->adc_if->ops.enable(h, enable);
}

int audio_codec_adc_mute(const audio_codec_if_t *h, int ch_mask, bool mute)
{
    ESP_RETURN_ON_FALSE(h && h->adc_if && h->adc_if->ops.mute, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    return h->adc_if->ops.mute(h, ch_mask, mute);
}

int audio_codec_adc_set_vol(const audio_codec_if_t *h, int ch_mask, float db)
{
    ESP_RETURN_ON_FALSE(h && h->adc_if && h->adc_if->ops.set_vol, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    return h->adc_if->ops.set_vol(h, ch_mask, db);
}

int audio_codec_dac_enable(const audio_codec_if_t *h, bool enable)
{
    ESP_RETURN_ON_FALSE(h && h->dac_if && h->dac_if->ops.enable, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    return h->dac_if->ops.enable(h, enable);
}

int audio_codec_dac_mute(const audio_codec_if_t *h, int ch_mask, bool mute)
{
    ESP_RETURN_ON_FALSE(h && h->dac_if && h->dac_if->ops.mute, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    return h->dac_if->ops.mute(h, ch_mask, mute);
}

int audio_codec_dac_set_vol(const audio_codec_if_t *h, int ch_mask, float db)
{
    ESP_RETURN_ON_FALSE(h && h->dac_if && h->dac_if->ops.set_vol, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    return h->dac_if->ops.set_vol(h, ch_mask, db);
}

int audio_codec_dac_pa_enable(const audio_codec_if_t *h, bool enable)
{
    ESP_RETURN_ON_FALSE(h && h->dac_if && h->dac_if->pa.enable, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    return h->dac_if->pa.enable(h, enable);
}
