/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "freertos/FreeRTOS.h"

#include "esp_log.h"

#include "es8311_codec.h"
#include "es8311_reg.h"
#include "codec_ref_mgr.h"
#include "es_common.h"
#include "codec_reg_dump.h"
#include "esp_codec_dev_os.h"
#include "es8311_proc_priv.h"

static const char *TAG = "ES8311";

#ifndef ES8311_DEBUG_VERIFY_REG_RW
#define ES8311_DEBUG_VERIFY_REG_RW  0
#endif  /* ES8311_DEBUG_VERIFY_REG_RW */

/**
 * @brief  ES8311 codec driver instance
 */
typedef struct {
    audio_codec_if_t    base;                                   /*!< Codec interface vtable container */
    audio_hw_adc_if_t   adc_ops;                                /*!< ADC operation callbacks */
    audio_hw_dac_if_t   dac_ops;                                /*!< DAC operation callbacks */
    es8311_codec_cfg_t  cfg;                                    /*!< Board configuration snapshot */
    bool                is_open;                                /*!< True after open completes */
    bool                adc_enabled;                            /*!< True when ADC path is running */
    bool                dac_enabled;                            /*!< True when DAC path is running */
    float               hw_gain;                                /*!< Cached hardware gain in dB */
    char                adc_label[AUDIO_HW_ADC_LABEL_MAX_LEN];  /*!< ADC label for multi-instance routing */
} audio_codec_es8311_t;

/**
 * @brief  ES8311 clock coefficient table entry
 */
typedef struct {
    uint32_t  mclk;       /*!< MCLK frequency in Hz */
    uint32_t  rate;       /*!< Sample rate in Hz */
    uint8_t   pre_div;    /*!< Pre-divider, range 1 to 8 */
    uint8_t   pre_multi;  /*!< Pre-multiplier selection (x1, x2, x4, x8) */
    uint8_t   adc_div;    /*!< ADC clock divider */
    uint8_t   dac_div;    /*!< DAC clock divider */
    uint8_t   fs_mode;    /*!< Speed mode: 0 single-speed, 1 double-speed */
    uint8_t   lrck_h;     /*!< High byte of LRCK divider */
    uint8_t   lrck_l;     /*!< Low byte of LRCK divider */
    uint8_t   bclk_div;   /*!< Bit clock divider */
    uint8_t   adc_osr;    /*!< ADC oversampling ratio */
    uint8_t   dac_osr;    /*!< DAC oversampling ratio */
} es8311_coeff_div_t;

