/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include <stdlib.h>

#include "esp_check.h"

#include "audio_codec_uac_priv.h"

typedef struct {
    audio_codec_if_t             base;
    audio_hw_adc_if_t            adc_ops;
    audio_hw_dac_if_t            dac_ops;
    uac_binding_t               *binding;
    esp_codec_dev_sample_info_t  fs;
    bool                         is_open;
    bool                         adc_enabled;
    bool                         dac_enabled;
} uac_codec_t;

static const char *TAG = "CODEC_UAC_DEVICE";

static bool uac_codec_is_open(const audio_hw_base_t *h)
{
    const uac_codec_t *codec = (const uac_codec_t *)h;
    return codec != NULL && codec->is_open;
}

static int uac_codec_open(const audio_hw_base_t *h, void *cfg, int cfg_size)
{
    (void)cfg;
    (void)cfg_size;
    uac_codec_t *codec = (uac_codec_t *)h;
    ESP_RETURN_ON_FALSE(codec != NULL, ESP_CODEC_DEV_INVALID_ARG, TAG, "codec is NULL");
    codec->is_open = true;
    return ESP_CODEC_DEV_OK;
}

static int uac_codec_close(const audio_hw_base_t *h)
{
    uac_codec_t *codec = (uac_codec_t *)h;
    ESP_RETURN_ON_FALSE(codec != NULL, ESP_CODEC_DEV_INVALID_ARG, TAG, "codec is NULL");
    codec->adc_enabled = false;
    codec->dac_enabled = false;
    codec->is_open = false;
    memset(&codec->fs, 0, sizeof(codec->fs));
    return ESP_CODEC_DEV_OK;
}

