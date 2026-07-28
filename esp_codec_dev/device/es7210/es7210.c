/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_err.h"

#include "es7210_adc.h"
#include "es7210_reg.h"
#include "es_common.h"
#include "codec_reg_dump.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_vol.h"
#include "es7210_proc_priv.h"

static const char *TAG = "ES7210";

/**
 * @brief  ES7210 codec driver instance
 */
typedef struct {
    audio_codec_if_t     base;                                   /*!< Codec interface vtable container */
    audio_hw_adc_if_t    adc_ops;                                /*!< ADC operation callbacks */
    es7210_codec_cfg_t   cfg;                                    /*!< Board configuration snapshot */
    bool                 is_open;                                /*!< True after open completes */
    bool                 enabled;                                /*!< True when ADC path is running */
    es7210_gain_value_t  gain;                                   /*!< Cached gain setting */
    uint8_t              off_reg;                                /*!< Register offset for channel map */
    uint16_t             channel_mask;                           /*!< Channel mask from fs->channel_mask */
    char                 adc_label[AUDIO_HW_ADC_LABEL_MAX_LEN];  /*!< ADC label for multi-instance routing */
} audio_codec_es7210_t;

/**
 * @brief  ES7210 clock coefficient table entry
 */
typedef struct {
    uint32_t  mclk;      /*!< MCLK frequency in Hz */
    uint32_t  lrck;      /*!< LRCK sample rate in Hz */
    uint8_t   ss_ds;     /*!< Single-speed or double-speed mode */
    uint8_t   adc_div;   /*!< ADC clock divider */
    uint8_t   dll;       /*!< DLL bypass setting */
    uint8_t   doubler;   /*!< Doubler enable flag */
    uint8_t   osr;       /*!< ADC oversampling ratio */
    uint8_t   mclk_src;  /*!< MCLK source selection */
    uint32_t  lrck_h;    /*!< High bits of LRCK divider */
    uint32_t  lrck_l;    /*!< Low bits of LRCK divider */
} es7210_coeff_div_t;

/* Codec hifi mclk clock divider coefficients
 *           MEMBER      REG
 *           mclk:       0x03
 *           lrck:       standard
 *           ss_ds:      --
 *           adc_div:    0x02
 *           dll:        0x06
 *           doubler:    0x02
 *           osr:        0x07
 *           mclk_src:   0x03
 *           lrckh:      0x04
 *           lrckl:      0x05
 */
