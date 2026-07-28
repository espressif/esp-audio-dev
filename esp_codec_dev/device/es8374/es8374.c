/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "esp_log.h"

#include "es8374_codec.h"
#include "esp_codec_dev_vol.h"
#include "es_common.h"
#include "codec_reg_dump.h"

static const char *TAG = "ES8374";

/**
 * @brief  ES8374 codec driver instance
 */
typedef struct {
    audio_codec_if_t    base;         /*!< Codec interface vtable container */
    audio_hw_adc_if_t   adc_ops;      /*!< ADC operation callbacks */
    audio_hw_dac_if_t   dac_ops;      /*!< DAC operation callbacks */
    es8374_codec_cfg_t  cfg;          /*!< Board configuration snapshot */
    bool                is_open;      /*!< True after open completes */
    bool                adc_enabled;  /*!< True when ADC path is running */
    bool                dac_enabled;  /*!< True when DAC path is running */
    float               hw_gain;      /*!< Cached hardware gain in dB */
} audio_codec_es8374_t;

static const esp_codec_dev_vol_range_t vol_range = {
    .min_vol = {
        .vol = 0xC0,
        .db_value = -96.0,
    },
    .max_vol = {
        .vol = 0x0,
        .db_value = 0.0,
    },
};

static int es8374_write_reg(audio_codec_es8374_t *codec, int reg, int value)
{
    const audio_codec_ctrl_if_t *ctrl_if = codec->base.ctrl_if;
    return ctrl_if->write_reg(ctrl_if, reg, 1, &value, 1);
}

static int es8374_read_reg(audio_codec_es8374_t *codec, int reg, int *value)
{
    *value = 0;
    const audio_codec_ctrl_if_t *ctrl_if = codec->base.ctrl_if;
    return ctrl_if->read_reg(ctrl_if, reg, 1, value, 1);
}

