/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "my_codec.h"
#include "esp_audio_hw_proc_if.h"

static int my_codec_alc_init(const audio_hw_base_t *h, const esp_audio_hw_alc_cfg_t *cfg)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h);
    if (proc == NULL || cfg == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->alc_min_gain = cfg->min_gain;
    proc->alc_max_gain = cfg->max_gain;
    proc->alc_target_gain = cfg->target_gain;
    proc->alc_noise_gate = cfg->noise_gate_threshold;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_set_alc_gain(const audio_hw_base_t *h, float target_gain)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h);
    if (proc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->alc_target_gain = target_gain;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_set_alc_channel(const audio_hw_base_t *h, int channel_mask)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h);
    if (proc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->alc_channel_mask = channel_mask;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_set_alc_noise_gate(const audio_hw_base_t *h, float threshold)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h);
    if (proc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->alc_noise_gate = threshold;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_drc_init(const audio_hw_base_t *h, const esp_audio_hw_drc_cfg_t *cfg)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h);
    if (proc == NULL || cfg == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->drc_min_gain = cfg->min_gain;
    proc->drc_max_gain = cfg->max_gain;
    proc->drc_offset_gain = cfg->offset_gain;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_set_drc_offset_gain(const audio_hw_base_t *h, float gain)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h);
    if (proc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->drc_offset_gain = gain;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_drc_enable(const audio_hw_base_t *h, bool enable)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h);
    if (proc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->drc_enabled = enable;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_eq_set_cfg(const audio_hw_base_t *h, const esp_audio_hw_eq_cfg_t *cfg)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h);
    if (proc == NULL || cfg == NULL || cfg->para == NULL || cfg->filter_num <= 0) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int count = cfg->filter_num;
    if (count > MY_CODEC_EQ_BAND_MAX) {
        count = MY_CODEC_EQ_BAND_MAX;
    }
    proc->eq_filter_num = count;
    for (int i = 0; i < count; i++) {
        proc->eq_band[i] = cfg->para[i];
    }
    return ESP_CODEC_DEV_OK;
}

static int my_codec_eq_set_para(const audio_hw_base_t *h, const esp_audio_hw_eq_para_t *para, int index)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h);
    if (proc == NULL || para == NULL || index < 0 || index >= MY_CODEC_EQ_BAND_MAX) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->eq_band[index] = *para;
    if (proc->eq_filter_num <= index) {
        proc->eq_filter_num = index + 1;
    }
    return ESP_CODEC_DEV_OK;
}

static int my_codec_eq_enable(const audio_hw_base_t *h, bool enable)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h);
    if (proc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->eq_enabled = enable;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_eq_dump_info(const audio_hw_base_t *h)
{
    (void)h;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_line_in(const audio_hw_base_t *h, bool enable)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h);
    if (proc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->line_in_enabled = enable;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_line_out(const audio_hw_base_t *h, bool enable)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h);
    if (proc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->line_out_enabled = enable;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_set_auto_mute_cfg(const audio_hw_base_t *h, const esp_audio_hw_auto_mute_cfg_t *cfg)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h);
    if (proc == NULL || cfg == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->auto_mute_noise_gate = cfg->noise_gate;
    proc->auto_mute_vol = cfg->mute_vol;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_auto_mute(const audio_hw_base_t *h, bool enable)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h);
    if (proc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->auto_mute_enabled = enable;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_set_soft_mute_cfg(const audio_hw_base_t *h, const esp_audio_hw_soft_mute_cfg_t *cfg)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h);
    if (proc == NULL || cfg == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->soft_mute_ramp_rate = cfg->ramp_rate;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_soft_mute(const audio_hw_base_t *h, bool enable)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h);
    if (proc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->soft_mute_enabled = enable;
    return ESP_CODEC_DEV_OK;
}

static const esp_audio_hw_alc_t my_codec_alc_ops = {
    .init = my_codec_alc_init,
    .set_gain = my_codec_set_alc_gain,
    .set_channel = my_codec_set_alc_channel,
    .set_noise_gate = my_codec_set_alc_noise_gate,
};

static const esp_audio_hw_drc_t my_codec_drc_ops = {
    .init = my_codec_drc_init,
    .set_offset_gain = my_codec_set_drc_offset_gain,
    .enable = my_codec_drc_enable,
};

static const esp_audio_hw_eq_t my_codec_eq_ops = {
    .set_band_para = my_codec_eq_set_para,
    .set_cfg = my_codec_eq_set_cfg,
    .enable = my_codec_eq_enable,
    .dump_info = my_codec_eq_dump_info,
};

static const esp_audio_hw_line_t my_codec_line_ops = {
    .enable_in = my_codec_line_in,
    .enable_out = my_codec_line_out,
};

static const esp_audio_hw_mute_t my_codec_mute_ops = {
    .set_auto_mute_cfg = my_codec_set_auto_mute_cfg,
    .enable_auto_mute = my_codec_auto_mute,
    .set_soft_mute_cfg = my_codec_set_soft_mute_cfg,
    .enable_soft_mute = my_codec_soft_mute,
};

const esp_audio_hw_proc_ops_t my_codec_hw_proc = {
    .alc  = &my_codec_alc_ops,
    .drc  = &my_codec_drc_ops,
    .eq   = &my_codec_eq_ops,
    .line = &my_codec_line_ops,
    .mute = &my_codec_mute_ops,
};
