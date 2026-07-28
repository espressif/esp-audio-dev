/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include <math.h>

#include "esp_check.h"
#include "esp_log.h"

#include "es8311_reg.h"
#include "es8311_proc_priv.h"

static const char *TAG = "ES8311_ALC";

#define ES8311_RETURN_ON_ERROR(ret, operation)  do {        \
    int err = (ret);                                        \
    if (err != ESP_CODEC_DEV_OK) {                          \
        ESP_LOGE(TAG, operation " failed: ret 0x%x", err);  \
        return err;                                         \
    }                                                       \
} while (0)

static inline void _limit_to_min_max(int *data, int min, int max)
{
    int temp = *data;
    temp = temp < min ? min : temp;
    temp = temp > max ? max : temp;
    *data = temp;
}

static int es8311_set_max_min_gain(const audio_hw_base_t *hw_base, float max_gain, float min_gain)
{
    int reg = 0;
    int max_gain_reg = (int)(exp((max_gain + 30.116) / 8.6936)) - 1;
    int min_gain_reg = (int)(exp((min_gain + 30.116) / 8.6936)) - 1;
    _limit_to_min_max(&max_gain_reg, 0x00, 0x0F);
    _limit_to_min_max(&min_gain_reg, 0x00, 0x0F);
    reg = (max_gain_reg << 4) | min_gain_reg;
    ES8311_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8311_ADC_REG19, reg), "Set ALC max min gain");
    ESP_LOGD(TAG, "Reg(0x%x): 0x%x", ES8311_ADC_REG19, reg);
    return ESP_CODEC_DEV_OK;
}

static int es8311_set_noise_gate(const audio_hw_alc_t *h, float threshold)
{
    const audio_hw_base_t *hw_base = h->base;
    ESP_RETURN_ON_FALSE(hw_base, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");

    int reg = 0;
    ES8311_RETURN_ON_ERROR(audio_hw_get_reg(hw_base, ES8311_ADC_REG1A, &reg), "Get noise gate");
    reg &= 0xF0;
    int noise_gate_reg = 0;
    if (threshold >= -54) {
        noise_gate_reg = (threshold + 54) / 3 + 7;
    } else {
        noise_gate_reg = (threshold + 96) / 6;
    }
    _limit_to_min_max(&noise_gate_reg, 0x00, 0x0F);
    reg |= noise_gate_reg;
    ES8311_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8311_ADC_REG1A, reg), "Set noise gate");
    ESP_LOGD(TAG, "Reg(0x%x): 0x%x", ES8311_ADC_REG1A, reg);
    return ESP_CODEC_DEV_OK;
}

static int es8311_alc_init(const audio_hw_alc_t *h, const audio_alc_cfg_t *alc_cfg)
{
    const audio_hw_base_t *hw_base = h->base;
    ESP_RETURN_ON_FALSE(hw_base, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(alc_cfg, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");

    int reg = 0;
    ES8311_RETURN_ON_ERROR(es8311_set_max_min_gain(hw_base, alc_cfg->max_gain, alc_cfg->min_gain),
                           "Set ALC max min gain");

    ES8311_RETURN_ON_ERROR(es8311_set_noise_gate(h, alc_cfg->noise_gate_threshold), "Set ALC noise gate");
    ES8311_RETURN_ON_ERROR(audio_hw_get_reg(hw_base, ES8311_ADC_REG1A, &reg), "Get ALC noise gate");
    reg = (0x03 << 4) | (reg & 0x0F);
    ES8311_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8311_ADC_REG1A, reg), "Set ALC noise gate mode");
    ESP_LOGD(TAG, "Reg(0x%x): 0x%x", ES8311_ADC_REG1A, reg);

    int tmp = audio_hw_get_reg(hw_base, ES8311_ADC_REG18, &reg);
    ES8311_RETURN_ON_ERROR(tmp, "Get ALC control");
    reg &= 0xBF;
    if (alc_cfg->noise_gate_mode != ALC_NOISE_GATE_DISABLE) {
        reg |= 0x40;
        ES8311_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8311_ADC_REG18, reg), "Set ALC control");
        ESP_LOGD(TAG, "Reg(0x%x): 0x%x", ES8311_ADC_REG18, reg);

        tmp = audio_hw_get_reg(hw_base, ES8311_ADC_REG1B, &reg);
        ES8311_RETURN_ON_ERROR(tmp, "Get ALC fade mode");
        reg &= 0x1F;
        if (alc_cfg->noise_gate_mode == ALC_NOISE_GATE_MUTE_ADC) {
            reg = (0x07 << 4) | reg;
        }
        ES8311_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8311_ADC_REG1B, reg), "Set ALC fade mode");
        ESP_LOGD(TAG, "Reg(0x%x): 0x%x", ES8311_ADC_REG1B, reg);
    } else {
        ES8311_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8311_ADC_REG18, reg), "Disable ALC noise gate");
        ESP_LOGD(TAG, "Reg(0x%x): 0x%x", ES8311_ADC_REG18, reg);
    }

    return ESP_CODEC_DEV_OK;
}

