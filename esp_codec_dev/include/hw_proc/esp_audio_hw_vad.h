/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_codec_dev.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Default VAD configuration
 */
#define ESP_AUDIO_HW_VAD_CFG_DEFAULT()  {  \
    .sample_rate      = 48000,             \
    .bits_per_sample  = 16,                \
    .channel_num      = 2,                 \
    .init_frame_num   = 20,                \
    .min_energy       = 4000,              \
    .hangover_silent  = 30,                \
    .hangover_speech  = 3,                 \
    .max_speech_count = 100,               \
    .min_speech_count = 1,                 \
    .skip_band_energy = false,             \
    .gain_db          = 0.0f,              \
    .data_out_enable  = true,              \
}

/**
 * @brief  VAD hardware audio processing configuration
 *
 * @note  Every field below that is counted in frames refers to the VAD analysis frame, the block of
 *        audio the detector scores at a time. Its duration is fixed by the codec and follows
 *        neither `sample_rate`, which describes the main ADC, nor the read unit that
 *        esp_audio_hw_vad_get_frame_info() reports, which may cover several analysis frames. Take
 *        the analysis frame period from the codec documentation to turn any count below into a
 *        duration, and treat recommended counts as belonging to the codec that published them.
 */
typedef struct {
    uint32_t  sample_rate;       /*!< Main ADC/system sample rate in Hz */
    uint8_t   bits_per_sample;   /*!< Main ADC sample width in bits */
    uint8_t   channel_num;       /*!< Hardware channel encoding: capture channel count minus one */
    uint16_t  init_frame_num;    /*!< Length of the initialization phase, in analysis frames of
                                      codec-specific duration. The codec learns the ambient noise
                                      during this phase, so keep the device in the target noise
                                      environment while it runs. A longer phase suppresses noise
                                      better and lowers the false trigger rate, but speech occurring
                                      inside it is missed. See the codec documentation for the
                                      recommended length */
    uint32_t  min_energy;        /*!< Energy below which a frame is rejected without further
                                      analysis, used to ignore low-energy activity such as distant
                                      speech and so cut false wakeups and power. The value reaches
                                      the hardware unchanged, so how the codec defines frame energy
                                      and what scale this threshold uses are codec-specific rather
                                      than a fixed unit such as dB. See the codec documentation for
                                      the usable range */
    uint8_t   hangover_silent;   /*!< Silence hangover, in analysis frames of codec-specific duration */
    uint8_t   hangover_speech;   /*!< Speech hangover, in analysis frames of codec-specific duration */
    uint8_t   max_speech_count;  /*!< Maximum speech count, in analysis frames of codec-specific duration */
    uint8_t   min_speech_count;  /*!< Minimum speech count, in analysis frames of codec-specific duration */
    bool      skip_band_energy;  /*!< Disable the band energy check, which tests whether the
                                      passband holds a large enough share of the total spectral
                                      energy. Leaving the check enabled (false) can lower the false
                                      trigger rate in some environments while raising the miss rate
                                      in others, so choose it against the environment the device
                                      runs in */
    float     gain_db;           /*!< VAD filter volume gain in dB */
    bool      data_out_enable;   /*!< Enable VAD filtered-audio output FIFO */
} esp_audio_hw_vad_cfg_t;

/**
 * @brief  VAD detection status
 */
typedef struct {
    bool  has_speech;  /*!< Voice activity currently detected */
} esp_audio_hw_vad_status_t;

/**
 * @brief  Format of the VAD filtered-audio output
 *
 * @note  This describes the VAD filter output only. It is a different data format from the main
 *        ADC capture format, so it must not be derived from the `sample_rate`, `bits_per_sample`
 *        and `channel_num` fields of `esp_audio_hw_vad_cfg_t`, which describe the ADC input.
 */
typedef struct {
    uint32_t  sample_rate;      /*!< Output sample rate in Hz */
    uint8_t   bits_per_sample;  /*!< Output sample width in bits */
    uint8_t   channel_num;      /*!< Output channel count; 1 for mono */
    size_t    frame_bytes;      /*!< Exact number of bytes one esp_audio_hw_vad_read_frame() returns */
} esp_audio_hw_vad_frame_info_t;

/**
 * @brief  VAD event callback, invoked when the detected speech state changes
 *
 *         The callback runs in the driver's own event context, usually a task woken by the codec
 *         interrupt, and the hw_proc lock is not held while it runs.
 *
 * @note  Every esp_audio_hw_vad_*() API serializes on a single global hw_proc lock, so calling
 *        one from this callback contends with any other thread using hw_proc. In particular
 *        esp_audio_hw_vad_set_event_cb() holds that lock while it unregisters, so a callback that
 *        is already running can block for up to the lock timeout and then fail with
 *        ESP_CODEC_DEV_TIMEOUT. Keep the callback short and prefer signalling a task that does
 *        the VAD calls, rather than calling VAD APIs here.
 *
 * @param[in]  has_speech  True when voice activity is detected
 * @param[in]  arg         User argument given to esp_audio_hw_vad_set_event_cb()
 */