static int uac_codec_set_fs(const audio_hw_base_t *h, esp_codec_dev_sample_info_t *fs, esp_codec_dev_type_t type)
{
    (void)type;
    uac_codec_t *codec = (uac_codec_t *)h;
    ESP_RETURN_ON_FALSE(codec != NULL && fs != NULL, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid set_fs args");
    codec->fs = *fs;
    return ESP_CODEC_DEV_OK;
}

static int uac_codec_get_caps(const audio_hw_base_t *h, esp_codec_dev_type_t dev_type,
                              esp_codec_dev_capability_t *caps, int *count)
{
    uac_codec_t *codec = (uac_codec_t *)h;
    ESP_RETURN_ON_FALSE(codec != NULL, ESP_CODEC_DEV_INVALID_ARG, TAG, "codec is NULL");
    return audio_codec_uac_get_caps(codec->binding, dev_type, caps, count);
}

static int uac_codec_set_reg(const audio_hw_base_t *h, int reg, int value)
{
    (void)h;
    (void)reg;
    (void)value;
    return ESP_CODEC_DEV_NOT_SUPPORT;
}

static int uac_codec_get_reg(const audio_hw_base_t *h, int reg, int *value)
{
    (void)h;
    (void)reg;
    if (value) {
        *value = 0;
    }
    return ESP_CODEC_DEV_NOT_SUPPORT;
}

static int uac_codec_adc_enable(const audio_codec_if_t *h, bool enable)
{
    uac_codec_t *codec = (uac_codec_t *)h;
    ESP_RETURN_ON_FALSE(codec != NULL, ESP_CODEC_DEV_INVALID_ARG, TAG, "codec is NULL");
    ESP_RETURN_ON_FALSE(codec->is_open, ESP_CODEC_DEV_WRONG_STATE, TAG, "codec not opened");
    codec->adc_enabled = enable;
    return ESP_CODEC_DEV_OK;
}

static int uac_codec_dac_enable(const audio_codec_if_t *h, bool enable)
{
    uac_codec_t *codec = (uac_codec_t *)h;
    ESP_RETURN_ON_FALSE(codec != NULL, ESP_CODEC_DEV_INVALID_ARG, TAG, "codec is NULL");
    ESP_RETURN_ON_FALSE(codec->is_open, ESP_CODEC_DEV_WRONG_STATE, TAG, "codec not opened");
    codec->dac_enabled = enable;
    return ESP_CODEC_DEV_OK;
}

static int uac_codec_adc_mute(const audio_codec_if_t *h, int ch_mask, bool mute)
{
    (void)ch_mask;
    uac_codec_t *codec = (uac_codec_t *)h;
    ESP_RETURN_ON_FALSE(codec != NULL, ESP_CODEC_DEV_INVALID_ARG, TAG, "codec is NULL");
    return audio_codec_uac_set_mute(codec->binding, ESP_CODEC_DEV_TYPE_IN, mute);
}

static int uac_codec_dac_mute(const audio_codec_if_t *h, int ch_mask, bool mute)
{
    (void)ch_mask;
    uac_codec_t *codec = (uac_codec_t *)h;
    ESP_RETURN_ON_FALSE(codec != NULL, ESP_CODEC_DEV_INVALID_ARG, TAG, "codec is NULL");
    return audio_codec_uac_set_mute(codec->binding, ESP_CODEC_DEV_TYPE_OUT, mute);
}

static int uac_codec_adc_set_vol(const audio_codec_if_t *h, int ch_mask, float db)
{
    (void)ch_mask;
    uac_codec_t *codec = (uac_codec_t *)h;
    ESP_RETURN_ON_FALSE(codec != NULL, ESP_CODEC_DEV_INVALID_ARG, TAG, "codec is NULL");
    return audio_codec_uac_set_volume_db(codec->binding, ESP_CODEC_DEV_TYPE_IN, db);
}

static int uac_codec_dac_set_vol(const audio_codec_if_t *h, int ch_mask, float db)
{
    (void)ch_mask;
    uac_codec_t *codec = (uac_codec_t *)h;
    ESP_RETURN_ON_FALSE(codec != NULL, ESP_CODEC_DEV_INVALID_ARG, TAG, "codec is NULL");
    return audio_codec_uac_set_volume_db(codec->binding, ESP_CODEC_DEV_TYPE_OUT, db);
}

const audio_codec_if_t *uac_codec_new(uac_binding_t *binding)
{
    ESP_RETURN_ON_FALSE(binding != NULL, NULL, TAG, "binding is NULL");
    uac_codec_t *codec = calloc(1, sizeof(uac_codec_t));
    ESP_RETURN_ON_FALSE(codec != NULL, NULL, TAG, "No memory for UAC codec");
    codec->binding = binding;

    codec->base.hw_base.open = uac_codec_open;
    codec->base.hw_base.is_open = uac_codec_is_open;
    codec->base.hw_base.close = uac_codec_close;
    codec->base.hw_base.set_fs = uac_codec_set_fs;
    codec->base.hw_base.set_reg = uac_codec_set_reg;
    codec->base.hw_base.get_reg = uac_codec_get_reg;
    codec->base.hw_base.get_caps = uac_codec_get_caps;

    if (binding->dev_type & ESP_CODEC_DEV_TYPE_IN) {
        codec->adc_ops.ops.enable = uac_codec_adc_enable;
        codec->adc_ops.ops.mute = uac_codec_adc_mute;
        codec->adc_ops.ops.set_vol = uac_codec_adc_set_vol;
        codec->base.adc_if = &codec->adc_ops;
    }
    if (binding->dev_type & ESP_CODEC_DEV_TYPE_OUT) {
        codec->dac_ops.ops.enable = uac_codec_dac_enable;
        codec->dac_ops.ops.mute = uac_codec_dac_mute;
        codec->dac_ops.ops.set_vol = uac_codec_dac_set_vol;
        codec->base.dac_if = &codec->dac_ops;
    }

    if (codec->base.hw_base.open(&codec->base.hw_base, NULL, 0) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Fail to open UAC codec");
        free(codec);
        return NULL;
    }
    return &codec->base;
}
