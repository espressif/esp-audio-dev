/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_log.h"

#include "aw88298_dac.h"
#include "aw88298_reg.h"
#include "esp_codec_dev_os.h"
#include "codec_reg_dump.h"

static const char *TAG = "AW88298";

/**
 * @brief  AW88298 codec driver instance
 */
typedef struct {
    audio_codec_if_t     base;     /*!< Codec interface vtable container */
    audio_hw_dac_if_t    dac_ops;  /*!< DAC operation callbacks */
    aw88298_codec_cfg_t  cfg;      /*!< Board configuration snapshot */
    bool                 is_open;  /*!< True after open completes */
    bool                 enabled;  /*!< True when DAC path is running */
    float                hw_gain;  /*!< Cached hardware gain in dB */
} audio_codec_aw88298_t;

/* The volume register mapped to decibel table can get from codec data-sheet
   Volume control register 0x0C description:
       0xC0 - '-96dB' ... 0x00 - '+0dB'
*/
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

static int aw88298_write_reg(audio_codec_aw88298_t *codec, int reg, int value)
{
    uint8_t write_data[2] = {(uint8_t)((value & 0xFFFF) >> 8), (uint8_t)(value & 0xFF)};
    return codec->cfg.ctrl_if->write_reg(codec->cfg.ctrl_if, reg, 1, write_data, 2);
}

static int aw88298_read_reg(audio_codec_aw88298_t *codec, int reg, int *value)
{
    int ret = 0;
    uint8_t read_data[2] = {0};
    ret = codec->cfg.ctrl_if->read_reg(codec->cfg.ctrl_if, reg, 1, read_data, 2);
    *value = ((int)read_data[0] << 8) | read_data[1];
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_READ_FAIL;
}