/* codec hifi mclk clock divider coefficients */
static const es8311_coeff_div_t coeff_div[] = {
    //  mclk     rate   pre_div  mult  adc_div dac_div fs_mode lrch  lrcl  bckdiv osr
    /* 8k */
    {12288000, 8000, 0x06, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {18432000, 8000, 0x03, 0x02, 0x03, 0x03, 0x00, 0x05, 0xff, 0x18, 0x10, 0x20},
    {16384000, 8000, 0x08, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {8192000, 8000, 0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {6144000, 8000, 0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {4096000, 8000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {3072000, 8000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {2048000, 8000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {1536000, 8000, 0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {1024000, 8000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},

    /* 11.025k */
    {11289600, 11025, 0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {5644800, 11025, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {2822400, 11025, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {1411200, 11025, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},

    /* 12k */
    {12288000, 12000, 0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {6144000, 12000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {3072000, 12000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {1536000, 12000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},

    /* 16k */
    {12288000, 16000, 0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {18432000, 16000, 0x03, 0x02, 0x03, 0x03, 0x00, 0x02, 0xff, 0x0c, 0x10, 0x20},
    {16384000, 16000, 0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {8192000, 16000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {6144000, 16000, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {4096000, 16000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {3072000, 16000, 0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {2048000, 16000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {1536000, 16000, 0x03, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {1024000, 16000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},

    /* 22.05k */
    {11289600, 22050, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {5644800, 22050, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2822400, 22050, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1411200, 22050, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},

    /* 24k */
    {12288000, 24000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 24000, 0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 24000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000, 24000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000, 24000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},

    /* 32k */
    {12288000, 32000, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 32000, 0x03, 0x04, 0x03, 0x03, 0x00, 0x02, 0xff, 0x0c, 0x10, 0x10},
    {16384000, 32000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {8192000, 32000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 32000, 0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {4096000, 32000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000, 32000, 0x03, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2048000, 32000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000, 32000, 0x03, 0x08, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x02, 0x10, 0x10},
    {1024000, 32000, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},

    /* 44.1k */
    {11289600, 44100, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {5644800, 44100, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2822400, 44100, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1411200, 44100, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},

    /* 48k */
    {12288000, 48000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 48000, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 48000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000, 48000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000, 48000, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},

    /* 64k */
    {12288000, 64000, 0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 64000, 0x03, 0x04, 0x03, 0x03, 0x01, 0x01, 0x7f, 0x06, 0x10, 0x10},
    {16384000, 64000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {8192000, 64000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 64000, 0x01, 0x04, 0x03, 0x03, 0x01, 0x01, 0x7f, 0x06, 0x10, 0x10},
    {4096000, 64000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000, 64000, 0x01, 0x08, 0x03, 0x03, 0x01, 0x01, 0x7f, 0x06, 0x10, 0x10},
    {2048000, 64000, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000, 64000, 0x01, 0x08, 0x01, 0x01, 0x01, 0x00, 0xbf, 0x03, 0x18, 0x18},
    {1024000, 64000, 0x01, 0x08, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x02, 0x10, 0x10},

    /* 88.2k */
    {11289600, 88200, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {5644800, 88200, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2822400, 88200, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1411200, 88200, 0x01, 0x08, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x02, 0x10, 0x10},

    /* 96k */
    {24576000, 96000, 0x02, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {12288000, 96000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 96000, 0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 96000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000, 96000, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000, 96000, 0x01, 0x08, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x02, 0x10, 0x10},
};

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

static const audio_codec_hw_proc_ops_t hw_proc = {
    .alc_new = audio_hw_es8311_alc_new,
    .drc_new = audio_hw_es8311_drc_new,
    .eq_new = audio_hw_es8311_eq_new,
    .mute_new = audio_hw_es8311_mute_new,
};

static const esp_codec_dev_device_map_info_t order_info[] = {
    {ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0)}},
    {ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS, 2, {.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0)}},
};

static const uint8_t es8311_cap_bits[] = {16, 24, 32};

static const uint32_t es8311_cap_rates[] = {
    8000, 11025, 12000, 16000, 22050, 24000,
    32000, 44100, 48000, 64000, 88200, 96000,
};

static const esp_codec_dev_capability_t adc_caps = {
    .dev_type = ESP_CODEC_DEV_TYPE_IN,
    .mode = ESP_CODEC_DEV_CAPS_MODE_FLEXIBLE,
    .flexible = {
        .max_channels = 2,  // Include ref signal channel
        .bits_per_sample = es8311_cap_bits,
        .bits_num = sizeof(es8311_cap_bits) / sizeof(es8311_cap_bits[0]),
        .sample_rates = es8311_cap_rates,
        .sample_rate_num = sizeof(es8311_cap_rates) / sizeof(es8311_cap_rates[0]),
    },
};

static const esp_codec_dev_capability_t dac_caps = {
    .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    .mode = ESP_CODEC_DEV_CAPS_MODE_FLEXIBLE,
    .flexible = {
        .max_channels = 1,
        .bits_per_sample = es8311_cap_bits,
        .bits_num = sizeof(es8311_cap_bits) / sizeof(es8311_cap_bits[0]),
        .sample_rates = es8311_cap_rates,
        .sample_rate_num = sizeof(es8311_cap_rates) / sizeof(es8311_cap_rates[0]),
    },
};

static int es8311_write_reg(audio_codec_es8311_t *codec, int reg, int value)
{
    const audio_codec_ctrl_if_t *ctrl_if = codec->cfg.ctrl_if;
    int ret = ctrl_if->write_reg(ctrl_if, reg, 1, &value, 1);
    if (ret != ESP_CODEC_DEV_OK || ES8311_DEBUG_VERIFY_REG_RW == 0) {
        return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
    }

    int regv = 0;
    ret = ctrl_if->read_reg(ctrl_if, reg, 1, &regv, 1);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Write verify failed: read failed, reg=0x%02x, write=%d", reg, value);
        return ESP_CODEC_DEV_READ_FAIL;
    }
    if ((uint8_t)regv != (uint8_t)value) {
        ESP_LOGE(TAG, "Write verify failed: reg=0x%02x, expect=0x%02x, actual=0x%02x", reg, value & 0xFF, regv & 0xFF);
        return ESP_CODEC_DEV_WRITE_FAIL;
    }
    return ESP_CODEC_DEV_OK;
}

static int es8311_read_reg(audio_codec_es8311_t *codec, int reg, int *value)
{
    *value = 0;
    return codec->cfg.ctrl_if->read_reg(codec->cfg.ctrl_if, reg, 1, value, 1);
}

static int es8311_update_reg_bit(audio_codec_es8311_t *codec, uint8_t reg_addr, uint8_t update_bits, uint8_t data)
{
    int regv = 0;
    int ret = ESP_CODEC_DEV_OK;
    ret |= es8311_read_reg(codec, reg_addr, &regv);
    if (ret != ESP_CODEC_DEV_OK) {
        return ESP_CODEC_DEV_READ_FAIL;
    }
    regv = (regv & (~update_bits)) | (update_bits & data);
    ret = es8311_write_reg(codec, reg_addr, regv);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8311_config_fmt(audio_codec_es8311_t *codec, es_i2s_fmt_t fmt)
{
    int ret = ESP_CODEC_DEV_OK;
    int adc_iface = 0, dac_iface = 0;
    ret = es8311_read_reg(codec, ES8311_SDPIN_REG09, &dac_iface);
    ret |= es8311_read_reg(codec, ES8311_SDPOUT_REG0A, &adc_iface);
    switch (fmt) {
        case ES_I2S_NORMAL:
            ESP_LOGD(TAG, "ES8311 in I2S Format");
            dac_iface &= 0xFC;
            adc_iface &= 0xFC;
            break;
        case ES_I2S_LEFT:
        case ES_I2S_RIGHT:
            ESP_LOGD(TAG, "ES8311 in LJ Format");
            adc_iface &= 0xFC;
            dac_iface &= 0xFC;
            adc_iface |= 0x01;
            dac_iface |= 0x01;
            break;
        case ES_I2S_DSP:
            ESP_LOGD(TAG, "ES8311 in DSP-A Format");
            adc_iface &= 0xDC;
            dac_iface &= 0xDC;
            adc_iface |= 0x03;
            dac_iface |= 0x03;
            break;
        default:
            dac_iface &= 0xFC;
            adc_iface &= 0xFC;
            break;
    }
    ret |= es8311_write_reg(codec, ES8311_SDPIN_REG09, dac_iface);
    ret |= es8311_write_reg(codec, ES8311_SDPOUT_REG0A, adc_iface);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8311_set_bits_per_sample(audio_codec_es8311_t *codec, int bits)
{
    int ret = ESP_CODEC_DEV_OK;
    if (bits <= 8) {
        ESP_LOGE(TAG, "ES8311 does not support bits %d", bits);
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    /* SDPIN/SDPOUT data length field is bits [4:2] (mask 0x1c). Always clear then set. */
    uint8_t data_len;
    switch (bits) {
        case 16:
        default:
            data_len = 0x0c;
            break;
        case 24:
            data_len = 0x00;
            break;
        case 32:
            data_len = 0x10;
            break;
    }
    ret |= es8311_update_reg_bit(codec, ES8311_SDPIN_REG09, 0x1c, data_len);
    ret |= es8311_update_reg_bit(codec, ES8311_SDPOUT_REG0A, 0x1c, data_len);

    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int get_coeff(uint32_t mclk, uint32_t rate)
{
    for (int i = 0; i < (sizeof(coeff_div) / sizeof(coeff_div[0])); i++) {
        if (coeff_div[i].rate == rate && coeff_div[i].mclk == mclk) {
            return i;
        }
    }
    return -1;
}

static int es8311_power_down(audio_codec_es8311_t *codec)
{
    int ret = es8311_write_reg(codec, ES8311_DAC_REG32, 0x00);
    ret |= es8311_write_reg(codec, ES8311_ADC_REG17, 0x00);
    ret |= es8311_write_reg(codec, ES8311_SYSTEM_REG0E, 0xFF);
    ret |= es8311_write_reg(codec, ES8311_SYSTEM_REG12, 0x02);
    ret |= es8311_write_reg(codec, ES8311_SYSTEM_REG14, 0x00);
    ret |= es8311_write_reg(codec, ES8311_SYSTEM_REG0D, 0xFA);
    ret |= es8311_write_reg(codec, ES8311_ADC_REG15, 0x00);
    ret |= es8311_write_reg(codec, ES8311_CLK_MANAGER_REG02, 0x10);
    ret |= es8311_write_reg(codec, ES8311_RESET_REG00, 0x00);
    ret |= es8311_write_reg(codec, ES8311_RESET_REG00, 0x1F);
    ret |= es8311_write_reg(codec, ES8311_CLK_MANAGER_REG01, 0x30);
    ret |= es8311_write_reg(codec, ES8311_CLK_MANAGER_REG01, 0x00);
    ret |= es8311_write_reg(codec, ES8311_GP_REG45, 0x00);
    ret |= es8311_write_reg(codec, ES8311_SYSTEM_REG0D, 0xFC);
    ret |= es8311_write_reg(codec, ES8311_CLK_MANAGER_REG02, 0x00);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8311_start(audio_codec_es8311_t *codec, bool is_adc)
{
    int ret = ESP_CODEC_DEV_OK;
    if (is_adc) {
        // PDM DMIC enable or disable
        int regv = codec->cfg.adc_cfg.digital_mic ? 0x5A : 0x1A;
        ret |= es8311_write_reg(codec, ES8311_SYSTEM_REG14, regv);
        ret |= es8311_update_reg_bit(codec, ES8311_SDPOUT_REG0A, 0x40, 0x00);
        ret |= es8311_write_reg(codec, ES8311_ADC_REG17, 0xBF);
        ret |= es8311_write_reg(codec, ES8311_SYSTEM_REG0E, 0x02);
        ret |= es8311_update_reg_bit(codec, ES8311_SYSTEM_REG0D, 0x30, 0x00);
        ret |= es8311_write_reg(codec, ES8311_ADC_REG15, 0x00);
        ret |= es8311_update_reg_bit(codec, ES8311_RESET_REG00, 0x02, 0x00);
        ret |= es8311_update_reg_bit(codec, ES8311_CLK_MANAGER_REG01, 0x0A, 0x0A);
    } else {
        ret |= es8311_update_reg_bit(codec, ES8311_SDPIN_REG09, 0x40, 0x00);
        ret |= es8311_write_reg(codec, ES8311_SYSTEM_REG0E, 0x02);
        ret |= es8311_write_reg(codec, ES8311_SYSTEM_REG12, 0x00);
        ret |= es8311_update_reg_bit(codec, ES8311_SYSTEM_REG0D, 0x08, 0x00);
        ret |= es8311_write_reg(codec, ES8311_DAC_REG37, 0x08);
        ret |= es8311_update_reg_bit(codec, ES8311_RESET_REG00, 0x01, 0x00);
        ret |= es8311_update_reg_bit(codec, ES8311_CLK_MANAGER_REG01, 0x05, 0x05);
    }
    esp_codec_dev_sleep(50);
    ret |= es8311_write_reg(codec, ES8311_GP_REG45, 0x00);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8311_stop(audio_codec_es8311_t *codec, bool is_adc)
{
    int ret = ESP_CODEC_DEV_OK;
    if (is_adc) {
        ret |= es8311_update_reg_bit(codec, ES8311_SDPOUT_REG0A, 0x40, 0x40);
        ret |= es8311_write_reg(codec, ES8311_ADC_REG17, 0x00);
        ret |= es8311_write_reg(codec, ES8311_SYSTEM_REG0E, 0x5F);
        ret |= es8311_update_reg_bit(codec, ES8311_SYSTEM_REG0D, 0x30, 0x30);
        ret |= es8311_write_reg(codec, ES8311_ADC_REG15, 0x00);
        // ret |= es8311_update_reg_bit(codec, ES8311_RESET_REG00, 0x02, 0x02);
        // ret |= es8311_update_reg_bit(codec, ES8311_CLK_MANAGER_REG01, 0x0A, 0x00);
    } else {
        ret |= es8311_update_reg_bit(codec, ES8311_SDPIN_REG09, 0x40, 0x40);
        ret |= es8311_write_reg(codec, ES8311_SYSTEM_REG0E, 0x0F);
        ret |= es8311_write_reg(codec, ES8311_SYSTEM_REG12, 0x02);
        ret |= es8311_update_reg_bit(codec, ES8311_SYSTEM_REG0D, 0x08, 0x08);
        ret |= es8311_write_reg(codec, ES8311_DAC_REG37, 0x08);
        ret |= es8311_update_reg_bit(codec, ES8311_RESET_REG00, 0x01, 0x01);
        ret |= es8311_update_reg_bit(codec, ES8311_CLK_MANAGER_REG01, 0x05, 0x00);
    }
    ret |= es8311_write_reg(codec, ES8311_GP_REG45, 0x00);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8311_dac_mute(const audio_codec_if_t *h, int ch_mask, bool mute)
{
    audio_codec_es8311_t *codec = (audio_codec_es8311_t *)h;
    (void)ch_mask;
    if (codec == NULL || codec->is_open == false) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int regv;
    int ret = ESP_CODEC_DEV_OK;
    ret |= es8311_read_reg(codec, ES8311_DAC_REG31, &regv);
    if (ret != ESP_CODEC_DEV_OK) {
        return ESP_CODEC_DEV_READ_FAIL;
    }
    regv &= 0x9f;
    if (mute) {
        ret |= es8311_write_reg(codec, ES8311_DAC_REG31, regv | 0x60);
    } else {
        ret |= es8311_write_reg(codec, ES8311_DAC_REG31, regv);
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8311_dac_set_vol(const audio_codec_if_t *h, int ch_mask, float db_value)
{
    audio_codec_es8311_t *codec = (audio_codec_es8311_t *)h;
    (void)ch_mask;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    db_value -= codec->hw_gain;
    int reg = esp_codec_dev_vol_calc_reg(&vol_range, db_value);
    ESP_LOGD(TAG, "Set volume reg:%x db:%d", reg, (int)db_value);
    int ret = es8311_write_reg(codec, ES8311_DAC_REG32, (uint8_t)reg);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8311_adc_set_vol(const audio_codec_if_t *h, int ch_mask, float db)
{
    audio_codec_es8311_t *codec = (audio_codec_es8311_t *)h;
    (void)ch_mask;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    es8311_mic_gain_t gain_db = ES8311_MIC_GAIN_0DB;
    if (db < 6) {
    } else if (db < 12) {
        gain_db = ES8311_MIC_GAIN_6DB;
    } else if (db < 18) {
        gain_db = ES8311_MIC_GAIN_12DB;
    } else if (db < 24) {
        gain_db = ES8311_MIC_GAIN_18DB;
    } else if (db < 30) {
        gain_db = ES8311_MIC_GAIN_24DB;
    } else if (db < 36) {
        gain_db = ES8311_MIC_GAIN_30DB;
    } else if (db < 42) {
        gain_db = ES8311_MIC_GAIN_36DB;
    } else {
        gain_db = ES8311_MIC_GAIN_42DB;
    }
    int ret = es8311_write_reg(codec, ES8311_ADC_REG16, gain_db);  // MIC gain scale
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static void es8311_pa_power(audio_codec_es8311_t *codec, es_pa_setting_t pa_setting)
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
}

static int es8311_pa_enable(const audio_codec_if_t *h, bool enable)
{
    audio_codec_es8311_t *codec = (audio_codec_es8311_t *)h;
    if (codec == NULL || codec->is_open == false) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    es8311_pa_power(codec, enable ? ES_PA_ENABLE : ES_PA_DISABLE);
    return ESP_CODEC_DEV_OK;
}

static int es8311_config_sample(audio_codec_es8311_t *codec, int sample_rate, uint16_t mclk_multiple)
{
    int datmp, regv;
    int mclk_freq = sample_rate * mclk_multiple;
    int coeff = get_coeff(mclk_freq, sample_rate);
    if (coeff < 0) {
        ESP_LOGE(TAG, "Unable to configure sample rate %dHz with %dHz MCLK", sample_rate, mclk_freq);
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    ESP_LOGD(TAG, "config_sample: sample_rate: %dHz, mclk_multiple: %d, mclk: %dHz, coeff: %d",
             sample_rate, mclk_multiple, mclk_freq, coeff);
    bool use_mclk = !codec->cfg.sys_cfg.no_mclk;
    int ret = ESP_CODEC_DEV_OK;
    ret |= es8311_read_reg(codec, ES8311_CLK_MANAGER_REG02, &regv);
    regv &= 0x7;
    regv |= (coeff_div[coeff].pre_div - 1) << 5;
    datmp = 0;
    switch (coeff_div[coeff].pre_multi) {
        case 1:
            datmp = 0;
            break;
        case 2:
            datmp = 1;
            break;
        case 4:
            datmp = 2;
            break;
        case 8:
            datmp = 3;
            break;
        default:
            break;
    }
    if (use_mclk == false) {
        datmp = 3;
        if (sample_rate == 8000) {
            /* When the sample rate is 8kHz, BCLK requires at least 512K (slot bit needs to be configured to 32bit).
                DIG_MCLK = LRCK * 256 = BCLK * 4 */
            datmp = 2;
        }
    }
    regv |= (datmp) << 3;
    ret |= es8311_write_reg(codec, ES8311_CLK_MANAGER_REG02, regv);

    regv = 0x00;
    regv |= (coeff_div[coeff].adc_div - 1) << 4;
    regv |= (coeff_div[coeff].dac_div - 1) << 0;
    ret |= es8311_write_reg(codec, ES8311_CLK_MANAGER_REG05, regv);

    ret |= es8311_read_reg(codec, ES8311_CLK_MANAGER_REG03, &regv);
    regv &= 0x80;
    regv |= coeff_div[coeff].fs_mode << 6;
    regv |= coeff_div[coeff].adc_osr << 0;
    ret |= es8311_write_reg(codec, ES8311_CLK_MANAGER_REG03, regv);

    ret |= es8311_read_reg(codec, ES8311_CLK_MANAGER_REG04, &regv);
    regv &= 0x80;
    regv |= coeff_div[coeff].dac_osr << 0;
    ret |= es8311_write_reg(codec, ES8311_CLK_MANAGER_REG04, regv);

    ret |= es8311_read_reg(codec, ES8311_CLK_MANAGER_REG07, &regv);
    regv &= 0xC0;
    regv |= coeff_div[coeff].lrck_h << 0;
    ret |= es8311_write_reg(codec, ES8311_CLK_MANAGER_REG07, regv);

    regv = 0x00;
    regv |= coeff_div[coeff].lrck_l << 0;
    ret |= es8311_write_reg(codec, ES8311_CLK_MANAGER_REG08, regv);

    ret |= es8311_read_reg(codec, ES8311_CLK_MANAGER_REG06, &regv);
    regv &= 0xE0;
    if (coeff_div[coeff].bclk_div < 19) {
        regv |= (coeff_div[coeff].bclk_div - 1) << 0;
    } else {
        regv |= (coeff_div[coeff].bclk_div) << 0;
    }
    ret |= es8311_write_reg(codec, ES8311_CLK_MANAGER_REG06, regv);
    if (codec->cfg.sys_cfg.is_master) {
        // If codec is master, must use external mclk, and need set bclk_div >= 6
        ret |= es8311_write_reg(codec, ES8311_CLK_MANAGER_REG06, 0x07);
        ret |= es8311_write_reg(codec, ES8311_CLK_MANAGER_REG02, 0x00);
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static inline void es8311_apply_cfg(audio_codec_es8311_t *codec, const es8311_codec_cfg_t *codec_cfg)
{
    memcpy(&codec->cfg, codec_cfg, sizeof(es8311_codec_cfg_t));
    if (codec->cfg.sys_cfg.is_master) {
        codec->cfg.sys_cfg.no_mclk = false;
    }
}

static int es8311_open(const audio_hw_base_t *h, void *cfg, int cfg_size)
{
    audio_codec_es8311_t *codec = (audio_codec_es8311_t *)h;
    es8311_codec_cfg_t *codec_cfg = (es8311_codec_cfg_t *)cfg;
    if (codec == NULL || codec_cfg == NULL || codec_cfg->ctrl_if == NULL || cfg_size != sizeof(es8311_codec_cfg_t)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    bool is_master = codec->cfg.sys_cfg.is_master;
    bool use_mclk = !codec->cfg.sys_cfg.no_mclk;
    bool dac_ref_enabled = codec->cfg.dac_cfg.ref_enable;

    int regv = 0;
    int ret = ESP_CODEC_DEV_OK;

    ret = es8311_read_reg(codec, ES8311_SYSTEM_REG0D, &regv);
    if (regv != 0xFA) {
        ret |= es8311_write_reg(codec, ES8311_SYSTEM_REG0D, 0xFA);
    }

    /* Enhance ES8311 I2C noise immunity */
    ret |= es8311_write_reg(codec, ES8311_GPIO_REG44, 0x08);
    /* Due to occasional failures during the first I2C write with the ES8311 chip, a second write is performed to ensure reliability */
    ret |= es8311_write_reg(codec, ES8311_GPIO_REG44, 0x08);

    ret |= es8311_write_reg(codec, ES8311_CLK_MANAGER_REG01, 0x30);
    ret |= es8311_write_reg(codec, ES8311_CLK_MANAGER_REG02, 0x00);
    ret |= es8311_write_reg(codec, ES8311_CLK_MANAGER_REG03, 0x10);
    ret |= es8311_write_reg(codec, ES8311_ADC_REG16, 0x24);
    ret |= es8311_write_reg(codec, ES8311_CLK_MANAGER_REG04, 0x10);
    ret |= es8311_write_reg(codec, ES8311_CLK_MANAGER_REG05, 0x00);
    ret |= es8311_write_reg(codec, ES8311_SYSTEM_REG0B, 0x00);
    ret |= es8311_write_reg(codec, ES8311_SYSTEM_REG0C, 0x00);
    ret |= es8311_write_reg(codec, ES8311_SYSTEM_REG10, 0x1F);
    ret |= es8311_write_reg(codec, ES8311_SYSTEM_REG11, 0x7F);
    ret |= es8311_write_reg(codec, ES8311_RESET_REG00, 0x80);

    ret |= es8311_read_reg(codec, ES8311_RESET_REG00, &regv);
    if (is_master) {
        regv |= 0x40;
    } else {
        regv &= 0xBF;
    }
    ESP_LOGI(TAG, "Work in %s mode", is_master ? "Master" : "Slave");
    ret |= es8311_write_reg(codec, ES8311_RESET_REG00, regv);

    // Select clock source for internal mclk
    regv = 0x3F;
    if (use_mclk) {
        regv &= 0x7F;
    } else {
        regv |= 0x80;
    }
    ESP_LOGI(TAG, "Clock source: %s", use_mclk ? "MCLK PIN" : "BCLK PIN");
    ret |= es8311_write_reg(codec, ES8311_CLK_MANAGER_REG01, regv);

    ret |= es8311_write_reg(codec, ES8311_SYSTEM_REG13, 0x10);
    ret |= es8311_write_reg(codec, ES8311_ADC_REG1B, 0x0A);
    ret |= es8311_write_reg(codec, ES8311_ADC_REG1C, 0x6A);

    // Configure DAC reference (internal loopback)
    if (dac_ref_enabled) {
        /* Enable internal reference signal (ADCL + DACR) */
        ret |= es8311_write_reg(codec, ES8311_GPIO_REG44, 0x58);
    } else {
        ret |= es8311_write_reg(codec, ES8311_GPIO_REG44, 0x08);
    }
    ESP_LOGI(TAG, "Reference signal: %s", dac_ref_enabled ? "Enabled" : "Disabled");
    ret |= es8311_write_reg(codec, ES8311_SYSTEM_REG0D, 0x01);

    if (ret != 0) {
        return ESP_CODEC_DEV_WRITE_FAIL;
    }
    codec->adc_enabled = false;
    codec->dac_enabled = false;
    codec->is_open = true;
    return ESP_CODEC_DEV_OK;
}

static int es8311_close(const audio_hw_base_t *h)
{
    audio_codec_es8311_t *codec = (audio_codec_es8311_t *)h;
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
            es8311_dac_mute(&codec->base, 0x03, true);
            es8311_pa_power(codec, ES_PA_DISABLE);
            es8311_power_down(codec);
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

static bool es8311_is_open(const audio_hw_base_t *h)
{
    audio_codec_es8311_t *codec = (audio_codec_es8311_t *)h;
    if (codec == NULL) {
        return false;
    }
    return codec->is_open;
}

static int es8311_set_fs(const audio_hw_base_t *h, esp_codec_dev_sample_info_t *fs, esp_codec_dev_type_t type)
{
    (void)type;
    audio_codec_es8311_t *codec = (audio_codec_es8311_t *)h;
    if (codec == NULL || codec->is_open == false || fs == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    uint16_t mclk_multiple = fs->mclk_multiple ? fs->mclk_multiple : MCLK_DEFAULT_DIV;
    int ret = es8311_set_bits_per_sample(codec, fs->bits_per_sample);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    ret = es8311_config_fmt(codec, ES_I2S_NORMAL);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    if (codec->cfg.sys_cfg.no_mclk == true) {
        // MCLKIN selects BCLK, use real BCLK/LRCK as mclk_multiple
        // Cannot get real slot bit, use 64 as default
        // Real BCLK = sample_rate * total_slot_bit * slot_bit
        mclk_multiple = 64;
        if (fs->sample_rate < 16000) {
            // For less than 16000Hz, need mclk_multiple >= 128
            mclk_multiple = 128;
        }
    }
    return es8311_config_sample(codec, fs->sample_rate, mclk_multiple);
}

static int es8311_adc_enable(const audio_codec_if_t *h, bool enable)
{
    int ret = ESP_CODEC_DEV_OK;
    audio_codec_es8311_t *codec = (audio_codec_es8311_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    if (enable == codec->adc_enabled) {
        return ESP_CODEC_DEV_OK;
    }
    if (enable) {
        ret = es8311_start(codec, true);
    } else {
        ret = es8311_stop(codec, true);
    }
    if (ret == ESP_CODEC_DEV_OK) {
        codec->adc_enabled = enable;
        ESP_LOGD(TAG, "Codec ADC is %s", enable ? "enabled" : "disabled");
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8311_dac_enable(const audio_codec_if_t *h, bool enable)
{
    int ret = ESP_CODEC_DEV_OK;
    audio_codec_es8311_t *codec = (audio_codec_es8311_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    if (enable == codec->dac_enabled) {
        return ESP_CODEC_DEV_OK;
    }
    if (enable) {
        ret |= es8311_start(codec, false);
        es8311_pa_power(codec, ES_PA_ENABLE);
        ret |= es8311_dac_mute(&codec->base, 0x03, false);
    } else {
        ret |= es8311_dac_mute(&codec->base, 0x03, true);
        es8311_pa_power(codec, ES_PA_DISABLE);
        ret |= es8311_stop(codec, false);
    }
    if (ret == ESP_CODEC_DEV_OK) {
        codec->dac_enabled = enable;
        ESP_LOGD(TAG, "Codec DAC is %s", enable ? "enabled" : "disabled");
    }
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8311_set_reg(const audio_hw_base_t *h, int reg, int value)
{
    audio_codec_es8311_t *codec = (audio_codec_es8311_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = es8311_write_reg(codec, reg, value);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int es8311_get_reg(const audio_hw_base_t *h, int reg, int *value)
{
    audio_codec_es8311_t *codec = (audio_codec_es8311_t *)h;
    if (codec == NULL || value == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = es8311_read_reg(codec, reg, value);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_READ_FAIL;
}

static void es8311_dump(const audio_hw_base_t *h)
{
    audio_codec_es8311_t *codec = (audio_codec_es8311_t *)h;
    if (codec == NULL || codec->is_open == false) {
        return;
    }
    codec_reg_dump_ctx_t dump;
    codec_reg_dump_init(&dump, TAG, 2);
    for (int i = 0; i < ES8311_MAX_REGISTER; i++) {
        int value = 0;
        int ret = es8311_read_reg(codec, i, &value);
        if (ret != ESP_CODEC_DEV_OK) {
            break;
        }
        codec_reg_dump_push(&dump, i, value);
    }
    codec_reg_dump_end(&dump);
}

static int es8311_get_adc_label(const audio_hw_base_t *h, const char **label)
{
    audio_codec_es8311_t *codec = (audio_codec_es8311_t *)h;
    if (codec == NULL || label == NULL || codec->adc_label[0] == '\0') {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *label = codec->adc_label;
    return ESP_CODEC_DEV_OK;
}

static int es8311_get_order_list(const audio_hw_base_t *h, const esp_codec_dev_device_map_info_t **order_list, int *list_size)
{
    audio_codec_es8311_t *codec = (audio_codec_es8311_t *)h;
    if (codec == NULL || order_list == NULL || list_size == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *list_size = sizeof(order_info) / sizeof(order_info[0]);
    *order_list = order_info;
    return ESP_CODEC_DEV_OK;
}

static int es8311_copy_caps(const esp_codec_dev_capability_t *src_caps, int src_count,
                            esp_codec_dev_capability_t *out_caps, int *out_count)
{
    if (out_caps == NULL || *out_count == 0) {
        *out_count = src_count;
        return ESP_CODEC_DEV_OK;
    }
    if (*out_count < src_count) {
        *out_count = src_count;
        return ESP_CODEC_DEV_NO_MEM;
    }
    for (int i = 0; i < src_count; i++) {
        out_caps[i] = src_caps[i];
    }
    *out_count = src_count;
    return ESP_CODEC_DEV_OK;
}

static int es8311_get_caps(const audio_hw_base_t *h, esp_codec_dev_type_t dev_type,
                           esp_codec_dev_capability_t *caps, int *count)
{
    if (h == NULL || count == NULL || *count < 0 ||
        dev_type == ESP_CODEC_DEV_TYPE_NONE ||
        (dev_type & ~(ESP_CODEC_DEV_TYPE_IN_OUT)) != 0) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (dev_type == ESP_CODEC_DEV_TYPE_IN) {
        return es8311_copy_caps(&adc_caps, 1, caps, count);
    }
    if (dev_type == ESP_CODEC_DEV_TYPE_OUT) {
        return es8311_copy_caps(&dac_caps, 1, caps, count);
    }
    const esp_codec_dev_capability_t in_out_caps[] = { adc_caps, dac_caps };
    return es8311_copy_caps(in_out_caps, 2, caps, count);
}

static void es8311_save_adc_label(audio_codec_es8311_t *codec, const char *label)
{
    codec->adc_label[0] = '\0';
    if (label != NULL) {
        strncpy(codec->adc_label, label, sizeof(codec->adc_label) - 1);
        codec->adc_label[sizeof(codec->adc_label) - 1] = '\0';
    }
}

const audio_codec_if_t *es8311_codec_new(es8311_codec_cfg_t *codec_cfg)
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

    audio_codec_es8311_t *codec = (audio_codec_es8311_t *)calloc(1, sizeof(audio_codec_es8311_t));
    if (codec == NULL) {
        ESP_LOGE(TAG, "Fail to alloc memory at %s:%d", __FUNCTION__, __LINE__);
        return NULL;
    }

    // Initialize base hardware interface
    codec->base.hw_base.open = es8311_open;
    codec->base.hw_base.is_open = es8311_is_open;
    codec->base.hw_base.set_fs = es8311_set_fs;
    codec->base.hw_base.set_reg = es8311_set_reg;
    codec->base.hw_base.get_reg = es8311_get_reg;
    codec->base.hw_base.dump_reg = es8311_dump;
    codec->base.hw_base.close = es8311_close;
    codec->base.hw_base.get_order_list = es8311_get_order_list;
    codec->base.hw_base.get_adc_label = es8311_get_adc_label;
    codec->base.hw_base.get_caps = es8311_get_caps;
    codec->base.ctrl_if = codec_cfg->ctrl_if;
    es8311_save_adc_label(codec, codec_cfg->adc_cfg.label);
    codec->base.hw_proc = &hw_proc;

    // Initialize ADC operations
    codec->adc_ops.ops.enable = es8311_adc_enable;
    codec->adc_ops.ops.mute = NULL;  // ES8311 ADC mute not supported directly
    codec->adc_ops.ops.set_vol = es8311_adc_set_vol;
    codec->base.adc_if = &codec->adc_ops;

    // Initialize DAC operations
    codec->dac_ops.ops.enable = es8311_dac_enable;
    codec->dac_ops.ops.mute = es8311_dac_mute;
    codec->dac_ops.ops.set_vol = es8311_dac_set_vol;
    codec->dac_ops.pa.enable = es8311_pa_enable;
    codec->base.dac_if = &codec->dac_ops;

    codec->hw_gain = esp_codec_dev_vol_calc_hw_gain(&codec_cfg->pa_cfg.hw_gain);
    do {
        int open_cnt = codec_ref_acquire(&ctrl_info);
        if (open_cnt < 0) {
            ESP_LOGE(TAG, "Failed to acquire codec device open reference");
            break;
        }
        es8311_apply_cfg(codec, codec_cfg);
        es8311_pa_power(codec, ES_PA_SETUP | ES_PA_DISABLE);
        if (open_cnt == 1) {
            int ret = codec->base.hw_base.open(&codec->base.hw_base, &codec->cfg, sizeof(es8311_codec_cfg_t));
            if (ret != 0) {
                ESP_LOGE(TAG, "Open fail, ret: %d", ret);
                codec_ref_release(&ctrl_info);
                break;
            }
        } else {
            codec->is_open = true;
            ESP_LOGI(TAG, "Codec already opened, reusing (open_count=%d)", open_cnt);
        }
        return &codec->base;
    } while (0);
    if (codec) {
        free(codec);
    }
    return NULL;
}
