/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "zl38063_codec.h"
#include "tw_spi_access.h"
#include "vproc_common.h"

#define HBI_PAGED_READ(offset, length)    ((uint16_t)(((uint16_t)(offset) << 8) | (length)))
#define HBI_PAGED_WRITE(offset, length)   ((uint16_t)(HBI_PAGED_READ(offset, length) | 0x0080))
#define HBI_SELECT_PAGE(page)             ((uint16_t)(0xFE00 | (page)))
#define HBI_DIRECT_READ(offset, length)   ((uint16_t)(0x8000 | ((uint16_t)(offset) << 8) | (length)))
#define HBI_DIRECT_WRITE(offset, length)  ((uint16_t)(HBI_DIRECT_READ(offset, length) | 0x0080))

/**
 * @brief  ZL38063 codec driver instance
 */
typedef struct {
    audio_codec_if_t     base;     /*!< Codec interface vtable container */
    audio_hw_dac_if_t    dac_ops;  /*!< DAC operation callbacks */
    zl38063_codec_cfg_t  cfg;      /*!< Board configuration snapshot */
    bool                 is_open;  /*!< True after open completes */
    bool                 enabled;  /*!< True when DAC path is running */
    float                hw_gain;  /*!< PA hardware gain offset in dB */
} audio_codec_zl38063_t;

static const char *TAG = "ZL38063";

static inline uint16_t convert_endian(uint16_t v)
{
    return (v >> 8) | ((v & 0xFF) << 8);
}

static void get_write_cmd(uint16_t addr, int size, uint16_t *dst, int *n)
{
    uint8_t page;
    uint8_t offset;
    page = addr >> 8;
    offset = (addr & 0xFF) / 2;
    if (page == 0) {
        dst[(*n)++] = convert_endian(HBI_DIRECT_WRITE(offset, size - 1));
    }
    if (page) {
        /* Indirect page access */
        if (page != 0xFF) {
            page -= 1;
        }
        dst[(*n)++] = convert_endian(HBI_SELECT_PAGE(page));
        dst[(*n)++] = convert_endian(HBI_PAGED_WRITE(offset, size - 1));
    }
}

static void get_read_cmd(uint16_t addr, int size, uint16_t *dst, int *n)
{
    uint8_t page;
    uint8_t offset;
    page = addr >> 8;
    offset = (addr & 0xFF) / 2;
    if (page == 0) {
        dst[(*n)++] = convert_endian(HBI_DIRECT_READ(offset, size - 1));
    }
    if (page) {
        /* Indirect page access */
        if (page != 0xFF) {
            page -= 1;
        }
        dst[(*n)++] = convert_endian(HBI_SELECT_PAGE(page));
        dst[(*n)++] = convert_endian(HBI_PAGED_READ(offset, size - 1));
    }
}

static int read_addr(audio_codec_zl38063_t *codec, uint16_t addr, int words, uint16_t *data)
{
    int total_addr = 0;
    int n = 0;
    get_read_cmd(addr, words, (uint16_t *)&total_addr, &n);
    if (codec->cfg.ctrl_if->read_reg) {
        int ret =
            codec->cfg.ctrl_if->read_reg(codec->cfg.ctrl_if, total_addr, n * sizeof(uint16_t), data, words * sizeof(uint16_t));
        for (int i = 0; i < words; i++) {
            data[i] = convert_endian(data[i]);
        }
        return ret;
    }
    return ESP_CODEC_DEV_NOT_SUPPORT;
}

static int write_addr(audio_codec_zl38063_t *codec, uint16_t addr, int words, uint16_t *data)
{
    int total_addr = 0;
    int n = 0;
    get_write_cmd(addr, words, (uint16_t *)&total_addr, &n);
    if (codec->cfg.ctrl_if->write_reg) {
        for (int i = 0; i < words; i++) {
            data[i] = convert_endian(data[i]);
        }
        return codec->cfg.ctrl_if->write_reg(codec->cfg.ctrl_if, total_addr, n * sizeof(uint16_t), data,
                                             words * sizeof(uint16_t));
    }
    return ESP_CODEC_DEV_NOT_SUPPORT;
}

