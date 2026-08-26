/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_bit_defs.h"
#include "esp_log.h"

#include "esp_codec_dev.h"
#include "audio_codec_if.h"
#include "audio_codec_data_if.h"
#include "audio_codec_sw_vol.h"
#include "codec_dev_map.h"
#include "codec_dev_mirror.h"
#include "codec_dev_priv.h"
#include "codec_dev_layout.h"
#include "audio_codec_hw_proc.h"
#include "audio_hw_base_priv.h"

#define VOL_TRANSITION_TIME             (50)
#define ESP_CODEC_DEV_ALL_CHANNEL_MASK  (0x0F)

static const char *TAG = "ADEV_CODEC";

const audio_codec_if_t *esp_audio_hw_proc_get_codec_if(esp_codec_dev_handle_t handle)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    return dev ? dev->codec_if : NULL;
}

static inline bool _verify_codec_ready(codec_dev_t *dev)
{
    if (dev->codec_if && dev->codec_if->hw_base.is_open) {
        if (dev->codec_if->hw_base.is_open(&dev->codec_if->hw_base) == false) {
            return false;
        }
    }
    return true;
}

static bool _verify_drv_ready(codec_dev_t *dev, bool playback)
{
    if (_verify_codec_ready(dev) == false) {
        ESP_LOGE(TAG, "Codec is not open");
        return false;
    }
    if (dev->data_if->is_open && dev->data_if->is_open(dev->data_if) == false) {
        ESP_LOGE(TAG, "Codec data interface is not open");
        return false;
    }
    if (playback && dev->data_if->write == NULL) {
        ESP_LOGE(TAG, "Data interface write callback is required");
        return false;
    }
    if (playback == false && dev->data_if->read == NULL) {
        ESP_LOGE(TAG, "Data interface read callback is required");
        return false;
    }
    return true;
}

