/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "esp_log.h"

#include "es8388_reg.h"
#include "es8388_codec.h"
#include "es_common.h"
#include "codec_reg_dump.h"
#include "codec_ref_mgr.h"
#include "esp_codec_dev_vol.h"
#include "es8388_proc_priv.h"

static const char *TAG = "ES8388";

/**
 * @brief  ES8388 codec driver instance
 */
typedef struct {
    audio_codec_if_t    base;         /*!< Codec interface vtable container */
    audio_hw_adc_if_t   adc_ops;      /*!< ADC operation callbacks */
    audio_hw_dac_if_t   dac_ops;      /*!< DAC operation callbacks */
    es8388_codec_cfg_t  cfg;          /*!< Board configuration snapshot */
    bool                is_open;      /*!< True after open completes */
    bool                adc_enabled;  /*!< True when ADC path is running */
    bool                dac_enabled;  /*!< True when DAC path is running */
    float               hw_gain;      /*!< Cached hardware gain in dB */
} audio_codec_es8388_t;

static const esp_codec_dev_vol_range_t vol_range = {
    .min_vol = {
        .vol = 0xC0,
        .db_value = -96.0,
    },
    .max_vol = {
        .vol = 0,
        .db_value = 0.0,
    },
};

static const audio_codec_hw_proc_ops_t hw_proc = {
    .alc_new = audio_hw_es8388_alc_new,
    .line_new = audio_hw_es8388_line_new,
};

static const esp_codec_dev_device_map_info_t order_info[] = {
    {ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0)}},
    {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0)}},
};

static int es8388_write_reg(audio_codec_es8388_t *codec, int reg, int value)
{
    const audio_codec_ctrl_if_t *ctrl_if = codec->base.ctrl_if;
    return ctrl_if->write_reg(ctrl_if, reg, 1, &value, 1);
}

static int es8388_read_reg(audio_codec_es8388_t *codec, int reg, int *value)
{
    *value = 0;
    const audio_codec_ctrl_if_t *ctrl_if = codec->base.ctrl_if;
    return ctrl_if->read_reg(ctrl_if, reg, 1, value, 1);
}

static int es8388_set_adc_dac_volume(audio_codec_es8388_t *codec, bool is_adc, int volume, int dot)
{
    int res = 0;
    if (volume < -96 || volume > 0) {
        if (volume < -96) {
            volume = -96;
        } else {
            volume = 0;
        }
    }
    dot = (dot >= 5 ? 1 : 0);
    volume = (-volume << 1) + dot;
    if (is_adc) {
        res |= es8388_write_reg(codec, ES8388_ADCCONTROL8, volume);
        res |= es8388_write_reg(codec, ES8388_ADCCONTROL9, volume);  // ADC Right Volume=0db
    } else {
        res |= es8388_write_reg(codec, ES8388_DACCONTROL5, volume);
        res |= es8388_write_reg(codec, ES8388_DACCONTROL4, volume);
    }
    return res;
}

static int es8388_set_mic_voice_mute(audio_codec_es8388_t *codec, bool mute, bool is_adc)
{
    int res = 0;
    int reg = 0;
    int reg_addr = is_adc ? ES8388_ADCCONTROL7 : ES8388_DACCONTROL3;
    res = es8388_read_reg(codec, reg_addr, &reg);
    reg = reg & 0xFB;
    res |= es8388_write_reg(codec, reg_addr, reg | (((int)mute) << 2));
    return res;
}

static int es8388_start(audio_codec_es8388_t *codec, bool is_adc)
{
    int res = 0;
    if (is_adc) {
        res |= es8388_write_reg(codec, ES8388_ADCPOWER, 0x00);  // power up adc and line in
    } else {
        res |= es8388_write_reg(codec, ES8388_DACPOWER, 0x3c);  // power up dac and line out
    }
    ESP_LOGD(TAG, "Start on mode: %s", is_adc ? "ADC" : "DAC");
    return res;
}

