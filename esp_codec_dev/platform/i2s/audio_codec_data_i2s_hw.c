/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>

#include "esp_clk_tree.h"
#include "hal/clk_tree_hal.h"
#include "hal/i2s_types.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_common.h"
#include "esp_log.h"

#include "audio_codec_data_i2s_priv.h"

static const char *TAG = "I2S_HW";

#define I2S_SLOT_BITS_MAX  (32)

/**
 * @brief  Slot bit widths accepted by the I2S hardware
 * @note  The slot bit width register field is 5 bits wide, so any value above
 *         I2S_SLOT_BITS_MAX is silently truncated instead of being rejected.
 */
#define I2S_SLOT_BITS_IS_VALID(bits)  ((bits) == 8 || (bits) == 16 || (bits) == 24 || (bits) == 32)

static inline const char *_i2s_mode_to_str(i2s_comm_mode_t mode)
{
    switch (mode) {
        case I2S_COMM_MODE_NONE:
            return "NONE";
        case I2S_COMM_MODE_STD:
            return "STD";
#if SOC_I2S_SUPPORTS_TDM
        case I2S_COMM_MODE_TDM:
            return "TDM";
#endif  /* SOC_I2S_SUPPORTS_TDM */
#if SOC_I2S_SUPPORTS_PDM
        case I2S_COMM_MODE_PDM:
            return "PDM";
#endif  /* SOC_I2S_SUPPORTS_PDM */
        default:
            return "UNKNOWN";
    }
}

// IDF >= v5.5.2 supports lazy init and can automatically form full-duplex mode.
static inline void _show_channel_info(i2s_chan_handle_t channel)
{
    i2s_chan_info_t info = {0};
    if (i2s_channel_get_info(channel, &info) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get I2S channel info");
        return;
    }
    ESP_LOGI(TAG,
             "I2S channel info: port=%d role=%s dir=%s mode=%s enabled=%d "
             "sclk=%" PRIu32 "Hz mclk=%" PRIu32 "Hz bclk=%" PRIu32 "Hz dma=%" PRIu32 "B",
             info.id, info.role == I2S_ROLE_MASTER ? "MASTER" : "SLAVE",
             info.dir == I2S_DIR_TX ? "TX" : "RX", _i2s_mode_to_str(info.mode),
             info.is_enabled, info.sclk_hz, info.mclk_hz, info.bclk_hz,
             info.total_dma_buf_size);
    ESP_LOGD(TAG, "I2S channel pointers: pair=%p mode_cfg=%p", info.pair_chan, info.mode_cfg);
}