static int es8311_set_alc_channel(const audio_hw_alc_t *h, int channel_mask)
{
    const audio_hw_base_t *hw_base = h->base;
    ESP_RETURN_ON_FALSE(hw_base, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");

    int reg = 0;
    ES8311_RETURN_ON_ERROR(audio_hw_get_reg(hw_base, ES8311_ADC_REG18, &reg), "Get ALC channel");
    reg &= 0x7F;
    if (channel_mask != 0) {
        reg |= 0x80;
    }
    ES8311_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8311_ADC_REG18, reg), "Set ALC channel");
    ESP_LOGD(TAG, "Reg(0x%x): 0x%x, alc is %s", ES8311_ADC_REG18, reg, channel_mask ? "enabled" : "disabled");
    return ESP_CODEC_DEV_OK;
}

static int es8311_set_alc_target_gain(const audio_hw_alc_t *h, float target_gain)
{
    const audio_hw_base_t *hw_base = h->base;
    ESP_RETURN_ON_FALSE(hw_base, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");

    int reg = 0;
    ES8311_RETURN_ON_ERROR(audio_hw_get_reg(hw_base, ES8311_ADC_REG19, &reg), "Get ALC target gain");
    reg &= 0x0F;
    int target_gain_reg = (int)((target_gain + 16.5) / 1.5);
    _limit_to_min_max(&target_gain_reg, 0x00, 0x0F);
    reg |= (target_gain_reg << 4);
    ES8311_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8311_ADC_REG19, reg), "Set ALC target gain");
    ESP_LOGD(TAG, "Reg(0x%x): 0x%x", ES8311_ADC_REG19, reg);
    return ESP_CODEC_DEV_OK;
}

int audio_hw_es8311_alc_new(const audio_hw_base_t *h, audio_hw_alc_handle_t *alc)
{
    ESP_RETURN_ON_FALSE(alc, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(h, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    if (*alc != NULL) {
        return ESP_CODEC_DEV_OK;
    }
    ESP_RETURN_ON_FALSE(audio_hw_is_open(h), ESP_CODEC_DEV_WRONG_STATE, TAG, "Codec is not opened");

    audio_hw_alc_t *alc_handle = calloc(1, sizeof(audio_hw_alc_t));
    ESP_RETURN_ON_FALSE(alc_handle, ESP_CODEC_DEV_NO_MEM, TAG, "No memory");

    alc_handle->base = h;
    alc_handle->set_gain = es8311_set_alc_target_gain;
    alc_handle->set_channel = es8311_set_alc_channel;
    alc_handle->init = es8311_alc_init;
    alc_handle->set_noise_gate = es8311_set_noise_gate;
    *alc = alc_handle;
    return ESP_CODEC_DEV_OK;
}
