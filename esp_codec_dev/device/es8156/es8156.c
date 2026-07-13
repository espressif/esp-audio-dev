/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "esp_log.h"

#include "es8156_dac.h"
#include "es8156_reg.h"
#include "es_common.h"
#include "codec_reg_dump.h"
#include "esp_codec_dev_os.h"

static const char *TAG = "ES8156";

/**
 * @brief  ES8156 codec driver instance
 */
typedef struct {
    audio_codec_if_t    base;     /*!< Codec interface vtable container */
    audio_hw_dac_if_t   dac_ops;  /*!< DAC operation callbacks */
    es8156_codec_cfg_t  cfg;      /*!< Board configuration snapshot */
    bool                is_open;  /*!< True after open completes */
    bool                enabled;  /*!< True when DAC path is running */
    float               hw_gain;  /*!< Cached hardware gain in dB */
} audio_codec_es8156_t;

/* The volume register mapped to decibel table can get from codec data-sheet
   Volume control register 0x14 description:
       0x00 - '-95.5dB' ... 0xFF - '+32dB'
*/
static const esp_codec_dev_vol_range_t vol_range = {
    .min_vol = {
        .vol = 0x0,
        .db_value = -95.5,
    },
    .max_vol = {
        .vol = 0xFF,
        .db_value = 32.0,
    },
};

static int es8156_write_reg(audio_codec_es8156_t *codec, int reg, int value)
{
    return codec->cfg.ctrl_if->write_reg(codec->cfg.ctrl_if, reg, 1, &value, 1);
}

static int es8156_read_reg(audio_codec_es8156_t *codec, int reg, int *value)
{
    *value = 0;
    return codec->cfg.ctrl_if->read_reg(codec->cfg.ctrl_if, reg, 1, value, 1);
}