static int aw88298_set_bits_per_sample(audio_codec_aw88298_t *codec, uint8_t bits)
{
    int ret = 0;
    int dac_iface = 0;
    ret = aw88298_read_reg(codec, AW88298_I2SCTRL_REG06, &dac_iface);
    dac_iface &= ~(0xF0);
    switch (bits) {
        case 16:
        default:
            break;
        case 24:
            dac_iface |= 0x90;
            break;
        case 32:
            dac_iface |= 0xE0;
            break;
    }
    ret |= aw88298_write_reg(codec, AW88298_I2SCTRL_REG06, dac_iface);
    ESP_LOGD(TAG, "Bits %d", bits);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int aw88298_config_sample(audio_codec_aw88298_t *codec, int sample_rate)
{
    int ret = 0;
    int dac_iface = 0;
    ret = aw88298_read_reg(codec, AW88298_I2SCTRL_REG06, &dac_iface);
    dac_iface &= ~(0x0F);
    switch (sample_rate) {
        case 8000:
            break;
        case 11025:
            dac_iface |= 0x01;
            break;
        case 12000:
            dac_iface |= 0x02;
            break;
        case 16000:
            dac_iface |= 0x03;
            break;
        case 22050:
            dac_iface |= 0x04;
            break;
        case 24000:
            dac_iface |= 0x05;
            break;
        case 32000:
            dac_iface |= 0x06;
            break;
        case 44100:
            dac_iface |= 0x07;
            break;
        case 48000:
            dac_iface |= 0x08;
            break;
        case 96000:
            dac_iface |= 0x09;
            break;
        case 192000:
            dac_iface |= 0x0A;
            break;
        default:
            ESP_LOGE(TAG, "Sample rate(%d) can not support", sample_rate);
            return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    ESP_LOGD(TAG, "Current sample rate: %d", sample_rate);
    ret |= aw88298_write_reg(codec, AW88298_I2SCTRL_REG06, dac_iface);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int aw88298_set_fs(const audio_hw_base_t *h, esp_codec_dev_sample_info_t *fs, esp_codec_dev_type_t type)
{
    (void)type;
    audio_codec_aw88298_t *codec = (audio_codec_aw88298_t *)h;
    if (codec == NULL || fs == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = 0;
    ret |= aw88298_set_bits_per_sample(codec, fs->bits_per_sample);
    ret |= aw88298_config_sample(codec, fs->sample_rate);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int aw88298_stop(audio_codec_aw88298_t *codec)
{
    int ret = 0;
    int data = 0;
    ret |= aw88298_read_reg(codec, AW88298_SYSCTRL_REG04, &data);
    data |= 0x03;
    data &= ~(1 << 6);
    ret |= aw88298_write_reg(codec, AW88298_SYSCTRL_REG04, data);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int aw88298_start(audio_codec_aw88298_t *codec)
{
    int ret = 0;
    int data = 0;
    ret |= aw88298_read_reg(codec, AW88298_SYSCTRL_REG04, &data);
    data &= ~0x03;
    data |= (1 << 6);
    ret |= aw88298_write_reg(codec, AW88298_SYSCTRL_REG04, data);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int aw88298_set_mute(const audio_codec_if_t *h, int ch_mask, bool mute)
{
    audio_codec_aw88298_t *codec = (audio_codec_aw88298_t *)h;
    (void)ch_mask;
    if (codec == NULL || codec->is_open == false) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int regv;
    int ret = aw88298_read_reg(codec, AW88298_SYSCTRL2_REG05, &regv);
    if (ret != ESP_CODEC_DEV_OK) {
        return ESP_CODEC_DEV_READ_FAIL;
    }
    if (mute) {
        regv = regv | (1 << 4);
    } else {
        regv = regv & (~(1 << 4));
    }
    ret = aw88298_write_reg(codec, AW88298_SYSCTRL2_REG05, regv);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int aw88298_set_vol(const audio_codec_if_t *h, int ch_mask, float volume)
{
    audio_codec_aw88298_t *codec = (audio_codec_aw88298_t *)h;
    (void)ch_mask;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    volume -= codec->hw_gain;
    int reg = esp_codec_dev_vol_calc_reg(&vol_range, volume);
    reg = (reg << 8) | 0x64;
    int ret = aw88298_write_reg(codec, AW88298_HAGCCFG4_REG0C, reg);
    ESP_LOGD(TAG, "Set volume reg:%x db:%.2f", reg, esp_codec_dev_vol_calc_db(&vol_range, reg >> 8));
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static void aw88298_reset(audio_codec_aw88298_t *codec)
{
    int16_t reset_pin = codec->cfg.reset_cfg.reset_pin;
    bool reset_active_low = codec->cfg.reset_cfg.reset_active_low;
    if (reset_pin <= 0 || codec->cfg.gpio_if == NULL) {
        return;
    }
    codec->cfg.gpio_if->setup(reset_pin, AUDIO_GPIO_DIR_OUT, AUDIO_GPIO_MODE_FLOAT);
    codec->cfg.gpio_if->set(reset_pin, reset_active_low ? 0 : 1);
    esp_codec_dev_sleep(10);
    codec->cfg.gpio_if->set(reset_pin, reset_active_low ? 1 : 0);
    esp_codec_dev_sleep(50);
}

static int aw88298_open(const audio_hw_base_t *h, void *cfg, int cfg_size)
{
    audio_codec_aw88298_t *codec = (audio_codec_aw88298_t *)h;
    aw88298_codec_cfg_t *codec_cfg = (aw88298_codec_cfg_t *)cfg;
    if (codec == NULL || codec_cfg == NULL || cfg_size != sizeof(aw88298_codec_cfg_t) || codec_cfg->ctrl_if == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = ESP_CODEC_DEV_OK;
    memcpy(&codec->cfg, codec_cfg, sizeof(aw88298_codec_cfg_t));
    aw88298_reset(codec);

    ret |= aw88298_write_reg(codec, AW88298_RESET_REG00, 0x55aa);     // Reset chip
    ret |= aw88298_write_reg(codec, AW88298_SYSCTRL_REG04, 0x4040);   // I2SEN=1 AMPPD=0 PWDN=0
    ret |= aw88298_write_reg(codec, AW88298_SYSCTRL2_REG05, 0x0008);  // RMSE=0 HAGCE=0 HDCCE=0 HMUTE=0
    ret |= aw88298_write_reg(codec, AW88298_I2SCTRL_REG06, 0x3CC8);   // I2SBCK=0 (BCK mode 16*2)
    ret |= aw88298_write_reg(codec, AW88298_HAGCCFG4_REG0C, 0x3064);  // volume setting
    ret |= aw88298_write_reg(codec, AW88298_BSTCTRL2_REG61, 0x0673);  // default:0x6673: BOOST mode disabled

    if (ret != 0) {
        return ESP_CODEC_DEV_WRITE_FAIL;
    }

    codec->is_open = true;
    return ESP_CODEC_DEV_OK;
}

static int aw88298_close(const audio_hw_base_t *h)
{
    audio_codec_aw88298_t *codec = (audio_codec_aw88298_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open) {
        aw88298_stop(codec);
        codec->is_open = false;
    }
    return ESP_CODEC_DEV_OK;
}

static bool aw88298_is_open(const audio_hw_base_t *h)
{
    audio_codec_aw88298_t *codec = (audio_codec_aw88298_t *)h;
    if (codec == NULL) {
        return false;
    }
    return codec->is_open;
}

static int aw88298_enable(const audio_codec_if_t *h, bool enable)
{
    int ret = ESP_CODEC_DEV_OK;
    audio_codec_aw88298_t *codec = (audio_codec_aw88298_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    if (codec->enabled == enable) {
        return ESP_CODEC_DEV_OK;
    }
    int16_t reset_pin = codec->cfg.reset_cfg.reset_pin;
    bool reset_active_low = codec->cfg.reset_cfg.reset_active_low;
    if (enable) {
        if (reset_pin > 0 && codec->cfg.gpio_if) {
            ret |= codec->cfg.gpio_if->set(reset_pin, reset_active_low ? 1 : 0);
            esp_codec_dev_sleep(10);
        }
        ret |= aw88298_start(codec);
    } else {
        ret |= aw88298_stop(codec);
        if (reset_pin > 0 && codec->cfg.gpio_if) {
            esp_codec_dev_sleep(10);
            ret |= codec->cfg.gpio_if->set(reset_pin, reset_active_low ? 0 : 1);
        }
    }
    if (ret == ESP_CODEC_DEV_OK) {
        codec->enabled = enable;
        ESP_LOGD(TAG, "Codec is %s", enable ? "enabled" : "disabled");
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int aw88298_set_reg(const audio_hw_base_t *h, int reg, int value)
{
    audio_codec_aw88298_t *codec = (audio_codec_aw88298_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = aw88298_write_reg(codec, reg, value);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int aw88298_get_reg(const audio_hw_base_t *h, int reg, int *value)
{
    audio_codec_aw88298_t *codec = (audio_codec_aw88298_t *)h;
    if (codec == NULL || value == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = aw88298_read_reg(codec, reg, value);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_READ_FAIL;
}

static void aw88298_dump(const audio_hw_base_t *h)
{
    audio_codec_aw88298_t *codec = (audio_codec_aw88298_t *)h;
    if (codec == NULL || codec->is_open == false) {
        return;
    }
    codec_reg_dump_ctx_t dump;
    codec_reg_dump_init(&dump, TAG, 4);
    for (int i = 0; i <= 0x14; i++) {
        if (i == 0x08 || i == 0x0D || i == 0x0E || i == 0x0F || i == 0x11) {
            continue;
        }
        int value = 0;
        int ret = aw88298_read_reg(codec, i, &value);
        if (ret != ESP_CODEC_DEV_OK) {
            break;
        }
        codec_reg_dump_push(&dump, i, value);
    }
    for (int i = 0x60; i <= 0x61; i++) {
        int value = 0;
        int ret = aw88298_read_reg(codec, i, &value);
        if (ret != ESP_CODEC_DEV_OK) {
            break;
        }
        codec_reg_dump_push(&dump, i, value);
    }
    codec_reg_dump_end(&dump);
}

const audio_codec_if_t *aw88298_codec_new(aw88298_codec_cfg_t *codec_cfg)
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

    audio_codec_aw88298_t *codec = (audio_codec_aw88298_t *)calloc(1, sizeof(audio_codec_aw88298_t));
    if (codec == NULL) {
        ESP_LOGE(TAG, "Fail to alloc memory at %s:%d", __FUNCTION__, __LINE__);
        return NULL;
    }

    codec->base.hw_base.open = aw88298_open;
    codec->base.hw_base.is_open = aw88298_is_open;
    codec->base.hw_base.set_fs = aw88298_set_fs;
    codec->base.hw_base.set_reg = aw88298_set_reg;
    codec->base.hw_base.get_reg = aw88298_get_reg;
    codec->base.hw_base.dump_reg = aw88298_dump;
    codec->base.hw_base.close = aw88298_close;
    codec->base.ctrl_if = codec_cfg->ctrl_if;

    codec->dac_ops.ops.enable = aw88298_enable;
    codec->dac_ops.ops.set_vol = aw88298_set_vol;
    codec->dac_ops.ops.mute = aw88298_set_mute;
    codec->base.dac_if = &codec->dac_ops;
    codec->base.adc_if = NULL;

    codec->hw_gain = esp_codec_dev_vol_calc_hw_gain(&codec_cfg->pa_cfg.hw_gain);
    do {
        int ret = codec->base.hw_base.open(&codec->base.hw_base, codec_cfg, sizeof(aw88298_codec_cfg_t));
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