static void _check_mclk_jitter(int mode, void *clk_cfg)
{
    uint32_t source_clk = 0;
    uint32_t mclk = 0;
    i2s_clock_src_t clk_src __attribute__((unused)) = I2S_CLK_SRC_DEFAULT;
    if (I2S_CLK_SRC_DEFAULT == 0) {
        // ESP32P4 I2S_CLK_SRC_DEFAULT is zero, so can not get source_clk, skip check
        return;
    }
    switch (mode) {
        case I2S_COMM_MODE_STD: {
            i2s_std_clk_config_t *std_clk_cfg = (i2s_std_clk_config_t *)clk_cfg;
            clk_src = std_clk_cfg->clk_src;
            esp_clk_tree_src_get_freq_hz(std_clk_cfg->clk_src, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &source_clk);
            mclk = std_clk_cfg->sample_rate_hz * std_clk_cfg->mclk_multiple;
            break;
        }
#if SOC_I2S_SUPPORTS_TDM
        case I2S_COMM_MODE_TDM: {
            i2s_tdm_clk_config_t *tdm_clk_cfg = (i2s_tdm_clk_config_t *)clk_cfg;
            clk_src = tdm_clk_cfg->clk_src;
            esp_clk_tree_src_get_freq_hz(tdm_clk_cfg->clk_src, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &source_clk);
            mclk = tdm_clk_cfg->sample_rate_hz * tdm_clk_cfg->mclk_multiple;
            break;
        }
#endif  /* SOC_I2S_SUPPORTS_TDM */
#if SOC_I2S_SUPPORTS_PDM
        case I2S_COMM_MODE_PDM: {
            i2s_pdm_tx_clk_config_t *pdm_clk_cfg = (i2s_pdm_tx_clk_config_t *)clk_cfg;
            clk_src = pdm_clk_cfg->clk_src;
            esp_clk_tree_src_get_freq_hz(pdm_clk_cfg->clk_src, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &source_clk);
            mclk = pdm_clk_cfg->sample_rate_hz * pdm_clk_cfg->mclk_multiple;
            break;
        }
#endif  /* SOC_I2S_SUPPORTS_PDM */
        default: {
            ESP_LOGE(TAG, "Unsupported mode: %d", mode);
            return;
        }
    }
#if SOC_I2S_SUPPORTS_APLL
    if (clk_src == I2S_CLK_SRC_APLL) {
        source_clk = clk_hal_apll_get_freq_hz();
    }
#endif  /* SOC_I2S_SUPPORTS_APLL */
    {
        float_t jitter = (float_t)source_clk / (float_t)mclk - (uint32_t)(source_clk / mclk);
        jitter = (0.5f - fabsf(0.5f - jitter)) / 0.5f;
        jitter = jitter * 100.0f;
        int jitter_pct = (int)(jitter + 0.5f);
        if (jitter_pct > 10) {
            ESP_LOGW(TAG, "MCLK jitter ratio: %d%%, source_clk: %" PRIu32 ", mclk: %" PRIu32, jitter_pct, source_clk, mclk);
        } else {
            ESP_LOGD(TAG, "MCLK jitter ratio: %d%%, source_clk: %" PRIu32 ", mclk: %" PRIu32, jitter_pct, source_clk, mclk);
        }
    }
}

int i2s_data_hw_enable(i2s_data_t *i2s_data, bool is_playback, bool enable)
{
    i2s_chan_handle_t channel = _get_channel_handle(i2s_data, is_playback);
    if (channel == NULL) {
        return ESP_CODEC_DEV_NOT_FOUND;
    }
    if (_is_enabled(channel) == enable) {
        return ESP_CODEC_DEV_OK;
    }
    int ret;
    if (enable) {
        ret = i2s_channel_enable(channel);
    } else {
        ret = i2s_channel_disable(channel);
    }
    return ret == ESP_OK ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}