static int get_status(audio_codec_zl38063_t *codec, uint16_t *status)
{
    return read_addr(codec, 0x030, 1, status);
}

static int zl38063_pa_power(audio_codec_zl38063_t *codec, bool on)
{
    int16_t pa_pin = codec->cfg.pa_cfg.pa_pin;
    if (pa_pin != -1 && codec->cfg.gpio_if != NULL) {
        codec->cfg.gpio_if->setup(pa_pin, AUDIO_GPIO_DIR_OUT, AUDIO_GPIO_MODE_FLOAT);
        codec->cfg.gpio_if->set(pa_pin, codec->cfg.pa_cfg.pa_active_low ? !on : on);
    }
    return ESP_CODEC_DEV_OK;
}

static int zl38063_reset(audio_codec_zl38063_t *codec, bool on)
{
    int16_t reset_pin = codec->cfg.reset_cfg.reset_pin;
    bool reset_active_low = codec->cfg.reset_cfg.reset_active_low;
    if (reset_pin != -1 && codec->cfg.gpio_if != NULL) {
        codec->cfg.gpio_if->setup(reset_pin, AUDIO_GPIO_DIR_OUT, AUDIO_GPIO_MODE_FLOAT);
        codec->cfg.gpio_if->set(reset_pin, on ? (reset_active_low ? 0 : 1) : (reset_active_low ? 1 : 0));
    }
    return ESP_CODEC_DEV_OK;
}