static int es8388_stop(audio_codec_es8388_t *codec, bool is_adc)
{
    int res = 0;
    if (is_adc) {
        res |= es8388_write_reg(codec, ES8388_ADCPOWER, 0xFF);  // power down adc and line in
    } else {
        res |= es8388_write_reg(codec, ES8388_DACPOWER, 0x00);
    }
    ESP_LOGD(TAG, "Stop on mode: %s", is_adc ? "ADC" : "DAC");
    return res;
}

static int es8388_config_fmt(audio_codec_es8388_t *codec, es_i2s_fmt_t fmt)
{
    int res = 0;
    int reg = 0;
    res = es8388_read_reg(codec, ES8388_ADCCONTROL4, &reg);
    reg = reg & 0xfc;
    res |= es8388_write_reg(codec, ES8388_ADCCONTROL4, reg | fmt);

    res |= es8388_read_reg(codec, ES8388_DACCONTROL1, &reg);
    reg = reg & 0xf9;
    res |= es8388_write_reg(codec, ES8388_DACCONTROL1, reg | (fmt << 1));

    return res;
}

static int es8388_set_mic_gain(audio_codec_es8388_t *codec, int channel_mask, float db)
{
    /* ADCCONTROL1: 4-bit MIC PGA per slot (bits 7-4 ch0, bits 3-0 ch1). */
    const int mic_pga_gain_max = 0x0F;
    const int mic_pga_mask_l = 0xF0;
    const int mic_pga_mask_r = 0x0F;

    int gain_steps = db > 0 ? (int)(db / 3) : 0;
    if (gain_steps < 0) {
        gain_steps = 0;
    } else if (gain_steps > mic_pga_gain_max) {
        gain_steps = mic_pga_gain_max;
    }
    const unsigned gn = (unsigned)gain_steps & (unsigned)mic_pga_gain_max;

    unsigned upd = 0;
    unsigned val = 0;
    if (channel_mask & ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0)) {
        upd |= (unsigned)mic_pga_mask_l;
        val |= (gn << 4) & (unsigned)mic_pga_mask_l;
    }
    if (channel_mask & ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1)) {
        upd |= (unsigned)mic_pga_mask_r;
        val |= gn & (unsigned)mic_pga_mask_r;
    }

    int reg = 0;
    int res = es8388_read_reg(codec, ES8388_ADCCONTROL1, &reg);
    unsigned u = ((unsigned)reg & 0xFFU) & ~upd;
    u |= val & upd;
    res |= es8388_write_reg(codec, ES8388_ADCCONTROL1, (int)u);  // MIC PGA
    return res;
}

