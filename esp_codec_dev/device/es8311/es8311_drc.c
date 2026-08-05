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

static const char *TAG = "ES8311_DRC";

#define ES8311_RETURN_ON_ERROR(ret, operation)  do {        \
    int err = (ret);                                        \
    if (err != ESP_CODEC_DEV_OK) {                          \
        ESP_LOGE(TAG, operation " failed: ret 0x%x", err);  \
        return err;                                         \
    }                                                       \
} while (0)

static const int16_t target_table[] = {
    -301, -241, -206, -181,
    -161, -145, -132, -120,
    -110, -101, -93, -85,
    -78, -72, -66, -60,
};

static int get_reg_by_target(float target)
{
    int gain = (int)(target * 10);
    int reg = 0;
    int target_num = sizeof(target_table) / sizeof(target_table[0]);
    for (; reg < target_num; reg++) {
        if (gain <= target_table[reg]) {
            break;
        }
    }
    reg = reg >= target_num ? target_num - 1 : reg;
    return reg;
}

static int es8311_drc_enable(const audio_hw_base_t *hw_base, bool enable)
{
    int reg = 0;
    ES8311_RETURN_ON_ERROR(audio_hw_get_reg(hw_base, ES8311_DAC_REG34, &reg), "Get DRC enable");
    reg = enable == true ? (reg | 0x80) : (reg & 0x7F);
    ES8311_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8311_DAC_REG34, reg), "Set DRC enable");
    ESP_LOGD(TAG, "DRC is %s", enable ? "enabled" : "disabled");
    return ESP_CODEC_DEV_OK;
}

static int es8311_drc_init(const audio_hw_base_t *hw_base, const esp_audio_hw_drc_cfg_t *drc_cfg)
{
    ESP_RETURN_ON_FALSE(drc_cfg, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");

    int reg = 0;
    int max_gain_reg = get_reg_by_target(drc_cfg->max_gain);
    int min_gain_reg = get_reg_by_target(drc_cfg->min_gain);
    ESP_LOGD(TAG, "Max gain: %.1f, min gain: %.1f", target_table[max_gain_reg] / 10.0, target_table[min_gain_reg] / 10.0);
    reg = (max_gain_reg << 4) | min_gain_reg;
    ES8311_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8311_DAC_REG35, reg), "Set DRC gain");
    ESP_LOGD(TAG, "Reg(0x%x): 0x%x", ES8311_DAC_REG35, reg);

    ES8311_RETURN_ON_ERROR(es8311_drc_enable(hw_base, true), "Enable DRC");

    return ESP_CODEC_DEV_OK;
}

const esp_audio_hw_drc_t es8311_drc_ops = {
    .init = es8311_drc_init,
    .enable = es8311_drc_enable,
};