static int _verify_codec_setting(codec_dev_t *dev, bool playback)
{
    if ((playback && (dev->dev_caps & ESP_CODEC_DEV_TYPE_OUT) == 0) ||
        (!playback && (dev->dev_caps & ESP_CODEC_DEV_TYPE_IN) == 0)) {
        ESP_LOGE(TAG, "Codec does not support %s mode", playback ? "output" : "input");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    if (_verify_codec_ready(dev) == false) {
        ESP_LOGE(TAG, "Codec is not open");
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    return ESP_CODEC_DEV_OK;
}

static int _get_default_vol_curve(esp_codec_dev_vol_curve_t *curve)
{
    curve->vol_map = (esp_codec_dev_vol_map_t *)malloc(2 * sizeof(esp_codec_dev_vol_map_t));
    if (curve->vol_map) {
        curve->count = 2;
        curve->vol_map[0].vol = 0;
        curve->vol_map[0].db_value = -50.0;
        curve->vol_map[1].vol = 100;
        curve->vol_map[1].db_value = 0.0;
    }
    return ESP_CODEC_DEV_OK;
}

static float _get_vol_db(esp_codec_dev_vol_curve_t *curve, int vol)
{
    if (vol == 0) {
        return -96.0;
    }
    int n = curve->count;
    if (n == 0) {
        return 0.0;
    }
    if (vol >= curve->vol_map[n - 1].vol) {
        return curve->vol_map[n - 1].db_value;
    }
    for (int i = 0; i < n - 1; i++) {
        if (vol < curve->vol_map[i + 1].vol) {
            if (curve->vol_map[i].vol != curve->vol_map[i + 1].vol) {
                float ratio = (curve->vol_map[i + 1].db_value - curve->vol_map[i].db_value) /
                              (curve->vol_map[i + 1].vol - curve->vol_map[i].vol);
                return curve->vol_map[i].db_value + (vol - curve->vol_map[i].vol) * ratio;
            }
            break;
        }
    }
    return 0.0;
}

static bool _verify_fs_para(esp_codec_dev_sample_info_t *fs)
{
    if (fs == NULL) {
        ESP_LOGE(TAG, "Sample info is NULL");
        return false;
    }
    if (fs->channel == 0 || fs->channel > ESP_CODEC_DEV_MAX_BUS_SLOT || fs->channel_mask > (1U << fs->channel) - 1U) {
        ESP_LOGE(TAG, "Unsupported channel count: %d", fs->channel);
        return false;
    }
    if (fs->sample_rate < 8000 || fs->sample_rate > 192000) {
        ESP_LOGE(TAG, "Unsupported sample rate: %d", (int)fs->sample_rate);
        return false;
    }
    if (!(fs->bits_per_sample == 8 || fs->bits_per_sample == 16 ||
          fs->bits_per_sample == 24 || fs->bits_per_sample == 32)) {
        ESP_LOGE(TAG, "Unsupported bits per sample: %d", fs->bits_per_sample);
        return false;
    }

    uint16_t channel_mask = fs->channel_mask;
    uint16_t mclk_multiple = fs->mclk_multiple;
    if (mclk_multiple == 0) {
        ESP_LOGW(TAG, "MCLK multiple is not set, default to %d", 256);
        mclk_multiple = 256;  // default mclk multiple
    }
    if (channel_mask == 0) {
        channel_mask = (uint16_t)((1U << fs->channel) - 1U);
        ESP_LOGW(TAG, "Channel mask is not set, default to 0x%x", channel_mask);
    }
    if (fs->bits_per_sample == 24 && (mclk_multiple % 3) != 0) {
        ESP_LOGW(TAG, "Adjust MCLK multiple from %d to %d", mclk_multiple, 384);
        mclk_multiple = 384;
    }
    fs->channel_mask = channel_mask;
    fs->mclk_multiple = mclk_multiple;
    return true;
}

void codec_dev_apply_vol_mute(codec_dev_t *dev)
{
    esp_codec_dev_handle_t h = (esp_codec_dev_handle_t)dev;
    if (dev->output_opened) {
        esp_codec_dev_set_out_vol(h, dev->volume);
        esp_codec_dev_set_out_mute(h, dev->muted);
    }
    if (dev->input_opened) {
        esp_codec_dev_set_in_gain(h, dev->mic_gain);
        esp_codec_dev_set_in_mute(h, dev->mic_muted);
    }
}

esp_codec_dev_handle_t esp_codec_dev_new(esp_codec_dev_cfg_t *cfg)
{
    if (cfg == NULL || cfg->data_if == NULL || cfg->dev_type == ESP_CODEC_DEV_TYPE_NONE) {
        ESP_LOGE(TAG, "Invalid codec device configuration");
        return NULL;
    }
    codec_dev_t *dev = (codec_dev_t *)calloc(1, sizeof(codec_dev_t));
    if (dev == NULL) {
        ESP_LOGE(TAG, "Failed to allocate codec device");
        return NULL;
    }
    dev->dev_caps = cfg->dev_type;
    dev->codec_if = cfg->codec_if;
    dev->data_if = cfg->data_if;
    if (cfg->dev_type & ESP_CODEC_DEV_TYPE_OUT) {
        _get_default_vol_curve(&dev->vol_curve);
    }
    dev->disable_when_closed = true;
    dev->order_row_count = CODEC_DEV_ORDER_TABLE_UNLOADED;
    return (esp_codec_dev_handle_t)dev;
}

int esp_codec_dev_open(esp_codec_dev_handle_t handle, esp_codec_dev_sample_info_t *fs)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL || fs == NULL) {
        ESP_LOGE(TAG, "Invalid handle or sample info");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    esp_codec_dev_sample_info_t verified_fs = *fs;
    if (_verify_fs_para(&verified_fs) == false) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    // dev_caps is a bitmask, so mask it to keep the index within the table
    const char *codec_name[] = {"None", "Input", "Output", "Input and Output"};
    const char *dev_dir = codec_name[dev->dev_caps & ESP_CODEC_DEV_TYPE_IN_OUT];
    ESP_LOGI(TAG, "%s: use sample format ch_num=%d, ch_mask=0x%x, bits=%d, mclk_multiple=%d", dev_dir,
             verified_fs.channel, verified_fs.channel_mask, verified_fs.bits_per_sample, verified_fs.mclk_multiple);
    if (dev->input_opened || dev->output_opened) {
        ESP_LOGI(TAG, "Codec device is already open");
        return ESP_CODEC_DEV_OK;
    }
    bool data_if_enabled = false;
    bool adc_enabled = false;
    bool dac_enabled = false;
    int ret = ESP_CODEC_DEV_OK;
    if ((dev->dev_caps & ESP_CODEC_DEV_TYPE_IN)) {
        if (_verify_drv_ready(dev, false) == false) {
            ESP_LOGE(TAG, "Codec does not support input");
        } else {
            dev->input_opened = true;
        }
    }
    if ((dev->dev_caps & ESP_CODEC_DEV_TYPE_OUT)) {
        if (_verify_drv_ready(dev, true) == false) {
            ESP_LOGE(TAG, "Codec does not support output");
        } else {
            dev->output_opened = true;
        }
    }
    if (dev->input_opened == false && dev->output_opened == false) {
        ESP_LOGE(TAG, "Failed to open codec device because the driver is not ready");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    const audio_codec_if_t *codec = dev->codec_if;
    const audio_codec_data_if_t *data_if = dev->data_if;
    codec_dev_layout_plan_t layout = {0};
    ret = codec_dev_layout_prepare_open(dev, &verified_fs, &layout);
    if (ret != ESP_CODEC_DEV_OK) {
        goto open_cleanup;
    }
    if (dev->output_opened) {
        if (codec == NULL || codec->dac_if == NULL || codec->dac_if->ops.set_vol == NULL) {
            if (dev->sw_vol == NULL) {
                dev->sw_vol = audio_codec_new_sw_vol();
                if (dev->sw_vol == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate software volume");
                    ret = ESP_CODEC_DEV_NO_MEM;
                    goto open_cleanup;
                }
                dev->sw_vol_alloced = true;
            }
        }
        if (dev->sw_vol) {
            dev->sw_vol->open(dev->sw_vol, &verified_fs, VOL_TRANSITION_TIME);
        }
    }
    if (data_if->set_fmt) {
        esp_codec_dev_sample_info_t data_fs = verified_fs;
        ret = data_if->set_fmt(data_if, dev->dev_caps, &data_fs);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Failed to set data interface format, ret=0x%x", ret);
            goto open_cleanup;
        }
    }
    if (data_if->enable) {
        ret = data_if->enable(data_if, dev->dev_caps, true);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Failed to enable data interface, ret=0x%x", ret);
            goto open_cleanup;
        }
        data_if_enabled = true;
    }
    if (codec) {
        if (codec->hw_base.set_fs) {
            ret = codec->hw_base.set_fs(&codec->hw_base, &verified_fs, dev->dev_caps);
            if (ret != 0) {
                ret = ESP_CODEC_DEV_NOT_SUPPORT;
                ESP_LOGE(TAG, "Failed to set codec format, ret=0x%x", ret);
                goto open_cleanup;
            }
        }
        if (dev->input_opened && codec->adc_if && codec->adc_if->ops.enable) {
            if (codec->adc_if->ops.enable(codec, true) != ESP_CODEC_DEV_OK) {
                ESP_LOGE(TAG, "Failed to enable ADC, ret=0x%x", ret);
                ret = ESP_CODEC_DEV_DRV_ERR;
                goto open_cleanup;
            }
            adc_enabled = true;
        }
        if (dev->output_opened && codec->dac_if && codec->dac_if->ops.enable) {
            if (codec->dac_if->ops.enable(codec, true) != ESP_CODEC_DEV_OK) {
                ESP_LOGE(TAG, "Failed to enable DAC, ret=0x%x", ret);
                ret = ESP_CODEC_DEV_DRV_ERR;
                goto open_cleanup;
            }
            dac_enabled = true;
        }
    }
    codec_dev_layout_commit_open_map(dev, &layout);
    codec_dev_apply_vol_mute(dev);
    dev->fs = verified_fs;
    if (dev->cur_map.value == 0 && data_if->get_mode) {
        esp_codec_dev_i2s_mode_t in_mode = ESP_CODEC_DEV_I2S_MODE_NONE;
        esp_codec_dev_i2s_mode_t out_mode = ESP_CODEC_DEV_I2S_MODE_NONE;
        if (data_if->get_mode(data_if, &in_mode, &out_mode) == ESP_CODEC_DEV_OK) {
            esp_codec_dev_i2s_mode_t cur_mode = (dev->dev_caps == ESP_CODEC_DEV_TYPE_OUT) ? out_mode : in_mode;
            esp_codec_dev_channel_map_t cur_map = {0};
            codec_dev_layout_resolve_map_from_fs(dev, &verified_fs, cur_mode, &cur_map);
            dev->cur_map = cur_map;
        }
    }
    ESP_LOGI(TAG, "Opened %s codec device, current map is 0x%lX", dev_dir, (unsigned long)dev->cur_map.value);
    return ESP_CODEC_DEV_OK;

open_cleanup:
    // set_fmt must establish stable bus clocks before configuring a BCLK-dependent codec. If a
    // later codec operation fails and no precise shrink rollback exists, disable this direction and
    // unregister its map queries; an already-open peer may remain on the known-valid widened bus.
    if (codec) {
        if (dac_enabled && codec->dac_if && codec->dac_if->ops.enable) {
            codec->dac_if->ops.enable(codec, false);
        }
        if (adc_enabled && codec->adc_if && codec->adc_if->ops.enable) {
            codec->adc_if->ops.enable(codec, false);
        }
    }
    if (data_if_enabled && data_if->enable) {
        data_if->enable(data_if, dev->dev_caps, false);
    }
    if (dev->sw_vol && dev->output_opened) {
        dev->sw_vol->close(dev->sw_vol);
    }
    codec_dev_layout_clear_map_query(dev, ESP_CODEC_DEV_TYPE_OUT);
    codec_dev_layout_clear_map_query(dev, ESP_CODEC_DEV_TYPE_IN);
    dev->input_opened = false;
    dev->output_opened = false;
    dev->set_map.value = 0;
    dev->cur_map.value = 0;
    ESP_LOGE(TAG, "Failed to open codec device, ret=0x%x", ret);
    return ret;
}