static es_bits_length_t get_bits_enum(uint8_t bits)
{
    switch (bits) {
        case 16:
        default:
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

static int es8388_set_bits_per_sample(audio_codec_es8388_t *codec, uint8_t bits_length)
{
    int res = 0;
    int reg = 0;
    int bits = (int)get_bits_enum(bits_length);

    res = es8388_read_reg(codec, ES8388_ADCCONTROL4, &reg);
    reg = reg & 0xe3;
    res |= es8388_write_reg(codec, ES8388_ADCCONTROL4, reg | (bits << 2));
    res |= es8388_read_reg(codec, ES8388_DACCONTROL1, &reg);
    reg = reg & 0xc7;
    res |= es8388_write_reg(codec, ES8388_DACCONTROL1, reg | (bits << 3));
    return res;
}

static void es8388_pa_power(audio_codec_es8388_t *codec, es_pa_setting_t pa_setting)
{
    int16_t pa_pin = codec->cfg.pa_cfg.pa_pin;
    const audio_codec_gpio_if_t *gpio_if = codec->cfg.gpio_if;
    if (pa_pin == -1 || gpio_if == NULL) {
        ESP_LOGD(TAG, "Skip PA control: pa_pin:%d, gpio_if:%p", pa_pin, gpio_if);
        return;
    }
    bool active_low = codec->cfg.pa_cfg.pa_active_low;
    if (pa_setting & ES_PA_SETUP) {
        gpio_if->setup(pa_pin, AUDIO_GPIO_DIR_OUT, AUDIO_GPIO_MODE_FLOAT);
    }
    if (pa_setting & ES_PA_ENABLE) {
        gpio_if->set(pa_pin, active_low ? false : true);
    }
    if (pa_setting & ES_PA_DISABLE) {
        gpio_if->set(pa_pin, active_low ? true : false);
    }
    ESP_LOGD(TAG, "Pa power status is %d", pa_setting);
}

static inline void es8388_apply_cfg(audio_codec_es8388_t *codec, const es8388_codec_cfg_t *codec_cfg)
{
    memcpy(&codec->cfg, codec_cfg, sizeof(es8388_codec_cfg_t));
}

static int es8388_pa_enable(const audio_codec_if_t *h, bool enable)
{
    audio_codec_es8388_t *codec = (audio_codec_es8388_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    es8388_pa_power(codec, enable ? ES_PA_ENABLE : ES_PA_DISABLE);
    return ESP_OK;
}

static int es8388_open(const audio_hw_base_t *h, void *cfg, int cfg_size)
{
    audio_codec_es8388_t *codec = (audio_codec_es8388_t *)h;
    es8388_codec_cfg_t *codec_cfg = (es8388_codec_cfg_t *)cfg;
    if (codec == NULL || codec_cfg == NULL || codec_cfg->ctrl_if == NULL || cfg_size != sizeof(es8388_codec_cfg_t)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int res = ESP_CODEC_DEV_OK;

    // 0x04 mute/0x00 unmute&ramp;
    res |= es8388_write_reg(codec, ES8388_DACCONTROL3, 0x04);
    /* Chip Control and Power Management */
    res |= es8388_write_reg(codec, ES8388_CONTROL1, 0x86);
    res |= es8388_write_reg(codec, ES8388_CONTROL1, 0x06);
    res |= es8388_write_reg(codec, ES8388_CONTROL2, 0x50);
    res |= es8388_write_reg(codec, ES8388_CHIPPOWER, 0xFF);
    res |= es8388_write_reg(codec, ES8388_CHIPPOWER, 0x00);  // normal all and power up all

    // Disable the internal DLL to improve 8K sample rate
    res |= es8388_write_reg(codec, 0x35, 0xA0);
    res |= es8388_write_reg(codec, 0x37, 0xD0);
    res |= es8388_write_reg(codec, 0x39, 0xD0);

    res |= es8388_write_reg(codec, ES8388_MASTERMODE, codec_cfg->sys_cfg.is_master ? 0x80 : 0x00);  // CODEC IN I2S SLAVE MODE

    /* dac */
    res |= es8388_write_reg(codec, ES8388_DACPOWER, 0xC0);  // disable DAC and disable Lout/Rout/1/2
    res |= es8388_write_reg(codec, ES8388_CONTROL1, 0x12);  // Enfr=0,Play&Record Mode,(0x17-both of mic&play)
    //    res |= es8388_write_reg(codec, ES8388_CONTROL2, 0);  // LPVrefBuf=0, PDN_ANA=0
    res |= es8388_write_reg(codec, ES8388_DACCONTROL1, 0x18);   // 1a 0x18:16bit iis , 0x00:24
    res |= es8388_write_reg(codec, ES8388_DACCONTROL2, 0x02);   // DACFsMode,SINGLE SPEED; DACFsRatio,256
    res |= es8388_write_reg(codec, ES8388_DACCONTROL16, 0x00);  // 0x00 audio on LIN1&RIN1,  0x09 LIN2&RIN2
    res |= es8388_write_reg(codec, ES8388_DACCONTROL17, 0x90);  // only left DAC to left mixer enable 0db
    res |= es8388_write_reg(codec, ES8388_DACCONTROL20, 0x90);  // only right DAC to right mixer enable 0db
    // Set internal ADC and DAC use the same LRCK clock, ADC LRCK as internal LRCK
    res |= es8388_write_reg(codec, ES8388_DACCONTROL21, 0x80);
    res |= es8388_write_reg(codec, ES8388_DACCONTROL23, 0x00);  // vroi=0
    res |= es8388_set_adc_dac_volume(codec, false, 0, 0);  // 0db

    res |= es8388_write_reg(codec, ES8388_DACCONTROL24, 0x1E);  // Set L1 R1 L2 R2 volume. 0x00: -30dB, 0x1E: 0dB, 0x21: 3dB
    res |= es8388_write_reg(codec, ES8388_DACCONTROL25, 0x1E);
    res |= es8388_write_reg(codec, ES8388_DACCONTROL26, 0);
    res |= es8388_write_reg(codec, ES8388_DACCONTROL27, 0);

    // TODO: Default use DAC_ALL
    int tmp = DAC_OUTPUT_LOUT1 | DAC_OUTPUT_LOUT2 | DAC_OUTPUT_ROUT1 | DAC_OUTPUT_ROUT2;
    res |= es8388_write_reg(codec, ES8388_DACPOWER, tmp);  // 0x3c Enable DAC and Enable Lout/Rout/1/2
    /* adc */
    res |= es8388_write_reg(codec, ES8388_ADCPOWER, 0xFF);
    res |= es8388_write_reg(codec, ES8388_ADCCONTROL1, 0xbb);  // MIC Left and Right channel PGA gain
    tmp = 0;
    // TODO: Default use ADC LINE1
    // 0x00 LINSEL & RINSEL, LIN1/RIN1 as ADC Input; DSSEL,use one DS Reg11; DSR, LINPUT1-RINPUT1
    res |= es8388_write_reg(codec, ES8388_ADCCONTROL2, ADC_INPUT_LINPUT1_RINPUT1);
    res |= es8388_write_reg(codec, ES8388_ADCCONTROL3, 0x02);
    res |= es8388_write_reg(codec, ES8388_ADCCONTROL4, 0x0c);  // 16 Bits length and I2S serial audio data format
    res |= es8388_write_reg(codec, ES8388_ADCCONTROL5, 0x02);  // ADCFsMode,single SPEED,RATIO=256
    // ALC for Microphone
    res |= es8388_set_adc_dac_volume(codec, true, 0, 0);    // 0db
    res |= es8388_write_reg(codec, ES8388_ADCPOWER, 0x09);  // Power on ADC
    if (res != 0) {
        ESP_LOGI(TAG, "Fail to write register");
        return ESP_CODEC_DEV_WRITE_FAIL;
    }
    codec->is_open = true;
    return ESP_CODEC_DEV_OK;
}

static int es8388_adc_enable(const audio_codec_if_t *h, bool enable)
{
    audio_codec_es8388_t *codec = (audio_codec_es8388_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    if (codec->adc_enabled == enable) {
        return ESP_CODEC_DEV_OK;
    }
    int res;
    if (enable == false) {
        es8388_set_mic_voice_mute(codec, true, true);
        res = es8388_stop(codec, true);
    } else {
        res = es8388_start(codec, true);
        es8388_set_mic_voice_mute(codec, false, true);
    }
    if (res == ESP_CODEC_DEV_OK) {
        codec->adc_enabled = enable;
        ESP_LOGD(TAG, "Codec adc is %s", enable ? "enabled" : "disabled");
    }
    return res;
}

static int es8388_dac_enable(const audio_codec_if_t *h, bool enable)
{
    audio_codec_es8388_t *codec = (audio_codec_es8388_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    if (codec->dac_enabled == enable) {
        return ESP_CODEC_DEV_OK;
    }
    int res;
    if (enable == false) {
        es8388_set_mic_voice_mute(codec, true, false);
        es8388_pa_power(codec, ES_PA_DISABLE);
        res = es8388_stop(codec, false);
    } else {
        res = es8388_start(codec, false);
        es8388_pa_power(codec, ES_PA_ENABLE);
        es8388_set_mic_voice_mute(codec, false, false);
    }
    if (res == ESP_CODEC_DEV_OK) {
        codec->dac_enabled = enable;
        ESP_LOGD(TAG, "Codec dac is %s", enable ? "enabled" : "disabled");
    }
    return res;
}

static int es8388_mute(const audio_codec_if_t *h, int ch_mask, bool mute)
{
    audio_codec_es8388_t *codec = (audio_codec_es8388_t *)h;
    (void)ch_mask;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    return es8388_set_mic_voice_mute(codec, mute, false);
}

static int es8388_adc_mute(const audio_codec_if_t *h, int ch_mask, bool mute)
{
    audio_codec_es8388_t *codec = (audio_codec_es8388_t *)h;
    (void)ch_mask;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    return es8388_set_mic_voice_mute(codec, mute, true);
}

static int es8388_set_vol(const audio_codec_if_t *h, int ch_mask, float db_value)
{
    audio_codec_es8388_t *codec = (audio_codec_es8388_t *)h;
    (void)ch_mask;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    db_value -= codec->hw_gain;
    int volume = esp_codec_dev_vol_calc_reg(&vol_range, db_value);
    int res = 0;
    if (ch_mask & ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0)) {
        res |= es8388_write_reg(codec, ES8388_DACCONTROL4, volume);
    }
    if (ch_mask & ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1)) {
        res |= es8388_write_reg(codec, ES8388_DACCONTROL5, volume);
    }
    ESP_LOGD(TAG, "Set volume reg:%x db:%f", volume, db_value);
    return res ? ESP_CODEC_DEV_WRITE_FAIL : ESP_CODEC_DEV_OK;
}

static int es8388_set_fs(const audio_hw_base_t *h, esp_codec_dev_sample_info_t *fs, esp_codec_dev_type_t type)
{
    (void)type;
    audio_codec_es8388_t *codec = (audio_codec_es8388_t *)h;
    if (codec == NULL || fs == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int res = 0;
    res |= es8388_config_fmt(codec, ES_I2S_NORMAL);
    res |= es8388_set_bits_per_sample(codec, fs->bits_per_sample);
    return res;
}

static int es8388_set_gain(const audio_codec_if_t *h, int ch_mask, float db)
{
    audio_codec_es8388_t *codec = (audio_codec_es8388_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    return es8388_set_mic_gain(codec, ch_mask, db);
}

static int es8388_close(const audio_hw_base_t *h)
{
    audio_codec_es8388_t *codec = (audio_codec_es8388_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open) {
        audio_codec_ctrl_info_t ctrl_info = {0};
        codec->cfg.ctrl_if->get_info(codec->cfg.ctrl_if, &ctrl_info);
        int ref_count = codec_ref_release(&ctrl_info);
        if (ref_count < 0) {
            ESP_LOGE(TAG, "Failed to release codec device open reference");
            return ESP_CODEC_DEV_WRITE_FAIL;
        }
        if (ref_count == 0) {
            es8388_pa_power(codec, ES_PA_DISABLE);
            ESP_LOGI(TAG, "Codec hardware closed");
        } else {
            ESP_LOGI(TAG, "Codec still in use (ref_count=%d), skip hardware close", ref_count);
        }
        codec->adc_enabled = false;
        codec->dac_enabled = false;
        codec->is_open = false;
    }
    return ESP_CODEC_DEV_OK;
}

static void es8388_dump(const audio_hw_base_t *h)
{
    audio_codec_es8388_t *codec = (audio_codec_es8388_t *)h;
    if (codec == NULL || codec->is_open == false) {
        return;
    }
    codec_reg_dump_ctx_t dump;
    codec_reg_dump_init(&dump, TAG, 2);
    for (int i = 0; i <= ES8388_DACCONTROL30; i++) {
        int value = 0;
        int ret = es8388_read_reg(codec, i, &value);
        if (ret != ESP_CODEC_DEV_OK) {
            break;
        }
        codec_reg_dump_push(&dump, i, value);
    }
    codec_reg_dump_end(&dump);
}

static int es8388_set_reg(const audio_hw_base_t *h, int reg, int value)
{
    audio_codec_es8388_t *codec = (audio_codec_es8388_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = es8388_write_reg(codec, reg, value);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8388_get_reg(const audio_hw_base_t *h, int reg, int *value)
{
    audio_codec_es8388_t *codec = (audio_codec_es8388_t *)h;
    if (codec == NULL || value == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = es8388_read_reg(codec, reg, value);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_READ_FAIL;
}

static bool es8388_is_open(const audio_hw_base_t *h)
{
    audio_codec_es8388_t *codec = (audio_codec_es8388_t *)h;
    if (codec == NULL) {
        return false;
    }
    return codec->is_open;
}

static int es8388_get_order_list(const audio_hw_base_t *h, const esp_codec_dev_device_map_info_t **order_list, int *list_size)
{
    audio_codec_es8388_t *codec = (audio_codec_es8388_t *)h;
    if (codec == NULL || order_list == NULL || list_size == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *list_size = sizeof(order_info) / sizeof(order_info[0]);
    *order_list = order_info;
    return ESP_CODEC_DEV_OK;
}

const audio_codec_if_t *es8388_codec_new(es8388_codec_cfg_t *codec_cfg)
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
    if (codec_cfg->ctrl_if->get_info == NULL) {
        ESP_LOGE(TAG, "Control interface missing get_info");
        return NULL;
    }
    audio_codec_ctrl_info_t ctrl_info = {0};
    if (codec_cfg->ctrl_if->get_info(codec_cfg->ctrl_if, &ctrl_info) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to get control interface info");
        return NULL;
    }

    audio_codec_es8388_t *codec = (audio_codec_es8388_t *)calloc(1, sizeof(audio_codec_es8388_t));
    if (codec == NULL) {
        ESP_LOGE(TAG, "Fail to alloc memory at %s:%d", __FUNCTION__, __LINE__);
        return NULL;
    }

    codec->base.hw_base.open = es8388_open;
    codec->base.hw_base.is_open = es8388_is_open;
    codec->base.hw_base.set_fs = es8388_set_fs;
    codec->base.hw_base.set_reg = es8388_set_reg;
    codec->base.hw_base.get_reg = es8388_get_reg;
    codec->base.hw_base.dump_reg = es8388_dump;
    codec->base.hw_base.get_order_list = es8388_get_order_list;
    codec->base.hw_base.close = es8388_close;
    codec->base.ctrl_if = codec_cfg->ctrl_if;
    codec->base.hw_proc = &hw_proc;

    codec->adc_ops.ops.mute = es8388_adc_mute;
    codec->adc_ops.ops.set_vol = es8388_set_gain;
    codec->adc_ops.ops.enable = es8388_adc_enable;
    codec->base.adc_if = &codec->adc_ops;

    codec->dac_ops.ops.mute = es8388_mute;
    codec->dac_ops.ops.set_vol = es8388_set_vol;
    codec->dac_ops.ops.enable = es8388_dac_enable;
    codec->dac_ops.pa.enable = es8388_pa_enable;
    codec->base.dac_if = &codec->dac_ops;

    codec->hw_gain = esp_codec_dev_vol_calc_hw_gain(&codec_cfg->pa_cfg.hw_gain);
    do {
        int ref_count = codec_ref_acquire(&ctrl_info);
        if (ref_count < 0) {
            ESP_LOGE(TAG, "Failed to acquire codec device open reference");
            break;
        }
        es8388_apply_cfg(codec, codec_cfg);
        es8388_pa_power(codec, ES_PA_SETUP | ES_PA_DISABLE);
        if (ref_count == 1) {
            int ret = codec->base.hw_base.open(&codec->base.hw_base, &codec->cfg, sizeof(es8388_codec_cfg_t));
            if (ret != 0) {
                ESP_LOGE(TAG, "Open fail, ret: %d", ret);
                codec_ref_release(&ctrl_info);
                break;
            }
        } else {
            codec->is_open = true;
            ESP_LOGI(TAG, "Codec already opened, reusing (ref_count=%d)", ref_count);
        }
        return &codec->base;
    } while (0);
    if (codec) {
        free(codec);
    }
    return NULL;
}
