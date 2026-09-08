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

static int my_codec_vad_init(const audio_hw_base_t *h, const esp_audio_hw_vad_cfg_t *cfg)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h);
    if (proc == NULL || cfg == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->vad_cfg = *cfg;
    proc->vad_initialized = true;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_vad_enable(const audio_hw_base_t *h, bool enable, bool output_enable)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h);
    if (proc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->vad_enabled = enable;
    proc->vad_output_enabled = enable && output_enable;
    proc->vad_has_speech = enable;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_vad_reset(const audio_hw_base_t *h)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h);
    if (proc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->vad_has_speech = false;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_vad_get_status(const audio_hw_base_t *h, esp_audio_hw_vad_status_t *status)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h);
    if (proc == NULL || status == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    status->has_speech = proc->vad_has_speech;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_vad_set_event_cb(const audio_hw_base_t *h, esp_audio_hw_vad_event_cb_t cb, void *arg)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h);
    if (proc == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    proc->vad_event_cb = cb;
    proc->vad_event_arg = arg;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_vad_get_frame_info(const audio_hw_base_t *h, esp_audio_hw_vad_frame_info_t *info)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h);
    if (proc == NULL || info == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    info->sample_rate = MY_CODEC_VAD_SAMPLE_RATE;
    info->bits_per_sample = MY_CODEC_VAD_BITS;
    info->channel_num = MY_CODEC_VAD_CHANNELS;
    info->frame_bytes = MY_CODEC_VAD_FRAME_BYTES;
    return ESP_CODEC_DEV_OK;
}

static int my_codec_vad_read_frame(const audio_hw_base_t *h, uint8_t *buf, size_t len, size_t *read_len)
{
    my_codec_proc_state_t *proc = my_codec_get_mutable_proc_state(h);
    if (proc == NULL || buf == NULL || read_len == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *read_len = 0;
    /* Validate the buffer before the output state, so a caller that gets both wrong still
     * learns about the buffer. This matches the order the generic layer uses. */
    if (len < MY_CODEC_VAD_FRAME_BYTES) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (!proc->vad_output_enabled) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    /* Deterministic ramp so a test can tell a full frame from a partially filled buffer */
    for (size_t i = 0; i < MY_CODEC_VAD_FRAME_BYTES; i++) {
        buf[i] = (uint8_t)i;
    }
    *read_len = MY_CODEC_VAD_FRAME_BYTES;
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

static const esp_audio_hw_vad_t my_codec_vad_ops = {
    .init           = my_codec_vad_init,
    .enable         = my_codec_vad_enable,
    .reset          = my_codec_vad_reset,
    .get_status     = my_codec_vad_get_status,
    .set_event_cb   = my_codec_vad_set_event_cb,
    .get_frame_info = my_codec_vad_get_frame_info,
    .read_frame     = my_codec_vad_read_frame,
};

const esp_audio_hw_proc_ops_t my_codec_hw_proc = {
    .alc  = &my_codec_alc_ops,
    .drc  = &my_codec_drc_ops,
    .eq   = &my_codec_eq_ops,
    .line = &my_codec_line_ops,
    .mute = &my_codec_mute_ops,
    .vad  = &my_codec_vad_ops,
};