int esp_codec_dev_get_caps(esp_codec_dev_handle_t handle, esp_codec_dev_capability_t *caps, int *count)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL || count == NULL || *count < 0) {
        ESP_LOGE(TAG, "Invalid handle or count");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (dev->codec_if && dev->codec_if->hw_base.get_caps) {
        int ret = dev->codec_if->hw_base.get_caps(&dev->codec_if->hw_base, dev->dev_caps, caps, count);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Failed to get codec capabilities, ret=0x%x", ret);
        }
        return ret;
    }
    ESP_LOGE(TAG, "Codec capability query is not supported");
    return ESP_CODEC_DEV_NOT_SUPPORT;
}

int esp_codec_dev_set_data_layout(esp_codec_dev_handle_t handle, const esp_codec_dev_channel_map_t *map)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL || map == NULL || codec_dev_map_is_valid(map) == false) {
        ESP_LOGE(TAG, "Invalid handle or data layout map");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (dev->input_opened == false && dev->output_opened == false) {
        ESP_LOGE(TAG, "Codec device is not open");
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    if (dev->data_if == NULL || dev->data_if->get_mode == NULL) {
        ESP_LOGE(TAG, "Data interface does not support mode query");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    esp_codec_dev_i2s_mode_t in_mode, out_mode;
    if (dev->data_if->get_mode(dev->data_if, &in_mode, &out_mode) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to get data interface mode");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    esp_codec_dev_type_t dir = (dev->dev_caps == ESP_CODEC_DEV_TYPE_OUT) ? ESP_CODEC_DEV_TYPE_OUT : ESP_CODEC_DEV_TYPE_IN;
    esp_codec_dev_i2s_mode_t cur_mode = dir == ESP_CODEC_DEV_TYPE_OUT ? out_mode : in_mode;
    const char *dir_str = dir == ESP_CODEC_DEV_TYPE_OUT ? "Output" : "Input";
    uint16_t ch_mask = dev->fs.channel_mask;
    uint8_t ch_num = dev->fs.channel;
    esp_codec_dev_channel_map_t cur_map = dev->cur_map;
    if (cur_map.value == 0) {
        int ret = codec_dev_layout_resolve_map_from_fs(dev, &dev->fs, cur_mode, &cur_map);
        if (ret != ESP_CODEC_DEV_OK || cur_map.value == 0) {
            ESP_LOGE(TAG, "[%s] Current data layout is unknown; check the current format or call set_data_layout after open", dir_str);
            return ESP_CODEC_DEV_NOT_SUPPORT;
        }
    }
    ESP_LOGI(TAG, "[%s] Change data layout from 0x%lX to 0x%lX, ch_num=%d, ch_mask=0x%x, mode=%s",
             dir_str, (unsigned long)cur_map.value, (unsigned long)map->value,
             ch_num, ch_mask, codec_dev_i2s_mode_name(cur_mode));
    if (map->value == cur_map.value) {
        dev->set_map = *map;
        ESP_LOGI(TAG, "[%s] Data layout is already 0x%lX", dir_str, (unsigned long)cur_map.value);
        return ESP_CODEC_DEV_OK;
    }

    if (dir == ESP_CODEC_DEV_TYPE_IN) {
        bool need_modify = true;
        if (ch_num == 4 && cur_mode == ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS && cur_map.value == ESP_CODEC_DEV_STD_4CH_MAP) {
            need_modify = false;
            ESP_LOGI(TAG, "[%s] Skip hardware reconfigure for default 4-channel STD map, current map=0x%lX, requested map=0x%lX",
                     dir_str, (unsigned long)cur_map.value, (unsigned long)map->value);
        }
        if (need_modify) {
            int hw_ret = codec_dev_layout_reconfigure_hw(dev, map);
            if (hw_ret == ESP_CODEC_DEV_OK) {
                ESP_LOGI(TAG, "[%s] Applied requested layout by reconfiguring hardware, current map=0x%lX",
                         dir_str, (unsigned long)dev->cur_map.value);
                return ESP_CODEC_DEV_OK;
            }
            ESP_LOGI(TAG, "[%s] Hardware reconfigure failed, use software layout conversion instead, ret=0x%x, current map=0x%lX, requested map=0x%lX",
                     dir_str, hw_ret, (unsigned long)cur_map.value, (unsigned long)map->value);
        }
    }

    const esp_codec_dev_channel_map_t *superset_map = &cur_map;
    const esp_codec_dev_channel_map_t *subset_map = map;
    if (dir == ESP_CODEC_DEV_TYPE_OUT) {
        superset_map = map;
        subset_map = &cur_map;
    }
    if (codec_dev_map_contains(superset_map, subset_map)) {
        ESP_LOGI(TAG, "[%s] Requested layout 0x%lX can be converted from current map 0x%lX",
                 dir_str, (unsigned long)map->value, (unsigned long)cur_map.value);
    } else {
        ESP_LOGW(TAG, "[%s] Requested layout 0x%lX is not supported by current map 0x%lX",
                 dir_str, (unsigned long)map->value, (unsigned long)cur_map.value);
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }

    dev->set_map = *map;
    dev->cur_map = cur_map;
    if (cur_map.value != map->value) {
        ESP_LOGI(TAG, "[%s] Use software layout conversion from 0x%lX to 0x%lX",
                 dir_str, (unsigned long)cur_map.value, (unsigned long)map->value);
    }
    return ESP_CODEC_DEV_OK;
}

int esp_codec_dev_get_data_layout(esp_codec_dev_handle_t handle, esp_codec_dev_channel_map_t *map)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL || map == NULL) {
        ESP_LOGE(TAG, "Invalid handle or map");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (dev->input_opened || dev->output_opened) {
        if (dev->data_if == NULL || dev->data_if->get_mode == NULL) {
            ESP_LOGE(TAG, "Data interface does not support mode query");
            return ESP_CODEC_DEV_NOT_SUPPORT;
        }
        esp_codec_dev_i2s_mode_t in_mode, out_mode;
        if (dev->data_if->get_mode(dev->data_if, &in_mode, &out_mode) != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Failed to get data interface mode");
            return ESP_CODEC_DEV_NOT_SUPPORT;
        }
        esp_codec_dev_type_t dir = ESP_CODEC_DEV_TYPE_IN;
        if (dev->dev_caps == ESP_CODEC_DEV_TYPE_OUT) {
            dir = ESP_CODEC_DEV_TYPE_OUT;
        }
        esp_codec_dev_i2s_mode_t cur_mode = (dir == ESP_CODEC_DEV_TYPE_OUT) ? out_mode : in_mode;
        bool need_convert = false;
        codec_dev_layout_get_maps(dev, NULL, NULL, &need_convert);
        if (!need_convert && codec_dev_layout_can_use_map_query(dev, dir, cur_mode)) {
            int ret = codec_dev_layout_resolve_map_from_bus(dev, dir, map, NULL);
            if (ret == ESP_CODEC_DEV_OK) {
                dev->cur_map = *map;
                return ESP_CODEC_DEV_OK;
            }
            ESP_LOGE(TAG, "Failed to derive live bus layout, ret=0x%x", ret);
            return ret;
        }
        if (codec_dev_layout_get_app_map(dev, map) == ESP_CODEC_DEV_OK) {
            return ESP_CODEC_DEV_OK;
        }
        int ret = codec_dev_layout_resolve_map_from_fs(dev, &dev->fs, cur_mode, map);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Failed to resolve data layout from sample format, ret=0x%x", ret);
            return ret;
        }
        return ESP_CODEC_DEV_OK;
    }

    // If the codec device is not opened yet, try to resolve the layout from the current data interface format.
    if (dev->data_if == NULL || dev->data_if->get_fmt == NULL || dev->data_if->get_mode == NULL) {
        ESP_LOGE(TAG, "Data interface does not support format or mode query");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    esp_codec_dev_sample_info_t fs = {0};
    int ret = dev->data_if->get_fmt(dev->data_if, dev->dev_caps, &fs);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to get data interface format, ret=0x%x", ret);
        return ret;
    }
    esp_codec_dev_i2s_mode_t in_mode, out_mode;
    if (dev->data_if->get_mode(dev->data_if, &in_mode, &out_mode) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to get data interface mode");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    esp_codec_dev_i2s_mode_t cur_mode = dev->dev_caps == ESP_CODEC_DEV_TYPE_OUT ? out_mode : in_mode;
    if (cur_mode == ESP_CODEC_DEV_I2S_MODE_NONE) {
        ESP_LOGE(TAG, "Data interface mode is none");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    ret = codec_dev_layout_resolve_map_from_fs(dev, &fs, cur_mode, map);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to resolve data layout from sample format, ret=0x%x", ret);
        return ret;
    }
    return ESP_CODEC_DEV_OK;
}

int esp_codec_dev_set_data_layout_label(esp_codec_dev_handle_t handle, const char *label)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL || label == NULL) {
        ESP_LOGE(TAG, "Invalid handle or label");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if ((dev->dev_caps & ESP_CODEC_DEV_TYPE_IN) == 0) {
        ESP_LOGE(TAG, "Codec does not support input mode");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    const char *adc_label = NULL;
    if (dev->codec_if == NULL) {
        ESP_LOGE(TAG, "Failed to get ADC label, ret=0x%x", ESP_CODEC_DEV_NOT_SUPPORT);
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    int ret = audio_hw_get_adc_label(&dev->codec_if->hw_base, &adc_label);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to get ADC label, ret=0x%x", ret);
        return ret;
    }
    esp_codec_dev_channel_map_t map = {0};
    ret = codec_dev_map_from_labels(adc_label, label, &map);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to convert label to data layout map, ret=0x%x", ret);
        return ret;
    }
    return esp_codec_dev_set_data_layout(handle, &map);
}

int esp_codec_dev_get_data_layout_label(esp_codec_dev_handle_t handle, char *label, int label_size)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL || label == NULL || label_size <= 0) {
        ESP_LOGE(TAG, "Invalid handle, label, or label_size");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if ((dev->dev_caps & ESP_CODEC_DEV_TYPE_IN) == 0) {
        ESP_LOGE(TAG, "Codec does not support input mode");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    const char *adc_label = NULL;
    if (dev->codec_if == NULL) {
        ESP_LOGE(TAG, "Failed to get ADC label, ret=0x%x", ESP_CODEC_DEV_NOT_SUPPORT);
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    int ret = audio_hw_get_adc_label(&dev->codec_if->hw_base, &adc_label);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to get ADC label, ret=0x%x", ret);
        return ret;
    }
    esp_codec_dev_channel_map_t map = {0};
    ret = esp_codec_dev_get_data_layout(handle, &map);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to get data layout, ret=0x%x", ret);
        return ret;
    }
    ret = codec_dev_map_to_labels(adc_label, &map, label, label_size);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to convert data layout map to label, ret=0x%x", ret);
    }
    return ret;
}

