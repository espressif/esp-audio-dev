/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>

#include "esp_log.h"
#include "esp_check.h"

#include "es8311_proc_priv.h"
#include "es8311_reg.h"

static const char *TAG = "ES8311_EQ";

#define ES8311_RETURN_ON_ERROR(ret, operation)  do {        \
    int err = (ret);                                        \
    if (err != ESP_CODEC_DEV_OK) {                          \
        ESP_LOGE(TAG, operation " failed: ret 0x%x", err);  \
        return err;                                         \
    }                                                       \
} while (0)

static const int adc_eq_band_base[] = {
    ES8311_ADC_EQ_B0_BASE,
    ES8311_ADC_EQ_B1_BASE,
    ES8311_ADC_EQ_B2_BASE,
    ES8311_ADC_EQ_A1_BASE,
    ES8311_ADC_EQ_A2_BASE,
};

static const int dac_eq_band_base[] = {
    ES8311_DAC_EQ_B0_BASE,
    ES8311_DAC_EQ_B1_BASE,
    -1,
    ES8311_DAC_EQ_A1_BASE,
    -1,
};

static int es8311_update_reg_bit(const audio_hw_base_t *hw_base, uint8_t reg_addr, uint8_t update_bits, uint8_t data)
{
    int regv = 0;
    ES8311_RETURN_ON_ERROR(audio_hw_get_reg(hw_base, reg_addr, &regv), "Get EQ register");
    regv = (regv & (~update_bits)) | (update_bits & data);
    ES8311_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, reg_addr, regv), "Set EQ register");
    return ESP_CODEC_DEV_OK;
}

static int es8311_eq_enable(const audio_hw_eq_t *h, bool enable)
{
    const audio_hw_base_t *hw_base = h->base;
    ESP_RETURN_ON_FALSE(hw_base, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");

    // TODO: Fix noise when enable is true
    enable = false;
    ES8311_RETURN_ON_ERROR(es8311_update_reg_bit(hw_base, ES8311_DAC_REG37, 0x08, enable ? 0x00 : 0x08),
                           "Set DAC EQ enable");
    ES8311_RETURN_ON_ERROR(es8311_update_reg_bit(hw_base, ES8311_ADC_REG1C, 0x40, enable ? 0x00 : 0x40),
                           "Set ADC EQ enable");
    ESP_LOGD(TAG, "Auto eq %s", enable ? "enable" : "disable");
    return ESP_CODEC_DEV_OK;
}

static int es8311_set_eq_para(const audio_hw_base_t *hw_base, int base_reg, int32_t coef)
{
    for (int i = 0; i < 32; i += 8) {
        ES8311_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, base_reg--, (int)((coef >> i) & 0xFF)),
                               "Set EQ coefficient");
    }
    return ESP_CODEC_DEV_OK;
}

static int es8311_set_cfg(const audio_hw_eq_t *h, const audio_eq_cfg_t *eq_cfg)
{
    const audio_hw_base_t *hw_base = h->base;
    ESP_RETURN_ON_FALSE(hw_base, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(eq_cfg, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(eq_cfg->para, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid EQ parameters");
    ESP_RETURN_ON_FALSE(eq_cfg->filter_num >= 5, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid filter number");

    for (int i = 0; i < 5; i++) {
        if (adc_eq_band_base[i] == -1) {
            continue;
        }
        ES8311_RETURN_ON_ERROR(es8311_set_eq_para(hw_base, adc_eq_band_base[i], eq_cfg->para[i].gain),
                               "Set ADC EQ band");
    }
    for (int i = 0; i < 5; i++) {
        if (dac_eq_band_base[i] == -1) {
            continue;
        }
        ES8311_RETURN_ON_ERROR(es8311_set_eq_para(hw_base, dac_eq_band_base[i], eq_cfg->para[i].gain),
                               "Set DAC EQ band");
    }

    return ESP_CODEC_DEV_OK;
}

static int es8311_dump_eq_info(const audio_hw_eq_t *h)
{
    const audio_hw_base_t *hw_base = h->base;
    ESP_RETURN_ON_FALSE(hw_base, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    int res = ESP_CODEC_DEV_OK;
    ESP_LOGI(TAG, "Support 5-band(adc) and 3-band(dac) eq, and 30bit coefficient");
    return res;
}

static int es8311_set_band_para(const audio_hw_eq_t *h, const eq_para_t *eq_para, int index)
{
    const audio_hw_base_t *hw_base = h->base;
    ESP_RETURN_ON_FALSE(hw_base && eq_para, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");

    ES8311_RETURN_ON_ERROR(es8311_set_eq_para(hw_base, ES8311_ADC_EQ_B0_BASE, eq_para->gain), "Set EQ band");
    return ESP_CODEC_DEV_OK;
}

int audio_hw_es8311_eq_new(const audio_hw_base_t *h, audio_hw_eq_handle_t *eq)
{
    ESP_RETURN_ON_FALSE(eq, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(h, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    if (*eq != NULL) {
        return ESP_CODEC_DEV_OK;
    }
    ESP_RETURN_ON_FALSE(audio_hw_is_open(h), ESP_CODEC_DEV_WRONG_STATE, TAG, "Codec is not opened");

    audio_hw_eq_t *eq_handle = calloc(1, sizeof(audio_hw_eq_t));
    ESP_RETURN_ON_FALSE(eq_handle, ESP_CODEC_DEV_NO_MEM, TAG, "No memory");

    eq_handle->base = h;
    eq_handle->enable = es8311_eq_enable;
    eq_handle->dump_info = es8311_dump_eq_info;
    eq_handle->set_cfg = es8311_set_cfg;
    eq_handle->set_band_para = es8311_set_band_para;
    *eq = eq_handle;
    return ESP_CODEC_DEV_OK;
}