static int es8374_set_voice_mute(audio_codec_es8374_t *codec, bool enable)
{
    int ret = 0;
    int reg = 0;
    ret |= es8374_read_reg(codec, 0x36, &reg);
    if (ret == ESP_CODEC_DEV_OK) {
        reg = reg & 0xdf;
        ret |= es8374_write_reg(codec, 0x36, reg | (((int)enable) << 5));
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static es_bits_length_t get_bits(uint8_t bits)
{
    switch (bits) {
        default:
        case 16:
            return BIT_LENGTH_16BITS;
        case 18:
            return BIT_LENGTH_18BITS;
        case 20:
            return BIT_LENGTH_20BITS;
        case 24:
            return BIT_LENGTH_24BITS;
        case 32:
            return BIT_LENGTH_32BITS;
    }
}

static int es8374_set_bits_per_sample(audio_codec_es8374_t *codec, uint8_t bits)
{
    int ret = ESP_CODEC_DEV_OK;
    int reg = 0;
    es_bits_length_t bit_per_sample = get_bits(bits);
    bits = (int)bit_per_sample & 0x0f;

    ret |= es8374_read_reg(codec, 0x10, &reg);
    if (ret == ESP_CODEC_DEV_OK) {
        reg = reg & 0xe3;
        ret |= es8374_write_reg(codec, 0x10, reg | (bits << 2));
    }
    ret |= es8374_read_reg(codec, 0x11, &reg);
    if (ret == ESP_CODEC_DEV_OK) {
        reg = reg & 0xe3;
        ret |= es8374_write_reg(codec, 0x11, reg | (bits << 2));
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int _set_mic_gain(audio_codec_es8374_t *codec, float gain)
{
    int ret = 0;
    if (gain >= 0 && gain < 24) {
        int gain_n = 0;
        gain_n = (int)gain / 3;
        ret = es8374_write_reg(codec, 0x22, gain_n | (gain_n << 4));  // MIC PGA
    } else {
        ret = -1;
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8374_i2s_config_clock(audio_codec_es8374_t *codec, es_i2s_clock_t cfg)
{
    int ret = 0;
    int reg = 0;
    ret |= es8374_read_reg(codec, 0x0f, &reg);  // power up adc and input
    reg &= 0xe0;
    int divratio = 0;
    switch (cfg.sclk_div) {
        case MCLK_DIV_1:
            divratio = 1;
            break;
        case MCLK_DIV_2:  // = 2,
            divratio = 2;
            break;
        case MCLK_DIV_3:  // = 3,
            divratio = 3;
            break;
        case MCLK_DIV_4:  // = 4,
            divratio = 4;
            break;
        case MCLK_DIV_5:  // = 20,
            divratio = 5;
            break;
        case MCLK_DIV_6:  // = 5,
            divratio = 6;
            break;
        case MCLK_DIV_7:  // = 29,
            divratio = 7;
            break;
        case MCLK_DIV_8:  // = 6,
            divratio = 8;
            break;
        case MCLK_DIV_9:  // = 7,
            divratio = 9;
            break;
        case MCLK_DIV_10:  // = 21,
            divratio = 10;
            break;
        case MCLK_DIV_11:  // = 8,
            divratio = 11;
            break;
        case MCLK_DIV_12:  // = 9,
            divratio = 12;
            break;
        case MCLK_DIV_13:  // = 30,
            divratio = 13;
            break;
        case MCLK_DIV_14:  // = 31
            divratio = 14;
            break;
        case MCLK_DIV_15:  // = 22,
            divratio = 15;
            break;
        case MCLK_DIV_16:  // = 10,
            divratio = 16;
            break;
        case MCLK_DIV_17:  // = 23,
            divratio = 17;
            break;
        case MCLK_DIV_18:  // = 11,
            divratio = 18;
            break;
        case MCLK_DIV_20:  // = 24,
            divratio = 19;
            break;
        case MCLK_DIV_22:  // = 12,
            divratio = 20;
            break;
        case MCLK_DIV_24:  // = 13,
            divratio = 21;
            break;
        case MCLK_DIV_25:  // = 25,
            divratio = 22;
            break;
        case MCLK_DIV_30:  // = 26,
            divratio = 23;
            break;
        case MCLK_DIV_32:  // = 27,
            divratio = 24;
            break;
        case MCLK_DIV_33:  // = 14,
            divratio = 25;
            break;
        case MCLK_DIV_34:  // = 28,
            divratio = 26;
            break;
        case MCLK_DIV_36:  // = 15,
            divratio = 27;
            break;
        case MCLK_DIV_44:  // = 16,
            divratio = 28;
            break;
        case MCLK_DIV_48:  // = 17,
            divratio = 29;
            break;
        case MCLK_DIV_66:  // = 18,
            divratio = 30;
            break;
        case MCLK_DIV_72:  // = 19,
            divratio = 31;
            break;
        default:
            break;
    }
    reg |= divratio;
    ret |= es8374_write_reg(codec, 0x0f, reg);

    int dacratio_l = 0;
    int dacratio_h = 0;

    switch (cfg.lclk_div) {
        case LCLK_DIV_128:
            dacratio_l = 128 % 256;
            dacratio_h = 128 / 256;
            break;
        case LCLK_DIV_192:
            dacratio_l = 192 % 256;
            dacratio_h = 192 / 256;
            break;
        case LCLK_DIV_256:
            dacratio_l = 256 % 256;
            dacratio_h = 256 / 256;
            break;
        case LCLK_DIV_384:
            dacratio_l = 384 % 256;
            dacratio_h = 384 / 256;
            break;
        case LCLK_DIV_512:
            dacratio_l = 512 % 256;
            dacratio_h = 512 / 256;
            break;
        case LCLK_DIV_576:
            dacratio_l = 576 % 256;
            dacratio_h = 576 / 256;
            break;
        case LCLK_DIV_768:
            dacratio_l = 768 % 256;
            dacratio_h = 768 / 256;
            break;
        case LCLK_DIV_1024:
            dacratio_l = 1024 % 256;
            dacratio_h = 1024 / 256;
            break;
        case LCLK_DIV_1152:
            dacratio_l = 1152 % 256;
            dacratio_h = 1152 / 256;
            break;
        case LCLK_DIV_1408:
            dacratio_l = 1408 % 256;
            dacratio_h = 1408 / 256;
            break;
        case LCLK_DIV_1536:
            dacratio_l = 1536 % 256;
            dacratio_h = 1536 / 256;
            break;
        case LCLK_DIV_2112:
            dacratio_l = 2112 % 256;
            dacratio_h = 2112 / 256;
            break;
        case LCLK_DIV_2304:
            dacratio_l = 2304 % 256;
            dacratio_h = 2304 / 256;
            break;
        case LCLK_DIV_125:
            dacratio_l = 125 % 256;
            dacratio_h = 125 / 256;
            break;
        case LCLK_DIV_136:
            dacratio_l = 136 % 256;
            dacratio_h = 136 / 256;
            break;
        case LCLK_DIV_250:
            dacratio_l = 250 % 256;
            dacratio_h = 250 / 256;
            break;
        case LCLK_DIV_272:
            dacratio_l = 272 % 256;
            dacratio_h = 272 / 256;
            break;
        case LCLK_DIV_375:
            dacratio_l = 375 % 256;
            dacratio_h = 375 / 256;
            break;
        case LCLK_DIV_500:
            dacratio_l = 500 % 256;
            dacratio_h = 500 / 256;
            break;
        case LCLK_DIV_544:
            dacratio_l = 544 % 256;
            dacratio_h = 544 / 256;
            break;
        case LCLK_DIV_750:
            dacratio_l = 750 % 256;
            dacratio_h = 750 / 256;
            break;
        case LCLK_DIV_1000:
            dacratio_l = 1000 % 256;
            dacratio_h = 1000 / 256;
            break;
        case LCLK_DIV_1088:
            dacratio_l = 1088 % 256;
            dacratio_h = 1088 / 256;
            break;
        case LCLK_DIV_1496:
            dacratio_l = 1496 % 256;
            dacratio_h = 1496 / 256;
            break;
        case LCLK_DIV_1500:
            dacratio_l = 1500 % 256;
            dacratio_h = 1500 / 256;
            break;
        default:
            break;
    }
    ret |= es8374_write_reg(codec, 0x06, dacratio_h);  // ADCFsMode, single SPEED, RATIO=256
    ret |= es8374_write_reg(codec, 0x07, dacratio_l);  // ADCFsMode, single SPEED, RATIO=256

    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8374_set_d2se_pga(audio_codec_es8374_t *codec, es_d2se_pga_t gain)
{
    int ret = 0;
    int reg = 0;
    if (gain > D2SE_PGA_GAIN_MIN && gain < D2SE_PGA_GAIN_MAX) {
        ret |= es8374_read_reg(codec, 0x21, &reg);
        reg &= 0xfb;
        reg |= gain << 2;
        ret |= es8374_write_reg(codec, 0x21, reg);  // MIC PGA
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8374_config_fmt(audio_codec_es8374_t *codec, es_i2s_fmt_t fmt)
{
    int ret = 0;
    int reg = 0;
    int fmt_i2s = fmt & 0x0f;
    ret |= es8374_read_reg(codec, 0x10, &reg);
    if (ret == ESP_CODEC_DEV_OK) {
        reg = reg & 0xfc;
        ret |= es8374_write_reg(codec, 0x10, reg | fmt_i2s);
    }
    ret |= es8374_read_reg(codec, 0x11, &reg);
    if (ret == ESP_CODEC_DEV_OK) {
        reg = reg & 0xfc;
        ret |= es8374_write_reg(codec, 0x11, reg | fmt_i2s);
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8374_config_dac_output(audio_codec_es8374_t *codec, es_dac_output_t output)
{
    int ret = 0;
    int reg = 0;
    ret = es8374_write_reg(codec, 0x1d, 0x02);
    ret |= es8374_read_reg(codec, 0x1c, &reg);  // set spk mixer
    reg |= 0x80;
    ret |= es8374_write_reg(codec, 0x1c, reg);
    ret |= es8374_write_reg(codec, 0x1D, 0x02);  // spk set
    ret |= es8374_write_reg(codec, 0x1F, 0x00);  // spk set
    ret |= es8374_write_reg(codec, 0x1E, 0xA0);  // spk on
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8374_config_adc_input(audio_codec_es8374_t *codec, es_adc_input_t input)
{
    int ret = 0;
    int reg = 0;
    ret |= es8374_read_reg(codec, 0x21, &reg);
    if (ret == ESP_CODEC_DEV_OK) {
        reg = (reg & 0xcf) | 0x14;
        ret |= es8374_write_reg(codec, 0x21, reg);
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8374_set_adc_volume(audio_codec_es8374_t *codec, float db_value)
{
    int reg = esp_codec_dev_vol_calc_reg(&vol_range, db_value);
    int ret = es8374_write_reg(codec, 0x25, (uint8_t)reg);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8374_set_dac_volume(audio_codec_es8374_t *codec, float db_value)
{
    int reg = esp_codec_dev_vol_calc_reg(&vol_range, db_value);
    int ret = es8374_write_reg(codec, 0x38, (uint8_t)reg);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8374_init_reg(audio_codec_es8374_t *codec, es_i2s_fmt_t fmt, es_i2s_clock_t cfg,
                           es_dac_output_t out_channel, es_adc_input_t in_channel)
{
    int ret = 0;
    int reg = 0;

    ret |= es8374_write_reg(codec, 0x00, 0x3F);  // IC Rst start
    ret |= es8374_write_reg(codec, 0x00, 0x03);  // IC Rst stop
    ret |= es8374_write_reg(codec, 0x01, 0x7F);  // IC clk on

    ret |= es8374_read_reg(codec, 0x0F, &reg);
    reg &= 0x7f;

    reg |= (codec->cfg.sys_cfg.is_master << 7);
    ret |= es8374_write_reg(codec, 0x0f, reg);  // CODEC IN I2S SLAVE MODE

    ret |= es8374_write_reg(codec, 0x6F, 0xA0);  // pll set:mode enable
    ret |= es8374_write_reg(codec, 0x72, 0x41);  // pll set:mode set
    ret |= es8374_write_reg(codec, 0x09, 0x01);  // pll set:reset on ,set start
    ret |= es8374_write_reg(codec, 0x0C, 0x22);  // pll set:k
    ret |= es8374_write_reg(codec, 0x0D, 0x2E);  // pll set:k
    ret |= es8374_write_reg(codec, 0x0E, 0xC6);  // pll set:k
    ret |= es8374_write_reg(codec, 0x0A, 0x3A);  // pll set:
    ret |= es8374_write_reg(codec, 0x0B, 0x07);  // pll set:n
    ret |= es8374_write_reg(codec, 0x09, 0x41);  // pll set:reset off ,set stop

    ret |= es8374_i2s_config_clock(codec, cfg);

    ret |= es8374_write_reg(codec, 0x24, 0x08);  // adc set
    ret |= es8374_write_reg(codec, 0x36, 0x00);  // dac set
    ret |= es8374_write_reg(codec, 0x12, 0x30);  // timing set
    ret |= es8374_write_reg(codec, 0x13, 0x20);  // timing set

    ret |= es8374_config_fmt(codec, fmt);

    ret |= es8374_write_reg(codec, 0x21, 0x50);  // adc set: SEL LIN1 CH+PGAGAIN=0DB
    ret |= es8374_write_reg(codec, 0x22, 0xFF);  // adc set: PGA GAIN=0DB
    ret |= es8374_write_reg(codec, 0x21, 0x14);  // adc set: SEL LIN1 CH+PGAGAIN=18DB
    ret |= es8374_write_reg(codec, 0x22, 0x55);  // pga = +15db
    ret |= es8374_write_reg(codec, 0x08, 0x21);  // set class d divider = 33, to avoid the high frequency tone on laudspeaker
    ret |= es8374_write_reg(codec, 0x00, 0x80);  // IC START

    ret |= es8374_set_adc_volume(codec, 0.0);  // 0db

    ret |= es8374_write_reg(codec, 0x14, 0x8A);  // IC START
    ret |= es8374_write_reg(codec, 0x15, 0x40);  // IC START
    ret |= es8374_write_reg(codec, 0x1A, 0xA0);  // monoout set
    ret |= es8374_write_reg(codec, 0x1B, 0x19);  // monoout set
    ret |= es8374_write_reg(codec, 0x1C, 0x90);  // spk set
    ret |= es8374_write_reg(codec, 0x1D, 0x01);  // spk set
    ret |= es8374_write_reg(codec, 0x1F, 0x00);  // spk set
    ret |= es8374_write_reg(codec, 0x1E, 0x20);  // spk on
    ret |= es8374_write_reg(codec, 0x28, 0x00);  // alc set
    ret |= es8374_write_reg(codec, 0x25, 0x00);  // ADCVOLUME on
    ret |= es8374_write_reg(codec, 0x38, 0x00);  // DACVOLUME on
    ret |= es8374_write_reg(codec, 0x37, 0x30);  // dac set
    ret |= es8374_write_reg(codec, 0x6D, 0x60);  // SEL:GPIO1=DMIC CLK OUT+SEL:GPIO2=PLL CLK OUT
    ret |= es8374_write_reg(codec, 0x71, 0x05);  // for automute setting
    ret |= es8374_write_reg(codec, 0x73, 0x70);

    ret |= es8374_config_dac_output(codec, out_channel);  // 0x3c Enable DAC and Enable Lout/Rout/1/2
    ret |= es8374_config_adc_input(codec, in_channel);    // 0x00 LINSEL & RINSEL
    ret |= es8374_set_dac_volume(codec, -96.0);
    ret |= es8374_write_reg(codec, 0x37, 0x00);  // dac set
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8374_stop(audio_codec_es8374_t *codec, bool is_adc)
{
    int ret = 0;
    int reg = 0;
    if (is_adc == false) {
        ret |= es8374_set_voice_mute(codec, true);

        ret |= es8374_read_reg(codec, 0x1a, &reg);  // disable lout
        reg |= 0x08;
        ret |= es8374_write_reg(codec, 0x1a, reg);
        reg &= 0xdf;
        ret |= es8374_write_reg(codec, 0x1a, reg);
        ret |= es8374_write_reg(codec, 0x1D, 0x12);  // mute speaker
        ret |= es8374_write_reg(codec, 0x1E, 0x20);  // disable class d
        ret |= es8374_read_reg(codec, 0x15, &reg);   // power up dac
        reg |= 0x20;
        ret |= es8374_write_reg(codec, 0x15, reg);
    } else {
        ret |= es8374_read_reg(codec, 0x10, &reg);  // power up adc and input
        reg |= 0xc0;
        ret |= es8374_write_reg(codec, 0x10, reg);
        ret |= es8374_read_reg(codec, 0x21, &reg);  // power up adc and input
        reg |= 0xc0;
        ret |= es8374_write_reg(codec, 0x21, reg);
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8374_start(audio_codec_es8374_t *codec, bool is_adc)
{
    int ret = ESP_CODEC_DEV_OK;
    int reg = 0;
    if (is_adc) {
        ret |= es8374_read_reg(codec, 0x21, &reg);  // power up adc and input
        reg &= 0x3f;
        ret |= es8374_write_reg(codec, 0x21, reg);
        ret |= es8374_read_reg(codec, 0x10, &reg);  // power up adc and input
        reg &= 0x3f;
        ret |= es8374_write_reg(codec, 0x10, reg);
    } else {
        ret |= es8374_read_reg(codec, 0x1a, &reg);  // disable lout
        reg |= 0x08;
        ret |= es8374_write_reg(codec, 0x1a, reg);
        reg &= 0xdf;
        ret |= es8374_write_reg(codec, 0x1a, reg);
        ret |= es8374_write_reg(codec, 0x1D, 0x12);  // mute speaker
        ret |= es8374_write_reg(codec, 0x1E, 0x20);  // disable class d
        ret |= es8374_read_reg(codec, 0x15, &reg);   // power up dac
        reg &= 0xdf;
        ret |= es8374_write_reg(codec, 0x15, reg);
        ret |= es8374_read_reg(codec, 0x1a, &reg);  // disable lout
        reg |= 0x20;
        ret |= es8374_write_reg(codec, 0x1a, reg);
        reg &= 0xf7;
        ret |= es8374_write_reg(codec, 0x1a, reg);
        ret |= es8374_write_reg(codec, 0x1D, 0x02);  // mute speaker
        ret |= es8374_write_reg(codec, 0x1E, 0xa0);  // disable class d
        ret |= es8374_set_voice_mute(codec, false);
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8374_adc_enable(const audio_codec_if_t *h, bool enable)
{
    audio_codec_es8374_t *codec = (audio_codec_es8374_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    if (codec->adc_enabled == enable) {
        return ESP_CODEC_DEV_OK;
    }
    int ret = enable ? es8374_start(codec, true) : es8374_stop(codec, true);
    if (ret == ESP_CODEC_DEV_OK) {
        codec->adc_enabled = enable;
        ESP_LOGD(TAG, "Codec adc is %s", enable ? "enabled" : "disabled");
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static void es8374_pa_power(audio_codec_es8374_t *codec, bool enable)
{
    int16_t pa_pin = codec->cfg.pa_cfg.pa_pin;
    const audio_codec_gpio_if_t *gpio_if = codec->cfg.gpio_if;
    bool pa_active_low = codec->cfg.pa_cfg.pa_active_low;
    if (pa_pin == -1 || gpio_if == NULL) {
        return;
    }
    gpio_if->setup(pa_pin, AUDIO_GPIO_DIR_OUT, AUDIO_GPIO_MODE_FLOAT);
    gpio_if->set(pa_pin, pa_active_low ? !enable : enable);
    ESP_LOGD(TAG, "Pa power is %s", enable ? "enable" : "disable");
}

static int es8374_dac_enable(const audio_codec_if_t *h, bool enable)
{
    audio_codec_es8374_t *codec = (audio_codec_es8374_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    if (codec->dac_enabled == enable) {
        return ESP_CODEC_DEV_OK;
    }
    int ret;
    if (enable) {
        ret = es8374_start(codec, false);
        es8374_pa_power(codec, true);
    } else {
        es8374_pa_power(codec, false);
        ret = es8374_stop(codec, false);
    }
    if (ret == ESP_CODEC_DEV_OK) {
        codec->dac_enabled = enable;
        ESP_LOGD(TAG, "Codec dac is %s", enable ? "enabled" : "disabled");
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8374_dac_mute(const audio_codec_if_t *h, int ch_mask, bool mute)
{
    audio_codec_es8374_t *codec = (audio_codec_es8374_t *)h;
    (void)ch_mask;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    return es8374_set_voice_mute(codec, mute);
}

static int es8374_dac_set_vol(const audio_codec_if_t *h, int ch_mask, float db_value)
{
    audio_codec_es8374_t *codec = (audio_codec_es8374_t *)h;
    (void)ch_mask;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    db_value -= codec->hw_gain;
    return es8374_set_dac_volume(codec, db_value);
}

static int es8374_adc_set_vol(const audio_codec_if_t *h, int ch_mask, float db)
{
    audio_codec_es8374_t *codec = (audio_codec_es8374_t *)h;
    (void)ch_mask;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = _set_mic_gain(codec, db);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8374_pa_enable(const audio_codec_if_t *h, bool enable)
{
    audio_codec_es8374_t *codec = (audio_codec_es8374_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    es8374_pa_power(codec, enable);
    return ESP_CODEC_DEV_OK;
}

static int es8374_open(const audio_hw_base_t *h, void *cfg, int cfg_size)
{
    audio_codec_es8374_t *codec = (audio_codec_es8374_t *)h;
    es8374_codec_cfg_t *codec_cfg = (es8374_codec_cfg_t *)cfg;
    if (codec == NULL || codec_cfg == NULL || codec_cfg->ctrl_if == NULL || cfg_size != sizeof(es8374_codec_cfg_t)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = ESP_CODEC_DEV_OK;
    memcpy(&codec->cfg, codec_cfg, sizeof(es8374_codec_cfg_t));
    es8374_pa_power(codec, false);
    es_i2s_clock_t clkdiv;
    clkdiv.lclk_div = LCLK_DIV_256;
    clkdiv.sclk_div = MCLK_DIV_4;
    ret |= es8374_stop(codec, true);
    ret |= es8374_stop(codec, false);
    ret |= es8374_init_reg(codec, (BIT_LENGTH_16BITS << 4) | ES_I2S_NORMAL, clkdiv, DAC_OUTPUT_ALL,
                           ADC_INPUT_LINPUT1_RINPUT1);
    ret |= _set_mic_gain(codec, 15);
    ret |= es8374_set_d2se_pga(codec, D2SE_PGA_GAIN_EN);
    ret |= es8374_config_fmt(codec, ES_I2S_NORMAL);
    if (ret != 0) {
        return ESP_CODEC_DEV_WRITE_FAIL;
    }
    codec->is_open = true;
    return ESP_CODEC_DEV_OK;
}

static int es8374_close(const audio_hw_base_t *h)
{
    audio_codec_es8374_t *codec = (audio_codec_es8374_t *)h;
    int ret = ESP_CODEC_DEV_OK;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open) {
        es8374_pa_power(codec, false);
        if (codec->adc_enabled) {
            ret |= es8374_stop(codec, true);
            codec->adc_enabled = false;
        }
        if (codec->dac_enabled) {
            ret |= es8374_stop(codec, false);
            codec->dac_enabled = false;
        }
        ret |= es8374_write_reg(codec, 0x00, 0x7F);  // IC Reset and STOP
        codec->is_open = false;
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static bool es8374_is_open(const audio_hw_base_t *h)
{
    audio_codec_es8374_t *codec = (audio_codec_es8374_t *)h;
    if (codec == NULL) {
        return false;
    }
    return codec->is_open;
}

static int es8374_set_fs(const audio_hw_base_t *h, esp_codec_dev_sample_info_t *fs, esp_codec_dev_type_t type)
{
    (void)type;
    audio_codec_es8374_t *codec = (audio_codec_es8374_t *)h;
    if (codec == NULL || fs == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = 0;
    ret |= es8374_config_fmt(codec, ES_I2S_NORMAL);
    ret |= es8374_set_bits_per_sample(codec, fs->bits_per_sample);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8374_set_reg(const audio_hw_base_t *h, int reg, int value)
{
    audio_codec_es8374_t *codec = (audio_codec_es8374_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = es8374_write_reg(codec, reg, value);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8374_get_reg(const audio_hw_base_t *h, int reg, int *value)
{
    audio_codec_es8374_t *codec = (audio_codec_es8374_t *)h;
    if (codec == NULL || value == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = es8374_read_reg(codec, reg, value);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_READ_FAIL;
}

static void es8374_dump(const audio_hw_base_t *h)
{
    audio_codec_es8374_t *codec = (audio_codec_es8374_t *)h;
    if (codec == NULL || codec->is_open == false) {
        return;
    }
    codec_reg_dump_ctx_t dump;
    codec_reg_dump_init(&dump, TAG, 2);
    for (int i = 0; i < 50; i++) {
        int value = 0;
        int ret = es8374_read_reg(codec, i, &value);
        if (ret != ESP_CODEC_DEV_OK) {
            break;
        }
        codec_reg_dump_push(&dump, i, value);
    }
    codec_reg_dump_end(&dump);
}

const audio_codec_if_t *es8374_codec_new(es8374_codec_cfg_t *codec_cfg)
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

    audio_codec_es8374_t *codec = (audio_codec_es8374_t *)calloc(1, sizeof(audio_codec_es8374_t));
    if (codec == NULL) {
        ESP_LOGE(TAG, "Fail to alloc memory at %s:%d", __FUNCTION__, __LINE__);
        return NULL;
    }

    codec->base.hw_base.open = es8374_open;
    codec->base.hw_base.is_open = es8374_is_open;
    codec->base.hw_base.set_fs = es8374_set_fs;
    codec->base.hw_base.set_reg = es8374_set_reg;
    codec->base.hw_base.get_reg = es8374_get_reg;
    codec->base.hw_base.dump_reg = es8374_dump;
    codec->base.hw_base.close = es8374_close;
    codec->base.ctrl_if = codec_cfg->ctrl_if;

    codec->adc_ops.ops.enable = es8374_adc_enable;
    codec->adc_ops.ops.mute = NULL;
    codec->adc_ops.ops.set_vol = es8374_adc_set_vol;
    codec->base.adc_if = &codec->adc_ops;

    codec->dac_ops.ops.enable = es8374_dac_enable;
    codec->dac_ops.ops.mute = es8374_dac_mute;
    codec->dac_ops.ops.set_vol = es8374_dac_set_vol;
    codec->dac_ops.pa.enable = es8374_pa_enable;
    codec->base.dac_if = &codec->dac_ops;

    codec->hw_gain = esp_codec_dev_vol_calc_hw_gain(&codec_cfg->pa_cfg.hw_gain);
    do {
        int ret = codec->base.hw_base.open(&codec->base.hw_base, codec_cfg, sizeof(es8374_codec_cfg_t));
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
