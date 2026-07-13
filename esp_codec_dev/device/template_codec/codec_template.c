/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "esp_log.h"

#include "codec_template.h"
#include "codec_template_reg.h"
#include "es_common.h"
#include "codec_reg_dump.h"
#include "esp_codec_dev_vol.h"
#include "esp_codec_dev_os.h"
#include "hw_proc/audio_hw_proc_if.h"

static const char *TAG = "CHIP";

/**
 * Template notes:
 * - This file is not part of the build. It is a reference skeleton for adding
 *   a new codec driver under the audio_codec_if_t framework.
 * - Symbol prefix chip_ and TAG "CHIP" apply to functions, types, and public
 *   symbols (for example ES8311, ES8389). File-scope static tables use short
 *   names: coeff_div, vol_range, hw_proc, order_info, codec_caps.
 * - The default shape follows a full-duplex codec like ES8311:
 *     base + adc_ops + dac_ops + cfg + open/enable state
 * - For ADC-only codecs, remove dac_ops / dac_enabled / DAC callbacks.
 * - For DAC-only codecs, remove adc_ops / adc_enabled / ADC callbacks.
 * - If multiple logical instances can share one physical chip, copy the reference
 *   counting pattern from es8311/es8389 (codec_ref_helper). Do not add ref_mgr
 *   statistics to this template; that helper API may change.
 *
 * File layout before functions (optional blocks may be removed):
 *   instance struct -> coeff_div -> vol_range -> hw_proc ->
 *   order_info -> codec_caps.
 *
 * Function order in this file (match chip_codec_new() vtable assignment):
 * 1. Internal helpers (static, audio_codec_chip_t * where possible):
 *    check_codec -> check_codec_and_ptr -> write_reg / read_reg / update_bits ->
 *    pa_power / reset -> set_bits_per_sample / config_fmt / get_coeff /
 *    config_sample -> adc_start / adc_stop / dac_start / dac_stop -> suspend ->
 *    apply_adc_mute / apply_adc_vol / apply_dac_mute / apply_dac_vol.
 *    Register logic lives in apply_*; open / enable / close call apply_* directly,
 *    not vtable wrappers. No forward declarations between helpers and callbacks.
 * 2. hw_base callbacks (audio_hw_base.h order):
 *    open -> is_open -> set_fs -> set_reg -> get_reg -> dump -> close ->
 *    get_order_list -> get_adc_label -> get_caps.
 * 3. adc ops: enable -> mute -> set_vol.
 * 4. dac ops: enable -> mute -> set_vol.
 * 5. pa_enable.
 * 6. save_adc_label (optional, only used by chip_codec_new).
 * 7. chip_codec_new (last).
 *
 * Copy workflow: global replace chip_ -> <your_codec>_, audio_codec_chip_t ->
 * audio_codec_<your_codec>_t, chip_cfg_t -> <your_codec>_codec_cfg_t,
 * chip_codec_new -> <your_codec>_codec_new, CHIP -> <YOUR_CODEC> (TAG, for example ES8311).
 */

/**
 * @brief  Codec template driver instance
 */
typedef struct {
    audio_codec_if_t   base;                                   /*!< Codec interface vtable container */
    audio_hw_adc_if_t  adc_ops;                                /*!< ADC operation callbacks */
    audio_hw_dac_if_t  dac_ops;                                /*!< DAC operation callbacks */
    chip_cfg_t         cfg;                                    /*!< Board configuration snapshot */
    bool               is_open;                                /*!< True after open completes */
    bool               adc_enabled;                            /*!< True when ADC path is running */
    bool               dac_enabled;                            /*!< True when DAC path is running */
    float              hw_gain;                                /*!< Cached hardware gain in dB */
    char               adc_label[AUDIO_HW_ADC_LABEL_MAX_LEN];  /*!< Optional, for get_adc_label */
} audio_codec_chip_t;

/**
 * @brief  Clock coefficient table entry (optional, see es8311/es8389)
 */
typedef struct {
    uint32_t  mclk;       /*!< MCLK frequency in Hz */
    uint32_t  rate;       /*!< Sample rate in Hz */
    uint8_t   pre_div;    /*!< Pre-divider */
    uint8_t   pre_multi;  /*!< Pre-multiplier */
    uint8_t   adc_div;    /*!< ADC clock divider */
    uint8_t   dac_div;    /*!< DAC clock divider */
    uint8_t   fs_mode;    /*!< Speed mode */
    uint8_t   lrck_h;     /*!< High byte of LRCK divider */
    uint8_t   lrck_l;     /*!< Low byte of LRCK divider */
    uint8_t   bclk_div;   /*!< Bit clock divider */
    uint8_t   adc_osr;    /*!< ADC oversampling ratio */
    uint8_t   dac_osr;    /*!< DAC oversampling ratio */
} chip_coeff_div_t;

