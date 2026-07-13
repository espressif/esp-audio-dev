/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>

#include "esp_log.h"

#include "es7210_reg.h"
#include "es7210_proc_priv.h"

static const char *TAG = "ES7210_ALC";

#define ES7210_RETURN_ON_ERROR(ret, operation)  do {        \
    int err = (ret);                                        \
    if (err != ESP_CODEC_DEV_OK) {                          \
        ESP_LOGE(TAG, operation " failed: ret 0x%x", err);  \
        return err;                                         \
    }                                                       \
} while (0)

static const int16_t alc_level[] = {
    -301, -241, -206, -181,
    -161, -145, -132, -120,
    -110, -101, -93, -85,
    -78, -72, -66, -60,
};

static int get_reg_by_level(float level)
{
    int gain = (int)(level * 10);
    int reg = 0;
    int level_num = sizeof(alc_level) / sizeof(alc_level[0]);
    for (; reg < level_num; reg++) {
        if (gain <= alc_level[reg]) {
            break;
        }
    }
    reg = reg >= level_num ? level_num - 1 : reg;
    return reg;
}

static int es7210_set_min_max_level(const audio_hw_base_t *hw_base, bool alc12, float min, float max)
{
    int res = 0;
    int reg_v = (get_reg_by_level(max) << 4) | (get_reg_by_level(min));
    int reg = alc12 ? ES7210_ADC12_LEVEL_REG19 : ES7210_ADC34_LEVEL_REG18;
    res = audio_hw_set_reg(hw_base, reg, reg_v);
    ESP_LOGD(TAG, "Reg(0x%x): 0x%x", reg, reg_v);
    return res;
}

static int es7210_alc_init(const audio_hw_alc_t *h, const audio_alc_cfg_t *alc_cfg)
{
    const audio_hw_base_t *hw_base = h->base;
    if (hw_base == NULL || alc_cfg == NULL) {
        ESP_LOGE(TAG, "Initialize ALC failed: invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int reg = 0x30;
    ES7210_RETURN_ON_ERROR(es7210_set_min_max_level(hw_base, true, alc_cfg->min_gain, alc_cfg->max_gain),
                           "Set ALC12 min max level");
    ES7210_RETURN_ON_ERROR(es7210_set_min_max_level(hw_base, false, alc_cfg->min_gain, alc_cfg->max_gain),
                           "Set ALC34 min max level");
    ES7210_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES7210_ALC_CONFIG1_REG17, reg), "Set ALC config1");
    ES7210_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES7210_ALC_CONFIG2_REG1A, reg), "Set ALC config2");
    return ESP_CODEC_DEV_OK;
}

static inline int es7210_get_reg_by_max_gain(float max_gain)
{
    int reg = (int)((max_gain + 95.5) * 2);
    reg = reg < 0 ? 0 : reg;
    reg = reg > 0xFF ? 0xFF : reg;
    return reg;
}

static int es7210_set_alc_target_gain(const audio_hw_alc_t *h, float target_gain)
{
    const audio_hw_base_t *hw_base = h->base;
    if (hw_base == NULL) {
        ESP_LOGE(TAG, "Set ALC gain failed: invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int reg = es7210_get_reg_by_max_gain(target_gain);
    ES7210_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES7210_ADC1_ALC_GAIN_REG1E, reg), "Set ADC1 ALC gain");
    ES7210_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES7210_ADC2_ALC_GAIN_REG1D, reg), "Set ADC2 ALC gain");
    ES7210_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES7210_ADC3_ALC_GAIN_REG1C, reg), "Set ADC3 ALC gain");
    ES7210_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES7210_ADC4_ALC_GAIN_REG1B, reg), "Set ADC4 ALC gain");
    return ESP_CODEC_DEV_OK;
}

static int es7210_set_alc_channel(const audio_hw_alc_t *h, int channel_mask)
{
    const audio_hw_base_t *hw_base = h->base;
    if (hw_base == NULL) {
        ESP_LOGE(TAG, "Set ALC channel failed: invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int reg = channel_mask;
    ES7210_RETURN_ON_ERROR(audio_hw_set_reg(hw_base, ES7210_ALC_SELECT_REG16, reg), "Set ALC channel");
    if (channel_mask == 0) {
        ESP_LOGD(TAG, "Disable ALC");
        return es7210_set_alc_target_gain(h, 0);
    }
    ESP_LOGD(TAG, "Reg(0x%x): 0x%x", ES7210_ALC_SELECT_REG16, reg);
    return ESP_CODEC_DEV_OK;
}

int audio_hw_es7210_alc_new(const audio_hw_base_t *h, audio_hw_alc_handle_t *alc)
{
    if (h == NULL || alc == NULL) {
        ESP_LOGE(TAG, "Create ALC failed: invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
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
    alc_handle->set_gain = es7210_set_alc_target_gain;
    alc_handle->set_channel = es7210_set_alc_channel;
    alc_handle->init = es7210_alc_init;
    *alc = alc_handle;
    return ESP_CODEC_DEV_OK;
}