static int es8156_stop(audio_codec_es8156_t *codec)
{
    int ret = 0;
    ret = es8156_write_reg(codec, 0x14, 0x00);
    ret |= es8156_write_reg(codec, 0x19, 0x02);
    ret |= es8156_write_reg(codec, 0x21, 0x1F);
    ret |= es8156_write_reg(codec, 0x22, 0x02);
    ret |= es8156_write_reg(codec, 0x25, 0x21);
    ret |= es8156_write_reg(codec, 0x25, 0xA1);
    ret |= es8156_write_reg(codec, 0x18, 0x01);
    ret |= es8156_write_reg(codec, 0x09, 0x02);
    ret |= es8156_write_reg(codec, 0x09, 0x01);
    ret |= es8156_write_reg(codec, 0x08, 0x00);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8156_start(audio_codec_es8156_t *codec)
{
    int ret = 0;
    ret |= es8156_write_reg(codec, 0x08, 0x3F);
    ret |= es8156_write_reg(codec, 0x09, 0x00);
    ret |= es8156_write_reg(codec, 0x18, 0x00);

    ret |= es8156_write_reg(codec, 0x25, 0x20);
    ret |= es8156_write_reg(codec, 0x22, 0x00);
    ret |= es8156_write_reg(codec, 0x21, 0x3C);
    ret |= es8156_write_reg(codec, 0x19, 0x20);
    ret |= es8156_write_reg(codec, 0x14, 179);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8156_set_mute(const audio_codec_if_t *h, int ch_mask, bool mute)
{
    audio_codec_es8156_t *codec = (audio_codec_es8156_t *)h;
    (void)ch_mask;
    if (codec == NULL || codec->is_open == false) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int regv;
    int ret = es8156_read_reg(codec, ES8156_DAC_MUTE_REG13, &regv);
    if (ret != ESP_CODEC_DEV_OK) {
        return ESP_CODEC_DEV_READ_FAIL;
    }
    if (mute) {
        regv = regv | BITS(1) | BITS(2);
    } else {
        regv = regv & (~(BITS(1) | BITS(2)));
    }
    ret = es8156_write_reg(codec, ES8156_DAC_MUTE_REG13, (uint8_t)regv);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8156_set_vol(const audio_codec_if_t *h, int ch_mask, float volume)
{
    audio_codec_es8156_t *codec = (audio_codec_es8156_t *)h;
    (void)ch_mask;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    volume -= codec->hw_gain;
    int reg = esp_codec_dev_vol_calc_reg(&vol_range, volume);
    int ret = es8156_write_reg(codec, ES8156_VOLUME_CONTROL_REG14, reg);
    ESP_LOGD(TAG, "Set volume reg:%x db:%f", reg, volume);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static void es8156_pa_power(audio_codec_es8156_t *codec, es_pa_setting_t pa_setting)
{
    int16_t pa_pin = codec->cfg.pa_cfg.pa_pin;
    if (pa_pin == -1 || codec->cfg.gpio_if == NULL) {
        return;
    }
    if (pa_setting & ES_PA_SETUP) {
        codec->cfg.gpio_if->setup(pa_pin, AUDIO_GPIO_DIR_OUT, AUDIO_GPIO_MODE_FLOAT);
    }
    if (pa_setting & ES_PA_ENABLE) {
        codec->cfg.gpio_if->set(pa_pin, codec->cfg.pa_cfg.pa_active_low ? false : true);
    }
    if (pa_setting & ES_PA_DISABLE) {
        codec->cfg.gpio_if->set(pa_pin, codec->cfg.pa_cfg.pa_active_low ? true : false);
    }
}

static int es8156_open(const audio_hw_base_t *h, void *cfg, int cfg_size)
{
    audio_codec_es8156_t *codec = (audio_codec_es8156_t *)h;
    es8156_codec_cfg_t *codec_cfg = (es8156_codec_cfg_t *)cfg;
    if (codec == NULL || codec_cfg == NULL || cfg_size != sizeof(es8156_codec_cfg_t) || codec_cfg->ctrl_if == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    int ret = ESP_CODEC_DEV_OK;
    memcpy(&codec->cfg, codec_cfg, sizeof(es8156_codec_cfg_t));
    es8156_pa_power(codec, ES_PA_SETUP | ES_PA_DISABLE);

    ret |= es8156_write_reg(codec, 0x02, 0x04);
    ret |= es8156_write_reg(codec, 0x20, 0x2A);
    ret |= es8156_write_reg(codec, 0x21, 0x3C);
    ret |= es8156_write_reg(codec, 0x22, 0x00);
    ret |= es8156_write_reg(codec, 0x24, 0x07);
    ret |= es8156_write_reg(codec, 0x23, 0x00);

    ret |= es8156_write_reg(codec, 0x0A, 0x01);
    ret |= es8156_write_reg(codec, 0x0B, 0x01);
    ret |= es8156_write_reg(codec, 0x11, 0x00);
    ret |= es8156_write_reg(codec, 0x14, 179);  // volume 70%

    ret |= es8156_write_reg(codec, 0x0D, 0x14);
    ret |= es8156_write_reg(codec, 0x18, 0x00);
    ret |= es8156_write_reg(codec, 0x08, 0x3F);
    ret |= es8156_write_reg(codec, 0x00, 0x02);
    ret |= es8156_write_reg(codec, 0x00, 0x03);
    ret |= es8156_write_reg(codec, 0x25, 0x20);
    if (ret != 0) {
        return ESP_CODEC_DEV_WRITE_FAIL;
    }
    codec->is_open = true;
    return ESP_CODEC_DEV_OK;
}

static int es8156_close(const audio_hw_base_t *h)
{
    audio_codec_es8156_t *codec = (audio_codec_es8156_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open) {
        es8156_set_mute(&codec->base, 0x03, true);
        es8156_pa_power(codec, ES_PA_DISABLE);
        es8156_stop(codec);
        codec->is_open = false;
    }
    return ESP_CODEC_DEV_OK;
}

static bool es8156_is_open(const audio_hw_base_t *h)
{
    audio_codec_es8156_t *codec = (audio_codec_es8156_t *)h;
    if (codec == NULL) {
        return false;
    }
    return codec->is_open;
}

static int es8156_enable(const audio_codec_if_t *h, bool enable)
{
    int ret = ESP_CODEC_DEV_OK;
    audio_codec_es8156_t *codec = (audio_codec_es8156_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    if (codec->enabled == enable) {
        return ESP_CODEC_DEV_OK;
    }
    if (enable) {
        ret = es8156_start(codec);
        es8156_pa_power(codec, ES_PA_ENABLE);
        es8156_set_mute(h, 0, false);
    } else {
        es8156_set_mute(h, 0, true);
        es8156_pa_power(codec, ES_PA_DISABLE);
        ret = es8156_stop(codec);
    }
    if (ret == ESP_CODEC_DEV_OK) {
        codec->enabled = enable;
        ESP_LOGD(TAG, "Codec is %s", enable ? "enabled" : "disabled");
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8156_pa_enable(const audio_codec_if_t *h, bool enable)
{
    audio_codec_es8156_t *codec = (audio_codec_es8156_t *)h;
    if (codec == NULL || codec->is_open == false) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    es8156_pa_power(codec, enable ? ES_PA_ENABLE : ES_PA_DISABLE);
    return ESP_CODEC_DEV_OK;
}

static int es8156_set_reg(const audio_hw_base_t *h, int reg, int value)
{
    audio_codec_es8156_t *codec = (audio_codec_es8156_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = es8156_write_reg(codec, reg, value);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8156_get_reg(const audio_hw_base_t *h, int reg, int *value)
{
    audio_codec_es8156_t *codec = (audio_codec_es8156_t *)h;
    if (codec == NULL || value == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = es8156_read_reg(codec, reg, value);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_READ_FAIL;
}

static void es8156_dump(const audio_hw_base_t *h)
{
    audio_codec_es8156_t *codec = (audio_codec_es8156_t *)h;
    if (codec == NULL || codec->is_open == false) {
        return;
    }
    codec_reg_dump_ctx_t dump;
    codec_reg_dump_init(&dump, TAG, 2);
    for (int i = 0; i <= 0x25; i++) {
        int value = 0;
        int ret = es8156_read_reg(codec, i, &value);
        if (ret != ESP_CODEC_DEV_OK) {
            break;
        }
        codec_reg_dump_push(&dump, i, value);
    }
    codec_reg_dump_end(&dump);
}

const audio_codec_if_t *es8156_codec_new(es8156_codec_cfg_t *codec_cfg)
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

    audio_codec_es8156_t *codec = (audio_codec_es8156_t *)calloc(1, sizeof(audio_codec_es8156_t));
    if (codec == NULL) {
        ESP_LOGE(TAG, "Fail to alloc memory at %s:%d", __FUNCTION__, __LINE__);
        return NULL;
    }

    codec->base.hw_base.open = es8156_open;
    codec->base.hw_base.is_open = es8156_is_open;
    codec->base.hw_base.set_reg = es8156_set_reg;
    codec->base.hw_base.get_reg = es8156_get_reg;
    codec->base.hw_base.dump_reg = es8156_dump;
    codec->base.hw_base.close = es8156_close;
    codec->base.ctrl_if = codec_cfg->ctrl_if;

    codec->dac_ops.ops.enable = es8156_enable;
    codec->dac_ops.ops.set_vol = es8156_set_vol;
    codec->dac_ops.ops.mute = es8156_set_mute;
    codec->dac_ops.pa.enable = es8156_pa_enable;
    codec->base.dac_if = &codec->dac_ops;
    codec->base.adc_if = NULL;

    codec->hw_gain = esp_codec_dev_vol_calc_hw_gain(&codec_cfg->pa_cfg.hw_gain);
    do {
        int ret = codec->base.hw_base.open(&codec->base.hw_base, codec_cfg, sizeof(es8156_codec_cfg_t));
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