int esp_codec_dev_read(esp_codec_dev_handle_t handle, void *data, int len)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Invalid handle, data, or length");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (dev->data_if == NULL || dev->data_if->read == NULL) {
        ESP_LOGE(TAG, "Data interface read is not supported");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    if (dev->input_opened == false) {
        ESP_LOGE(TAG, "Input is not open");
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    return codec_dev_layout_read(dev, data, len);
}

int esp_codec_dev_mirror_cfg(esp_codec_dev_handle_t handle, int size)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL || size <= 0) {
        ESP_LOGE(TAG, "Invalid arguments for mirror config");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if ((dev->dev_caps & ESP_CODEC_DEV_TYPE_IN) == 0) {
        ESP_LOGW(TAG, "Mirror config is not supported because the device does not support input mode");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    if (dev->mirror) {
        return ESP_CODEC_DEV_OK;
    }
    int ret = codec_dev_mirror_init(size, &dev->mirror);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to initialize mirror buffer, ret=0x%x", ret);
    }
    return ret;
}

int esp_codec_dev_mirror_read(esp_codec_dev_handle_t handle,
                              uint8_t *buffer,
                              int size,
                              int timeout_ms,
                              int *bytes_read)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL || buffer == NULL || size <= 0 || bytes_read == NULL) {
        ESP_LOGE(TAG, "Invalid arguments for mirror read");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *bytes_read = 0;
    if ((dev->dev_caps & ESP_CODEC_DEV_TYPE_IN) == 0) {
        ESP_LOGW(TAG, "Mirror read is not supported because the device does not support input mode");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    if (dev->mirror == NULL) {
        ESP_LOGW(TAG, "Mirror read is not available because the mirror is not initialized");
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = codec_dev_mirror_read(dev->mirror, buffer, size, timeout_ms, bytes_read);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to read mirror data, ret=0x%x", ret);
    }
    return ret;
}

int esp_codec_dev_write(esp_codec_dev_handle_t handle, void *data, int len)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Invalid handle, data, or length");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (dev->output_opened == false) {
        ESP_LOGE(TAG, "Output is not open");
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    return codec_dev_layout_write(dev, data, len);
}

