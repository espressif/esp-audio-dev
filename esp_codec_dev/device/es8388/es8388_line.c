/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_log.h"

#include "esp_audio_hw_proc_if.h"
#include "audio_hw_base_priv.h"
#include "es8388_reg.h"

static const char *TAG = "ES8388_LINE";

#define ES8388_RETURN_ON_ERROR(ret, operation)  do {        \
    int err = (ret);                                        \
    if (err != ESP_CODEC_DEV_OK) {                          \
        ESP_LOGE(TAG, operation " failed: ret 0x%x", err);  \
        return err;                                         \
    }                                                       \
} while (0)

static int es8388_line_mode_init(const audio_hw_base_t *hw_base)
{
    int reg = 0x09;

    ES8388_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8388_DACCONTROL16, reg), "Set line input mux");

    ES8388_RETURN_ON_ERROR(audio_hw_get_reg(hw_base, ES8388_DACCONTROL17, &reg), "Get left mixer init value");
    reg &= 0xC0;
    reg |= 0x38;  // 0 dB
    ES8388_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8388_DACCONTROL17, reg), "Set left mixer init value");

    ES8388_RETURN_ON_ERROR(audio_hw_get_reg(hw_base, ES8388_DACCONTROL20, &reg), "Get right mixer init value");
    reg &= 0xC0;
    reg |= 0x38;  // 0 dB
    ES8388_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8388_DACCONTROL20, reg), "Set right mixer init value");

    return ESP_CODEC_DEV_OK;
}

static int es8388_line_in_mode(const audio_hw_base_t *hw_base, bool enable)
{
    int ret = es8388_line_mode_init(hw_base);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    int reg = 0;
    if (enable) {
        ES8388_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8388_ADCPOWER, 0x00), "Set line-in mode");
        reg = 0x09;
    }
    ES8388_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8388_DACCONTROL16, reg), "Set line-in mux");
    ESP_LOGD(TAG, "line in %s", enable ? "enable" : "disable");
    return ESP_CODEC_DEV_OK;
}

static int es8388_line_out_mode(const audio_hw_base_t *hw_base, bool enable)
{
    int ret = es8388_line_mode_init(hw_base);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    int reg = 0;
    // Get left mixer status
    ES8388_RETURN_ON_ERROR(audio_hw_get_reg(hw_base, ES8388_DACCONTROL17, &reg), "Get left mixer");
    reg &= 0xBF;
    if (enable) {
        ES8388_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8388_DACPOWER, 0x3C), "Set line-out mode");
        reg |= 0x40;
    }
    ES8388_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8388_DACCONTROL17, reg), "Set left mixer");
    ESP_LOGD(TAG, "Reg(%d): 0x%x", ES8388_DACCONTROL17, reg);

    // Get right mixer status
    ES8388_RETURN_ON_ERROR(audio_hw_get_reg(hw_base, ES8388_DACCONTROL20, &reg), "Get right mixer");
    reg &= 0xBF;
    if (enable) {
        reg |= 0x40;
    }
    ES8388_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8388_DACCONTROL20, reg), "Set right mixer");
    ESP_LOGD(TAG, "line out %s", enable ? "enable" : "disable");
    return ESP_CODEC_DEV_OK;
}

const esp_audio_hw_line_t es8388_line_ops = {
    .enable_in = es8388_line_in_mode,
    .enable_out = es8388_line_out_mode,
};
