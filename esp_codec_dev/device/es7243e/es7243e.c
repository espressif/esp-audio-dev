/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"

#include "es7243e_adc.h"
#include "es_common.h"
#include "codec_reg_dump.h"

static const char *TAG = "ES7243E";

/**
 * @brief  ES7243E codec driver instance
 */
typedef struct {
    audio_codec_if_t     base;                                   /*!< Codec interface vtable container */
    audio_hw_adc_if_t    adc_ops;                                /*!< ADC operation callbacks */
    es7243e_codec_cfg_t  cfg;                                    /*!< Board configuration snapshot */
    bool                 is_open;                                /*!< True after open completes */
    bool                 enabled;                                /*!< True when ADC path is running */
    char                 adc_label[AUDIO_HW_ADC_LABEL_MAX_LEN];  /*!< ADC label for multi-instance routing */
} audio_codec_es7243e_t;

/**
 * @brief  ES7243E clock coefficient table entry
 */
typedef struct {
    uint32_t  fs;             /*!< Sample rate in Hz */
    uint16_t  mclk_multiple;  /*!< MCLK multiple relative to sample rate */
    uint32_t  mclk;           /*!< MCLK frequency in Hz */
    uint8_t   reg03;          /*!< Register 0x03 value */
    uint8_t   reg04;          /*!< Register 0x04 value */
    uint8_t   reg05;          /*!< Register 0x05 value */
    uint8_t   reg06;          /*!< Register 0x06 value */
    uint8_t   reg0d;          /*!< Register 0x0D value */
    uint8_t   regf6;          /*!< Register 0xF6 value */
    uint8_t   reg09;          /*!< Register 0x09 value */
} es7243e_clock_coeff_t;