int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t handle, int volume)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = _verify_codec_setting(dev, true);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    const audio_codec_if_t *codec = dev->codec_if;
    float db_value = _get_vol_db(&dev->vol_curve, volume);
    // Prefer to use software volume setting
    if (dev->sw_vol) {
        dev->sw_vol->set_vol(dev->sw_vol, db_value);
        dev->volume = volume;
        return ESP_CODEC_DEV_OK;
    }
    if (codec && codec->dac_if && codec->dac_if->ops.set_vol) {
        ret = codec->dac_if->ops.set_vol(codec, ESP_CODEC_DEV_ALL_CHANNEL_MASK, db_value);
        if (ret == ESP_CODEC_DEV_OK) {
            dev->volume = volume;
        } else {
            ESP_LOGE(TAG, "Failed to set output volume, ret=0x%x", ret);
        }
        return ret;
    }
    ESP_LOGE(TAG, "Output volume control is not supported");
    return ESP_CODEC_DEV_NOT_SUPPORT;
}

int esp_codec_dev_get_out_vol(esp_codec_dev_handle_t handle, int *volume)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL || volume == NULL) {
        ESP_LOGE(TAG, "Invalid handle or volume pointer");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = _verify_codec_setting(dev, true);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    *volume = dev->volume;
    return ESP_CODEC_DEV_OK;
}