/* Optional: remove when the chip has no programmable MCLK/LRCK table */
static const chip_coeff_div_t coeff_div[] = {
    {12288000, 48000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
};

static const esp_codec_dev_vol_range_t vol_range = {
    .min_vol = {
        .vol = 0x00,
        .db_value = -96.0,
    },
    .max_vol = {
        .vol = 0xFF,
        .db_value = 0.0,
    },
};

/* Optional: wire base.hw_proc when *_new handlers are implemented (see es8311) */
static const audio_codec_hw_proc_ops_t hw_proc = {
    .alc_new = NULL,
    .drc_new = NULL,
    .eq_new = NULL,
    .line_new = NULL,
    .mute_new = NULL,
};

static const esp_codec_dev_device_map_info_t order_info[] = {
    {ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0)}},
    {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0)}},
};

/* Optional: remove when get_caps is not implemented */
static const esp_codec_dev_capability_t codec_caps = {
    .mode = ESP_CODEC_DEV_CAPS_MODE_FLEXIBLE,
    .flexible = {
        .max_channels = 2,
        .bits_per_sample = (const uint8_t[]){ 16, 24, 32 },
        .bits_num = 3,
        .sample_rates = (const uint32_t[]){
            8000, 16000, 32000, 44100, 48000,
        },
        .sample_rate_num = 5,
    },
};

static int chip_check_codec(const audio_codec_chip_t *codec)
{
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    return ESP_CODEC_DEV_OK;
}

