/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "esp_log.h"

#include "tas5805m_dac.h"
#include "tas5805m_reg.h"
#include "tas5805m_reg_cfg.h"
#include "esp_codec_dev_os.h"
#include "esp_codec_dev_vol.h"
#include "codec_reg_dump.h"

static const char *TAG = "TAS5805M";

/**
 * @brief  TAS5805M codec driver instance
 */
typedef struct {
    audio_codec_if_t      base;     /*!< Codec interface vtable container */
    audio_hw_dac_if_t     dac_ops;  /*!< DAC operation callbacks */
    tas5805m_codec_cfg_t  cfg;      /*!< Board configuration snapshot */
    bool                  is_open;  /*!< True after open completes */
    bool                  enabled;  /*!< True when DAC path is running */
    bool                  muted;    /*!< True when output is muted */
    float                 hw_gain;  /*!< Cached hardware gain in dB */
} audio_codec_tas5805m_t;

static const esp_codec_dev_vol_range_t vol_range = {
    .min_vol = {
        .vol = 0xFE,
        .db_value = -103.0,
    },
    .max_vol = {
        .vol = 0,
        .db_value = 24.0,
    },
};

static int tas5805m_write_reg(audio_codec_tas5805m_t *codec, int reg, int value)
{
    return codec->cfg.ctrl_if->write_reg(codec->cfg.ctrl_if, reg, 1, &value, 1);
}

static int tas5805m_read_reg(audio_codec_tas5805m_t *codec, int reg, int *value)
{
    *value = 0;
    return codec->cfg.ctrl_if->read_reg(codec->cfg.ctrl_if, reg, 1, value, 1);
}

static int tas5805m_write_data(audio_codec_tas5805m_t *codec, uint8_t reg_addr, uint8_t *data, int size)
{
    return codec->cfg.ctrl_if->write_reg(codec->cfg.ctrl_if, reg_addr, 1, data, size);
}

