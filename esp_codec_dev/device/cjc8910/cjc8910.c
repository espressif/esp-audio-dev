/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "esp_log.h"

#include "esp_codec_dev_vol.h"
#include "cjc8910_codec.h"
#include "cjc8910_reg.h"
#include "codec_reg_dump.h"

typedef enum {
    PA_SETUP   = 1 << 0,
    PA_ENABLE  = 1 << 1,
    PA_DISABLE = 1 << 2,
} pa_setting_t;

typedef enum {
    CJC8910_I2S_FMT_I2S  = 0,
    CJC8910_I2S_FMT_LEFT = 1,
    CJC8910_I2S_FMT_DSP  = 2,
} cjc8910_i2s_fmt_t;

/**
 * @brief  CJC8910 codec driver instance
 */
typedef struct {
    audio_codec_if_t     base;     /*!< Codec interface vtable container */
    audio_hw_adc_if_t    adc_ops;  /*!< ADC operation callbacks */
    audio_hw_dac_if_t    dac_ops;  /*!< DAC operation callbacks */
    cjc8910_codec_cfg_t  cfg;      /*!< Board configuration snapshot */
    float                hw_gain;  /*!< Cached hardware gain in dB */
    bool                 is_open;  /*!< True after open completes */
    bool                 enabled;  /*!< True when codec path is running */
} audio_codec_cjc8910_t;

static const char *TAG = "CJC8910";

static const esp_codec_dev_vol_range_t vol_range = {
    .min_vol = {
        .vol = 0x01,
        .db_value = -97.0,
    },
    .max_vol = {
        .vol = 0xFF,
        .db_value = 30.0,
    },
};

static int cjc8910_write_reg(audio_codec_cjc8910_t *codec, int reg, int value)
{
    ESP_LOGD(TAG, "Write reg %d, value %d", reg, value);
    return codec->cfg.ctrl_if->write_reg(codec->cfg.ctrl_if, reg, 1, &value, 1);
}

static int cjc8910_read_reg(audio_codec_cjc8910_t *codec, int reg, int *value)
{
    *value = 0;
    return codec->cfg.ctrl_if->read_reg(codec->cfg.ctrl_if, reg, 1, value, 1);
}