static int chip_check_codec_and_ptr(const audio_codec_chip_t *codec, const void *ptr)
{
    int ret = chip_check_codec(codec);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    if (ptr == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    return ESP_CODEC_DEV_OK;
}

static int chip_write_reg(audio_codec_chip_t *codec, int reg, int value)
{
    return codec->cfg.ctrl_if->write_reg(codec->cfg.ctrl_if, reg, 1, &value, 1);
}

static int chip_read_reg(audio_codec_chip_t *codec, int reg, int *value)
{
    *value = 0;
    return codec->cfg.ctrl_if->read_reg(codec->cfg.ctrl_if, reg, 1, value, 1);
}

static int chip_update_bits(audio_codec_chip_t *codec, int reg, int mask, int value)
{
    int regv = 0;
    int ret = chip_read_reg(codec, reg, &regv);
    if (ret != ESP_CODEC_DEV_OK) {
        return ESP_CODEC_DEV_READ_FAIL;
    }
    regv &= ~mask;
    regv |= (value & mask);
    ret = chip_write_reg(codec, reg, regv);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static void chip_pa_power(audio_codec_chip_t *codec, es_pa_setting_t pa_setting)
{
    int16_t pa_pin = codec->cfg.pa_cfg.pa_pin;
    const audio_codec_gpio_if_t *gpio_if = codec->cfg.gpio_if;
    if (pa_pin < 0 || gpio_if == NULL) {
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
}

static void chip_reset(audio_codec_chip_t *codec)
{
    int16_t reset_pin = codec->cfg.reset_cfg.reset_pin;
    const audio_codec_gpio_if_t *gpio_if = codec->cfg.gpio_if;
    if (reset_pin < 0 || gpio_if == NULL) {
        return;
    }
    bool active_low = codec->cfg.reset_cfg.reset_active_low;
    gpio_if->setup(reset_pin, AUDIO_GPIO_DIR_OUT, AUDIO_GPIO_MODE_FLOAT);
    gpio_if->set(reset_pin, active_low ? 0 : 1);
    esp_codec_dev_sleep(20);
    gpio_if->set(reset_pin, active_low ? 1 : 0);
    esp_codec_dev_sleep(200);
}

static int chip_set_bits_per_sample(audio_codec_chip_t *codec, uint8_t bits)
{
    int ret = chip_check_codec(codec);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    switch (bits) {
        case 16:
        case 24:
        case 32:
            break;
        default:
            return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    /**
     * TODO:
     * Map bits_per_sample to the chip's serial interface registers.
     * Common locations are IFACE / SDP / I2S control registers.
     */
    return ESP_CODEC_DEV_OK;
}

static int chip_config_fmt(audio_codec_chip_t *codec)
{
    int ret = chip_check_codec(codec);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    /**
     * TODO:
     * Configure the codec's serial audio interface format from the codec
     * configuration or the driver's fixed bus-format convention.
     * Example: I2S / left-justified / DSP / TDM.
     */
    return ESP_CODEC_DEV_OK;
}

/* Optional: remove with coeff_div when clock table is not used */
static int chip_get_coeff(uint32_t mclk, uint32_t rate)
{
    for (int i = 0; i < (int)(sizeof(coeff_div) / sizeof(coeff_div[0])); i++) {
        if (coeff_div[i].rate == rate && coeff_div[i].mclk == mclk) {
            return i;
        }
    }
    return -1;
}

static int chip_config_sample(audio_codec_chip_t *codec, uint32_t sample_rate, int mclk_multiple)
{
    int ret = chip_check_codec(codec);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    if (sample_rate == 0) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (mclk_multiple <= 0) {
        mclk_multiple = 256;
    }
    int mclk_freq = (int)(sample_rate * mclk_multiple);
    int coeff = chip_get_coeff((uint32_t)mclk_freq, sample_rate);
    if (coeff < 0) {
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    (void)coeff;
    /**
     * TODO:
     * Apply coeff_div[coeff] to the clock registers according to the datasheet.
     * Remove coeff_div / chip_get_coeff when the chip uses a fixed clock scheme.
     */
    return ESP_CODEC_DEV_OK;
}

static int chip_adc_start(audio_codec_chip_t *codec)
{
    /* TODO: Add ADC pipeline/startup sequence if needed. */
    return chip_update_bits(codec,
                            CODEC_TEMPLATE_REG_ADC_POWER,
                            CODEC_TEMPLATE_BIT_ADC_ENABLE,
                            CODEC_TEMPLATE_BIT_ADC_ENABLE);
}

static int chip_adc_stop(audio_codec_chip_t *codec)
{
    /* TODO: Add ADC shutdown sequence if needed. */
    return chip_update_bits(codec,
                            CODEC_TEMPLATE_REG_ADC_POWER,
                            CODEC_TEMPLATE_BIT_ADC_ENABLE,
                            0);
}

static int chip_dac_start(audio_codec_chip_t *codec)
{
    /* TODO: Add clock/pipeline/startup sequence if needed. */
    return chip_update_bits(codec,
                            CODEC_TEMPLATE_REG_DAC_POWER,
                            CODEC_TEMPLATE_BIT_DAC_ENABLE,
                            CODEC_TEMPLATE_BIT_DAC_ENABLE);
}

static int chip_dac_stop(audio_codec_chip_t *codec)
{
    /* TODO: Add shutdown/power-down sequence if needed. */
    return chip_update_bits(codec,
                            CODEC_TEMPLATE_REG_DAC_POWER,
                            CODEC_TEMPLATE_BIT_DAC_ENABLE,
                            0);
}

static int chip_suspend(audio_codec_chip_t *codec)
{
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = ESP_CODEC_DEV_OK;
    /**
     * TODO:
     * No generic suspend flow is provided in this template.
     * Implement the chip-specific standby / power-down sequence according to the
     * datasheet, and keep PA / mute / register update ordering consistent with the
     * target codec's recommended shutdown flow.
     */
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int chip_apply_adc_mute(audio_codec_chip_t *codec, int ch_mask, bool mute)
{
    (void)ch_mask;
    /**
     * TODO:
     * Replace CODEC_TEMPLATE_REG_ADC_MUTE / BIT_ADC_MUTE with the real field.
     * If the chip has no dedicated ADC mute, return ESP_CODEC_DEV_NOT_SUPPORT.
     */
    return chip_update_bits(codec,
                            CODEC_TEMPLATE_REG_ADC_MUTE,
                            CODEC_TEMPLATE_BIT_ADC_MUTE,
                            mute ? CODEC_TEMPLATE_BIT_ADC_MUTE : 0);
}

static int chip_apply_adc_vol(audio_codec_chip_t *codec, int ch_mask, float db)
{
    float db_before = db;
    /* ADC setpoint is a direct register value mapping in this template. */
    int regv = (int)db;
    if (regv < 0x00) {
        regv = 0x00;
    } else if (regv > 0xFF) {
        regv = 0xFF;
    }
    ESP_LOGD(TAG, "Codec ADC volume set: ch_mask=0x%x db_before=%.2f reg=0x%X", ch_mask, db_before, regv);
    int ret = chip_write_reg(codec, CODEC_TEMPLATE_REG_ADC_VOLUME, regv);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int chip_apply_dac_mute(audio_codec_chip_t *codec, int ch_mask, bool mute)
{
    (void)ch_mask;
    return chip_update_bits(codec,
                            CODEC_TEMPLATE_REG_DAC_MUTE,
                            CODEC_TEMPLATE_BIT_DAC_MUTE,
                            mute ? CODEC_TEMPLATE_BIT_DAC_MUTE : 0);
}

static int chip_apply_dac_vol(audio_codec_chip_t *codec, int ch_mask, float db)
{
    float db_before = db;
    db -= codec->hw_gain;
    int regv = esp_codec_dev_vol_calc_reg(&vol_range, db);
    ESP_LOGD(TAG, "Codec DAC volume set: ch_mask=0x%x db_before=%.2f hw_gain=%.2f db_after=%.2f reg=0x%X",
             ch_mask, db_before, codec->hw_gain, db, regv);
    int ret = chip_write_reg(codec, CODEC_TEMPLATE_REG_DAC_VOLUME, regv);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int chip_open(const audio_hw_base_t *h, void *cfg, int cfg_size)
{
    audio_codec_chip_t *codec = (audio_codec_chip_t *)h;
    chip_cfg_t *codec_cfg = (chip_cfg_t *)cfg;
    if (codec == NULL || codec_cfg == NULL || codec_cfg->ctrl_if == NULL ||
        cfg_size != sizeof(chip_cfg_t)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }

    memcpy(&codec->cfg, codec_cfg, sizeof(chip_cfg_t));

    chip_pa_power(codec, ES_PA_SETUP | ES_PA_DISABLE);
    chip_reset(codec);

    /**
     * TODO:
     * Replace the example register sequence below with the real chip init
     * sequence. Keep reset/clock/interface/power sections separated so the
     * bring-up flow is easy to review.
     */
    int ret = ESP_CODEC_DEV_OK;
    ret |= chip_write_reg(codec, CODEC_TEMPLATE_REG_RESET, 0xFF);
    ret |= chip_write_reg(codec, CODEC_TEMPLATE_REG_SYSTEM_POWER, 0x00);
    ret |= chip_write_reg(codec, CODEC_TEMPLATE_REG_SYSTEM_IFACE, 0x00);
    if (ret != ESP_CODEC_DEV_OK) {
        chip_reset(codec);
        return ESP_CODEC_DEV_WRITE_FAIL;
    }

    codec->adc_enabled = false;
    codec->dac_enabled = false;
    codec->is_open = true;
    return ESP_CODEC_DEV_OK;
}

static bool chip_is_open(const audio_hw_base_t *h)
{
    const audio_codec_chip_t *codec = (const audio_codec_chip_t *)h;
    return codec && codec->is_open;
}

static int chip_set_fs(const audio_hw_base_t *h, esp_codec_dev_sample_info_t *fs, esp_codec_dev_type_t type)
{
    (void)type;
    audio_codec_chip_t *codec = (audio_codec_chip_t *)h;
    int ret = chip_check_codec_and_ptr(codec, fs);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }

    int mclk_multiple = fs->mclk_multiple ? fs->mclk_multiple : 256;
    ret = chip_set_bits_per_sample(codec, fs->bits_per_sample);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }

    ret = chip_config_fmt(codec);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }

    return chip_config_sample(codec, fs->sample_rate, mclk_multiple);
}

static int chip_set_reg(const audio_hw_base_t *h, int reg, int value)
{
    audio_codec_chip_t *codec = (audio_codec_chip_t *)h;
    int ret = chip_check_codec(codec);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    int write_ret = chip_write_reg(codec, reg, value);
    return (write_ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int chip_get_reg(const audio_hw_base_t *h, int reg, int *value)
{
    audio_codec_chip_t *codec = (audio_codec_chip_t *)h;
    int ret = chip_check_codec_and_ptr(codec, value);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    int read_ret = chip_read_reg(codec, reg, value);
    return (read_ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_READ_FAIL;
}

static void chip_dump(const audio_hw_base_t *h)
{
    audio_codec_chip_t *codec = (audio_codec_chip_t *)h;
    int ret = chip_check_codec(codec);
    if (ret != ESP_CODEC_DEV_OK) {
        return;
    }
    codec_reg_dump_ctx_t dump;
    codec_reg_dump_init(&dump, TAG, 2);
    for (int i = 0; i <= CODEC_TEMPLATE_MAX_REGISTER; i++) {
        int regv = 0;
        if (chip_read_reg(codec, i, &regv) != ESP_CODEC_DEV_OK) {
            break;
        }
        codec_reg_dump_push(&dump, i, regv);
    }
    codec_reg_dump_end(&dump);
}

static int chip_close(const audio_hw_base_t *h)
{
    audio_codec_chip_t *codec = (audio_codec_chip_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = ESP_CODEC_DEV_OK;
    if (codec->is_open) {
        ret |= chip_apply_adc_mute(codec, 0x03, true);
        ret |= chip_apply_dac_mute(codec, 0x03, true);
        chip_pa_power(codec, ES_PA_DISABLE);
        ret |= chip_suspend(codec);
        if (ret != ESP_CODEC_DEV_OK) {
            return ret;
        }
        codec->adc_enabled = false;
        codec->dac_enabled = false;
        codec->is_open = false;
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int chip_get_order_list(const audio_hw_base_t *h,
                               const esp_codec_dev_device_map_info_t **order_list,
                               int *list_size)
{
    audio_codec_chip_t *codec = (audio_codec_chip_t *)h;
    if (codec == NULL || order_list == NULL || list_size == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *order_list = order_info;
    *list_size = sizeof(order_info) / sizeof(order_info[0]);
    return ESP_CODEC_DEV_OK;
}

static int chip_get_adc_label(const audio_hw_base_t *h, const char **label)
{
    audio_codec_chip_t *codec = (audio_codec_chip_t *)h;
    if (codec == NULL || label == NULL || codec->adc_label[0] == '\0') {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *label = codec->adc_label;
    return ESP_CODEC_DEV_OK;
}

/* Optional: remove when capability query is not needed */
static int chip_get_caps(const audio_hw_base_t *h, esp_codec_dev_type_t dev_type,
                         esp_codec_dev_capability_t *caps, int *count)
{
    if (h == NULL || count == NULL || *count < 0 ||
        dev_type == ESP_CODEC_DEV_TYPE_NONE ||
        (dev_type & ~(ESP_CODEC_DEV_TYPE_IN_OUT)) != 0) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (caps == NULL || *count == 0) {
        *count = 1;
        return ESP_CODEC_DEV_OK;
    }
    caps[0] = codec_caps;
    caps[0].dev_type = dev_type;
    *count = 1;
    return ESP_CODEC_DEV_OK;
}

static int chip_adc_enable(const audio_codec_if_t *h, bool enable)
{
    audio_codec_chip_t *codec = (audio_codec_chip_t *)h;
    int ret = chip_check_codec(codec);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    if (codec->adc_enabled == enable) {
        return ESP_CODEC_DEV_OK;
    }
    if (enable) {
        ret = chip_adc_start(codec);
    } else {
        ret = chip_adc_stop(codec);
    }
    if (ret == ESP_CODEC_DEV_OK) {
        codec->adc_enabled = enable;
        ESP_LOGD(TAG, "Codec ADC is %s", enable ? "enabled" : "disabled");
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int chip_adc_mute(const audio_codec_if_t *h, int ch_mask, bool mute)
{
    audio_codec_chip_t *codec = (audio_codec_chip_t *)h;
    int ret = chip_check_codec(codec);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    return chip_apply_adc_mute(codec, ch_mask, mute);
}

static int chip_adc_set_vol(const audio_codec_if_t *h, int ch_mask, float db)
{
    audio_codec_chip_t *codec = (audio_codec_chip_t *)h;
    int ret = chip_check_codec(codec);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    return chip_apply_adc_vol(codec, ch_mask, db);
}

static int chip_dac_enable(const audio_codec_if_t *h, bool enable)
{
    audio_codec_chip_t *codec = (audio_codec_chip_t *)h;
    int ret = chip_check_codec(codec);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    if (codec->dac_enabled == enable) {
        return ESP_CODEC_DEV_OK;
    }
    if (enable) {
        /* Follow startup flow: start path -> power on PA -> unmute channel path. */
        ret = chip_dac_start(codec);
        if (ret != ESP_CODEC_DEV_OK) {
            return ret;
        }
        chip_pa_power(codec, ES_PA_ENABLE);
        ret |= chip_apply_dac_mute(codec, 0x03, false);
    } else {
        /* Follow shutdown flow: mute first -> power off PA -> stop path. */
        ret = chip_apply_dac_mute(codec, 0x03, true);
        chip_pa_power(codec, ES_PA_DISABLE);
        ret |= chip_dac_stop(codec);
    }
    if (ret == ESP_CODEC_DEV_OK) {
        codec->dac_enabled = enable;
        ESP_LOGD(TAG, "Codec DAC is %s", enable ? "enabled" : "disabled");
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int chip_dac_mute(const audio_codec_if_t *h, int ch_mask, bool mute)
{
    audio_codec_chip_t *codec = (audio_codec_chip_t *)h;
    int ret = chip_check_codec(codec);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    return chip_apply_dac_mute(codec, ch_mask, mute);
}

static int chip_dac_set_vol(const audio_codec_if_t *h, int ch_mask, float db)
{
    audio_codec_chip_t *codec = (audio_codec_chip_t *)h;
    int ret = chip_check_codec(codec);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    return chip_apply_dac_vol(codec, ch_mask, db);
}

static int chip_pa_enable(const audio_codec_if_t *h, bool enable)
{
    audio_codec_chip_t *codec = (audio_codec_chip_t *)h;
    int ret = chip_check_codec(codec);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    chip_pa_power(codec, enable ? ES_PA_ENABLE : ES_PA_DISABLE);
    return ESP_CODEC_DEV_OK;
}

/* Optional: remove when adc_cfg.label is not used */
static void chip_save_adc_label(audio_codec_chip_t *codec, const char *label)
{
    codec->adc_label[0] = '\0';
    if (label != NULL) {
        strncpy(codec->adc_label, label, sizeof(codec->adc_label) - 1);
        codec->adc_label[sizeof(codec->adc_label) - 1] = '\0';
    }
}

const audio_codec_if_t *chip_codec_new(chip_cfg_t *codec_cfg)
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

    audio_codec_chip_t *codec = (audio_codec_chip_t *)calloc(1, sizeof(audio_codec_chip_t));
    if (codec == NULL) {
        ESP_LOGE(TAG, "Fail to alloc memory at %s:%d", __FUNCTION__, __LINE__);
        return NULL;
    }

    codec->base.hw_base.open = chip_open;
    codec->base.hw_base.is_open = chip_is_open;
    codec->base.hw_base.set_fs = chip_set_fs;
    codec->base.hw_base.set_reg = chip_set_reg;
    codec->base.hw_base.get_reg = chip_get_reg;
    codec->base.hw_base.dump_reg = chip_dump;
    codec->base.hw_base.close = chip_close;
    codec->base.hw_base.get_order_list = chip_get_order_list;
    codec->base.hw_base.get_adc_label = chip_get_adc_label;
    codec->base.hw_base.get_caps = chip_get_caps;
    codec->base.ctrl_if = codec_cfg->ctrl_if;
    chip_save_adc_label(codec, codec_cfg->adc_cfg.label);
    codec->base.hw_proc = &hw_proc;  /* Optional: remove when hw_proc is not used */

    codec->adc_ops.ops.enable = chip_adc_enable;
    codec->adc_ops.ops.mute = chip_adc_mute;
    codec->adc_ops.ops.set_vol = chip_adc_set_vol;
    codec->base.adc_if = &codec->adc_ops;

    codec->dac_ops.ops.enable = chip_dac_enable;
    codec->dac_ops.ops.mute = chip_dac_mute;
    codec->dac_ops.ops.set_vol = chip_dac_set_vol;
    codec->dac_ops.pa.enable = chip_pa_enable;
    codec->base.dac_if = &codec->dac_ops;

    codec->hw_gain = esp_codec_dev_vol_calc_hw_gain(&codec_cfg->pa_cfg.hw_gain);

    /**
     * For ADC-only codecs:
     *   codec->base.dac_if = NULL;
     * For DAC-only codecs:
     *   codec->base.adc_if = NULL;
     */

    int ret = codec->base.hw_base.open(&codec->base.hw_base, codec_cfg, sizeof(chip_cfg_t));
    if (ret != 0) {
        ESP_LOGE(TAG, "Open fail, ret: %d", ret);
        free(codec);
        return NULL;
    }
    return &codec->base;
}
