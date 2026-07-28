/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>

#include "esp_check.h"
#include "esp_log.h"

#include "es8311_reg.h"
#include "es8311_proc_priv.h"

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

static int es8311_drc_enable(const audio_hw_drc_t *h, bool enable)
{
    const audio_hw_base_t *hw_base = h->base;
    ESP_RETURN_ON_FALSE(hw_base, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");

    int reg = 0;
    ES8311_RETURN_ON_ERROR(audio_hw_get_reg(hw_base, ES8311_DAC_REG34, &reg), "Get DRC enable");
    reg = enable == true ? (reg | 0x80) : (reg & 0x7F);
    ES8311_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8311_DAC_REG34, reg), "Set DRC enable");
    ESP_LOGD(TAG, "DRC is %s", enable ? "enabled" : "disabled");
    return ESP_CODEC_DEV_OK;
}

static int es8311_drc_init(const audio_hw_drc_t *h, const audio_drc_cfg_t *drc_cfg)
{
    const audio_hw_base_t *hw_base = h->base;
    ESP_RETURN_ON_FALSE(hw_base, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(drc_cfg, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");

    int reg = 0;
    int max_gain_reg = get_reg_by_target(drc_cfg->max_gain);
    int min_gain_reg = get_reg_by_target(drc_cfg->min_gain);
    ESP_LOGD(TAG, "Max gain: %.1f, min gain: %.1f", target_table[max_gain_reg] / 10.0, target_table[min_gain_reg] / 10.0);
    reg = (max_gain_reg << 4) | min_gain_reg;
    ES8311_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES8311_DAC_REG35, reg), "Set DRC gain");
    ESP_LOGD(TAG, "Reg(0x%x): 0x%x", ES8311_DAC_REG35, reg);

    ES8311_RETURN_ON_ERROR(es8311_drc_enable(h, true), "Enable DRC");

    return ESP_CODEC_DEV_OK;
}

int audio_hw_es8311_drc_new(const audio_hw_base_t *h, audio_hw_drc_handle_t *drc)
{
    ESP_RETURN_ON_FALSE(drc, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    ESP_RETURN_ON_FALSE(h, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    if (*drc != NULL) {
        return ESP_CODEC_DEV_OK;
    }
    ESP_RETURN_ON_FALSE(audio_hw_is_open(h), ESP_CODEC_DEV_WRONG_STATE, TAG, "Codec is not opened");

    audio_hw_drc_t *drc_handle = calloc(1, sizeof(audio_hw_drc_t));
    ESP_RETURN_ON_FALSE(drc_handle, ESP_CODEC_DEV_NO_MEM, TAG, "No memory");

    drc_handle->base = h;
    drc_handle->init = es8311_drc_init;
    drc_handle->enable = es8311_drc_enable;
    *drc = drc_handle;
    return ESP_CODEC_DEV_OK;
}