int esp_codec_dev_set_vol_handler(esp_codec_dev_handle_t handle, const audio_codec_vol_if_t *vol_handler)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL || vol_handler == NULL) {
        ESP_LOGE(TAG, "Invalid handle or volume handler");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = _verify_codec_setting(dev, true);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    if (dev->sw_vol == vol_handler) {
        return ESP_CODEC_DEV_OK;
    }
    if (dev->sw_vol) {
        if (dev->sw_vol_alloced) {
            audio_codec_delete_vol_if(dev->sw_vol);
            dev->sw_vol_alloced = false;
        }
    }
    dev->sw_vol = vol_handler;
    return ESP_CODEC_DEV_OK;
}

int esp_codec_dev_set_vol_curve(esp_codec_dev_handle_t handle, esp_codec_dev_vol_curve_t *curve)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL || curve == NULL || curve->vol_map == NULL) {
        ESP_LOGE(TAG, "Invalid handle or volume curve");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = _verify_codec_setting(dev, true);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    int size = curve->count * sizeof(esp_codec_dev_vol_map_t);
    esp_codec_dev_vol_map_t *new_map = (esp_codec_dev_vol_map_t *)realloc(dev->vol_curve.vol_map, size);
    if (new_map == NULL) {
        curve->count = 0;
        ESP_LOGE(TAG, "Failed to allocate volume curve");
        return ESP_CODEC_DEV_NO_MEM;
    }
    dev->vol_curve.vol_map = new_map;
    memcpy(dev->vol_curve.vol_map, curve->vol_map, size);
    dev->vol_curve.count = curve->count;
    return ESP_CODEC_DEV_OK;
}