static int _set_vol(audio_codec_zl38063_t *codec, uint8_t vol)
{
    uint16_t reg = vol + (vol << 8);
    int ret = write_addr(codec, 0x238, 1, &reg);
    ret |= write_addr(codec, 0x23A, 1, &reg);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int zl38063_get_vol(audio_codec_zl38063_t *codec, float *vol) __attribute__((unused));

static int zl38063_get_vol(audio_codec_zl38063_t *codec, float *vol)
{
    uint16_t reg = 0;
    int ret = read_addr(codec, 0x238, 1, &reg);
    *vol = (int8_t)(reg >> 8);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int zl38063_open(const audio_hw_base_t *h, void *cfg, int cfg_size)
{
    audio_codec_zl38063_t *codec = (audio_codec_zl38063_t *)h;
    zl38063_codec_cfg_t *codec_cfg = (zl38063_codec_cfg_t *)cfg;
    if (codec == NULL || codec_cfg == NULL || codec_cfg->ctrl_if == NULL || cfg_size != sizeof(zl38063_codec_cfg_t)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    memcpy(&codec->cfg, codec_cfg, sizeof(zl38063_codec_cfg_t));
    uint16_t status = 0;
    VprocSetCtrlIf((void *)codec->cfg.ctrl_if);
    zl38063_reset(codec, true);
    int ret = get_status(codec, &status);
    if (ret != 0) {
        ESP_LOGE(TAG, "Read status failed: ret=%d", ret);
        return ESP_CODEC_DEV_READ_FAIL;
    }
    if (status == 0) {
        ESP_LOGI(TAG, "Start upload firmware");
        ret = tw_upload_dsp_firmware(0);
        if (ret != 0) {
            ESP_LOGE(TAG, "Upload firmware failed: ret=%d", ret);
            return ESP_CODEC_DEV_WRITE_FAIL;
        }
    }
    codec->enabled = false;
    codec->is_open = true;
    return ESP_CODEC_DEV_OK;
}

static int zl38063_enable(const audio_codec_if_t *h, bool enable)
{
    audio_codec_zl38063_t *codec = (audio_codec_zl38063_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    if (codec->enabled == enable) {
        return ESP_CODEC_DEV_OK;
    }
    zl38063_pa_power(codec, enable);
    codec->enabled = enable;
    ESP_LOGD(TAG, "Codec is %s", enable ? "enabled" : "disabled");
    return ESP_CODEC_DEV_OK;
}

static int zl38063_pa_enable(const audio_codec_if_t *h, bool enable)
{
    audio_codec_zl38063_t *codec = (audio_codec_zl38063_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    return zl38063_pa_power(codec, enable);
}

static int zl38063_set_vol(const audio_codec_if_t *h, int ch_mask, float db_vol)
{
    audio_codec_zl38063_t *codec = (audio_codec_zl38063_t *)h;
    (void)ch_mask;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    db_vol -= codec->hw_gain;
    if (db_vol < -90.0) {
        db_vol = -90.0;
    } else if (db_vol > 6.0) {
        db_vol = 6.0;
    }
    int8_t reg = (int8_t)db_vol;
    int ret = _set_vol(codec, reg);
    ESP_LOGD(TAG, "Set vol reg:%d", reg);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int zl38063_close(const audio_hw_base_t *h)
{
    audio_codec_zl38063_t *codec = (audio_codec_zl38063_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open) {
        zl38063_pa_power(codec, false);
        codec->enabled = false;
        codec->is_open = false;
    }
    zl38063_reset(codec, false);
    VprocSetCtrlIf(NULL);
    return ESP_CODEC_DEV_OK;
}

static bool zl38063_is_open(const audio_hw_base_t *h)
{
    audio_codec_zl38063_t *codec = (audio_codec_zl38063_t *)h;
    if (codec == NULL) {
        return false;
    }
    return codec->is_open;
}

static int zl38063_set_reg(const audio_hw_base_t *h, int reg, int value)
{
    audio_codec_zl38063_t *codec = (audio_codec_zl38063_t *)h;
    if (codec == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = write_addr(codec, (uint16_t)reg, 1, (uint16_t *)&value);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int zl38063_get_reg(const audio_hw_base_t *h, int reg, int *value)
{
    audio_codec_zl38063_t *codec = (audio_codec_zl38063_t *)h;
    if (codec == NULL || value == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    *value = 0;
    int ret = read_addr(codec, reg, 1, (uint16_t *)value);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_READ_FAIL;
}

static int zl38063_set_fs(const audio_hw_base_t *h, esp_codec_dev_sample_info_t *fs, esp_codec_dev_type_t type)
{
    (void)type;
    audio_codec_zl38063_t *codec = (audio_codec_zl38063_t *)h;
    if (codec == NULL || fs == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (codec->is_open == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    if (fs->channel != 2 || fs->sample_rate != 48000 || fs->bits_per_sample != 16) {
        ESP_LOGE(TAG, "Firmware only support 48k 2channel 16 bits");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    return ESP_CODEC_DEV_OK;
}

const audio_codec_if_t *zl38063_codec_new(zl38063_codec_cfg_t *codec_cfg)
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

    audio_codec_zl38063_t *codec = (audio_codec_zl38063_t *)calloc(1, sizeof(audio_codec_zl38063_t));
    if (codec == NULL) {
        ESP_LOGE(TAG, "Fail to alloc memory at %s:%d", __FUNCTION__, __LINE__);
        return NULL;
    }

    codec->base.hw_base.open = zl38063_open;
    codec->base.hw_base.is_open = zl38063_is_open;
    codec->base.hw_base.set_fs = zl38063_set_fs;
    codec->base.hw_base.set_reg = zl38063_set_reg;
    codec->base.hw_base.get_reg = zl38063_get_reg;
    codec->base.hw_base.close = zl38063_close;
    codec->base.ctrl_if = codec_cfg->ctrl_if;

    codec->dac_ops.ops.enable = zl38063_enable;
    codec->dac_ops.ops.set_vol = zl38063_set_vol;
    codec->dac_ops.pa.enable = zl38063_pa_enable;
    codec->base.dac_if = &codec->dac_ops;
    codec->base.adc_if = NULL;

    codec->hw_gain = esp_codec_dev_vol_calc_hw_gain(&codec_cfg->pa_cfg.hw_gain);
    do {
        int ret = codec->base.hw_base.open(&codec->base.hw_base, codec_cfg, sizeof(zl38063_codec_cfg_t));
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