static const es7210_coeff_div_t coeff_div[] = {
    //  mclk      lrck    ss_ds adc_div  dll  doubler osr  mclk_src  lrckh   lrckl
    /* 8k */
    {12288000, 8000, 0x00, 0x03, 0x01, 0x00, 0x20, 0x00, 0x06, 0x00},
    {16384000, 8000, 0x00, 0x04, 0x01, 0x00, 0x20, 0x00, 0x08, 0x00},
    {19200000, 8000, 0x00, 0x1e, 0x00, 0x01, 0x28, 0x00, 0x09, 0x60},
    {4096000, 8000, 0x00, 0x01, 0x01, 0x00, 0x20, 0x00, 0x02, 0x00},

    /* 11.025k */
    {11289600, 11025, 0x00, 0x02, 0x01, 0x00, 0x20, 0x00, 0x01, 0x00},

    /* 12k */
    {12288000, 12000, 0x00, 0x02, 0x01, 0x00, 0x20, 0x00, 0x04, 0x00},
    {19200000, 12000, 0x00, 0x14, 0x00, 0x01, 0x28, 0x00, 0x06, 0x40},

    /* 16k */
    {4096000, 16000, 0x00, 0x01, 0x01, 0x01, 0x20, 0x00, 0x01, 0x00},
    {19200000, 16000, 0x00, 0x0a, 0x00, 0x00, 0x1e, 0x00, 0x04, 0x80},
    {16384000, 16000, 0x00, 0x02, 0x01, 0x00, 0x20, 0x00, 0x04, 0x00},
    {12288000, 16000, 0x00, 0x03, 0x01, 0x01, 0x20, 0x00, 0x03, 0x00},

    /* 22.05k */
    {11289600, 22050, 0x00, 0x01, 0x01, 0x00, 0x20, 0x00, 0x02, 0x00},

    /* 24k */
    {12288000, 24000, 0x00, 0x01, 0x01, 0x00, 0x20, 0x00, 0x02, 0x00},
    {19200000, 24000, 0x00, 0x0a, 0x00, 0x01, 0x28, 0x00, 0x03, 0x20},

    /* 32k */
    {8192000, 32000, 0x00, 0x01, 0x01, 0x01, 0x20, 0x00, 0x01, 0x00},
    {12288000, 32000, 0x00, 0x03, 0x00, 0x00, 0x20, 0x00, 0x01, 0x80},
    {16384000, 32000, 0x00, 0x01, 0x01, 0x00, 0x20, 0x00, 0x02, 0x00},
    {19200000, 32000, 0x00, 0x05, 0x00, 0x00, 0x1e, 0x00, 0x02, 0x58},

    /* 44.1k */
    {11289600, 44100, 0x00, 0x01, 0x01, 0x01, 0x20, 0x00, 0x01, 0x00},

    /* 48k */
    {12288000, 48000, 0x00, 0x01, 0x01, 0x01, 0x20, 0x00, 0x01, 0x00},
    {19200000, 48000, 0x00, 0x05, 0x00, 0x01, 0x28, 0x00, 0x01, 0x90},

    /* 64k */
    {16384000, 64000, 0x01, 0x01, 0x01, 0x00, 0x20, 0x00, 0x01, 0x00},
    {19200000, 64000, 0x00, 0x05, 0x00, 0x01, 0x1e, 0x00, 0x01, 0x2c},

    /* 88.2k */
    {11289600, 88200, 0x01, 0x01, 0x01, 0x01, 0x20, 0x00, 0x00, 0x80},

    /* 96k */
    {12288000, 96000, 0x01, 0x01, 0x01, 0x01, 0x20, 0x00, 0x00, 0x80},
    {19200000, 96000, 0x01, 0x05, 0x00, 0x01, 0x28, 0x00, 0x00, 0xc8},
};

static const audio_codec_hw_proc_ops_t hw_proc = {
    .alc_new = audio_hw_es7210_alc_new,
    .mute_new = audio_hw_es7210_mute_new,
};

static const esp_codec_dev_device_map_info_t order_info[] = {
    {ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0)}},
    {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0)}},
    {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 4, {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 3, 2, 4, 0, 0, 0, 0)}},
};

static const esp_codec_dev_capability_t adc_caps = {
    .dev_type = ESP_CODEC_DEV_TYPE_IN,
    .mode = ESP_CODEC_DEV_CAPS_MODE_FLEXIBLE,
    .flexible = {
        .max_channels = 4,
        .bits_per_sample = (const uint8_t[]){ 16, 24, 32 },
        .bits_num = 3,
        .sample_rates = (const uint32_t[]){
            8000, 11025, 12000, 16000, 22050, 24000,
            32000, 44100, 48000, 64000, 88200, 96000,
        },
        .sample_rate_num = 12,
    },
};

static int es7210_write_reg(audio_codec_es7210_t *codec, int reg, int value)
{
    return codec->cfg.ctrl_if->write_reg(codec->cfg.ctrl_if, reg, 1, &value, 1);
}

static int es7210_read_reg(audio_codec_es7210_t *codec, int reg, int *value)
{
    *value = 0;
    return codec->cfg.ctrl_if->read_reg(codec->cfg.ctrl_if, reg, 1, value, 1);
}