int esp_codec_dev_set_out_mute(esp_codec_dev_handle_t handle, bool mute)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = _verify_codec_setting(dev, true);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    const audio_codec_if_t *codec = dev->codec_if;
    if (codec && codec->dac_if && codec->dac_if->ops.mute) {
        ret = codec->dac_if->ops.mute(codec, ESP_CODEC_DEV_ALL_CHANNEL_MASK, mute);
        if (ret == ESP_CODEC_DEV_OK) {
            dev->muted = mute;
        } else {
            ESP_LOGE(TAG, "Failed to set output mute, ret=0x%x", ret);
        }
        return ret;
    }
    // When codec not support mute set volume instead
    if (dev->sw_vol) {
        float db_value = mute ? -100.0 : _get_vol_db(&dev->vol_curve, dev->volume);
        dev->sw_vol->set_vol(dev->sw_vol, db_value);
        dev->muted = mute;
        return ESP_CODEC_DEV_OK;
    }
    ESP_LOGE(TAG, "Output mute control is not supported");
    return ESP_CODEC_DEV_NOT_SUPPORT;
}

int esp_codec_dev_get_out_mute(esp_codec_dev_handle_t handle, bool *muted)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL || muted == NULL) {
        ESP_LOGE(TAG, "Invalid handle or mute state pointer");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = _verify_codec_setting(dev, true);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    *muted = dev->muted;
    return ESP_CODEC_DEV_OK;
}

int esp_codec_dev_set_in_gain(esp_codec_dev_handle_t handle, float db)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = _verify_codec_setting(dev, false);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    const audio_codec_if_t *codec = dev->codec_if;
    if (codec && codec->adc_if && codec->adc_if->ops.set_vol) {
        ret = codec->adc_if->ops.set_vol(codec, ESP_CODEC_DEV_ALL_CHANNEL_MASK, db);
        if (ret == ESP_CODEC_DEV_OK) {
            dev->mic_gain = db;
        } else {
            ESP_LOGE(TAG, "Failed to set input gain, ret=0x%x", ret);
        }
        return ret;
    }
    ESP_LOGE(TAG, "Input gain control is not supported");
    return ESP_CODEC_DEV_NOT_SUPPORT;
}

int esp_codec_dev_set_in_channel_gain(esp_codec_dev_handle_t handle, uint16_t channel_mask, float db)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL || channel_mask == 0) {
        ESP_LOGE(TAG, "Invalid handle or channel mask");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = _verify_codec_setting(dev, false);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    const audio_codec_if_t *codec = dev->codec_if;
    if (codec && codec->adc_if && codec->adc_if->ops.set_vol) {
        ret = codec->adc_if->ops.set_vol(codec, channel_mask, db);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Failed to set input channel gain, ret=0x%x", ret);
        }
        return ret;
    }
    ESP_LOGE(TAG, "Per-channel input gain control is not supported");
    return ESP_CODEC_DEV_NOT_SUPPORT;
}

int esp_codec_dev_get_in_gain(esp_codec_dev_handle_t handle, float *db_value)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL || db_value == NULL) {
        ESP_LOGE(TAG, "Invalid handle or gain pointer");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = _verify_codec_setting(dev, false);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    *db_value = dev->mic_gain;
    return ESP_CODEC_DEV_OK;
}

