/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_check.h"
#include "esp_log.h"

#include "esp_audio_hw_proc_if.h"
#include "audio_hw_base_priv.h"
#include "es8311_reg.h"

static const char *TAG = "ES8311_MUTE";

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

static int es8311_auto_mute_enable(const audio_hw_base_t *hw_base, bool enable)
{
    int reg = 0;
    ES8311_RETURN_ON_ERROR(audio_hw_get_reg(hw_base, ES8311_ADC_REG18, &reg), "Get auto mute enable");
    reg &= (~0x40);
    if (enable) {
        reg |= 0x40;
    }
    ES8311_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8311_ADC_REG18, reg), "Set auto mute enable");
    ESP_LOGD(TAG, "Auto mute %s", enable ? "enable" : "disable");
    return ESP_CODEC_DEV_OK;
}

static int es8311_set_auto_mute(const audio_hw_base_t *hw_base, const esp_audio_hw_auto_mute_cfg_t *mute_cfg)
{
    ESP_RETURN_ON_FALSE(mute_cfg, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");

    int reg = 0;
    int ws_r = mute_cfg->window_size / 2048 - 1;
    _limit_to_min_max(&ws_r, 0, 15);
    int ng_r = ((int)mute_cfg->noise_gate + 96.0f) / 6.0f;
    _limit_to_min_max(&ng_r, 0, 15);
    reg = (ws_r << 4) | ng_r;
    ES8311_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8311_ADC_REG1A, reg), "Set auto mute threshold");

    ES8311_RETURN_ON_ERROR(audio_hw_get_reg(hw_base, ES8311_ADC_REG1B, &reg), "Get auto mute volume");
    reg &= 0x1F;
    int vol = (int)(-mute_cfg->mute_vol / 4.0f);
    _limit_to_min_max(&vol, 0, 7);
    reg |= (vol << 5);
    ES8311_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8311_ADC_REG1B, reg), "Set auto mute volume");
    return ESP_CODEC_DEV_OK;
}

const esp_audio_hw_mute_t es8311_mute_ops = {
    .enable_auto_mute = es8311_auto_mute_enable,
    .set_auto_mute_cfg = es8311_set_auto_mute,
};