int i2s_data_hw_set_fs(i2s_chan_handle_t channel, bool is_playback, int slot_bits, i2s_clock_src_t clk_src,
                       const esp_codec_dev_sample_info_t *fs)
{
    if (!I2S_SLOT_BITS_IS_VALID(slot_bits)) {
        ESP_LOGE(TAG, "Slot bit width %d is not supported, must be 8/16/24/32", slot_bits);
        if (slot_bits > I2S_SLOT_BITS_MAX) {
            int total_frame_bits = slot_bits * fs->channel;
            int need_channel = (total_frame_bits + fs->bits_per_sample - 1) / fs->bits_per_sample;
            ESP_LOGE(TAG, "Frame of %d bits cannot be built from %d channels of %d bits; "
                          "increase the channel count to %d and keep the channel mask unchanged",
                     total_frame_bits, (int)fs->channel, (int)fs->bits_per_sample, need_channel);
        }
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    i2s_chan_info_t chan_info = {0};
    int ret = ESP_CODEC_DEV_OK;
    if (i2s_channel_get_info(channel, &chan_info) != ESP_OK) {
        return ESP_CODEC_DEV_DRV_ERR;
    }
    // STD mode carries more than 2 channels as 2 slots of multiplied width; resolve it before touching the channel
    if (chan_info.mode == I2S_COMM_MODE_STD && fs->channel > 2) {
        int std_slot_bits = slot_bits * fs->channel / 2;
        if (!I2S_SLOT_BITS_IS_VALID(std_slot_bits)) {
            ESP_LOGE(TAG, "STD mode needs a %d-bit slot to carry %d channels of %d bits, which is not supported",
                     std_slot_bits, (int)fs->channel, (int)fs->bits_per_sample);
            ESP_LOGE(TAG, "Use TDM mode instead, where the frame is widened by adding slots rather than widening them");
            return ESP_CODEC_DEV_NOT_SUPPORT;
        }
        slot_bits = std_slot_bits;
    }
    if (chan_info.is_enabled) {
        ESP_LOGD(TAG, "Channel is enabled, need disable first");
        ret = i2s_channel_disable(channel);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to disable channel");
            return ESP_CODEC_DEV_DRV_ERR;
        }
    }
    int effective_mclk = fs->mclk_multiple ? fs->mclk_multiple : I2S_MCLK_MULTIPLE_256;
    int bclk_div = effective_mclk / (fs->channel * slot_bits);
    if (chan_info.dir == I2S_DIR_TX && !_is_master(channel)) {
        int need_mclk_multiple = fs->channel * slot_bits * 6;
        if (bclk_div < 6) {
            ESP_LOGW(TAG, "BCLK division %d is too small for TX slave; increase MCLK multiple to %d",
                     bclk_div, need_mclk_multiple);
        }
    }
    switch (chan_info.mode) {
        case I2S_COMM_MODE_STD: {
            // Support the following cases:
            // 1. 2ch && mask < 0x03 : use mono mode
            // 2. 2ch && mask >= 0x03: use stereo mode
            // 3. 4ch                : use stereo mode, and data_bits = slot_bits = 2 * bits_per_sample
            uint8_t data_bits = fs->bits_per_sample;
            int slot_mask = (fs->channel > 2) ? I2S_STD_SLOT_BOTH : fs->channel_mask;
            slot_mask = slot_mask & I2S_STD_SLOT_BOTH;
            i2s_slot_mode_t slot_mode = (slot_mask == I2S_STD_SLOT_BOTH) ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO;
            // STD use 2ch 32bit to get 4ch 16bit data, slot_bits was already widened on entry
            if (fs->channel > 2) {
#if SOC_I2S_HW_VERSION_2
                ESP_LOGW(TAG, "TDM mode is recommended for 4-channel 16-bit data; avoid using STD mode with 2-channel 32-bit data");
#endif  /* SOC_I2S_HW_VERSION_2 */
                data_bits = (uint8_t)slot_bits;
            }
            i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(data_bits, slot_mode);
            slot_cfg.slot_mask = slot_mask;
            slot_cfg.slot_mode = slot_mode;
            slot_cfg.data_bit_width = data_bits;
            slot_cfg.slot_bit_width = slot_bits;
            slot_cfg.ws_width = slot_bits;

            i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(fs->sample_rate);
            if (clk_src) {
                clk_cfg.clk_src = clk_src;
            }
            if (fs->mclk_multiple) {
                clk_cfg.mclk_multiple = fs->mclk_multiple;
            }
            if (slot_bits == 24 && (clk_cfg.mclk_multiple % 3) != 0) {
                clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;
            }
            ret = i2s_channel_reconfig_std_slot(channel, &slot_cfg);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to reconfigure STD slot, ret=%d", ret);
                return ESP_CODEC_DEV_DRV_ERR;
            }
            ret = i2s_channel_reconfig_std_clock(channel, &clk_cfg);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to reconfigure STD clock, ret=%d", ret);
                return ESP_CODEC_DEV_DRV_ERR;
            }

            /* STD clk_cfg.bclk_div exists since IDF 5.5; log computed divider for all versions */
            ESP_LOGI(TAG, "STD %s: %" PRIu32 "Hz data/slot=%d/%db ws=%" PRIu32 " %s mask=0x%x mclk=%dx bdiv=%d",
                     chan_info.dir == I2S_DIR_RX ? "RX" : "TX", clk_cfg.sample_rate_hz,
                     (int)slot_cfg.data_bit_width, (int)slot_cfg.slot_bit_width, slot_cfg.ws_width,
                     slot_cfg.slot_mode == I2S_SLOT_MODE_MONO ? "MONO" : "STEREO", (int)slot_cfg.slot_mask,
                     (int)clk_cfg.mclk_multiple, bclk_div);
            _check_mclk_jitter(I2S_COMM_MODE_STD, &clk_cfg);
        } break;
