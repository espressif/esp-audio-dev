/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "esp_log.h"

#include "es7243_adc.h"
#include "es_common.h"
#include "codec_reg_dump.h"
#include "esp_codec_dev_os.h"

static const char *TAG = "ES7243";

/**
 * @brief  ES7243 codec driver instance
 */
typedef struct {
    audio_codec_if_t    base;                                   /*!< Codec interface vtable container */
    audio_hw_adc_if_t   adc_ops;                                /*!< ADC operation callbacks */
    es7243_codec_cfg_t  cfg;                                    /*!< Board configuration snapshot */
    bool                is_open;                                /*!< True after open completes */
    bool                enabled;                                /*!< True when ADC path is running */
    char                adc_label[AUDIO_HW_ADC_LABEL_MAX_LEN];  /*!< ADC label for multi-instance routing */
} audio_codec_es7243_t;

static int es7243_write_reg(audio_codec_es7243_t *codec, int reg, int value)
{
    return codec->cfg.ctrl_if->write_reg(codec->cfg.ctrl_if, reg, 1, &value, 1);
}

static int es7243_read_reg(audio_codec_es7243_t *codec, int reg, int *value)
{
    *value = 0;
    return codec->cfg.ctrl_if->read_reg(codec->cfg.ctrl_if, reg, 1, value, 1);
}

