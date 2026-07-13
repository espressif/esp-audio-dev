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
    audio_codec_data_if_t        base;
    bool                         is_open;
    uac_binding_t               *binding;
    esp_codec_dev_sample_info_t  fs;
} audio_codec_uac_data_t;

static const char *TAG = "CODEC_UAC_DATA";

static int uac_data_open(const audio_codec_data_if_t *h, void *data_cfg, int cfg_size)
{
    (void)data_cfg;
    (void)cfg_size;
    ESP_RETURN_ON_FALSE(h != NULL, ESP_CODEC_DEV_INVALID_ARG, TAG, "data_if is NULL");
    audio_codec_uac_data_t *uac_data = (audio_codec_uac_data_t *)h;
    uac_data->is_open = true;
    return ESP_CODEC_DEV_OK;
}

static bool uac_data_is_open(const audio_codec_data_if_t *h)
{
    audio_codec_uac_data_t *uac_data = (audio_codec_uac_data_t *)h;
    return uac_data != NULL && uac_data->is_open;
}

static int uac_data_get_fmt(const audio_codec_data_if_t *h, esp_codec_dev_type_t dev_type, esp_codec_dev_sample_info_t *fs)
{
    (void)dev_type;
    ESP_RETURN_ON_FALSE(h != NULL && fs != NULL, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid get_fmt args");
    audio_codec_uac_data_t *uac_data = (audio_codec_uac_data_t *)h;
    if (uac_data->fs.sample_rate == 0) {
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    *fs = uac_data->fs;
    return ESP_CODEC_DEV_OK;
}

static int uac_data_enable(const audio_codec_data_if_t *h, esp_codec_dev_type_t dev_type, bool enable)
{
    ESP_RETURN_ON_FALSE(h != NULL, ESP_CODEC_DEV_INVALID_ARG, TAG, "data_if is NULL");
    audio_codec_uac_data_t *uac_data = (audio_codec_uac_data_t *)h;
    if (enable && !uac_data->is_open) {
        int ret = uac_data_open(h, NULL, 0);
        if (ret != ESP_CODEC_DEV_OK) {
            return ret;
        }
    }
    return audio_codec_uac_enable(uac_data->binding, dev_type, enable);
}

static int uac_data_set_fmt(const audio_codec_data_if_t *h, esp_codec_dev_type_t dev_type, esp_codec_dev_sample_info_t *fs)
{
    ESP_RETURN_ON_FALSE(h != NULL && fs != NULL, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid set_fmt args");
    audio_codec_uac_data_t *uac_data = (audio_codec_uac_data_t *)h;
    uac_data->fs = *fs;
    return audio_codec_uac_set_fmt(uac_data->binding, dev_type, fs);
}

static int uac_data_read(const audio_codec_data_if_t *h, uint8_t *data, int size)
{
    audio_codec_uac_data_t *uac_data = (audio_codec_uac_data_t *)h;
    ESP_RETURN_ON_FALSE(uac_data != NULL, ESP_CODEC_DEV_INVALID_ARG, TAG, "data_if is NULL");
    return audio_codec_uac_read(uac_data->binding, data, size);
}

static int uac_data_write(const audio_codec_data_if_t *h, uint8_t *data, int size)
{
    audio_codec_uac_data_t *uac_data = (audio_codec_uac_data_t *)h;
    ESP_RETURN_ON_FALSE(uac_data != NULL, ESP_CODEC_DEV_INVALID_ARG, TAG, "data_if is NULL");
    return audio_codec_uac_write(uac_data->binding, data, size);
}

static int uac_data_close(const audio_codec_data_if_t *h)
{
    ESP_RETURN_ON_FALSE(h != NULL, ESP_CODEC_DEV_INVALID_ARG, TAG, "data_if is NULL");
    audio_codec_uac_data_t *uac_data = (audio_codec_uac_data_t *)h;
    uac_data->is_open = false;
    /* Tear down the underlying UAC stream so the device can be reopened later. */
    audio_codec_uac_enable(uac_data->binding, uac_data->binding->dev_type, false);
    memset(&uac_data->fs, 0, sizeof(uac_data->fs));
    return ESP_CODEC_DEV_OK;
}

const audio_codec_data_if_t *audio_codec_new_uac_data(uac_binding_t *binding)
{
    ESP_RETURN_ON_FALSE(binding != NULL, NULL, TAG, "binding is NULL");
    audio_codec_uac_data_t *uac_data = calloc(1, sizeof(audio_codec_uac_data_t));
    ESP_RETURN_ON_FALSE(uac_data != NULL, NULL, TAG, "No memory for UAC data_if");

    uac_data->binding = binding;
    uac_data->is_open = true;
    uac_data->base.open = uac_data_open;
    uac_data->base.is_open = uac_data_is_open;
    uac_data->base.get_fmt = uac_data_get_fmt;
    uac_data->base.enable = uac_data_enable;
    uac_data->base.set_fmt = uac_data_set_fmt;
    uac_data->base.read = uac_data_read;
    uac_data->base.write = uac_data_write;
    uac_data->base.close = uac_data_close;
    return &uac_data->base;
}