#if SOC_I2S_SUPPORTS_PDM
        case I2S_COMM_MODE_PDM: {
            if (!is_playback) {
#if SOC_I2S_SUPPORTS_PDM_RX
                i2s_pdm_rx_clk_config_t clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(fs->sample_rate);
                if (clk_src) {
                    clk_cfg.clk_src = clk_src;
                }
                if (fs->mclk_multiple) {
                    clk_cfg.mclk_multiple = fs->mclk_multiple;
                }
                i2s_pdm_rx_slot_config_t slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(slot_bits, I2S_SLOT_MODE_STEREO);
                i2s_pdm_slot_mask_t slot_mask = fs->channel_mask ? (i2s_pdm_slot_mask_t)fs->channel_mask : I2S_PDM_SLOT_BOTH;
                // Stereo channel mask is ignored in driver, need use mono instead
                if (fs->channel_mask && fs->channel_mask < 3) {
                    slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
                }
                slot_cfg.slot_mask = slot_mask;
                if (slot_bits > fs->bits_per_sample) {
                    slot_cfg.data_bit_width = fs->bits_per_sample;
                    slot_cfg.slot_bit_width = slot_bits;
                }
                ret = i2s_channel_reconfig_pdm_rx_clock(channel, &clk_cfg);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to reconfigure PDM RX clock");
                    return ESP_CODEC_DEV_DRV_ERR;
                }
                ret = i2s_channel_reconfig_pdm_rx_slot(channel, &slot_cfg);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to reconfigure PDM RX slot");
                    return ESP_CODEC_DEV_DRV_ERR;
                }
                ESP_LOGI(TAG, "PDM RX: %" PRIu32 "Hz data/slot=%d/%db mask=0x%x",
                         clk_cfg.sample_rate_hz, (int)slot_cfg.data_bit_width,
                         (int)slot_cfg.slot_bit_width, (int)slot_cfg.slot_mask);
#else
                ESP_LOGE(TAG, "PDM RX is not supported");
                return ESP_CODEC_DEV_NOT_SUPPORT;
#endif  /* SOC_I2S_SUPPORTS_PDM_RX */
            } else {
#if SOC_I2S_SUPPORTS_PDM_TX
                i2s_pdm_tx_clk_config_t clk_cfg = I2S_PDM_TX_CLK_DEFAULT_CONFIG(fs->sample_rate);
                if (clk_src) {
                    clk_cfg.clk_src = clk_src;
                }
                if (fs->mclk_multiple) {
                    clk_cfg.mclk_multiple = fs->mclk_multiple;
                }
                clk_cfg.up_sample_fs = fs->sample_rate / 100;
                i2s_pdm_tx_slot_config_t slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(slot_bits, I2S_SLOT_MODE_STEREO);
                // Stereo channel mask is ignored, need use mono instead
                if (fs->channel_mask && fs->channel_mask < 3) {
                    slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
                }
#if SOC_I2S_HW_VERSION_1
                i2s_pdm_slot_mask_t slot_mask = fs->channel_mask ? (i2s_pdm_slot_mask_t)fs->channel_mask : I2S_PDM_SLOT_BOTH;
                slot_cfg.slot_mask = slot_mask;
#endif  /* SOC_I2S_HW_VERSION_1 */
                if (slot_bits > fs->bits_per_sample) {
                    slot_cfg.data_bit_width = fs->bits_per_sample;
                    slot_cfg.slot_bit_width = slot_bits;
                }
                ret = i2s_channel_reconfig_pdm_tx_clock(channel, &clk_cfg);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to reconfigure PDM TX clock");
                    return ESP_CODEC_DEV_DRV_ERR;
                }
                ret = i2s_channel_reconfig_pdm_tx_slot(channel, &slot_cfg);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to reconfigure PDM TX slot");
                    return ESP_CODEC_DEV_DRV_ERR;
                }
                ESP_LOGI(TAG, "PDM TX: %" PRIu32 "Hz data/slot=%d/%db mask=0x%x",
                         clk_cfg.sample_rate_hz, (int)slot_cfg.data_bit_width,
                         (int)slot_cfg.slot_bit_width, (int)fs->channel_mask);
                _check_mclk_jitter(I2S_COMM_MODE_PDM, &clk_cfg);