static int tas5805m_transmit_registers(audio_codec_tas5805m_t *codec, const tas5805m_cfg_reg_t *conf_buf, int size)
{
    int i = 0;
    int ret = 0;
    while (i < size) {
        switch (conf_buf[i].offset) {
            case CFG_META_SWITCH:
                // Used in legacy applications.  Ignored here.
                break;
            case CFG_META_DELAY:
                esp_codec_dev_sleep(conf_buf[i].value);
                break;
            case CFG_META_BURST:
                ret |= tas5805m_write_data(codec, conf_buf[i + 1].offset,
                                           (uint8_t *)(&conf_buf[i + 1].value), conf_buf[i].value);
                i += (conf_buf[i].value / 2) + 1;
                break;
            default:
                ret |= tas5805m_write_reg(codec, conf_buf[i].offset, conf_buf[i].value);
                break;
        }
        i++;
    }
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Fail to load configuration to tas5805m");
        return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int tas5805m_set_mute_fade(audio_codec_tas5805m_t *codec, int value)
{
    int ret = 0;
    uint8_t fade_reg = 0;
    /* Time for register value
     *   000: 11.5 ms
     *   001: 53 ms
     *   010: 106.5 ms
     *   011: 266.5 ms
     *   100: 0.535 sec
     *   101: 1.065 sec
     *   110: 2.665 sec
     *   111: 5.33 sec
     */
    if (value <= 12) {
        fade_reg = 0;
    } else if (value <= 53) {
        fade_reg = 1;
    } else if (value <= 107) {
        fade_reg = 2;
    } else if (value <= 267) {
        fade_reg = 3;
    } else if (value <= 535) {
        fade_reg = 4;
    } else if (value <= 1065) {
        fade_reg = 5;
    } else if (value <= 2665) {
        fade_reg = 6;
    } else {
        fade_reg = 7;
    }
    fade_reg |= (fade_reg << 4);
    ret |= tas5805m_write_reg(codec, MUTE_TIME_REG_ADDR, fade_reg);
    ESP_LOGD(TAG, "Set mute fade, value:%d, reg:0x%x", value, fade_reg);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static void tas5805m_reset(audio_codec_tas5805m_t *codec)
{
    int16_t reset_pin = codec->cfg.reset_cfg.reset_pin;
    bool reset_active_low = codec->cfg.reset_cfg.reset_active_low;
    if (reset_pin <= 0 || codec->cfg.gpio_if == NULL) {
        return;
    }
    codec->cfg.gpio_if->setup(reset_pin, AUDIO_GPIO_DIR_OUT, AUDIO_GPIO_MODE_FLOAT);
    codec->cfg.gpio_if->set(reset_pin, reset_active_low ? 0 : 1);
    esp_codec_dev_sleep(20);
    codec->cfg.gpio_if->set(reset_pin, reset_active_low ? 1 : 0);
    esp_codec_dev_sleep(200);
}

static int tas5805m_pa_enable(const audio_codec_if_t *h, bool enable)
{
    audio_codec_tas5805m_t *codec = (audio_codec_tas5805m_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->cfg.gpio_if == NULL || codec->cfg.pa_cfg.pa_pin < 0) {
        return ESP_CODEC_DEV_OK;
    }
    return codec->cfg.gpio_if->set(codec->cfg.pa_cfg.pa_pin,
                                   codec->cfg.pa_cfg.pa_active_low ? !enable : enable);
}

static int tas5805m_set_mute(const audio_codec_if_t *h, int ch_mask, bool enable)
{
    audio_codec_tas5805m_t *codec = (audio_codec_tas5805m_t *)h;
    (void)ch_mask;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int mute_reg = 0;
    int ret = ESP_CODEC_DEV_OK;
    ret |= tas5805m_read_reg(codec, TAS5805M_REG_03, &mute_reg);
    if (ret != ESP_CODEC_DEV_OK) {
        return ESP_CODEC_DEV_READ_FAIL;
    }
    if (enable) {
        mute_reg |= 0x8;
    } else {
        mute_reg &= (~0x08);
    }
    ret |= tas5805m_write_reg(codec, TAS5805M_REG_03, mute_reg);
    if (ret == ESP_CODEC_DEV_OK) {
        codec->muted = enable;
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int tas5805m_open(const audio_hw_base_t *h, void *cfg, int cfg_size)
{
    audio_codec_tas5805m_t *codec = (audio_codec_tas5805m_t *)h;
    tas5805m_codec_cfg_t *codec_cfg = (tas5805m_codec_cfg_t *)cfg;
    if (codec == NULL || codec_cfg == NULL || codec_cfg->ctrl_if == NULL || cfg_size != sizeof(tas5805m_codec_cfg_t)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    memcpy(&codec->cfg, codec_cfg, sizeof(tas5805m_codec_cfg_t));
    if (codec->cfg.gpio_if && codec->cfg.pa_cfg.pa_pin >= 0) {
        codec->cfg.gpio_if->setup(codec->cfg.pa_cfg.pa_pin, AUDIO_GPIO_DIR_OUT, AUDIO_GPIO_MODE_FLOAT);
    }
    tas5805m_pa_enable(&codec->base, false);
    tas5805m_reset(codec);
    int ret = tas5805m_transmit_registers(codec, tas5805m_registers,
                                          sizeof(tas5805m_registers) / sizeof(tas5805m_registers[0]));
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Fail write register group");
    } else {
        codec->is_open = true;
        codec->enabled = false;
        codec->muted = false;
        tas5805m_set_mute_fade(codec, 50);
        tas5805m_set_mute(&codec->base, 0, true);
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static bool tas5805m_is_open(const audio_hw_base_t *h)
{
    audio_codec_tas5805m_t *codec = (audio_codec_tas5805m_t *)h;
    if (codec == NULL) {
        return false;
    }
    return codec->is_open;
}

static int tas5805m_set_volume(const audio_codec_if_t *h, int ch_mask, float db_value)
{
    audio_codec_tas5805m_t *codec = (audio_codec_tas5805m_t *)h;
    (void)ch_mask;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    db_value -= codec->hw_gain;
    int volume = esp_codec_dev_vol_calc_reg(&vol_range, db_value);
    ESP_LOGD(TAG, "Set volume reg:%x db:%f", volume, db_value);
    int ret = tas5805m_write_reg(codec, MASTER_VOL_REG_ADDR, volume);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int tas5805m_enable(const audio_codec_if_t *h, bool enable)
{
    audio_codec_tas5805m_t *codec = (audio_codec_tas5805m_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    if (codec->enabled == enable) {
        return ESP_CODEC_DEV_OK;
    }
    int ret = tas5805m_set_mute(h, 0, !enable);
    if (ret == ESP_CODEC_DEV_OK) {
        ret = tas5805m_pa_enable(h, enable);
    }
    if (ret == ESP_CODEC_DEV_OK) {
        codec->enabled = enable;
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int tas5805m_set_reg(const audio_hw_base_t *h, int reg, int value)
{
    audio_codec_tas5805m_t *codec = (audio_codec_tas5805m_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = tas5805m_write_reg(codec, reg, value);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int tas5805m_get_reg(const audio_hw_base_t *h, int reg, int *value)
{
    audio_codec_tas5805m_t *codec = (audio_codec_tas5805m_t *)h;
    if (codec == NULL || value == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = tas5805m_read_reg(codec, reg, value);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_READ_FAIL;
}

static int tas5805m_close(const audio_hw_base_t *h)
{
    audio_codec_tas5805m_t *codec = (audio_codec_tas5805m_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open) {
        tas5805m_set_mute(&codec->base, 0, true);
        tas5805m_pa_enable(&codec->base, false);
        codec->enabled = false;
        codec->muted = false;
    }
    codec->is_open = false;
    return ESP_CODEC_DEV_OK;
}

static void tas5805m_dump(const audio_hw_base_t *h)
{
    audio_codec_tas5805m_t *codec = (audio_codec_tas5805m_t *)h;
    if (codec == NULL || codec->is_open == false) {
        return;
    }
    codec_reg_dump_ctx_t dump;
    codec_reg_dump_init(&dump, TAG, 2);
    for (int i = 0; i <= TAS5805M_REG_7F; i++) {
        int value = 0;
        if (tas5805m_read_reg(codec, i, &value) != ESP_CODEC_DEV_OK) {
            break;
        }
        codec_reg_dump_push(&dump, i, value);
    }
    codec_reg_dump_end(&dump);
}

const audio_codec_if_t *tas5805m_codec_new(tas5805m_codec_cfg_t *codec_cfg)
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

    audio_codec_tas5805m_t *codec = (audio_codec_tas5805m_t *)calloc(1, sizeof(audio_codec_tas5805m_t));
    if (codec == NULL) {
        ESP_LOGE(TAG, "Fail to alloc memory at %s:%d", __FUNCTION__, __LINE__);
        return NULL;
    }

    codec->base.hw_base.open = tas5805m_open;
    codec->base.hw_base.is_open = tas5805m_is_open;
    codec->base.hw_base.set_reg = tas5805m_set_reg;
    codec->base.hw_base.get_reg = tas5805m_get_reg;
    codec->base.hw_base.close = tas5805m_close;
    codec->base.hw_base.dump_reg = tas5805m_dump;
    codec->base.ctrl_if = codec_cfg->ctrl_if;

    codec->dac_ops.ops.enable = tas5805m_enable;
    codec->dac_ops.ops.set_vol = tas5805m_set_volume;
    codec->dac_ops.ops.mute = tas5805m_set_mute;
    codec->dac_ops.pa.enable = tas5805m_pa_enable;
    codec->base.dac_if = &codec->dac_ops;
    codec->base.adc_if = NULL;

    codec->hw_gain = esp_codec_dev_vol_calc_hw_gain(&codec_cfg->pa_cfg.hw_gain);
    do {
        int ret = codec->base.hw_base.open(&codec->base.hw_base, codec_cfg, sizeof(tas5805m_codec_cfg_t));
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