typedef void (*esp_audio_hw_vad_event_cb_t)(bool has_speech, void *arg);

/**
 * @brief  Configure VAD hardware audio processing
 *
 * @note  This API is not ISR-safe and may block on the hw_proc lock.
 *
 * @param[in]  dev  Codec device handle
 * @param[in]  cfg  VAD configuration; caller retains ownership
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  dev or cfg is NULL
 *       - ESP_CODEC_DEV_NOT_SUPPORT  The codec does not support VAD
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open
 *       - ESP_CODEC_DEV_NO_MEM       hw_proc lock creation failed
 *       - ESP_CODEC_DEV_TIMEOUT      hw_proc lock acquisition timed out
 */
int esp_audio_hw_vad_init(esp_codec_dev_handle_t dev, const esp_audio_hw_vad_cfg_t *cfg);

/**
 * @brief  Start or stop VAD detection
 *
 * @param[in]  dev            Codec device handle
 * @param[in]  enable         True to start, false to stop
 * @param[in]  output_enable  True to enable filtered-audio output
 *
 * @return
 *       - See  esp_audio_hw_vad_init()
 */
int esp_audio_hw_vad_enable(esp_codec_dev_handle_t dev, bool enable, bool output_enable);

/**
 * @brief  Reset VAD state
 *
 * @param[in]  dev  Codec device handle
 *
 * @return
 *       - See  esp_audio_hw_vad_init()
 */
int esp_audio_hw_vad_reset(esp_codec_dev_handle_t dev);

/**
 * @brief  Poll current VAD status
 *
 * @param[in]   dev     Codec device handle
 * @param[out]  status  VAD status
 *
 * @return
 *       - See  esp_audio_hw_vad_init()
 */
int esp_audio_hw_vad_get_status(esp_codec_dev_handle_t dev, esp_audio_hw_vad_status_t *status);

/**
 * @brief  Register or unregister a VAD activity callback
 *
 * @note  The hw_proc lock is held while the callback is registered or unregistered, so a callback
 *        that is already running and calls a VAD API blocks until this returns. See
 *        esp_audio_hw_vad_event_cb_t.
 *
 * @param[in]  dev  Codec device handle
 * @param[in]  cb   Callback, or NULL to unregister
 * @param[in]  arg  User argument passed to cb
 *
 * @return
 *       - See  esp_audio_hw_vad_init()
 */
int esp_audio_hw_vad_set_event_cb(esp_codec_dev_handle_t dev, esp_audio_hw_vad_event_cb_t cb, void *arg);

/**
 * @brief  Get the format of the VAD filtered-audio output
 *
 *         Query this before allocating the buffer for esp_audio_hw_vad_read_frame(), which needs
 *         a capacity of at least `frame_bytes`.
 *
 * @note  The returned format is guaranteed to be valid once esp_audio_hw_vad_init() has
 *        succeeded. A codec whose output format is fixed in hardware may also report it before
 *        init, but portable callers should not rely on that.
 *
 * @param[in]   dev   Codec device handle
 * @param[out]  info  VAD filtered-audio output format
 *
 * @return
 *       - See  esp_audio_hw_vad_init()
 */
int esp_audio_hw_vad_get_frame_info(esp_codec_dev_handle_t dev, esp_audio_hw_vad_frame_info_t *info);

/**
 * @brief  Read one filtered-audio frame from the VAD FIFO
 *
 *         A read is all-or-nothing: it either writes exactly the `frame_bytes` reported by
 *         esp_audio_hw_vad_get_frame_info() or writes nothing. Partial frames are never returned,
 *         because draining only part of a hardware FIFO would leave the remaining data misaligned.
 *
 * @note  Filtered-audio output must be enabled first, through `data_out_enable` of
 *        esp_audio_hw_vad_cfg_t or `output_enable` of esp_audio_hw_vad_enable(). Reading while it
 *        is disabled returns ESP_CODEC_DEV_WRONG_STATE.
 *
 * @note  This call does not wait for voice activity or for the FIFO to fill. Call it after a VAD
 *        event callback or once esp_audio_hw_vad_get_status() reports activity. It is not
 *        ISR-safe and may block on the hw_proc lock and on the codec control bus.
 *
 * @param[in]   dev       Codec device handle
 * @param[out]  buf       Destination buffer
 * @param[in]   len       Capacity of buf in bytes; must be at least `frame_bytes`
 * @param[out]  read_len  Bytes written to buf; set to `frame_bytes` on success and to 0 on
 *                        every error return, so it never carries a required-capacity hint
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  dev, buf or read_len is NULL, or len is below `frame_bytes`
 *       - ESP_CODEC_DEV_WRONG_STATE  The codec is not open, or filtered-audio output is disabled
 *       - ESP_CODEC_DEV_READ_FAIL    The codec control bus transfer failed
 *       - Other                      See esp_audio_hw_vad_init()
 */
int esp_audio_hw_vad_read_frame(esp_codec_dev_handle_t dev, uint8_t *buf, size_t len, size_t *read_len);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