static const es7243e_clock_coeff_t coeff_div[] = {
    {8000, 128, 1024000, 0x20, 0x02, 0x00, 0x01, 0x00, 0x00, 0xC2},
    {8000, 192, 1536000, 0x20, 0x23, 0x00, 0x02, 0x00, 0x00, 0xC2},
    {8000, 256, 2048000, 0x20, 0x01, 0x00, 0x03, 0x00, 0x00, 0xC2},
    {8000, 384, 3072000, 0x20, 0x22, 0x00, 0x05, 0x00, 0x00, 0xC2},
    {8000, 512, 4096000, 0x20, 0x00, 0x00, 0x07, 0x00, 0x00, 0xC2},
    {16000, 64, 1024000, 0x20, 0x03, 0x00, 0x00, 0x00, 0x00, 0xC4},
    {16000, 96, 1536000, 0x18, 0x03, 0x10, 0x00, 0x01, 0x00, 0xC4},
    {16000, 128, 2048000, 0x20, 0x02, 0x00, 0x01, 0x00, 0x00, 0xC4},
    {16000, 192, 3072000, 0x20, 0x23, 0x00, 0x02, 0x00, 0x00, 0xC4},
    {16000, 256, 4096000, 0x20, 0x01, 0x00, 0x03, 0x00, 0x00, 0xC4},
    {16000, 384, 6144000, 0x20, 0x22, 0x00, 0x05, 0x00, 0x00, 0xC4},
    {16000, 512, 8192000, 0x20, 0x00, 0x00, 0x07, 0x00, 0x00, 0xC4},
    {22050, 64, 1411200, 0x20, 0x03, 0x00, 0x00, 0x00, 0x00, 0xC4},
    {22050, 128, 2822400, 0x20, 0x02, 0x00, 0x01, 0x00, 0x00, 0xC4},
    {22050, 256, 5644800, 0x20, 0x01, 0x00, 0x03, 0x00, 0x00, 0xC4},
    {22050, 512, 11289600, 0x20, 0x00, 0x00, 0x07, 0x00, 0x00, 0xC4},
    {24000, 64, 1536000, 0x20, 0x03, 0x00, 0x00, 0x00, 0x00, 0xC4},
    {24000, 96, 2304000, 0x18, 0x03, 0x10, 0x00, 0x01, 0x00, 0xC4},
    {24000, 128, 3072000, 0x20, 0x02, 0x00, 0x01, 0x00, 0x00, 0xC4},
    {24000, 192, 4608000, 0x20, 0x23, 0x00, 0x02, 0x00, 0x00, 0xC4},
    {24000, 256, 6144000, 0x20, 0x01, 0x00, 0x03, 0x00, 0x00, 0xC4},
    {24000, 384, 9216000, 0x20, 0x22, 0x00, 0x05, 0x00, 0x00, 0xC4},
    {24000, 512, 12288000, 0x20, 0x00, 0x00, 0x07, 0x00, 0x00, 0xC4},
    {32000, 64, 2048000, 0x20, 0x03, 0x00, 0x00, 0x00, 0x00, 0xC6},
    {32000, 96, 3072000, 0x18, 0x03, 0x10, 0x00, 0x01, 0x00, 0xC6},
    {32000, 128, 4096000, 0x20, 0x02, 0x00, 0x01, 0x00, 0x00, 0xC6},
    {32000, 192, 6144000, 0x20, 0x23, 0x00, 0x02, 0x00, 0x00, 0xC6},
    {32000, 256, 8192000, 0x20, 0x01, 0x00, 0x03, 0x00, 0x00, 0xC6},
    {32000, 384, 12288000, 0x20, 0x22, 0x00, 0x05, 0x00, 0x00, 0xC6},
    {32000, 512, 16384000, 0x20, 0x00, 0x00, 0x07, 0x00, 0x00, 0xC6},
    {44100, 64, 2822400, 0x10, 0x03, 0x10, 0x00, 0x04, 0x00, 0xC6},
    {44100, 128, 5644800, 0x10, 0x02, 0x10, 0x01, 0x04, 0x00, 0xC6},
    {44100, 256, 11289600, 0x10, 0x01, 0x10, 0x03, 0x04, 0x00, 0xC6},
    {44100, 512, 22579200, 0x10, 0x00, 0x10, 0x07, 0x04, 0x00, 0xC6},
    {48000, 64, 3072000, 0x10, 0x03, 0x10, 0x00, 0x04, 0x00, 0xC8},
    {48000, 96, 4608000, 0x10, 0x03, 0x20, 0x00, 0x04, 0x00, 0xC8},
    {48000, 128, 6144000, 0x10, 0x02, 0x10, 0x01, 0x04, 0x00, 0xC8},
    {48000, 192, 9216000, 0x10, 0x13, 0x20, 0x02, 0x04, 0x00, 0xC8},
    {48000, 256, 12288000, 0x10, 0x01, 0x10, 0x03, 0x04, 0x00, 0xC8},
    {48000, 384, 18432000, 0x10, 0x22, 0x10, 0x05, 0x04, 0x00, 0xC8},
    {48000, 512, 24576000, 0x10, 0x00, 0x10, 0x07, 0x04, 0x00, 0xC8},
};

static const esp_codec_dev_device_map_info_t order_info[] = {
    {ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0)}},
    {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0)}},
};

static int es7243e_write_reg(audio_codec_es7243e_t *codec, int reg, int value)
{
    return codec->cfg.ctrl_if->write_reg(codec->cfg.ctrl_if, reg, 1, &value, 1);
}

static int es7243e_read_reg(audio_codec_es7243e_t *codec, int reg, int *value)
{
    *value = 0;
    return codec->cfg.ctrl_if->read_reg(codec->cfg.ctrl_if, reg, 1, value, 1);
}