#else
                ESP_LOGE(TAG, "PDM TX is not supported");
                return ESP_CODEC_DEV_NOT_SUPPORT;
#endif  /* SOC_I2S_SUPPORTS_PDM_TX */
            }
        } break;
#endif  /* SOC_I2S_SUPPORTS_PDM */
#if SOC_I2S_SUPPORTS_TDM
        case I2S_COMM_MODE_TDM: {
            if (fs->channel == 2 && fs->bits_per_sample == 32) {
                ESP_LOGW(TAG, "Using 32-bit samples to get 2-channel 16-bit data in TDM mode is not recommended; use 4-channel 16-bit instead");
            }
            i2s_tdm_clk_config_t clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(fs->sample_rate);
            if (clk_src) {
                clk_cfg.clk_src = clk_src;
            }
            if (fs->mclk_multiple) {
                clk_cfg.mclk_multiple = fs->mclk_multiple;
            }
            if (slot_bits == 24 && (clk_cfg.mclk_multiple % 3) != 0) {
                clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;
            }
            i2s_tdm_slot_config_t slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(
                slot_bits,
                I2S_SLOT_MODE_STEREO,
                (i2s_tdm_slot_mask_t)fs->channel_mask);
            slot_cfg.total_slot = fs->channel;
            slot_cfg.left_align = true;  // Use left align mode to keep the same with STD mode
            slot_cfg.data_bit_width = fs->bits_per_sample;
            slot_cfg.slot_bit_width = slot_bits;
            slot_cfg.ws_width = slot_bits * slot_cfg.total_slot / 2;
            ret = i2s_channel_reconfig_tdm_slot(channel, &slot_cfg);
            if (ret != ESP_OK) {
                return ESP_CODEC_DEV_DRV_ERR;
            }
            ret = i2s_channel_reconfig_tdm_clock(channel, &clk_cfg);
            if (ret != ESP_OK) {
                return ESP_CODEC_DEV_DRV_ERR;
            }
            ESP_LOGI(TAG, "TDM %s: %" PRIu32 "Hz data/slot=%d/%db ws=%" PRIu32 " slots=%" PRIu32
                          " mask=0x%x mclk=%dx bdiv=%" PRIu32,
                     chan_info.dir == I2S_DIR_RX ? "RX" : "TX", clk_cfg.sample_rate_hz,
                     (int)slot_cfg.data_bit_width, (int)slot_cfg.slot_bit_width, slot_cfg.ws_width,
                     slot_cfg.total_slot, (int)slot_cfg.slot_mask, (int)clk_cfg.mclk_multiple,
                     clk_cfg.bclk_div);
            _check_mclk_jitter(I2S_COMM_MODE_TDM, &clk_cfg);
        } break;
#endif  /* SOC_I2S_SUPPORTS_TDM */
        default:
            return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    _show_channel_info(channel);
    return ret;
}