int esp_codec_dev_set_in_mute(esp_codec_dev_handle_t handle, bool mute)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = _verify_codec_setting(dev, false);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    const audio_codec_if_t *codec = dev->codec_if;
    if (codec && codec->adc_if && codec->adc_if->ops.mute) {
        ret = codec->adc_if->ops.mute(codec, ESP_CODEC_DEV_ALL_CHANNEL_MASK, mute);
        if (ret == ESP_CODEC_DEV_OK) {
            dev->mic_muted = mute;
        } else {
            ESP_LOGE(TAG, "Failed to set input mute, ret=0x%x", ret);
        }
        return ret;
    }
    ESP_LOGE(TAG, "Input mute control is not supported");
    return ESP_CODEC_DEV_NOT_SUPPORT;
}

int esp_codec_dev_get_in_mute(esp_codec_dev_handle_t handle, bool *muted)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL || muted == NULL) {
        ESP_LOGE(TAG, "Invalid handle or mute state pointer");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = _verify_codec_setting(dev, false);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    *muted = dev->mic_muted;
    return ESP_CODEC_DEV_OK;
}

int esp_codec_dev_set_disable_when_closed(esp_codec_dev_handle_t handle, bool disable)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    dev->disable_when_closed = disable;
    return ESP_CODEC_DEV_OK;
}

int esp_codec_dev_read_reg(esp_codec_dev_handle_t handle, int reg, int *val)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL || val == NULL) {
        ESP_LOGE(TAG, "Invalid handle or register value pointer");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (dev->codec_if && dev->codec_if->hw_base.get_reg) {
        int ret = dev->codec_if->hw_base.get_reg(&dev->codec_if->hw_base, reg, val);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Failed to read register 0x%x, ret=0x%x", reg, ret);
        }
        return ret;
    }
    ESP_LOGE(TAG, "Codec register read is not supported");
    return ESP_CODEC_DEV_NOT_SUPPORT;
}

int esp_codec_dev_write_reg(esp_codec_dev_handle_t handle, int reg, int val)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (dev->codec_if && dev->codec_if->hw_base.set_reg) {
        int ret = dev->codec_if->hw_base.set_reg(&dev->codec_if->hw_base, reg, val);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Failed to write register 0x%x, ret=0x%x", reg, ret);
        }
        return ret;
    }
    ESP_LOGE(TAG, "Codec register write is not supported");
    return ESP_CODEC_DEV_NOT_SUPPORT;
}

int esp_codec_dev_dump_reg(esp_codec_dev_handle_t handle)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (dev->codec_if && dev->codec_if->hw_base.dump_reg) {
        dev->codec_if->hw_base.dump_reg(&dev->codec_if->hw_base);
        return ESP_CODEC_DEV_OK;
    }
    ESP_LOGE(TAG, "Codec register dump is not supported");
    return ESP_CODEC_DEV_NOT_SUPPORT;
}

int esp_codec_dev_close(esp_codec_dev_handle_t handle)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (dev->output_opened == false && dev->input_opened == false) {
        goto cleanup;
    }
    const audio_codec_if_t *codec = dev->codec_if;
    if (dev->disable_when_closed && codec) {
        // Disable ADC if input was opened
        if (dev->input_opened && codec->adc_if && codec->adc_if->ops.enable) {
            codec->adc_if->ops.enable(codec, false);
        }
        // Disable DAC if output was opened
        if (dev->output_opened && codec->dac_if && codec->dac_if->ops.enable) {
            codec->dac_if->ops.enable(codec, false);
        }
    }
    const audio_codec_data_if_t *data_if = dev->data_if;
    if (data_if->enable) {
        data_if->enable(data_if, dev->dev_caps, false);
    }
    codec_dev_layout_clear_map_query(dev, ESP_CODEC_DEV_TYPE_OUT);
    codec_dev_layout_clear_map_query(dev, ESP_CODEC_DEV_TYPE_IN);
    if (dev->sw_vol) {
        dev->sw_vol->close(dev->sw_vol);
    }
    dev->output_opened = dev->input_opened = false;
    dev->set_map.value = 0;
    dev->cur_map.value = 0;

cleanup:
    if (dev->mirror) {
        codec_dev_mirror_deinit(dev->mirror);
        dev->mirror = NULL;
    }
    return ESP_CODEC_DEV_OK;
}

void esp_codec_dev_delete(esp_codec_dev_handle_t handle)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev) {
        esp_codec_dev_close(handle);
        if (dev->vol_curve.vol_map) {
            free(dev->vol_curve.vol_map);
        }
        // Only delete software vol when alloced internally
        if (dev->sw_vol && dev->sw_vol_alloced) {
            audio_codec_delete_vol_if(dev->sw_vol);
        }
        free(dev);
    }
}

const char *esp_codec_dev_get_version(void)
{
    return ESP_CODEC_DEV_VERSION;
}