static int es7243e_update_reg_bits(audio_codec_es7243e_t *codec, int reg, int mask, int value)
{
    int cur = 0;
    int ret = es7243e_read_reg(codec, reg, &cur);
    if (ret != ESP_CODEC_DEV_OK) {
        return ESP_CODEC_DEV_READ_FAIL;
    }
    int v = (cur & ~mask) | (value & mask);
    ret = es7243e_write_reg(codec, reg, v);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es7243e_get_sdp_word_len_code(uint8_t bits)
{
    switch (bits) {
        case 16:
            return 3;
        case 18:
            return 2;
        case 20:
            return 1;
        case 24:
            return 0;
        case 32:
            return 4;
        default:
            return -1;
    }
}

static const es7243e_clock_coeff_t *es7243e_find_coeff(uint32_t sample_rate, uint16_t mclk_multiple)
{
    int n = sizeof(coeff_div) / sizeof(coeff_div[0]);
    for (int i = 0; i < n; i++) {
        const es7243e_clock_coeff_t *p = &coeff_div[i];
        if (p->fs == sample_rate && p->mclk_multiple == mclk_multiple) {
            return p;
        }
    }
    return NULL;
}

static int es7243e_set_bits_per_sample(audio_codec_es7243e_t *codec, uint8_t bits_per_sample)
{
    int sdp_word_len_code = es7243e_get_sdp_word_len_code(bits_per_sample);
    if (sdp_word_len_code < 0) {
        ESP_LOGW(TAG, "Unsupported bits_per_sample=%u", bits_per_sample);
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    // 0x0B[4:2]: SDP word length
    return es7243e_update_reg_bits(codec, 0x0B, 0x1C, sdp_word_len_code << 2);
}

static int es7243e_i2s_fmt_to_reg(es_i2s_fmt_t fmt)
{
    switch (fmt) {
        case ES_I2S_NORMAL:
            return 0;  // I2S
        case ES_I2S_LEFT:
            return 1;  // LJ
        case ES_I2S_DSP:
            return 3;  // DSP/PCM
        default:
            return -1;
    }
}

static int es7243e_config_fmt(audio_codec_es7243e_t *codec, es_i2s_fmt_t fmt)
{
    int sdp_fmt = es7243e_i2s_fmt_to_reg(fmt);
    if (sdp_fmt < 0) {
        ESP_LOGW(TAG, "Unsupported i2s fmt=%d", fmt);
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    // 0x0B[1:0]: SDP format (0:I2S, 1:LJ, 3:DSP)
    return es7243e_update_reg_bits(codec, 0x0B, 0x03, sdp_fmt);
}

static int es7243e_config_sample(audio_codec_es7243e_t *codec, uint32_t sample_rate, uint16_t mclk_multiple)
{
    const es7243e_clock_coeff_t *coeff = es7243e_find_coeff(sample_rate, mclk_multiple);
    if (coeff == NULL) {
        ESP_LOGW(TAG, "Unsupported fs=%" PRIu32 " mclk_multiple=%u", sample_rate, mclk_multiple);
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    int ret = 0;
    ret |= es7243e_write_reg(codec, 0x03, coeff->reg03);
    ret |= es7243e_write_reg(codec, 0x04, coeff->reg04);
    ret |= es7243e_write_reg(codec, 0x05, coeff->reg05);
    ret |= es7243e_write_reg(codec, 0x06, coeff->reg06);
    ret |= es7243e_write_reg(codec, 0x0D, coeff->reg0d);
    ret |= es7243e_write_reg(codec, 0xF6, coeff->regf6);
    ret |= es7243e_write_reg(codec, 0x09, coeff->reg09);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es7243e_adc_enable(audio_codec_es7243e_t *codec, bool enable)
{
    int ret = ESP_CODEC_DEV_OK;
    if (enable) {
        // Slave mode only
        ret |= es7243e_write_reg(codec, 0xF9, 0x00);
        ret |= es7243e_write_reg(codec, 0x04, 0x01);
        ret |= es7243e_write_reg(codec, 0x17, 0x01);
        ret |= es7243e_write_reg(codec, 0x20, 0x10);
        ret |= es7243e_write_reg(codec, 0x21, 0x10);
        ret |= es7243e_write_reg(codec, 0x00, 0x80);
        ret |= es7243e_write_reg(codec, 0x01, 0x3A);
        ret |= es7243e_write_reg(codec, 0x16, 0x3F);
        ret |= es7243e_write_reg(codec, 0x16, 0x00);
    } else {
        ret |= es7243e_write_reg(codec, 0x04, 0x02);
        ret |= es7243e_write_reg(codec, 0x04, 0x01);
        ret |= es7243e_write_reg(codec, 0xF7, 0x30);
        ret |= es7243e_write_reg(codec, 0xF9, 0x01);
        ret |= es7243e_write_reg(codec, 0x16, 0xFF);
        ret |= es7243e_write_reg(codec, 0x17, 0x00);
        ret |= es7243e_write_reg(codec, 0x01, 0x38);
        ret |= es7243e_write_reg(codec, 0x20, 0x00);
        ret |= es7243e_write_reg(codec, 0x21, 0x00);
        ret |= es7243e_write_reg(codec, 0x00, 0x00);
        ret |= es7243e_write_reg(codec, 0x00, 0x1E);
        ret |= es7243e_write_reg(codec, 0x01, 0x30);
        ret |= es7243e_write_reg(codec, 0x01, 0x00);
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es7243e_open(const audio_hw_base_t *h, void *cfg, int cfg_size)
{
    audio_codec_es7243e_t *codec = (audio_codec_es7243e_t *)h;
    es7243e_codec_cfg_t *codec_cfg = (es7243e_codec_cfg_t *)cfg;
    if (codec == NULL || codec_cfg == NULL || codec_cfg->ctrl_if == NULL || cfg_size != sizeof(es7243e_codec_cfg_t)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    memcpy(&codec->cfg, codec_cfg, sizeof(es7243e_codec_cfg_t));

    int ret = 0;
    ret |= es7243e_write_reg(codec, 0x01, 0x3A);
    ret |= es7243e_write_reg(codec, 0x00, 0x80);
    ret |= es7243e_write_reg(codec, 0xF9, 0x00);
    ret |= es7243e_write_reg(codec, 0x04, 0x02);
    ret |= es7243e_write_reg(codec, 0x04, 0x01);
    ret |= es7243e_write_reg(codec, 0xF9, 0x01);
    ret |= es7243e_write_reg(codec, 0x00, 0x1E);
    ret |= es7243e_write_reg(codec, 0x01, 0x00);

    ret |= es7243e_write_reg(codec, 0x02, 0x00);
    ret |= es7243e_write_reg(codec, 0x03, 0x20);
    ret |= es7243e_write_reg(codec, 0x04, 0x01);
    ret |= es7243e_write_reg(codec, 0x0D, 0x00);
    ret |= es7243e_write_reg(codec, 0x05, 0x00);
    ret |= es7243e_write_reg(codec, 0x06, 0x03);  // SCLK=MCLK/4
    ret |= es7243e_write_reg(codec, 0x07, 0x00);  // LRCK=MCLK/256
    ret |= es7243e_write_reg(codec, 0x08, 0xFF);  // LRCK=MCLK/256

    ret |= es7243e_write_reg(codec, 0x09, 0xCA);
    ret |= es7243e_write_reg(codec, 0x0A, 0x85);
    ret |= es7243e_write_reg(codec, 0x0B, 0x00);
    ret |= es7243e_write_reg(codec, 0x0E, 0xBF);
    ret |= es7243e_write_reg(codec, 0x0F, 0x80);
    ret |= es7243e_write_reg(codec, 0x14, 0x0C);
    ret |= es7243e_write_reg(codec, 0x15, 0x0C);
    ret |= es7243e_write_reg(codec, 0x17, 0x02);
    ret |= es7243e_write_reg(codec, 0x18, 0x26);
    ret |= es7243e_write_reg(codec, 0x19, 0x77);
    ret |= es7243e_write_reg(codec, 0x1A, 0xF4);
    ret |= es7243e_write_reg(codec, 0x1B, 0x66);
    ret |= es7243e_write_reg(codec, 0x1C, 0x44);
    ret |= es7243e_write_reg(codec, 0x1E, 0x00);
    ret |= es7243e_write_reg(codec, 0x1F, 0x0C);
    ret |= es7243e_write_reg(codec, 0x20, 0x1A);  // PGA gain +30dB
    ret |= es7243e_write_reg(codec, 0x21, 0x1A);

    ret |= es7243e_write_reg(codec, 0x00, 0x80);  // Slave Mode
    ret |= es7243e_write_reg(codec, 0x01, 0x3A);
    ret |= es7243e_write_reg(codec, 0x16, 0x3F);
    ret |= es7243e_write_reg(codec, 0x16, 0x00);
    if (ret != 0 || es7243e_adc_enable(codec, true) != ESP_CODEC_DEV_OK) {
        ESP_LOGI(TAG, "Fail to write register");
        return ESP_CODEC_DEV_WRITE_FAIL;
    }
    codec->enabled = true;
    codec->is_open = true;
    return ESP_CODEC_DEV_OK;
}

static int es7243e_enable(const audio_codec_if_t *h, bool enable)
{
    audio_codec_es7243e_t *codec = (audio_codec_es7243e_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    if (codec->enabled == enable) {
        return ESP_CODEC_DEV_OK;
    }
    int ret = es7243e_adc_enable(codec, enable);
    if (ret == ESP_CODEC_DEV_OK) {
        codec->enabled = enable;
        ESP_LOGD(TAG, "Codec is %s", enable ? "enabled" : "disabled");
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static uint8_t get_db_reg(float db)
{
    // Register: 12 = 34.5 dB, 13 = 36 dB, 14 = 37.5 dB
    db += 0.5;
    if (db <= 33.0) {
        return (uint8_t)db / 3;
    }
    if (db <= 34.5) {
        return 12;
    }
    if (db <= 36) {
        return 13;
    }
    return 14;
}

static int es7243e_adc_set_vol(const audio_codec_if_t *h, int ch_mask, float db_value)
{
    audio_codec_es7243e_t *codec = (audio_codec_es7243e_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    uint8_t reg = get_db_reg(db_value);
    int ret = ESP_CODEC_DEV_OK;
    ch_mask = ch_mask > 0 ? ch_mask : 0x03;
    if (ch_mask & 0x01) {
        ret |= es7243e_write_reg(codec, 0x20, 0x10 | reg);
    }
    if (ch_mask & 0x02) {
        ret |= es7243e_write_reg(codec, 0x21, 0x10 | reg);
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es7243e_adc_mute(const audio_codec_if_t *h, int ch_mask, bool mute)
{
    audio_codec_es7243e_t *codec = (audio_codec_es7243e_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    (void)ch_mask;
    int ret;
    if (mute) {
        ret = es7243e_update_reg_bits(codec, 0x0B, 0xC0, 0xC0);
    } else {
        ret = es7243e_update_reg_bits(codec, 0x0B, 0xC0, 0x00);
    }
    ESP_LOGD(TAG, "%s", mute ? "Muted" : "Unmuted");
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static bool es7243e_is_open(const audio_hw_base_t *h)
{
    audio_codec_es7243e_t *codec = (audio_codec_es7243e_t *)h;
    if (codec == NULL) {
        return false;
    }
    return codec->is_open;
}

static int es7243e_set_fs(const audio_hw_base_t *h, esp_codec_dev_sample_info_t *fs, esp_codec_dev_type_t type)
{
    (void)type;
    audio_codec_es7243e_t *codec = (audio_codec_es7243e_t *)h;
    if (codec == NULL || fs == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    uint16_t mclk_multiple = fs->mclk_multiple ? fs->mclk_multiple : 256;
    int ret = 0;
    ret |= es7243e_set_bits_per_sample(codec, fs->bits_per_sample);
    ret |= es7243e_config_fmt(codec, ES_I2S_NORMAL);
    ret |= es7243e_config_sample(codec, fs->sample_rate, mclk_multiple);
    if (ret != 0) {
        return (ret == ESP_CODEC_DEV_NOT_SUPPORT) ? ESP_CODEC_DEV_NOT_SUPPORT : ESP_CODEC_DEV_WRITE_FAIL;
    }
    return ESP_CODEC_DEV_OK;
}

static int es7243e_close(const audio_hw_base_t *h)
{
    audio_codec_es7243e_t *codec = (audio_codec_es7243e_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open) {
        es7243e_adc_enable(codec, false);
        codec->is_open = false;
    }
    return ESP_CODEC_DEV_OK;
}

static int es7243e_set_reg(const audio_hw_base_t *h, int reg, int value)
{
    audio_codec_es7243e_t *codec = (audio_codec_es7243e_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = es7243e_write_reg(codec, reg, value);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es7243e_get_reg(const audio_hw_base_t *h, int reg, int *value)
{
    audio_codec_es7243e_t *codec = (audio_codec_es7243e_t *)h;
    if (codec == NULL || value == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = es7243e_read_reg(codec, reg, value);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_READ_FAIL;
}

static void es7243e_dump(const audio_hw_base_t *h)
{
    audio_codec_es7243e_t *codec = (audio_codec_es7243e_t *)h;
    if (codec == NULL || codec->is_open == false) {
        return;
    }
    codec_reg_dump_ctx_t dump;
    codec_reg_dump_init(&dump, TAG, 2);
    for (int i = 0; i < 0xFF; i++) {
        int value = 0;
        int ret = es7243e_read_reg(codec, i, &value);
        if (ret != ESP_CODEC_DEV_OK) {
            break;
        }
        codec_reg_dump_push(&dump, i, value);
    }
    codec_reg_dump_end(&dump);
}

static int es7243e_get_adc_label(const audio_hw_base_t *h, const char **label)
{
    audio_codec_es7243e_t *codec = (audio_codec_es7243e_t *)h;
    if (codec == NULL || label == NULL || codec->adc_label[0] == '\0') {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *label = codec->adc_label;
    return ESP_CODEC_DEV_OK;
}

static int es7243e_get_order_list(const audio_hw_base_t *h, const esp_codec_dev_device_map_info_t **order_list, int *list_size)
{
    audio_codec_es7243e_t *codec = (audio_codec_es7243e_t *)h;
    if (codec == NULL || order_list == NULL || list_size == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *list_size = sizeof(order_info) / sizeof(order_info[0]);
    *order_list = order_info;
    return ESP_CODEC_DEV_OK;
}

static void es7243e_save_adc_label(audio_codec_es7243e_t *codec, const char *label)
{
    codec->adc_label[0] = '\0';
    if (label != NULL) {
        strncpy(codec->adc_label, label, sizeof(codec->adc_label) - 1);
        codec->adc_label[sizeof(codec->adc_label) - 1] = '\0';
    }
}

const audio_codec_if_t *es7243e_codec_new(es7243e_codec_cfg_t *codec_cfg)
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

    audio_codec_es7243e_t *codec = (audio_codec_es7243e_t *)calloc(1, sizeof(audio_codec_es7243e_t));
    if (codec == NULL) {
        ESP_LOGE(TAG, "Fail to alloc memory at %s:%d", __FUNCTION__, __LINE__);
        return NULL;
    }

    codec->base.hw_base.open = es7243e_open;
    codec->base.hw_base.is_open = es7243e_is_open;
    codec->base.hw_base.set_fs = es7243e_set_fs;
    codec->base.hw_base.set_reg = es7243e_set_reg;
    codec->base.hw_base.get_reg = es7243e_get_reg;
    codec->base.hw_base.dump_reg = es7243e_dump;
    codec->base.hw_base.close = es7243e_close;
    codec->base.hw_base.get_order_list = es7243e_get_order_list;
    codec->base.hw_base.get_adc_label = es7243e_get_adc_label;
    codec->base.ctrl_if = codec_cfg->ctrl_if;
    es7243e_save_adc_label(codec, codec_cfg->adc_cfg.label);

    codec->adc_ops.ops.enable = es7243e_enable;
    codec->adc_ops.ops.mute = es7243e_adc_mute;
    codec->adc_ops.ops.set_vol = es7243e_adc_set_vol;
    codec->base.adc_if = &codec->adc_ops;
    codec->base.dac_if = NULL;

    do {
        int ret = codec->base.hw_base.open(&codec->base.hw_base, codec_cfg, sizeof(es7243e_codec_cfg_t));
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