int i2s_data_hw_get_bus_info(i2s_chan_handle_t channel, esp_codec_dev_bus_info_t *bus_info)
{
    i2s_chan_info_t info = {0};
    if (channel == NULL || bus_info == NULL) {
        ESP_LOGE(TAG, "Parse I2S bus information failed: invalid argument");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (i2s_channel_get_info(channel, &info) != ESP_OK) {
        ESP_LOGE(TAG, "Parse I2S bus information failed: driver query failed");
        return ESP_CODEC_DEV_DRV_ERR;
    }
    if (info.mode == I2S_COMM_MODE_NONE || info.mode_cfg == NULL) {
        ESP_LOGE(TAG, "Parse I2S bus information failed: channel is not configured");
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    memset(bus_info, 0, sizeof(*bus_info));
    switch (info.mode) {
        case I2S_COMM_MODE_STD: {
            const i2s_std_config_t *cfg = (const i2s_std_config_t *)info.mode_cfg;
            bus_info->mode = ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS;
            bus_info->sample_rate = cfg->clk_cfg.sample_rate_hz;
            bus_info->mclk_multiple = cfg->clk_cfg.mclk_multiple;
            bus_info->total_slot = 2;
            bus_info->slot_bit = (uint8_t)cfg->slot_cfg.slot_bit_width;
            bus_info->data_bit = (uint8_t)cfg->slot_cfg.data_bit_width;
            bus_info->slot_mask = (uint16_t)cfg->slot_cfg.slot_mask;
            bus_info->total_frame_bits = (uint16_t)(bus_info->total_slot * bus_info->slot_bit);
            return ESP_CODEC_DEV_OK;
        }
#if SOC_I2S_SUPPORTS_TDM
        case I2S_COMM_MODE_TDM: {
            const i2s_tdm_config_t *cfg = (const i2s_tdm_config_t *)info.mode_cfg;
            bus_info->mode = ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS;
            bus_info->sample_rate = cfg->clk_cfg.sample_rate_hz;
            bus_info->mclk_multiple = cfg->clk_cfg.mclk_multiple;
            bus_info->total_slot = (uint8_t)cfg->slot_cfg.total_slot;
            bus_info->slot_bit = (uint8_t)cfg->slot_cfg.slot_bit_width;
            bus_info->data_bit = (uint8_t)cfg->slot_cfg.data_bit_width;
            bus_info->slot_mask = (uint16_t)cfg->slot_cfg.slot_mask;
            bus_info->total_frame_bits = (uint16_t)(bus_info->total_slot * bus_info->slot_bit);
            return ESP_CODEC_DEV_OK;
        }
#endif  /* SOC_I2S_SUPPORTS_TDM */
        default:
            ESP_LOGD(TAG, "Parse I2S bus information: mode %d is unsupported", info.mode);
            return ESP_CODEC_DEV_NOT_SUPPORT;
    }
}

int i2s_data_hw_get_mode(i2s_chan_handle_t channel, esp_codec_dev_i2s_mode_t *mode)
{
    i2s_chan_info_t chan_info = {0};
    if (i2s_channel_get_info(channel, &chan_info) != ESP_OK) {
        *mode = ESP_CODEC_DEV_I2S_MODE_NONE;
        return ESP_CODEC_DEV_DRV_ERR;
    }
    switch (chan_info.mode) {
        case I2S_COMM_MODE_STD:
            *mode = ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS;
            break;
#if SOC_I2S_SUPPORTS_PDM
        case I2S_COMM_MODE_PDM:
            if (chan_info.dir == I2S_DIR_RX) {
                *mode = ESP_CODEC_DEV_I2S_MODE_PDM_RX;
            } else {
                *mode = ESP_CODEC_DEV_I2S_MODE_PDM_TX;
            }
            break;
#endif  /* SOC_I2S_SUPPORTS_PDM */
#if SOC_I2S_SUPPORTS_TDM
        case I2S_COMM_MODE_TDM:
            *mode = ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS;
            break;
#endif  /* SOC_I2S_SUPPORTS_TDM */
        default:
            *mode = ESP_CODEC_DEV_I2S_MODE_NONE;
            break;
    }
    return ESP_CODEC_DEV_OK;
}

int i2s_data_hw_get_fs(i2s_chan_handle_t channel, esp_codec_dev_sample_info_t *fs)
{
    i2s_chan_info_t info = {0};
    if (i2s_channel_get_info(channel, &info) != ESP_OK) {
        return ESP_CODEC_DEV_DRV_ERR;
    }
    if (info.mode == I2S_COMM_MODE_NONE || info.mode_cfg == NULL) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    memset(fs, 0, sizeof(*fs));
    switch (info.mode) {
        case I2S_COMM_MODE_STD: {
            const i2s_std_config_t *cfg = (const i2s_std_config_t *)info.mode_cfg;
            fs->sample_rate = cfg->clk_cfg.sample_rate_hz;
            fs->mclk_multiple = cfg->clk_cfg.mclk_multiple;
            fs->bits_per_sample = (uint8_t)cfg->slot_cfg.data_bit_width;
            fs->channel_mask = (uint16_t)cfg->slot_cfg.slot_mask;
            fs->channel = (cfg->slot_cfg.slot_mode == I2S_SLOT_MODE_MONO) ? 1 : 2;
            break;
        }
#if SOC_I2S_SUPPORTS_TDM
        case I2S_COMM_MODE_TDM: {
            const i2s_tdm_config_t *cfg = (const i2s_tdm_config_t *)info.mode_cfg;
            fs->sample_rate = cfg->clk_cfg.sample_rate_hz;
            fs->mclk_multiple = cfg->clk_cfg.mclk_multiple;
            fs->bits_per_sample = (uint8_t)cfg->slot_cfg.data_bit_width;
            fs->channel = (uint8_t)cfg->slot_cfg.total_slot;
            fs->channel_mask = (uint16_t)cfg->slot_cfg.slot_mask;
            break;
        }
#endif  /* SOC_I2S_SUPPORTS_TDM */
#if SOC_I2S_SUPPORTS_PDM
        case I2S_COMM_MODE_PDM: {
            if (info.dir == I2S_DIR_RX) {
#if SOC_I2S_SUPPORTS_PDM_RX
                const i2s_pdm_rx_config_t *cfg = (const i2s_pdm_rx_config_t *)info.mode_cfg;
                fs->sample_rate = cfg->clk_cfg.sample_rate_hz;
                fs->mclk_multiple = cfg->clk_cfg.mclk_multiple;
                fs->bits_per_sample = (uint8_t)cfg->slot_cfg.data_bit_width;
                fs->channel_mask = (uint16_t)cfg->slot_cfg.slot_mask;
                fs->channel = (cfg->slot_cfg.slot_mode == I2S_SLOT_MODE_MONO) ? 1 : 2;
#else
                return ESP_CODEC_DEV_NOT_SUPPORT;
#endif  /* SOC_I2S_SUPPORTS_PDM_RX */
            } else {
#if SOC_I2S_SUPPORTS_PDM_TX
                const i2s_pdm_tx_config_t *cfg = (const i2s_pdm_tx_config_t *)info.mode_cfg;
                fs->sample_rate = cfg->clk_cfg.sample_rate_hz;
                fs->mclk_multiple = cfg->clk_cfg.mclk_multiple;
                fs->bits_per_sample = (uint8_t)cfg->slot_cfg.data_bit_width;
                fs->channel = (cfg->slot_cfg.slot_mode == I2S_SLOT_MODE_MONO) ? 1 : 2;
#if SOC_I2S_HW_VERSION_1
                fs->channel_mask = (uint16_t)cfg->slot_cfg.slot_mask;
#else
                fs->channel_mask = (cfg->slot_cfg.slot_mode == I2S_SLOT_MODE_MONO) ? 0x01 : 0x03;
#endif  /* SOC_I2S_HW_VERSION_1 */
#else
                return ESP_CODEC_DEV_NOT_SUPPORT;
#endif  /* SOC_I2S_SUPPORTS_PDM_TX */
            }
            break;
        }
#endif  /* SOC_I2S_SUPPORTS_PDM */
        default:
            return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    return ESP_CODEC_DEV_OK;
}
