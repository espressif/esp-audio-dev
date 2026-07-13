/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>

#include "esp_log.h"

#include "my_codec.h"
#include "audio_hw_proc_if.h"

static const char *TAG = "MY_CODEC_PROC";

static inline bool my_codec_hw_is_open(const audio_hw_base_t *h)
{
    return h != NULL && h->is_open != NULL && h->is_open(h);
}

static int my_codec_alc_init(const audio_hw_alc_t *h, const audio_alc_cfg_t *cfg)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h->base);
    if (proc == NULL || cfg == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->alc_min_gain = cfg->min_gain;
    proc->alc_max_gain = cfg->max_gain;
    proc->alc_target_gain = cfg->target_gain;
    proc->alc_noise_gate = cfg->noise_gate_threshold;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_set_alc_gain(const audio_hw_alc_t *h, float target_gain)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h->base);
    if (proc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->alc_target_gain = target_gain;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_set_alc_channel(const audio_hw_alc_t *h, int channel_mask)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h->base);
    if (proc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->alc_channel_mask = channel_mask;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_set_alc_noise_gate(const audio_hw_alc_t *h, float threshold)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h->base);
    if (proc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->alc_noise_gate = threshold;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_drc_init(const audio_hw_drc_t *h, const audio_drc_cfg_t *cfg)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h->base);
    if (proc == NULL || cfg == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->drc_min_gain = cfg->min_gain;
    proc->drc_max_gain = cfg->max_gain;
    proc->drc_offset_gain = cfg->offset_gain;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_set_drc_offset_gain(const audio_hw_drc_t *h, float gain)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h->base);
    if (proc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->drc_offset_gain = gain;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_drc_enable(const audio_hw_drc_t *h, bool enable)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h->base);
    if (proc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->drc_enabled = enable;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_eq_set_cfg(const audio_hw_eq_t *h, const audio_eq_cfg_t *cfg)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h->base);
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

static int my_codec_eq_set_para(const audio_hw_eq_t *h, const eq_para_t *para, int index)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h->base);
    if (proc == NULL || para == NULL || index < 0 || index >= MY_CODEC_EQ_BAND_MAX) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->eq_band[index] = *para;
    if (proc->eq_filter_num <= index) {
        proc->eq_filter_num = index + 1;
    }
    return ESP_CODEC_DEV_OK;
}