static int es7243_adc_set_voice_mute(audio_codec_es7243_t *codec, bool mute)
{
    int ret = ESP_CODEC_DEV_OK;
    ESP_LOGI(TAG, "mute = %d", mute);
    if (mute) {
        ret |= es7243_write_reg(codec, 0x05, 0x1B);
    } else {
        ret |= es7243_write_reg(codec, 0x05, 0x13);
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es7243_adc_set_gain(audio_codec_es7243_t *codec, float db)
{
    int ret = 0;
    if (db <= 1) {
        ret = es7243_write_reg(codec, 0x08, 0x11);  // 1db
    } else if (db <= 4) {
        ret = es7243_write_reg(codec, 0x08, 0x13);  // 3.5db
    } else if (db <= 18) {
        ret = es7243_write_reg(codec, 0x08, 0x21);  // 18db
    } else if (db < 21) {
        ret = es7243_write_reg(codec, 0x08, 0x23);  // 20.5db
    } else if (db < 23) {
        ret = es7243_write_reg(codec, 0x08, 0x06);  // 22.5db
    } else if (db < 25) {
        ret = es7243_write_reg(codec, 0x08, 0x41);  // 24.5db
    } else {
        ret = es7243_write_reg(codec, 0x08, 0x43);  // 27db
    }
    ESP_LOGI(TAG, "Set DB %f", db);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es7243_adc_enable(audio_codec_es7243_t *codec, bool enable)
{
    int ret = ESP_CODEC_DEV_OK;
    if (enable) {
        // Slave mode only
        ret |= es7243_write_reg(codec, 0x00, 0x01);
        ret |= es7243_write_reg(codec, 0x06, 0x00);
        ret |= es7243_write_reg(codec, 0x05, 0x1B);
        ret |= es7243_write_reg(codec, 0x01, 0x0C);
        ret |= es7243_write_reg(codec, 0x08, 0x43);
        ret |= es7243_write_reg(codec, 0x05, 0x13);
    } else {
        ret |= es7243_write_reg(codec, 0x06, 0x05);
        ret |= es7243_write_reg(codec, 0x05, 0x1B);
        ret |= es7243_write_reg(codec, 0x06, 0x5C);
        ret |= es7243_write_reg(codec, 0x07, 0x3F);
        ret |= es7243_write_reg(codec, 0x08, 0x4B);
        ret |= es7243_write_reg(codec, 0x09, 0x9F);
    }
    ESP_LOGI(TAG, "Set enable %d", enable);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es7243_open(const audio_hw_base_t *h, void *cfg, int cfg_size)
{
    audio_codec_es7243_t *codec = (audio_codec_es7243_t *)h;
    es7243_codec_cfg_t *codec_cfg = (es7243_codec_cfg_t *)cfg;
    if (codec == NULL || codec_cfg == NULL || codec_cfg->ctrl_if == NULL || cfg_size != sizeof(es7243_codec_cfg_t)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    memcpy(&codec->cfg, codec_cfg, sizeof(es7243_codec_cfg_t));
    if (es7243_adc_enable(codec, true)) {
        ESP_LOGE(TAG, "Fail to write register");
        return ESP_CODEC_DEV_WRITE_FAIL;
    }
    codec->enabled = true;
    codec->is_open = true;
    return ESP_CODEC_DEV_OK;
}

static bool es7243_is_open(const audio_hw_base_t *h)
{
    audio_codec_es7243_t *codec = (audio_codec_es7243_t *)h;
    if (codec == NULL) {
        return false;
    }
    return codec->is_open;
}

static int es7243_mute(const audio_codec_if_t *h, int ch_mask, bool mute)
{
    audio_codec_es7243_t *codec = (audio_codec_es7243_t *)h;
    (void)ch_mask;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    return es7243_adc_set_voice_mute(codec, mute);
}

static int es7243_enable(const audio_codec_if_t *h, bool enable)
{
    audio_codec_es7243_t *codec = (audio_codec_es7243_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    if (codec->enabled == enable) {
        return ESP_CODEC_DEV_OK;
    }
    int ret = es7243_adc_enable(codec, enable);
    if (ret == ESP_CODEC_DEV_OK) {
        codec->enabled = enable;
        ESP_LOGD(TAG, "Codec is %s", enable ? "enabled" : "disabled");
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es7243_set_gain(const audio_codec_if_t *h, int ch_mask, float db)
{
    audio_codec_es7243_t *codec = (audio_codec_es7243_t *)h;
    (void)ch_mask;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    return es7243_adc_set_gain(codec, db);
}

static int es7243_close(const audio_hw_base_t *h)
{
    audio_codec_es7243_t *codec = (audio_codec_es7243_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open) {
        es7243_adc_enable(codec, false);
        codec->is_open = false;
    }
    return ESP_CODEC_DEV_OK;
}

static int es7243_set_reg(const audio_hw_base_t *h, int reg, int value)
{
    audio_codec_es7243_t *codec = (audio_codec_es7243_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = es7243_write_reg(codec, reg, value);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es7243_get_reg(const audio_hw_base_t *h, int reg, int *value)
{
    audio_codec_es7243_t *codec = (audio_codec_es7243_t *)h;
    if (codec == NULL || value == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = es7243_read_reg(codec, reg, value);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_READ_FAIL;
}

static void es7243_dump(const audio_hw_base_t *h)
{
    audio_codec_es7243_t *codec = (audio_codec_es7243_t *)h;
    if (codec == NULL || codec->is_open == false) {
        return;
    }
    codec_reg_dump_ctx_t dump;
    codec_reg_dump_init(&dump, TAG, 2);
    for (int i = 0; i < 10; i++) {
        int value = 0;
        int ret = es7243_read_reg(codec, i, &value);
        if (ret != ESP_CODEC_DEV_OK) {
            break;
        }
        codec_reg_dump_push(&dump, i, value);
    }
    codec_reg_dump_end(&dump);
}

static int es7243_get_adc_label(const audio_hw_base_t *h, const char **label)
{
    audio_codec_es7243_t *codec = (audio_codec_es7243_t *)h;
    if (codec == NULL || label == NULL || codec->adc_label[0] == '\0') {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *label = codec->adc_label;
    return ESP_CODEC_DEV_OK;
}

static void es7243_save_adc_label(audio_codec_es7243_t *codec, const char *label)
{
    codec->adc_label[0] = '\0';
    if (label != NULL) {
        strncpy(codec->adc_label, label, sizeof(codec->adc_label) - 1);
        codec->adc_label[sizeof(codec->adc_label) - 1] = '\0';
    }
}

const audio_codec_if_t *es7243_codec_new(es7243_codec_cfg_t *codec_cfg)
{
    if (codec_cfg == NULL || codec_cfg->ctrl_if == NULL) {
        ESP_LOGE(TAG, "Wrong codec config");
        return NULL;
    }
    if (codec_cfg->ctrl_if->is_open(codec_cfg->ctrl_if) == false) {
        ESP_LOGE(TAG, "Control interface not open yet");
        return NULL;
    }
    if (codec_cfg->ctrl_if->read_reg == NULL || codec_cfg->ctrl_if->write_reg == NULL) {
        ESP_LOGE(TAG, "Control interface missing read/write callback");
        return NULL;
    }

    audio_codec_es7243_t *codec = (audio_codec_es7243_t *)calloc(1, sizeof(audio_codec_es7243_t));
    if (codec == NULL) {
        ESP_LOGE(TAG, "Fail to alloc memory at %s:%d", __FUNCTION__, __LINE__);
        return NULL;
    }

    codec->base.hw_base.open = es7243_open;
    codec->base.hw_base.is_open = es7243_is_open;
    codec->base.hw_base.set_reg = es7243_set_reg;
    codec->base.hw_base.get_reg = es7243_get_reg;
    codec->base.hw_base.dump_reg = es7243_dump;
    codec->base.hw_base.close = es7243_close;
    codec->base.hw_base.get_adc_label = es7243_get_adc_label;
    codec->base.ctrl_if = codec_cfg->ctrl_if;
    es7243_save_adc_label(codec, codec_cfg->adc_cfg.label);

    codec->adc_ops.ops.enable = es7243_enable;
    codec->adc_ops.ops.mute = es7243_mute;
    codec->adc_ops.ops.set_vol = es7243_set_gain;
    codec->base.adc_if = &codec->adc_ops;
    codec->base.dac_if = NULL;

    do {
        int ret = codec->base.hw_base.open(&codec->base.hw_base, codec_cfg, sizeof(es7243_codec_cfg_t));
        if (ret != 0) {
            ESP_LOGE(TAG, "Open fail, ret: %d", ret);
            break;
        }
        return &codec->base;
    } while (0);
    if (codec) {
        free(codec);
    }
    return NULL;
}
