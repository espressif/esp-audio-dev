/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "esp_log.h"

#include "dummy_codec.h"

/**
 * @brief  Dummy codec driver instance
 */
typedef struct {
    audio_codec_if_t   base;     /*!< Codec interface vtable container */
    audio_hw_dac_if_t  dac_ops;  /*!< DAC operation callbacks */
    dummy_codec_cfg_t  cfg;      /*!< Board configuration snapshot */
    bool               is_open;  /*!< True after open completes */
    bool               enabled;  /*!< True when DAC path is running */
    bool               muted;    /*!< True when output is muted */
} dummy_codec_t;

static const char *TAG = "DUMMY_CODEC";

static int dummy_codec_pa_set(dummy_codec_t *codec, bool on)
{
    if (codec == NULL || codec->cfg.gpio_if == NULL || codec->cfg.pa_cfg.pa_pin < 0) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    bool level = codec->cfg.pa_cfg.pa_active_low ? !on : on;
    return codec->cfg.gpio_if->set(codec->cfg.pa_cfg.pa_pin, level);
}

static int dummy_codec_pa_enable(const audio_codec_if_t *h, bool enable)
{
    dummy_codec_t *codec = (dummy_codec_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    return dummy_codec_pa_set(codec, enable);
}

static bool dummy_codec_is_open(const audio_hw_base_t *h)
{
    const dummy_codec_t *codec = (const dummy_codec_t *)h;
    return codec && codec->is_open;
}

static int dummy_codec_open(const audio_hw_base_t *h, void *cfg, int cfg_size)
{
    dummy_codec_t *codec = (dummy_codec_t *)h;
    dummy_codec_cfg_t *codec_cfg = (dummy_codec_cfg_t *)cfg;
    if (codec == NULL || codec_cfg == NULL || cfg_size != sizeof(dummy_codec_cfg_t)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    memcpy(&codec->cfg, codec_cfg, sizeof(dummy_codec_cfg_t));
    codec->enabled = false;
    codec->muted = false;

    if (codec->cfg.gpio_if == NULL || codec->cfg.pa_cfg.pa_pin < 0) {
        ESP_LOGW(TAG, "PA control disabled, gpio_if:%p pa_pin:%d", codec->cfg.gpio_if, codec->cfg.pa_cfg.pa_pin);
    } else {
        int ret = codec->cfg.gpio_if->setup(codec->cfg.pa_cfg.pa_pin, AUDIO_GPIO_DIR_OUT, AUDIO_GPIO_MODE_FLOAT);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "PA gpio setup failed:%d", ret);
            return ret;
        }
        ret = dummy_codec_pa_set(codec, false);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "PA default off failed:%d", ret);
            return ret;
        }
    }

    codec->is_open = true;
    return ESP_CODEC_DEV_OK;
}

static int dummy_codec_enable(const audio_codec_if_t *h, bool enable)
{
    dummy_codec_t *codec = (dummy_codec_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    codec->enabled = enable;
    if (codec->muted) {
        return dummy_codec_pa_set(codec, false);
    }
    return dummy_codec_pa_set(codec, enable);
}

static int dummy_codec_mute(const audio_codec_if_t *h, int ch_mask, bool mute)
{
    dummy_codec_t *codec = (dummy_codec_t *)h;
    (void)ch_mask;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    codec->muted = mute;
    if (mute) {
        return dummy_codec_pa_set(codec, false);
    }
    return dummy_codec_pa_set(codec, codec->enabled);
}

static int dummy_codec_close(const audio_hw_base_t *h)
{
    dummy_codec_t *codec = (dummy_codec_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open) {
        (void)dummy_codec_pa_set(codec, false);
        codec->enabled = false;
        codec->muted = false;
        codec->is_open = false;
    }
    return ESP_CODEC_DEV_OK;
}

const audio_codec_if_t *dummy_codec_new(dummy_codec_cfg_t *cfg)
{
    if (cfg == NULL) {
        ESP_LOGE(TAG, "Wrong codec config");
        return NULL;
    }

    dummy_codec_t *codec = (dummy_codec_t *)calloc(1, sizeof(dummy_codec_t));
    if (codec == NULL) {
        ESP_LOGE(TAG, "Fail to alloc memory at %s:%d", __FUNCTION__, __LINE__);
        return NULL;
    }

    codec->base.hw_base.open = dummy_codec_open;
    codec->base.hw_base.is_open = dummy_codec_is_open;
    codec->base.hw_base.close = dummy_codec_close;
    codec->dac_ops.ops.enable = dummy_codec_enable;
    codec->dac_ops.ops.mute = dummy_codec_mute;
    codec->dac_ops.pa.enable = dummy_codec_pa_enable;
    codec->base.adc_if = NULL;
    codec->base.dac_if = &codec->dac_ops;

    int ret = codec->base.hw_base.open(&codec->base.hw_base, cfg, sizeof(dummy_codec_cfg_t));
    if (ret != 0) {
        ESP_LOGE(TAG, "Open fail, ret: %d", ret);
        free(codec);
        return NULL;
    }
    return &codec->base;
}
