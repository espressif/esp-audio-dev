/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>

#include "esp_check.h"
#include "esp_log.h"

#include "es8388_reg.h"
#include "es8388_proc_priv.h"

static const char *TAG = "ES8388_ALC";

#define ES8388_RETURN_ON_ERROR(ret, operation)  do {        \
    int err = (ret);                                        \
    if (err != ESP_CODEC_DEV_OK) {                          \
        ESP_LOGE(TAG, operation " failed: ret 0x%x", err);  \
        return err;                                         \
    }                                                       \
} while (0)

static inline int get_power_of_two(int num)
{
    int data = 1;
    for (int i = 0; i < 0x0A; i++) {
        data = (data << 1);
        if (data > num) {
            return i;
        }
    }
    return 0x0A;
}

static inline void _limit_to_min_max(int *data, int min, int max)
{
    int temp = *data;
    temp = temp < min ? min : temp;
    temp = temp > max ? max : temp;
    *data = temp;
}

static int es8388_alc_init(const audio_hw_alc_t *h, const audio_alc_cfg_t *alc_cfg)
{
    const audio_hw_base_t *hw_base = h->base;
    if (hw_base == NULL || alc_cfg == NULL) {
        ESP_LOGE(TAG, "Initialize ALC failed: invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int reg = 0;
    int max_gain_reg = (int)((alc_cfg->max_gain + 6.5) / 6);
    int min_gain_reg = (int)((alc_cfg->min_gain + 12) / 6);
    _limit_to_min_max(&max_gain_reg, 0x00, 0x07);
    _limit_to_min_max(&min_gain_reg, 0x00, 0x07);
    reg = (max_gain_reg << 3) | min_gain_reg;
    ES8388_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8388_ADCCONTROL10, reg), "Set ALC max min gain");
    ESP_LOGD(TAG, "Reg(0x%x): 0x%x", ES8388_ADCCONTROL10, reg);

    int target_gain_reg = (int)((alc_cfg->target_gain + 16.5) / 1.5);
    int hold_time_reg = (int)(alc_cfg->hold_time_ms / 2.67);
    hold_time_reg = get_power_of_two(hold_time_reg);
    hold_time_reg += 1;
    _limit_to_min_max(&target_gain_reg, 0x00, 0x0F);
    _limit_to_min_max(&hold_time_reg, 0x00, 0x0F);
    reg = (target_gain_reg << 4) | hold_time_reg;
    ES8388_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8388_ADCCONTROL11, reg), "Set ALC target gain");
    ESP_LOGD(TAG, "Reg(0x%x): 0x%x", ES8388_ADCCONTROL11, reg);

    int decay_time_reg = (int)(alc_cfg->decay_time_us / 410);
    int attack_time_reg = (int)(alc_cfg->attack_time_us / 104);
    decay_time_reg = get_power_of_two(decay_time_reg);
    attack_time_reg = get_power_of_two(attack_time_reg);
    _limit_to_min_max(&decay_time_reg, 0x00, 0x0F);
    _limit_to_min_max(&attack_time_reg, 0x00, 0x0F);
    reg = (decay_time_reg << 4) | attack_time_reg;
    ES8388_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8388_ADCCONTROL12, reg), "Set ALC timing");
    ESP_LOGD(TAG, "Reg(0x%x): 0x%x", ES8388_ADCCONTROL12, reg);

    int noise_gate_reg = (int)((alc_cfg->noise_gate_threshold + 76.5) / 1.5);
    _limit_to_min_max(&noise_gate_reg, 0x00, 0x1F);
    reg = (noise_gate_reg << 3);
    if (alc_cfg->noise_gate_mode != ALC_NOISE_GATE_DISABLE) {
        reg |= 0x01;
    }
    if (alc_cfg->noise_gate_mode == ALC_NOISE_GATE_MUTE_ADC) {
        reg |= 0x02;
    }
    ES8388_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8388_ADCCONTROL14, reg), "Set ALC noise gate");
    ESP_LOGD(TAG, "Reg(0x%x): 0x%x", ES8388_ADCCONTROL14, reg);
    return ESP_CODEC_DEV_OK;
}