static int es7210_update_reg_bit(audio_codec_es7210_t *codec, uint8_t reg_addr, uint8_t update_bits, uint8_t data)
{
    int regv = 0;
    int ret = es7210_read_reg(codec, reg_addr, &regv);
    if (ret != ESP_CODEC_DEV_OK) {
        return ESP_CODEC_DEV_READ_FAIL;
    }
    regv = (regv & (~update_bits)) | (update_bits & data);
    ret = es7210_write_reg(codec, reg_addr, regv);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int get_coeff(uint32_t mclk, uint32_t lrck)
{
    for (int i = 0; i < (sizeof(coeff_div) / sizeof(coeff_div[0])); i++) {
        if (coeff_div[i].lrck == lrck && coeff_div[i].mclk == mclk) {
            return i;
        }
    }
    return -1;
}

static int es7210_config_sample(audio_codec_es7210_t *codec, int sample_fre, uint16_t mclk_div)
{
    bool is_master = codec->cfg.sys_cfg.is_master;
    if (is_master == false) {
        return ESP_CODEC_DEV_OK;
    }
    int regv;
    int coeff;
    int mclk_fre = 0;
    int ret = 0;
    mclk_fre = sample_fre * mclk_div;
    coeff = get_coeff(mclk_fre, sample_fre);
    if (coeff < 0) {
        ESP_LOGE(TAG, "Unable to configure sample rate %dHz with %dHz MCLK", sample_fre, mclk_fre);
        return ESP_FAIL;
    }
    /* Set clock parameters */
    if (coeff >= 0) {
        /* Set adc_div & doubler & dll */
        ret |= es7210_read_reg(codec, ES7210_MAINCLK_REG02, &regv);
        regv &= 0x00;
        regv |= coeff_div[coeff].adc_div;
        regv |= coeff_div[coeff].doubler << 6;
        regv |= coeff_div[coeff].dll << 7;
        ret |= es7210_write_reg(codec, ES7210_MAINCLK_REG02, regv);
        /* Set osr */
        regv = coeff_div[coeff].osr;
        ret |= es7210_write_reg(codec, ES7210_OSR_REG07, regv);
        /* Set lrck */
        regv = coeff_div[coeff].lrck_h;
        ret |= es7210_write_reg(codec, ES7210_LRCK_DIVH_REG04, regv);
        regv = coeff_div[coeff].lrck_l;
        ret |= es7210_write_reg(codec, ES7210_LRCK_DIVL_REG05, regv);
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es7210_select_mics(audio_codec_es7210_t *codec, uint16_t channel_mask)
{
    int ret = 0;
    if (channel_mask & (ES7210_INPUT_MIC1 | ES7210_INPUT_MIC2 | ES7210_INPUT_MIC3 | ES7210_INPUT_MIC4)) {
        for (int i = 0; i < 4; i++) {
            ret |= es7210_update_reg_bit(codec, ES7210_MIC1_GAIN_REG43 + i, 0x10, 0x00);
        }
        ret |= es7210_write_reg(codec, ES7210_MIC12_POWER_REG4B, 0xff);
        ret |= es7210_write_reg(codec, ES7210_MIC34_POWER_REG4C, 0xff);
        if (channel_mask & ES7210_INPUT_MIC1) {
            ESP_LOGI(TAG, "Enable ES7210_INPUT_MIC1");
            ret |= es7210_update_reg_bit(codec, ES7210_CLOCK_OFF_REG01, 0x0b, 0x00);
            ret |= es7210_write_reg(codec, ES7210_MIC12_POWER_REG4B, 0x00);
            ret |= es7210_update_reg_bit(codec, ES7210_MIC1_GAIN_REG43, 0x10, 0x10);
        }
        if (channel_mask & ES7210_INPUT_MIC2) {
            ESP_LOGI(TAG, "Enable ES7210_INPUT_MIC2");
            ret |= es7210_update_reg_bit(codec, ES7210_CLOCK_OFF_REG01, 0x0b, 0x00);
            ret |= es7210_write_reg(codec, ES7210_MIC12_POWER_REG4B, 0x00);
            ret |= es7210_update_reg_bit(codec, ES7210_MIC2_GAIN_REG44, 0x10, 0x10);
        }
        if (channel_mask & ES7210_INPUT_MIC3) {
            ESP_LOGI(TAG, "Enable ES7210_INPUT_MIC3");
            ret |= es7210_update_reg_bit(codec, ES7210_CLOCK_OFF_REG01, 0x15, 0x00);
            ret |= es7210_write_reg(codec, ES7210_MIC34_POWER_REG4C, 0x00);
            ret |= es7210_update_reg_bit(codec, ES7210_MIC3_GAIN_REG45, 0x10, 0x10);
        }
        if (channel_mask & ES7210_INPUT_MIC4) {
            ESP_LOGI(TAG, "Enable ES7210_INPUT_MIC4");
            ret |= es7210_update_reg_bit(codec, ES7210_CLOCK_OFF_REG01, 0x15, 0x00);
            ret |= es7210_write_reg(codec, ES7210_MIC34_POWER_REG4C, 0x00);
            ret |= es7210_update_reg_bit(codec, ES7210_MIC4_GAIN_REG46, 0x10, 0x10);
        }
    } else {
        ESP_LOGE(TAG, "Microphone selection error");
        return ESP_FAIL;
    }

    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es7210_config_fmt(audio_codec_es7210_t *codec, es_i2s_fmt_t fmt)
{
    int ret = 0;
    int adc_iface = 0;
    ret = es7210_read_reg(codec, ES7210_SDP_INTERFACE1_REG11, &adc_iface);
    adc_iface &= 0xfc;
    switch (fmt) {
        case ES_I2S_NORMAL:
            ESP_LOGD(TAG, "ES7210 in I2S Format");
            adc_iface |= 0x00;
            break;
        case ES_I2S_LEFT:
        case ES_I2S_RIGHT:
            ESP_LOGD(TAG, "ES7210 in LJ Format");
            adc_iface |= 0x01;
            break;
        case ES_I2S_DSP:
            ESP_LOGD(TAG, "ES7210 in DSP Format");
            adc_iface |= 0x03;
            break;
        default:
            adc_iface &= 0xfc;
            break;
    }
    ret |= es7210_write_reg(codec, ES7210_SDP_INTERFACE1_REG11, adc_iface);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es7210_set_bits(audio_codec_es7210_t *codec, uint8_t bits)
{
    int ret = 0;
    int adc_iface = 0;
    ret = es7210_read_reg(codec, ES7210_SDP_INTERFACE1_REG11, &adc_iface);
    adc_iface &= 0x1f;
    switch (bits) {
        case 16:
            adc_iface |= 0x60;
            break;
        case 24:
            adc_iface |= 0x00;
            break;
        case 32:
            adc_iface |= 0x80;
            break;
        default:
            adc_iface |= 0x60;
            break;
    }
    ret |= es7210_write_reg(codec, ES7210_SDP_INTERFACE1_REG11, adc_iface);
    ESP_LOGI(TAG, "Bits %d", bits);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es7210_start(audio_codec_es7210_t *codec, uint8_t clock_reg_value)
{
    int ret = 0;
    ret |= es7210_write_reg(codec, ES7210_CLOCK_OFF_REG01, clock_reg_value);
    ret |= es7210_write_reg(codec, ES7210_POWER_DOWN_REG06, 0x00);
    ret |= es7210_write_reg(codec, ES7210_ANALOG_REG40, 0x43);
    ret |= es7210_write_reg(codec, ES7210_MIC1_POWER_REG47, 0x08);
    ret |= es7210_write_reg(codec, ES7210_MIC2_POWER_REG48, 0x08);
    ret |= es7210_write_reg(codec, ES7210_MIC3_POWER_REG49, 0x08);
    ret |= es7210_write_reg(codec, ES7210_MIC4_POWER_REG4A, 0x08);
    ret |= es7210_select_mics(codec, codec->channel_mask);
    ret |= es7210_write_reg(codec, ES7210_ANALOG_REG40, 0x43);
    ret |= es7210_write_reg(codec, ES7210_RESET_REG00, 0x71);
    ret |= es7210_write_reg(codec, ES7210_RESET_REG00, 0x41);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es7210_stop(audio_codec_es7210_t *codec)
{
    int ret = 0;
    ret |= es7210_write_reg(codec, ES7210_MIC1_POWER_REG47, 0xff);
    ret |= es7210_write_reg(codec, ES7210_MIC2_POWER_REG48, 0xff);
    ret |= es7210_write_reg(codec, ES7210_MIC3_POWER_REG49, 0xff);
    ret |= es7210_write_reg(codec, ES7210_MIC4_POWER_REG4A, 0xff);
    ret |= es7210_write_reg(codec, ES7210_MIC12_POWER_REG4B, 0xff);
    ret |= es7210_write_reg(codec, ES7210_MIC34_POWER_REG4C, 0xff);
    ret |= es7210_write_reg(codec, ES7210_ANALOG_REG40, 0xc0);
    ret |= es7210_write_reg(codec, ES7210_CLOCK_OFF_REG01, 0x7f);
    ret |= es7210_write_reg(codec, ES7210_POWER_DOWN_REG06, 0x07);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static es7210_gain_value_t get_db(float db)
{
    db += 0.5;
    if (db < 33) {
        int idx = db < 3 ? 0 : db / 3;
        return GAIN_0DB + idx;
    }
    if (db < 34.5) {
        return GAIN_30DB;
    }
    if (db < 36) {
        return GAIN_34_5DB;
    }
    if (db < 37) {
        return GAIN_36DB;
    }
    return GAIN_37_5DB;
}

static int _es7210_set_channel_gain(audio_codec_es7210_t *codec, uint16_t channel_mask, float db)
{
    int ret = 0;
    es7210_gain_value_t gain = get_db(db);
    if (channel_mask & ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0)) {
        ret |= es7210_update_reg_bit(codec, ES7210_MIC1_GAIN_REG43, 0x0f, gain);
    }
    if (channel_mask & ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1)) {
        ret |= es7210_update_reg_bit(codec, ES7210_MIC2_GAIN_REG44, 0x0f, gain);
    }
    if (channel_mask & ESP_CODEC_DEV_MAKE_CHANNEL_MASK(2)) {
        ret |= es7210_update_reg_bit(codec, ES7210_MIC3_GAIN_REG45, 0x0f, gain);
    }
    if (channel_mask & ESP_CODEC_DEV_MAKE_CHANNEL_MASK(3)) {
        ret |= es7210_update_reg_bit(codec, ES7210_MIC4_GAIN_REG46, 0x0f, gain);
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int _es7210_set_mute(audio_codec_es7210_t *codec, uint16_t channel_mask, bool mute)
{
    int ret = 0;
    if (channel_mask == 0) {
        channel_mask = 0x0F;
    }

    uint8_t mask12 = channel_mask & 0x03;
    if (mask12 != 0) {
        uint8_t data12 = mute ? mask12 : 0;
        ret |= es7210_update_reg_bit(codec, ES7210_ADC12_MUTERANGE_REG15,
                                     mask12, data12);
    }

    uint8_t mask34 = (channel_mask >> 2) & 0x03;
    if (mask34 != 0) {
        uint8_t data34 = mute ? mask34 : 0;
        ret |= es7210_update_reg_bit(codec, ES7210_ADC34_MUTERANGE_REG14,
                                     mask34, data34);
    }

    ESP_LOGI(TAG, "%s", mute ? "Muted" : "Unmuted");
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es7210_enable(const audio_codec_if_t *h, bool enable)
{
    audio_codec_es7210_t *codec = (audio_codec_es7210_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    if (enable == codec->enabled) {
        return ESP_CODEC_DEV_OK;
    }
    int ret = 0;
    if (enable) {
        ret |= es7210_start(codec, codec->off_reg);
        ret |= _es7210_set_mute(codec, 0x0F, false);
    } else {
        ret |= _es7210_set_mute(codec, 0x0F, true);
        ret |= es7210_stop(codec);
    }
    if (ret == ESP_CODEC_DEV_OK) {
        ESP_LOGD(TAG, "Codec is %s", enable ? "enabled" : "disabled");
        codec->enabled = enable;
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es7210_adc_mute(const audio_codec_if_t *h, int ch_mask, bool mute)
{
    audio_codec_es7210_t *codec = (audio_codec_es7210_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    return _es7210_set_mute(codec, ch_mask, mute);
}

static int es7210_adc_set_vol(const audio_codec_if_t *h, int ch_mask, float db)
{
    audio_codec_es7210_t *codec = (audio_codec_es7210_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    return _es7210_set_channel_gain(codec, (uint16_t)ch_mask, db);
}

static int es7210_open(const audio_hw_base_t *h, void *cfg, int cfg_size)
{
    audio_codec_es7210_t *codec = (audio_codec_es7210_t *)h;
    es7210_codec_cfg_t *codec_cfg = (es7210_codec_cfg_t *)cfg;
    if (codec == NULL || codec_cfg == NULL || codec_cfg->ctrl_if == NULL || cfg_size != sizeof(es7210_codec_cfg_t)) {
        ESP_LOGE(TAG, "Wrong codec config");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    memcpy(&codec->cfg, codec_cfg, sizeof(es7210_codec_cfg_t));
    bool is_master = codec->cfg.sys_cfg.is_master;

    int ret = 0;
    ret |= es7210_write_reg(codec, ES7210_RESET_REG00, 0xff);
    ret |= es7210_write_reg(codec, ES7210_RESET_REG00, 0x41);
    ret |= es7210_write_reg(codec, ES7210_CLOCK_OFF_REG01, 0x3f);
    ret |= es7210_write_reg(codec, ES7210_TIME_CONTROL0_REG09, 0x30);  /* Set chip state cycle */
    ret |= es7210_write_reg(codec, ES7210_TIME_CONTROL1_REG0A, 0x30);  /* Set power on state cycle */
    ret |= es7210_write_reg(codec, ES7210_ADC12_HPF2_REG23, 0x2a);     /* Quick setup */
    ret |= es7210_write_reg(codec, ES7210_ADC12_HPF1_REG22, 0x0a);
    ret |= es7210_write_reg(codec, ES7210_ADC34_HPF2_REG20, 0x0a);
    ret |= es7210_write_reg(codec, ES7210_ADC34_HPF1_REG21, 0x2a);
    if (ret != 0) {
        ESP_LOGE(TAG, "Write register fail");
        return ESP_CODEC_DEV_WRITE_FAIL;
    }
    if (is_master) {
        ESP_LOGI(TAG, "Work in Master mode");
        ret |= es7210_update_reg_bit(codec, ES7210_MODE_CONFIG_REG08, 0x01, 0x01);
        /* Select clock source for internal mclk - default from PAD */
        ret |= es7210_update_reg_bit(codec, ES7210_MASTER_CLK_REG03, 0x80, 0x00);
    } else {
        ESP_LOGI(TAG, "Work in Slave mode");
        ret |= es7210_update_reg_bit(codec, ES7210_MODE_CONFIG_REG08, 0x01, 0x00);
    }
    /* Select power off analog, vdda = 3.3V, close vx20ff, VMID select 5KΩ start */
    ret |= es7210_write_reg(codec, ES7210_ANALOG_REG40, 0x43);
    ret |= es7210_write_reg(codec, ES7210_MIC12_BIAS_REG41, 0x70);  /* Select 2.87v */
    ret |= es7210_write_reg(codec, ES7210_MIC34_BIAS_REG42, 0x70);  /* Select 2.87v */
    ret |= es7210_write_reg(codec, ES7210_OSR_REG07, 0x20);
    /* Set the frequency division coefficient and use dll except clock doubler, and need to set 0xc1 to clear the state */
    ret |= es7210_write_reg(codec, ES7210_MAINCLK_REG02, 0xc1);

    // Default channel_mask to MIC1 and MIC2 (will be updated in set_fs)
    codec->channel_mask = 0x03;
    ret |= es7210_select_mics(codec, codec->channel_mask);
    ret |= _es7210_set_channel_gain(codec, 0x0F, 30.0);
    if (ret != 0) {
        return ESP_CODEC_DEV_WRITE_FAIL;
    }
    ret |= es7210_write_reg(codec, ES7210_SDP_INTERFACE2_REG12, 0x02);
    ESP_LOGI(TAG, "Enable TDM mode");
    int reg_val = 0;
    ret |= es7210_read_reg(codec, ES7210_CLOCK_OFF_REG01, &reg_val);
    if (reg_val >= 0) {
        codec->off_reg = (uint8_t)reg_val;
    }
    if (ret != 0) {
        return ESP_CODEC_DEV_WRITE_FAIL;
    }
    codec->is_open = true;
    return ESP_CODEC_DEV_OK;
}

static bool es7210_is_open(const audio_hw_base_t *h)
{
    audio_codec_es7210_t *codec = (audio_codec_es7210_t *)h;
    if (codec == NULL) {
        return false;
    }
    return codec->is_open;
}

static int es7210_set_fs(const audio_hw_base_t *h, esp_codec_dev_sample_info_t *fs, esp_codec_dev_type_t type)
{
    (void)type;
    audio_codec_es7210_t *codec = (audio_codec_es7210_t *)h;
    if (codec == NULL || fs == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = 0;
    uint8_t bits = fs->bits_per_sample;
    uint16_t mclk_div = fs->mclk_multiple ? fs->mclk_multiple : MCLK_DEFAULT_DIV;

    // Update channel_mask from fs
    uint16_t new_mask = fs->channel_mask;
    if (new_mask == 0) {
        // Default to MIC1 and MIC2 if no mask specified
        new_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1);
    }

    if (fs->channel == 4) {
        new_mask = 0x0F;
    }

    // Reconfigure MICs if channel_mask changed
    if (new_mask != codec->channel_mask) {
        codec->channel_mask = new_mask;
    }

    if (fs->channel == 2 && bits == 32) {
        ESP_LOGW(TAG, "Use 32bit to get 2ch 16bit data, not recommended, can use 4ch 16bit instead");
        bits >>= 1;
    }

    ret |= es7210_set_bits(codec, bits);
    ret |= es7210_config_sample(codec, fs->sample_rate, mclk_div);
    ret |= es7210_config_fmt(codec, ES_I2S_NORMAL);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es7210_close(const audio_hw_base_t *h)
{
    audio_codec_es7210_t *codec = (audio_codec_es7210_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = 0;
    if (codec->is_open) {
        ret = es7210_enable((const audio_codec_if_t *)h, false);
        codec->is_open = false;
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es7210_set_reg(const audio_hw_base_t *h, int reg, int value)
{
    audio_codec_es7210_t *codec = (audio_codec_es7210_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = es7210_write_reg(codec, reg, value);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es7210_get_reg(const audio_hw_base_t *h, int reg, int *value)
{
    audio_codec_es7210_t *codec = (audio_codec_es7210_t *)h;
    if (codec == NULL || value == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = es7210_read_reg(codec, reg, value);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_READ_FAIL;
}

static void es7210_dump(const audio_hw_base_t *h)
{
    audio_codec_es7210_t *codec = (audio_codec_es7210_t *)h;
    if (codec == NULL) {
        return;
    }
    codec_reg_dump_ctx_t dump;
    codec_reg_dump_init(&dump, TAG, 2);
    for (int i = 0; i <= 0x4E; i++) {
        int reg = 0;
        if (es7210_read_reg(codec, i, &reg) != ESP_CODEC_DEV_OK) {
            break;
        }
        codec_reg_dump_push(&dump, i, reg);
    }
    codec_reg_dump_end(&dump);
}

static int es7210_get_adc_label(const audio_hw_base_t *h, const char **label)
{
    audio_codec_es7210_t *codec = (audio_codec_es7210_t *)h;
    if (codec == NULL || label == NULL || codec->adc_label[0] == '\0') {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *label = codec->adc_label;
    return ESP_CODEC_DEV_OK;
}

static int es7210_get_order_list(const audio_hw_base_t *h, const esp_codec_dev_device_map_info_t **order_list, int *list_size)
{
    audio_codec_es7210_t *codec = (audio_codec_es7210_t *)h;
    if (codec == NULL || order_list == NULL || list_size == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *list_size = sizeof(order_info) / sizeof(order_info[0]);
    *order_list = order_info;
    return ESP_CODEC_DEV_OK;
}

static int es7210_get_caps(const audio_hw_base_t *h, esp_codec_dev_type_t dev_type,
                           esp_codec_dev_capability_t *caps, int *count)
{
    if (h == NULL || count == NULL || *count < 0 ||
        dev_type == ESP_CODEC_DEV_TYPE_NONE ||
        (dev_type & ~(ESP_CODEC_DEV_TYPE_IN_OUT)) != 0) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if ((dev_type & ESP_CODEC_DEV_TYPE_IN) == 0) {
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    if (caps == NULL || *count == 0) {
        *count = 1;
        return ESP_CODEC_DEV_OK;
    }
    caps[0] = adc_caps;
    *count = 1;
    return ESP_CODEC_DEV_OK;
}

static void es7210_save_adc_label(audio_codec_es7210_t *codec, const char *label)
{
    codec->adc_label[0] = '\0';
    if (label != NULL) {
        strncpy(codec->adc_label, label, sizeof(codec->adc_label) - 1);
        codec->adc_label[sizeof(codec->adc_label) - 1] = '\0';
    }
}

const audio_codec_if_t *es7210_codec_new(es7210_codec_cfg_t *codec_cfg)
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

    audio_codec_es7210_t *codec = (audio_codec_es7210_t *)calloc(1, sizeof(audio_codec_es7210_t));
    if (codec == NULL) {
        ESP_LOGE(TAG, "Fail to alloc memory at %s:%d", __FUNCTION__, __LINE__);
        return NULL;
    }

    codec->base.hw_base.open = es7210_open;
    codec->base.hw_base.is_open = es7210_is_open;
    codec->base.hw_base.set_fs = es7210_set_fs;
    codec->base.hw_base.set_reg = es7210_set_reg;
    codec->base.hw_base.get_reg = es7210_get_reg;
    codec->base.hw_base.dump_reg = es7210_dump;
    codec->base.hw_base.close = es7210_close;
    codec->base.hw_base.get_order_list = es7210_get_order_list;
    codec->base.hw_base.get_adc_label = es7210_get_adc_label;
    codec->base.hw_base.get_caps = es7210_get_caps;

    codec->base.hw_proc = &hw_proc;
    codec->base.ctrl_if = codec_cfg->ctrl_if;
    es7210_save_adc_label(codec, codec_cfg->adc_cfg.label);

    codec->adc_ops.ops.enable = es7210_enable;
    codec->adc_ops.ops.mute = es7210_adc_mute;
    codec->adc_ops.ops.set_vol = es7210_adc_set_vol;
    codec->base.adc_if = &codec->adc_ops;
    codec->base.dac_if = NULL;

    do {
        int ret = codec->base.hw_base.open(&codec->base.hw_base, codec_cfg, sizeof(es7210_codec_cfg_t));
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