static int cjc8910_config_fmt(audio_codec_cjc8910_t *codec, cjc8910_i2s_fmt_t fmt)
{
    int ret = ESP_CODEC_DEV_OK;
    int format = 0;
    ret = cjc8910_read_reg(codec, CJC8910_R7_AUDIO_INTERFACE, &format);

    format &= ~0x03;

    switch (fmt) {
        case CJC8910_I2S_FMT_I2S:
            format |= 0x02;  // FORMAT[1:0] = 10 (standard I2S)
            break;
        case CJC8910_I2S_FMT_LEFT:
            format |= 0x01;  // FORMAT[1:0] = 01 (left justified)
            break;
        case CJC8910_I2S_FMT_DSP:
            format |= 0x03;  // FORMAT[1:0] = 11 (DSP/PCM)
            break;
        default:
            format |= 0x02;
            break;
    }
    ret |= cjc8910_write_reg(codec, CJC8910_R7_AUDIO_INTERFACE, format);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static void cjc8910_pa_power(audio_codec_cjc8910_t *codec, pa_setting_t pa_setting)
{
    int pa_pin = codec->cfg.pa_cfg.pa_pin;
    if (pa_pin == -1 || codec->cfg.gpio_if == NULL) {
        return;
    }
    if (pa_setting & PA_SETUP) {
        codec->cfg.gpio_if->setup(pa_pin, AUDIO_GPIO_DIR_OUT, AUDIO_GPIO_MODE_FLOAT);
    }
    if (pa_setting & PA_ENABLE) {
        codec->cfg.gpio_if->set(pa_pin, codec->cfg.pa_cfg.pa_active_low ? false : true);
    }
    if (pa_setting & PA_DISABLE) {
        codec->cfg.gpio_if->set(pa_pin, codec->cfg.pa_cfg.pa_active_low ? true : false);
    }
}

static void cjc8910_dump(const audio_hw_base_t *h)
{
    audio_codec_cjc8910_t *codec = (audio_codec_cjc8910_t *)h;
    if (codec == NULL || codec->is_open == false) {
        return;
    }
    codec_reg_dump_ctx_t dump;
    codec_reg_dump_init(&dump, TAG, 2);
    for (int i = 0; i < CJC8910_MAX_REGISTER; i++) {
        int value = 0;
        int ret = cjc8910_read_reg(codec, i, &value);
        if (ret != ESP_CODEC_DEV_OK) {
            break;
        }
        codec_reg_dump_push(&dump, i, value);
    }
    codec_reg_dump_end(&dump);
}

static int cjc8910_open(const audio_hw_base_t *h, void *cfg, int cfg_size)
{
    audio_codec_cjc8910_t *codec = (audio_codec_cjc8910_t *)h;
    cjc8910_codec_cfg_t *codec_cfg = (cjc8910_codec_cfg_t *)cfg;
    if (codec == NULL || codec_cfg == NULL || codec_cfg->ctrl_if == NULL || cfg_size != sizeof(cjc8910_codec_cfg_t)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    memcpy(&codec->cfg, cfg, sizeof(cjc8910_codec_cfg_t));
    cjc8910_pa_power(codec, PA_SETUP | PA_DISABLE);
    int regv;
    int ret = ESP_CODEC_DEV_OK;
    ret |= cjc8910_write_reg(codec, CJC8910_R15_RESET, 0x00);  // Reset

    ret |= cjc8910_write_reg(codec, CJC8910_R0_LEFT_INPUT_VOLUME, 0x17);  // Audio input left channel volume
    ret |= cjc8910_write_reg(codec, CJC8910_R2_LOUT1_VOLUME, 0x79);       // Audio output letf channel1 volume
    ret |= cjc8910_write_reg(codec, CJC8910_R5_ADC_DAC_CONTROL, 0x00);    // ADC and DAC CONTROL
    ret |= cjc8910_write_reg(codec, CJC8910_R7_AUDIO_INTERFACE, 0x0A);    // Digital Audio interface format
    ret |= cjc8910_write_reg(codec, CJC8910_R8_SAMPLE_RATE, 0x00);        // Clock and Sample rate control
    ret |= cjc8910_write_reg(codec, CJC8910_R10_LEFT_DAC_VOLUME, 0xC3);   // Left channel DAC digital volume

    ret |= cjc8910_write_reg(codec, CJC8910_R12_BASS_CONTROL, 0x0f);    // BASS control
    ret |= cjc8910_write_reg(codec, CJC8910_R13_TREBLE_CONTROL, 0x0f);  // Treble control

    ret |= cjc8910_write_reg(codec, CJC8910_R17_ALC1_CONTROL, 0x7B);        // ALC1 control
    ret |= cjc8910_write_reg(codec, CJC8910_R18_ALC2_CONTROL, 0x00);        // ACL2 control
    ret |= cjc8910_write_reg(codec, CJC8910_R19_ALC3_CONTROL, 0x00);        // ALC3 control
    ret |= cjc8910_write_reg(codec, CJC8910_R20_NOISE_GATE_CONTROL, 0x00);  // Noise gate
    ret |= cjc8910_write_reg(codec, CJC8910_R21_LEFT_ADC_VOLUME, 0xc3);     // Left ADC digital volume

    ret |= cjc8910_write_reg(codec, CJC8910_R23_ADDITIONAL_CONTROL1, 0x00);  // Additional control 1
    ret |= cjc8910_write_reg(codec, CJC8910_R24_ADDITIONAL_CONTROL2, 0x00);  // Additional control 2
    ret |= cjc8910_write_reg(codec, CJC8910_R27_ADDITIONAL_CONTROL3, 0x00);  // Additional control 3

    ret |= cjc8910_write_reg(codec, CJC8910_R32_ADCL_SIGNAL_PATH, 0x00);  // ADC signal path control
    ret |= cjc8910_write_reg(codec, CJC8910_R33_MIC, 0x0A);               // MIC control

    ret |= cjc8910_write_reg(codec, CJC8910_R34_AUX, 0x0A);              // AUX control
    ret |= cjc8910_write_reg(codec, CJC8910_R35_LEFT_OUT_MIX2_H, 0x00);  // Left out Mix (2)

    ret |= cjc8910_write_reg(codec, CJC8910_R37_ADC_PDN, 0x00);  // Adc_pdn sel

    ret |= cjc8910_write_reg(codec, CJC8910_R67_LOW_POWER_PLAYBACK, 0x00);  // Low power playback

    ret |= cjc8910_write_reg(codec, CJC8910_R25_PWR_MGMT1_H, 0xE8);  // Power management1 and VMIDSEL
    ret |= cjc8910_write_reg(codec, CJC8910_R26_PWR_MGMT2_H, 0x40);  // Power management2 and DAC left power down

    ret |= cjc8910_read_reg(codec, CJC8910_R7_AUDIO_INTERFACE, &regv);

    /* Only support codec in slave mode */
    regv &= ~(1 << 6);
    ret |= cjc8910_write_reg(codec, CJC8910_R7_AUDIO_INTERFACE, regv);
    if (ret != 0) {
        return ESP_CODEC_DEV_WRITE_FAIL;
    }
    codec->is_open = true;
    ESP_LOGI(TAG, "Codec is opened");
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static bool cjc8910_is_open(const audio_hw_base_t *h)
{
    audio_codec_cjc8910_t *codec = (audio_codec_cjc8910_t *)h;
    if (codec == NULL) {
        return false;
    }
    return codec->is_open;
}

static int cjc8910_set_vol(const audio_codec_if_t *h, int ch_mask, float db_value)
{
    audio_codec_cjc8910_t *codec = (audio_codec_cjc8910_t *)h;
    (void)ch_mask;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }

    int ret = ESP_CODEC_DEV_OK;
    db_value -= codec->hw_gain;
    uint8_t volume_reg_value = esp_codec_dev_vol_calc_reg(&vol_range, db_value);
    ESP_LOGD(TAG, "%s:set vol: %f, reg: %d", __func__, db_value, volume_reg_value);
    ret |= cjc8910_write_reg(codec, CJC8910_R10_LEFT_DAC_VOLUME, volume_reg_value);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int cjc8910_adc_set_vol(const audio_codec_if_t *h, int ch_mask, float db)
{
    audio_codec_cjc8910_t *codec = (audio_codec_cjc8910_t *)h;
    (void)ch_mask;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = 0;
    uint8_t volume_reg_value = esp_codec_dev_vol_calc_reg(&vol_range, db);
    ret |= cjc8910_write_reg(codec, CJC8910_R21_LEFT_ADC_VOLUME, volume_reg_value);
    ESP_LOGD(TAG, "Set mic gain: %f, reg: %d", db, volume_reg_value);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int cjc8910_set_bits_per_sample(audio_codec_cjc8910_t *codec, int bits)
{
    int ret = ESP_CODEC_DEV_OK;
    int audio_iface = 0;
    ret |= cjc8910_read_reg(codec, CJC8910_R7_AUDIO_INTERFACE, &audio_iface);
    // Bit[3:2]: 11 = 32 bit, 10 = 24 bit, 01 = 20 bit, 00 = 16 bit
    audio_iface &= ~0x0C;
    switch (bits) {
        case 16:
            break;
        case 20:
            audio_iface |= 0x04;
            break;
        case 24:
            audio_iface |= 0x08;
            break;
        case 32:
            audio_iface |= 0x0C;
            break;
        default:
            break;
    }
    ret |= cjc8910_write_reg(codec, CJC8910_R7_AUDIO_INTERFACE, audio_iface);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int cjc8910_set_fs(const audio_hw_base_t *h, esp_codec_dev_sample_info_t *fs, esp_codec_dev_type_t type)
{
    (void)type;
    audio_codec_cjc8910_t *codec = (audio_codec_cjc8910_t *)h;
    if (codec == NULL || fs == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = ESP_CODEC_DEV_OK;
    ret |= cjc8910_set_bits_per_sample(codec, fs->bits_per_sample);
    ret |= cjc8910_config_fmt(codec, CJC8910_I2S_FMT_I2S);
    ESP_LOGD(TAG, "Set sample_rate:%ld, channel:%d, bits_per_sample:%d", fs->sample_rate, fs->channel, fs->bits_per_sample);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int cjc8910_mute_output(const audio_codec_if_t *h, int ch_mask, bool mute)
{
    audio_codec_cjc8910_t *codec = (audio_codec_cjc8910_t *)h;
    (void)ch_mask;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = ESP_CODEC_DEV_OK;
    int regv = 0;
    ret |= cjc8910_read_reg(codec, CJC8910_R5_ADC_DAC_CONTROL, &regv);
    if (mute) {
        regv |= (1 << 3);
    } else {
        regv &= ~(1 << 3);
    }
    ret |= cjc8910_write_reg(codec, CJC8910_R5_ADC_DAC_CONTROL, regv);
    ret |= cjc8910_read_reg(codec, CJC8910_R5_ADC_DAC_CONTROL, &regv);
    ESP_LOGD(TAG, "Set cjc8910 output mute: %d", mute);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int cjc8910_close(const audio_hw_base_t *h)
{
    int ret = ESP_CODEC_DEV_OK;
    audio_codec_cjc8910_t *codec = (audio_codec_cjc8910_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    cjc8910_pa_power(codec, PA_DISABLE);
    ret |= cjc8910_write_reg(codec, CJC8910_R15_RESET, 0x00);
    codec->is_open = false;
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int cjc8910_dac_enable(const audio_codec_if_t *h, bool enable)
{
    audio_codec_cjc8910_t *codec = (audio_codec_cjc8910_t *)h;
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
        cjc8910_mute_output(h, 0x01, false);
        cjc8910_pa_power(codec, PA_ENABLE);
    } else {
        cjc8910_pa_power(codec, PA_DISABLE);
        cjc8910_mute_output(h, 0x01, true);
    }
    codec->enabled = enable;
    return ESP_CODEC_DEV_OK;
}

static int cjc8910_set_reg(const audio_hw_base_t *h, int reg, int value)
{
    audio_codec_cjc8910_t *codec = (audio_codec_cjc8910_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = cjc8910_write_reg(codec, reg, value);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int cjc8910_get_reg(const audio_hw_base_t *h, int reg, int *value)
{
    audio_codec_cjc8910_t *codec = (audio_codec_cjc8910_t *)h;
    if (codec == NULL || value == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = cjc8910_read_reg(codec, reg, value);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_READ_FAIL;
}

const audio_codec_if_t *cjc8910_codec_new(cjc8910_codec_cfg_t *codec_cfg)
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

    audio_codec_cjc8910_t *codec = (audio_codec_cjc8910_t *)calloc(1, sizeof(audio_codec_cjc8910_t));
    if (codec == NULL) {
        ESP_LOGE(TAG, "Fail to alloc memory at %s:%d", __FUNCTION__, __LINE__);
        return NULL;
    }

    codec->base.hw_base.open = cjc8910_open;
    codec->base.hw_base.close = cjc8910_close;
    codec->base.hw_base.is_open = cjc8910_is_open;
    codec->base.hw_base.set_fs = cjc8910_set_fs;
    codec->base.hw_base.set_reg = cjc8910_set_reg;
    codec->base.hw_base.get_reg = cjc8910_get_reg;
    codec->base.hw_base.dump_reg = cjc8910_dump;
    codec->base.ctrl_if = codec_cfg->ctrl_if;

    codec->adc_ops.ops.enable = NULL;
    codec->adc_ops.ops.mute = NULL;
    codec->adc_ops.ops.set_vol = cjc8910_adc_set_vol;
    codec->base.adc_if = &codec->adc_ops;

    codec->dac_ops.ops.enable = cjc8910_dac_enable;
    codec->dac_ops.ops.set_vol = cjc8910_set_vol;
    codec->dac_ops.ops.mute = cjc8910_mute_output;
    codec->dac_ops.pa.enable = NULL;
    codec->base.dac_if = &codec->dac_ops;

    codec->hw_gain = esp_codec_dev_vol_calc_hw_gain(&codec_cfg->pa_cfg.hw_gain);
    do {
        int ret = codec->base.hw_base.open(&codec->base.hw_base, codec_cfg, sizeof(cjc8910_codec_cfg_t));
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
