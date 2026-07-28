/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>

#include "esp_log.h"

#include "es7210_proc_priv.h"
#include "es7210_reg.h"

static const char *TAG = "ES7210_MUTE";

#define ES7210_RETURN_ON_ERROR(ret, operation)  do {        \
    int err = (ret);                                        \
    if (err != ESP_CODEC_DEV_OK) {                          \
        ESP_LOGE(TAG, operation " failed: ret 0x%x", err);  \
        return err;                                         \
    }                                                       \
} while (0)

static const int16_t noise_gate[] = {
    -96, -90, -84, -78,
    -72, -66, -60, -54,
    -51, -48, -45, -42,
    -39, -36, -33, -30,
};

static int get_reg_by_gate(float level)
{
    int gain = (int)level;
    int reg = 0;
    int level_num = sizeof(noise_gate) / sizeof(noise_gate[0]);
    for (; reg < level_num; reg++) {
        if (gain <= noise_gate[reg]) {
            break;
        }
    }
    reg = reg >= level_num ? level_num - 1 : reg;
    return reg;
}

static int es7210_update_reg_bit(const audio_hw_base_t *hw_base, uint8_t reg_addr, uint8_t update_bits, uint8_t data)
{
    int regv = 0;
    int ret = audio_hw_get_reg(hw_base, reg_addr, &regv);
    if (ret != 0) {
        return ret;
    }
    regv = (regv & (~update_bits)) | (update_bits & data);
    return audio_hw_set_reg(hw_base, reg_addr, regv);
}

static int es7210_auto_mute_select(const audio_hw_base_t *hw_base, int channel_mask, bool enable, bool adc12)
{
    int mask = adc12 ? (channel_mask & 0x03) : ((channel_mask & 0x0C) >> 2);
    if (enable) {
        ES7210_RETURN_ON_ERROR(es7210_update_reg_bit(hw_base, ES7210_ADC_AUTOMUTE_REG13,
                                                     adc12 ? 0x04 : 0x08, adc12 ? 0x04 : 0x08),
                               "Enable auto mute");
        int reg_s = adc12 ? ES7210_ADC12_MUTERANGE_REG15 : ES7210_ADC34_MUTERANGE_REG14;
        switch (mask) {
            case 0x01:
                ES7210_RETURN_ON_ERROR(es7210_update_reg_bit(hw_base, reg_s, 0x0C, 0x02 << 2),
                                       "Select auto mute channel 1");
                break;
            case 0x02:
                ES7210_RETURN_ON_ERROR(es7210_update_reg_bit(hw_base, reg_s, 0x0C, 0x01 << 2),
                                       "Select auto mute channel 2");
                break;
            case 0x03:
                ES7210_RETURN_ON_ERROR(es7210_update_reg_bit(hw_base, reg_s, 0x0C, 0x03 << 2),
                                       "Select auto mute channel pair");
                break;
            default:
                break;
        }

    } else {
        switch (mask) {
            case 0x01:
                ES7210_RETURN_ON_ERROR(es7210_update_reg_bit(hw_base, ES7210_ADC12_MUTERANGE_REG15, 0x08, 0x00),
                                       "Disable auto mute channel 1");
                break;
            case 0x02:
                ES7210_RETURN_ON_ERROR(es7210_update_reg_bit(hw_base, ES7210_ADC12_MUTERANGE_REG15, 0x04, 0x00),
                                       "Disable auto mute channel 2");
                break;
            case 0x03:
                ES7210_RETURN_ON_ERROR(es7210_update_reg_bit(hw_base, ES7210_ADC12_MUTERANGE_REG15, 0x0C, 0x00),
                                       "Disable auto mute channel pair");
                ES7210_RETURN_ON_ERROR(es7210_update_reg_bit(hw_base, ES7210_ADC_AUTOMUTE_REG13, 0x04, 0x00),
                                       "Disable auto mute");
                break;
            default:
                break;
        }
    }
    return ESP_CODEC_DEV_OK;
}

static int es7210_auto_mute_enable(const audio_hw_mute_t *h, bool enable)
{
    const audio_hw_base_t *hw_base = h->base;
    if (hw_base == NULL) {
        ESP_LOGE(TAG, "Set auto mute enable failed: invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int channel_mask = 0x0F;
    ES7210_RETURN_ON_ERROR(es7210_auto_mute_select(hw_base, channel_mask, enable, true), "Set ADC12 auto mute");
    ES7210_RETURN_ON_ERROR(es7210_auto_mute_select(hw_base, channel_mask, enable, false), "Set ADC34 auto mute");

    ESP_LOGD(TAG, "Auto mute[0x%x] %s", channel_mask, enable ? "enable" : "disable");
    return ESP_CODEC_DEV_OK;
}

static int es7210_set_auto_mute(const audio_hw_mute_t *h, const auto_mute_cfg_t *cfg)
{
    const audio_hw_base_t *hw_base = h->base;
    const auto_mute_cfg_t *mute_cfg = cfg;
    if (hw_base == NULL || mute_cfg == NULL) {
        ESP_LOGE(TAG, "Set auto mute config failed: invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int reg = 0;
    int real_ws = 2048;
    if (mute_cfg->window_size <= 2048) {
    } else if (mute_cfg->window_size <= 4096) {
        reg = 1;
        real_ws = 4096;
    } else if (mute_cfg->window_size <= 8192) {
        reg = 2;
        real_ws = 8192;
    } else {
        reg = 3;
        real_ws = 16384;
    }
    ES7210_RETURN_ON_ERROR(es7210_update_reg_bit(hw_base, ES7210_ADC_AUTOMUTE_REG13, 0x03, reg),
                           "Set auto mute window");

    int gate_r = get_reg_by_gate(mute_cfg->noise_gate);
    ES7210_RETURN_ON_ERROR(es7210_update_reg_bit(hw_base, ES7210_ADC12_MUTERANGE_REG15, 0xF0, gate_r << 4),
                           "Set ADC12 auto mute gate");
    ES7210_RETURN_ON_ERROR(es7210_update_reg_bit(hw_base, ES7210_ADC34_MUTERANGE_REG14, 0xF0, gate_r << 4),
                           "Set ADC34 auto mute gate");
    ESP_LOGD(TAG, "windows_size: %d, noise_gate: %d", real_ws, noise_gate[gate_r]);
    return ESP_CODEC_DEV_OK;
}

int audio_hw_es7210_mute_new(const audio_hw_base_t *h, audio_hw_mute_handle_t *mute)
{
    if (mute == NULL || h == NULL) {
        ESP_LOGE(TAG, "Create mute failed: invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (*mute != NULL) {
        return ESP_CODEC_DEV_OK;
    }
    if (audio_hw_is_open(h) == false) {
        ESP_LOGE(TAG, "Create mute failed: codec is not open");
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    audio_hw_mute_t *mute_handle = calloc(1, sizeof(audio_hw_mute_t));
    if (mute_handle == NULL) {
        ESP_LOGE(TAG, "Create mute failed: no memory");
        return ESP_CODEC_DEV_NO_MEM;
    }
    mute_handle->base = h;
    mute_handle->enable_auto_mute = es7210_auto_mute_enable;
    mute_handle->set_auto_mute_cfg = es7210_set_auto_mute;
    *mute = mute_handle;
    return ESP_CODEC_DEV_OK;
}