static int my_codec_eq_enable(const audio_hw_eq_t *h, bool enable)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h->base);
    if (proc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->eq_enabled = enable;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_eq_dump_info(const audio_hw_eq_t *h)
{
    if (h == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    return ESP_CODEC_DEV_OK;
}

static int my_codec_line_in(const audio_hw_line_t *h, bool enable)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h->base);
    if (proc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->line_in_enabled = enable;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_line_out(const audio_hw_line_t *h, bool enable)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h->base);
    if (proc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->line_out_enabled = enable;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_set_auto_mute_cfg(const audio_hw_mute_t *h, const auto_mute_cfg_t *cfg)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h->base);
    if (proc == NULL || cfg == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->auto_mute_noise_gate = cfg->noise_gate;
    proc->auto_mute_vol = cfg->mute_vol;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_auto_mute(const audio_hw_mute_t *h, bool enable)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h->base);
    if (proc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->auto_mute_enabled = enable;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_set_soft_mute_cfg(const audio_hw_mute_t *h, const soft_mute_cfg_t *cfg)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h->base);
    if (proc == NULL || cfg == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->soft_mute_ramp_rate = cfg->ramp_rate;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_soft_mute(const audio_hw_mute_t *h, bool enable)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h->base);
    if (proc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->soft_mute_enabled = enable;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_alc_new(const audio_hw_base_t *h, audio_hw_alc_handle_t *alc)
{
    if (h == NULL || alc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (*alc != NULL) {
        return ESP_CODEC_DEV_OK;
    }
    if (my_codec_hw_is_open(h) == false) {
        ESP_LOGE(TAG, "Create ALC failed: codec is not open");
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    audio_hw_alc_t *alc_handle = calloc(1, sizeof(audio_hw_alc_t));
    if (alc_handle == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }
    alc_handle->base = h;
    alc_handle->init = my_codec_alc_init;
    alc_handle->set_gain = my_codec_set_alc_gain;
    alc_handle->set_channel = my_codec_set_alc_channel;
    alc_handle->set_noise_gate = my_codec_set_alc_noise_gate;
    *alc = alc_handle;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_drc_new(const audio_hw_base_t *h, audio_hw_drc_handle_t *drc)
{
    if (h == NULL || drc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (*drc != NULL) {
        return ESP_CODEC_DEV_OK;
    }
    if (my_codec_hw_is_open(h) == false) {
        ESP_LOGE(TAG, "Create DRC failed: codec is not open");
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    audio_hw_drc_t *drc_handle = calloc(1, sizeof(audio_hw_drc_t));
    if (drc_handle == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }
    drc_handle->base = h;
    drc_handle->init = my_codec_drc_init;
    drc_handle->set_offset_gain = my_codec_set_drc_offset_gain;
    drc_handle->enable = my_codec_drc_enable;
    *drc = drc_handle;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_eq_new(const audio_hw_base_t *h, audio_hw_eq_handle_t *eq)
{
    if (h == NULL || eq == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (*eq != NULL) {
        return ESP_CODEC_DEV_OK;
    }
    if (my_codec_hw_is_open(h) == false) {
        ESP_LOGE(TAG, "Create EQ failed: codec is not open");
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    audio_hw_eq_t *eq_handle = calloc(1, sizeof(audio_hw_eq_t));
    if (eq_handle == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }
    eq_handle->base = h;
    eq_handle->set_band_para = my_codec_eq_set_para;
    eq_handle->set_cfg = my_codec_eq_set_cfg;
    eq_handle->enable = my_codec_eq_enable;
    eq_handle->dump_info = my_codec_eq_dump_info;
    *eq = eq_handle;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_line_new(const audio_hw_base_t *h, audio_hw_line_handle_t *line)
{
    if (h == NULL || line == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (*line != NULL) {
        return ESP_CODEC_DEV_OK;
    }
    if (my_codec_hw_is_open(h) == false) {
        ESP_LOGE(TAG, "Create line failed: codec is not open");
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    audio_hw_line_t *line_handle = calloc(1, sizeof(audio_hw_line_t));
    if (line_handle == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }
    line_handle->base = h;
    line_handle->enable_in = my_codec_line_in;
    line_handle->enable_out = my_codec_line_out;
    *line = line_handle;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_mute_new(const audio_hw_base_t *h, audio_hw_mute_handle_t *mute)
{
    if (h == NULL || mute == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (*mute != NULL) {
        return ESP_CODEC_DEV_OK;
    }
    if (my_codec_hw_is_open(h) == false) {
        ESP_LOGE(TAG, "Create mute failed: codec is not open");
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    audio_hw_mute_t *mute_handle = calloc(1, sizeof(audio_hw_mute_t));
    if (mute_handle == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }
    mute_handle->base = h;
    mute_handle->set_auto_mute_cfg = my_codec_set_auto_mute_cfg;
    mute_handle->enable_auto_mute = my_codec_auto_mute;
    mute_handle->set_soft_mute_cfg = my_codec_set_soft_mute_cfg;
    mute_handle->enable_soft_mute = my_codec_soft_mute;
    *mute = mute_handle;
    return ESP_CODEC_DEV_OK;
}

const audio_codec_hw_proc_ops_t my_codec_hw_proc = {
    .alc_new  = my_codec_alc_new,
    .drc_new  = my_codec_drc_new,
    .eq_new   = my_codec_eq_new,
    .line_new = my_codec_line_new,
    .mute_new = my_codec_mute_new,
};