static int es8388_set_alc_channel(const audio_hw_alc_t *h, int channel_mask)
{
    const audio_hw_base_t *hw_base = h->base;
    if (hw_base == NULL) {
        ESP_LOGE(TAG, "Set ALC channel failed: invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int res = 0;
    int reg = 0;
    res = audio_hw_get_reg(hw_base, ES8388_ADCCONTROL10, &reg);
    if (res != 0) {
        return res;
    }
    reg &= 0x3F;
    switch (channel_mask) {
        case 0x00:
        default:
            break;
        case 0x01:
            reg |= 0x40;
            break;
        case 0x02:
            reg |= 0x80;
            break;
        case 0x03:
            reg |= 0xC0;
            break;
    }
    int vol_reg = 0;
    if (channel_mask != 0) {
        vol_reg = 50;
    }
    ES8388_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8388_ADCCONTROL8, vol_reg), "Set left ADC volume");
    ES8388_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8388_ADCCONTROL9, vol_reg), "Set right ADC volume");
    ES8388_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8388_ADCCONTROL10, reg), "Set ALC channel");
    ESP_LOGD(TAG, "Reg(0x%x): 0x%x", ES8388_ADCCONTROL10, reg);
    return ESP_CODEC_DEV_OK;
}

static int es8388_set_alc_target_gain(const audio_hw_alc_t *h, float target_gain)
{
    const audio_hw_base_t *hw_base = h->base;
    if (hw_base == NULL) {
        ESP_LOGE(TAG, "Set ALC gain failed: invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int res = 0;
    int reg = 0;
    res = audio_hw_get_reg(hw_base, ES8388_ADCCONTROL11, &reg);
    if (res != 0) {
        return res;
    }
    reg &= 0x0F;
    int target_gain_reg = (int)((target_gain + 16.5) / 1.5);
    _limit_to_min_max(&target_gain_reg, 0x00, 0x0F);
    reg |= (target_gain_reg << 4);
    ES8388_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8388_ADCCONTROL11, reg), "Set ALC target gain");
    ESP_LOGD(TAG, "Reg(0x%x): 0x%x", ES8388_ADCCONTROL11, reg);
    return ESP_CODEC_DEV_OK;
}

static int es8388_set_noise_gate(const audio_hw_alc_t *h, float threshold)
{
    const audio_hw_base_t *hw_base = h->base;
    if (hw_base == NULL) {
        ESP_LOGE(TAG, "Set noise gate failed: invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int res = 0;
    int reg = 0;
    res = audio_hw_get_reg(hw_base, ES8388_ADCCONTROL14, &reg);
    if (res != 0) {
        return res;
    }
    reg &= 0x07;
    int noise_gate_reg = (int)((threshold + 76.5) / 1.5);
    _limit_to_min_max(&noise_gate_reg, 0x00, 0x1F);
    reg |= (noise_gate_reg << 3);
    ES8388_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8388_ADCCONTROL14, reg), "Set noise gate");
    ESP_LOGD(TAG, "Reg(0x%x): 0x%x", ES8388_ADCCONTROL14, reg);
    return ESP_CODEC_DEV_OK;
}

int audio_hw_es8388_alc_new(const audio_hw_base_t *h, audio_hw_alc_handle_t *alc)
{
    ESP_RETURN_ON_FALSE(alc, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(h, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    if (*alc != NULL) {
        return ESP_CODEC_DEV_OK;
    }
    if (audio_hw_is_open(h) == false) {
        ESP_LOGE(TAG, "Create ALC failed: codec is not open");
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    audio_hw_alc_t *alc_handle = calloc(1, sizeof(audio_hw_alc_t));
    if (alc_handle == NULL) {
        ESP_LOGE(TAG, "Create ALC failed: no memory");
        return ESP_CODEC_DEV_NO_MEM;
    }
    alc_handle->base = h;
    alc_handle->set_gain = es8388_set_alc_target_gain;
    alc_handle->set_channel = es8388_set_alc_channel;
    alc_handle->init = es8388_alc_init;
    alc_handle->set_noise_gate = es8388_set_noise_gate;
    *alc = alc_handle;
    return ESP_CODEC_DEV_OK;
}
