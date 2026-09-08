/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2s_common.h"
#include "esp_log.h"

#include "audio_codec_data_if.h"
#if CONFIG_IDF_TARGET_ESP32
#include "codec_dev_data_cvt.h"
#endif  /* CONFIG_IDF_TARGET_ESP32 */
#include "audio_codec_data_i2s_priv.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_os.h"

static const char *TAG = "I2S_IF";

#define DISABLED_CHAN_SLEEP_MS  (10)

/**
 * @brief  Lock a live instance's port group, using the list lock only to look the group up
 *
 * @note  On success the caller holds the group lock and must call _i2s_data_unlock(). The list
 *         lock is not held across the returned critical section.
 */
static int _i2s_data_lock(i2s_data_t *i2s_data)
{
    int ret = i2s_data_port_list_lock();
    if (ret != ESP_CODEC_DEV_OK) {
        /* i2s_data_port_list_lock already logged */
        return ret;
    }
    i2s_port_group_t *group = i2s_data->port_group;
    if (group == NULL || i2s_data->closing) {
        i2s_data_port_list_unlock();
        ESP_LOGE(TAG, "Lock I2S interface failed: port group is unavailable");
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    ret = i2s_data_port_group_lock(group);
    i2s_data_port_list_unlock();
    return ret;
}

static void _i2s_data_unlock(i2s_data_t *i2s_data)
{
    i2s_data_port_group_unlock(i2s_data->port_group);
}

static bool _i2s_data_is_open(const audio_codec_data_if_t *h)
{
    i2s_data_t *i2s_data = (i2s_data_t *)h;
    if (i2s_data) {
        return i2s_data->is_open;
    }
    return false;
}

static int _i2s_data_open(const audio_codec_data_if_t *h, void *data_cfg, int cfg_size)
{
    i2s_data_t *i2s_data = (i2s_data_t *)h;
    if (h == NULL || data_cfg == NULL || cfg_size != sizeof(audio_codec_i2s_cfg_t)) {
        ESP_LOGE(TAG, "Open I2S data interface failed: invalid argument");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    // A second open would push the instance onto the group list again, which links its next pointer to
    // itself and makes every later walk of the list loop forever.
    if (i2s_data->is_open || i2s_data->port_group != NULL) {
        ESP_LOGE(TAG, "I2S data interface %p is already open on port %d", i2s_data, i2s_data->port);
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    audio_codec_i2s_cfg_t *i2s_cfg = (audio_codec_i2s_cfg_t *)data_cfg;
    i2s_data->port = i2s_cfg->port;
    i2s_data->tx.handle = i2s_cfg->tx_handle;
    i2s_data->rx.handle = i2s_cfg->rx_handle;
    i2s_data->clk_src = (i2s_clock_src_t)i2s_cfg->clk_src;
    i2s_data->tx.auto_paired = false;
    i2s_data->rx.auto_paired = false;
    i2s_data->closing = false;
    i2s_data->is_open = false;

    i2s_port_group_t *port_group = i2s_data_port_group_acquire(i2s_data->port);
    if (port_group == NULL) {
        ESP_LOGE(TAG, "Open I2S data interface failed: acquire port group returned NULL");
        return ESP_CODEC_DEV_NO_MEM;
    }
    int ret = i2s_data_port_list_lock();
    if (ret != ESP_CODEC_DEV_OK) {
        /* i2s_data_port_list_lock already logged */
        i2s_data_port_group_release(port_group);
        return ret;
    }
    ret = i2s_data_port_group_lock(port_group);
    if (ret != ESP_CODEC_DEV_OK) {
        /* i2s_data_port_group_lock already logged */
        goto release;
    }
    ret = i2s_data_port_group_add_instance(port_group, i2s_data);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Open I2S data interface failed: add instance returned %d", ret);
        i2s_data_port_group_unlock(port_group);
        goto release;
    }
    if (i2s_data->rx.handle && i2s_data->tx.handle == NULL && !_is_master(i2s_data->rx.handle)) {
        i2s_data_t *peer_data = i2s_data_port_group_find_peer(port_group, i2s_data, false);
        if (!peer_data) {
            // If use duplex mode, tx.handle is the pair of rx.handle, otherwise tx.handle is NULL
            i2s_data->tx.handle = _get_peer_channel(i2s_data->rx.handle);
            i2s_data->tx.auto_paired = i2s_data->tx.handle != NULL;
            ESP_LOGI(TAG, "Auto-paired output handle %p from input handle %p in duplex mode", i2s_data->tx.handle, i2s_data->rx.handle);
        }
    }
    // If RX is master, only open TX, then set fs for TX will not work, so get RX peer handle from TX
    if (i2s_data->tx.handle && i2s_data->rx.handle == NULL && !_is_master(i2s_data->tx.handle)) {
        i2s_data_t *peer_data = i2s_data_port_group_find_peer(port_group, i2s_data, true);
        if (!peer_data) {
            // If use duplex mode, rx.handle is the pair of tx.handle, otherwise rx.handle is NULL
            i2s_data->rx.handle = _get_peer_channel(i2s_data->tx.handle);
            i2s_data->rx.auto_paired = i2s_data->rx.handle != NULL;
            ESP_LOGI(TAG, "Auto-paired input handle %p from output handle %p in duplex mode", i2s_data->rx.handle, i2s_data->tx.handle);
        }
    }
    i2s_data_port_group_refresh_capability_index(port_group);
    i2s_data->port_group = port_group;
    i2s_data->is_open = true;
    i2s_data_port_group_unlock(port_group);
    i2s_data_port_list_unlock();
    ESP_LOGD(TAG, "I2S data handles: rx=%p, tx=%p, port=%d, self=%p",
             i2s_data->rx.handle, i2s_data->tx.handle, i2s_data->port, i2s_data);
    return ESP_CODEC_DEV_OK;

release:
    i2s_data_port_group_release_locked(port_group);
    i2s_data_port_list_unlock();
    return ret;
}

static int _check_peer_and_disable_data(i2s_data_t *i2s_data, i2s_data_t *peer_data, bool is_playback)
{
    i2s_stream_state_t *peer_stream = _get_stream_state(peer_data, !is_playback);
    bool peer_pending = peer_stream->disable_pending;
    bool peer_should_run = _should_keep_running(peer_data, !is_playback);
    if (peer_pending || !peer_should_run) {
        ESP_LOGI(TAG, "Disable peer channel (%s) because disable is pending", is_playback ? "in" : "out");
        int ret = i2s_data_hw_enable(peer_data, !is_playback, false);
        if (ret != ESP_CODEC_DEV_OK) {
            return ret;
        }
        peer_data->tx.disable_pending = false;
        peer_data->rx.disable_pending = false;
    }
    return i2s_data_hw_enable(i2s_data, is_playback, false);
}

/**
 * 1. ESP_CODEC_DEV_TYPE_IN_OUT: enable/disable both channels directly
 * 2. enable:
 *    - clear pending flags and mark corresponding channel as explicitly requested
 *    - if peer is master, keep peer channel running while this stream needs it
 * 3. !enable && peer == NULL: disable corresponding channel directly
 * 4. !enable && peer != NULL:
 *    - (_is_master(cur_handle) || _is_i2s_hw_version_1()) && peer_should_run: pending disable master channel for slave channel running
 *    - check_peer_and_disable_cur_data
 */
static int _i2s_data_enable_locked(i2s_data_t *i2s_data, bool is_playback, bool enable)
{
    int ret = ESP_CODEC_DEV_OK;
    i2s_stream_state_t *active_stream = _get_stream_state(i2s_data, is_playback);
    if (enable) {
        // For enable i2s, clear pending flags and ensure any required peer master channel is running.
        active_stream->req_enable = true;
        _clear_pending(i2s_data, is_playback);
        ret = i2s_data_port_acquire_peer_ref(i2s_data, is_playback, true, NULL);
        if (ret != ESP_CODEC_DEV_OK) {
            active_stream->req_enable = false;
            return ret;
        }
        ret = i2s_data_hw_enable(i2s_data, is_playback, true);
        if (ret != ESP_CODEC_DEV_OK) {
            i2s_data_port_release_peer_ref(i2s_data, is_playback, true);
            active_stream->req_enable = false;
        }
    } else {
        active_stream->req_enable = false;
        ret = i2s_data_port_release_peer_ref(i2s_data, is_playback, false);
        if (ret != ESP_CODEC_DEV_OK) {
            return ret;
        }
        if (_should_keep_running(i2s_data, is_playback)) {
            _clear_pending(i2s_data, is_playback);
            return ESP_CODEC_DEV_OK;
        }
        i2s_data_t *peer_data = i2s_data_port_group_find_peer(i2s_data->port_group, i2s_data, is_playback);
        if (peer_data == NULL) {
            // If not have peer channel, disable current directly
            ret = i2s_data_hw_enable(i2s_data, is_playback, false);
        } else {
            bool peer_should_run = _should_keep_running(peer_data, !is_playback);
#if SOC_I2S_HW_VERSION_1
            // For ESP32 and ESP32S2, if any channel is enabled, other channel disable should be pending
            if (peer_should_run) {
                ESP_LOGI(TAG, "Pending %s channel disable on ESP32 and ESP32S2", is_playback ? "out" : "in");
                _set_pending(i2s_data, is_playback);
            } else {
                ret = _check_peer_and_disable_data(i2s_data, peer_data, is_playback);
            }
#else
            i2s_chan_handle_t cur_handle = _get_channel_handle(i2s_data, is_playback);
            if (_is_master(cur_handle)) {
                // Master disable should be blocked when slave is working
                if (peer_should_run) {
                    ESP_LOGI(TAG, "Pending master channel disable (%s) while the slave channel is running", is_playback ? "out" : "in");
                    _set_pending(i2s_data, is_playback);
                } else {
                    ret = _check_peer_and_disable_data(i2s_data, peer_data, is_playback);
                }
            } else {
                ret = _check_peer_and_disable_data(i2s_data, peer_data, is_playback);
            }
#endif  /* SOC_I2S_HW_VERSION_1 */
        }
    }
    return ret;
}

static int _i2s_data_enable(const audio_codec_data_if_t *h, esp_codec_dev_type_t dev_type, bool enable)
{
    i2s_data_t *i2s_data = (i2s_data_t *)h;
    if (i2s_data == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (!i2s_data->is_open) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = _i2s_data_lock(i2s_data);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    if (dev_type == ESP_CODEC_DEV_TYPE_IN_OUT) {
        int first_ret = _i2s_data_enable_locked(i2s_data, true, enable);
        int second_ret = _i2s_data_enable_locked(i2s_data, false, enable);
        ret = first_ret != ESP_CODEC_DEV_OK ? first_ret : second_ret;
    } else {
        bool is_playback = dev_type & ESP_CODEC_DEV_TYPE_OUT ? true : false;
        ret = _i2s_data_enable_locked(i2s_data, is_playback, enable);
    }
    _i2s_data_unlock(i2s_data);
    return ret;
}

static bool _validate_fs_param(const esp_codec_dev_sample_info_t *fs)
{
    if (fs == NULL) {
        return false;
    }
    if (fs->channel > ESP_CODEC_DEV_MAX_BUS_SLOT || fs->channel == 0) {
        ESP_LOGE(TAG, "Channel count %d is not supported", fs->channel);
        return false;
    }
    if (fs->channel_mask > (1U << fs->channel) - 1U) {
        ESP_LOGE(TAG, "Channel mask 0x%x is not supported", fs->channel_mask);
        return false;
    }
    if (!(fs->bits_per_sample == 8 || fs->bits_per_sample == 16
          || fs->bits_per_sample == 24 || fs->bits_per_sample == 32)
        || fs->bits_per_sample == 0) {
        ESP_LOGE(TAG, "Bits per sample %d is not supported", fs->bits_per_sample);
        return false;
    }
    if (fs->sample_rate > 192000 || fs->sample_rate < 8000) {
        ESP_LOGE(TAG, "Sample rate %" PRIu32 " is not supported", fs->sample_rate);
        return false;
    }
    if (fs->mclk_multiple == 0 || (fs->bits_per_sample == 24 && fs->mclk_multiple % 3 != 0)) {
        ESP_LOGE(TAG, "MCLK multiple %d is not supported", fs->mclk_multiple);
        return false;
    }
    return true;
}

static int _i2s_data_set_fmt(const audio_codec_data_if_t *h, esp_codec_dev_type_t dev_type, esp_codec_dev_sample_info_t *fs)
{
    i2s_data_t *i2s_data = (i2s_data_t *)h;
    if (i2s_data == NULL || fs == NULL) {
        ESP_LOGE(TAG, "Set I2S format failed: interface or format is NULL");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    esp_codec_dev_sample_info_t app_fs = *fs;
    if (!_validate_fs_param(&app_fs)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (!i2s_data->is_open) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = _i2s_data_lock(i2s_data);
    if (ret != ESP_CODEC_DEV_OK) {
        /* _i2s_data_lock already logged */
        return ret;
    }
    if (dev_type == ESP_CODEC_DEV_TYPE_IN_OUT) {
        ret = i2s_data_reconfig_apply_self_duplex(i2s_data, &app_fs);
        if (ret == ESP_CODEC_DEV_OK) {
            *fs = i2s_data->rx.app_fs;
        }
    } else {
        bool is_playback = (dev_type & ESP_CODEC_DEV_TYPE_OUT) != 0;
        ret = i2s_data_reconfig_apply_fs(i2s_data, is_playback, &app_fs);
        if (ret == ESP_CODEC_DEV_OK) {
            *fs = *_get_stream_app_fs(i2s_data, is_playback);
        }
    }
    _i2s_data_unlock(i2s_data);
    return ret;
}

static int _i2s_data_read(const audio_codec_data_if_t *h, uint8_t *data, int size)
{
    i2s_data_t *i2s_data = (i2s_data_t *)h;
    if (i2s_data == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (!i2s_data->is_open) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    size_t bytes_read = 0;
    i2s_chan_handle_t rx_chan = (i2s_chan_handle_t)i2s_data->rx.handle;
    if (rx_chan == NULL) {
        return ESP_CODEC_DEV_DRV_ERR;
    }
    if (!_is_enabled(rx_chan)) {
        memset(data, 0, size);
        ESP_LOGW(TAG, "Read requested from a disabled channel");
        esp_codec_dev_sleep(DISABLED_CHAN_SLEEP_MS);
        return ESP_CODEC_DEV_OK;
    }
    esp_err_t err = ESP_OK;
#if CONFIG_IDF_TARGET_ESP32
    if (i2s_data->rx.app_fs.channel_mask < 0x03 && i2s_data->rx.app_fs.bits_per_sample <= 24) {
        int read_len = 0;  // Actual number of bytes read by I2S driver.
        if (i2s_data->rx.app_fs.bits_per_sample == 16) {
            read_len = size;
        } else if (i2s_data->rx.app_fs.bits_per_sample == 8) {
            read_len = size * 2;
        } else if (i2s_data->rx.app_fs.bits_per_sample == 24) {
            read_len = size * 4 / 3;
        }
        uint8_t *read_data = malloc(read_len);
        if (read_data == NULL) {
            ESP_LOGE(TAG, "Failed to allocate the read buffer");
            return ESP_CODEC_DEV_NO_MEM;
        }
        err = i2s_channel_read(rx_chan, read_data, read_len, &bytes_read, DEFAULT_WAIT_TIMEOUT);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "I2S channel read failed, err=0x%x", (unsigned)err);
            free(read_data);
            return err == ESP_ERR_TIMEOUT ? ESP_CODEC_DEV_TIMEOUT : ESP_CODEC_DEV_DRV_ERR;
        }
        err = esp32_read_mono_fix(read_data, read_len, i2s_data->rx.app_fs.bits_per_sample, data, size);
        if (err != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "ESP32 mono read fix failed");
            free(read_data);
            return err;
        }
        free(read_data);
    } else {
        err = i2s_channel_read(rx_chan, data, size, &bytes_read, DEFAULT_WAIT_TIMEOUT);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "I2S channel read failed, err=0x%x", (unsigned)err);
            return err == ESP_ERR_TIMEOUT ? ESP_CODEC_DEV_TIMEOUT : ESP_CODEC_DEV_DRV_ERR;
        }
    }
#else
    err = i2s_channel_read(rx_chan, data, size, &bytes_read, DEFAULT_WAIT_TIMEOUT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S channel read failed, err=0x%x", (unsigned)err);
        return err == ESP_ERR_TIMEOUT ? ESP_CODEC_DEV_TIMEOUT : ESP_CODEC_DEV_DRV_ERR;
    }
#endif  /* CONFIG_IDF_TARGET_ESP32 */
    return ESP_CODEC_DEV_OK;
}

static int _i2s_data_write(const audio_codec_data_if_t *h, uint8_t *data, int size)
{
    i2s_data_t *i2s_data = (i2s_data_t *)h;
    if (i2s_data == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (!i2s_data->is_open) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    size_t bytes_written = 0;
    i2s_chan_handle_t tx_chan = (i2s_chan_handle_t)i2s_data->tx.handle;
    if (tx_chan == NULL) {
        return ESP_CODEC_DEV_DRV_ERR;
    }
    if (!_is_enabled(tx_chan)) {
        ESP_LOGW(TAG, "Write requested to a disabled channel");
        esp_codec_dev_sleep(DISABLED_CHAN_SLEEP_MS);
        return ESP_CODEC_DEV_OK;
    }
    esp_err_t err = ESP_OK;
#if CONFIG_IDF_TARGET_ESP32
    if (i2s_data->tx.app_fs.channel_mask < 0x03 && i2s_data->tx.app_fs.bits_per_sample <= 24) {
        int write_len = 0;
        uint8_t *write_data = NULL;
        int convert_ret = esp32_write_mono_fix(data, size, i2s_data->tx.app_fs.bits_per_sample,
                                               &write_data, &write_len);
        if (convert_ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "ESP32 mono write fix failed");
            return convert_ret;
        }
        err = i2s_channel_write(tx_chan, write_data, write_len, &bytes_written, DEFAULT_WAIT_TIMEOUT);
        if (write_data != data) {
            free(write_data);
        }
    } else {
        err = i2s_channel_write(tx_chan, data, size, &bytes_written, DEFAULT_WAIT_TIMEOUT);
    }
#else
    err = i2s_channel_write(tx_chan, data, size, &bytes_written, DEFAULT_WAIT_TIMEOUT);
#endif  /* CONFIG_IDF_TARGET_ESP32 */
    if (err == ESP_OK) {
        return ESP_CODEC_DEV_OK;
    }
    return err == ESP_ERR_TIMEOUT ? ESP_CODEC_DEV_TIMEOUT : ESP_CODEC_DEV_DRV_ERR;
}

static int _i2s_data_close(const audio_codec_data_if_t *h)
{
    i2s_data_t *i2s_data = (i2s_data_t *)h;
    if (i2s_data == NULL) {
        ESP_LOGE(TAG, "Close I2S data interface failed: interface is NULL");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = i2s_data_port_list_lock();
    if (ret != ESP_CODEC_DEV_OK) {
        /* i2s_data_port_list_lock already logged */
        return ret;
    }
    i2s_port_group_t *port_group = i2s_data->port_group;
    if (port_group == NULL || port_group->mutex == NULL) {
        i2s_data->rx = (i2s_stream_state_t) {0};
        i2s_data->tx = (i2s_stream_state_t) {0};
        i2s_data->is_open = false;
        i2s_data->closing = true;
        i2s_data_port_list_unlock();
        return ESP_CODEC_DEV_OK;
    }
    if (i2s_data->closing) {
        i2s_data_port_list_unlock();
        ESP_LOGE(TAG, "Close I2S data interface failed: close is already in progress");
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    i2s_data->closing = true;
    ret = i2s_data_port_group_lock(port_group);
    if (ret != ESP_CODEC_DEV_OK) {
        i2s_data->closing = false;
        i2s_data_port_list_unlock();
        /* i2s_data_port_group_lock already logged */
        return ret;
    }
    int tx_ret = i2s_data_port_release_peer_ref(i2s_data, true, false);
    int rx_ret = i2s_data_port_release_peer_ref(i2s_data, false, false);
    // Removing the instance also gives back the refs the other members hold on it, so do it before the
    // streams are cleared and while every link is still describing live state.
    i2s_data_port_group_remove_instance(port_group, i2s_data);
    i2s_data->rx = (i2s_stream_state_t) {0};
    i2s_data->tx = (i2s_stream_state_t) {0};
    i2s_data->is_open = false;
    i2s_data->port_group = NULL;
    i2s_data_port_group_unlock(port_group);
    i2s_data_port_group_release_locked(port_group);
    i2s_data_port_list_unlock();
    if (tx_ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Close I2S data interface: release TX peer reference failed, ret=%d", tx_ret);
        return tx_ret;
    }
    if (rx_ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Close I2S data interface: release RX peer reference failed, ret=%d", rx_ret);
        return rx_ret;
    }
    return ESP_CODEC_DEV_OK;
}

static int _i2s_data_get_fmt(const audio_codec_data_if_t *h, esp_codec_dev_type_t dev_type, esp_codec_dev_sample_info_t *fs)
{
    i2s_data_t *i2s_data = (i2s_data_t *)h;
    if (i2s_data == NULL || fs == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = _i2s_data_lock(i2s_data);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    bool is_playback = (dev_type & ESP_CODEC_DEV_TYPE_OUT) != 0;
    esp_codec_dev_sample_info_t *cached = _get_stream_app_fs(i2s_data, is_playback);
    if (cached->channel != 0) {
        memcpy(fs, cached, sizeof(*fs));
    } else {
        i2s_chan_handle_t channel = _get_channel_handle(i2s_data, is_playback);
        if (channel == NULL) {
            _i2s_data_unlock(i2s_data);
            ESP_LOGW(TAG, "Get I2S format: channel is not available");
            return ESP_CODEC_DEV_NOT_FOUND;
        }
        ret = i2s_data_hw_get_fs(channel, fs);
    }
    _i2s_data_unlock(i2s_data);
    return ret;
}

static int _i2s_data_set_map_query(const audio_codec_data_if_t *h, esp_codec_dev_type_t dev_type,
                                   const esp_codec_dev_map_query_t *query)
{
    i2s_data_t *i2s_data = (i2s_data_t *)h;
    if (i2s_data == NULL || (dev_type != ESP_CODEC_DEV_TYPE_IN && dev_type != ESP_CODEC_DEV_TYPE_OUT)) {
        ESP_LOGE(TAG, "Set map query failed: invalid interface or direction");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (query != NULL && query->resolve_cb == NULL) {
        ESP_LOGE(TAG, "Set map query failed: resolve callback is NULL");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = _i2s_data_lock(i2s_data);
    if (ret != ESP_CODEC_DEV_OK) {
        /* _i2s_data_lock already logged */
        return ret;
    }
    bool is_playback = dev_type == ESP_CODEC_DEV_TYPE_OUT;
    i2s_stream_state_t *stream = _get_stream_state(i2s_data, is_playback);
    if (query != NULL) {
        stream->map_query = *query;
    } else {
        memset(&stream->map_query, 0, sizeof(stream->map_query));
    }
    _i2s_data_unlock(i2s_data);
    return ESP_CODEC_DEV_OK;
}

static int _i2s_data_get_bus_info(const audio_codec_data_if_t *h, esp_codec_dev_type_t dev_type,
                                  esp_codec_dev_bus_info_t *bus_info)
{
    i2s_data_t *i2s_data = (i2s_data_t *)h;
    if (i2s_data == NULL || bus_info == NULL ||
        (dev_type != ESP_CODEC_DEV_TYPE_IN && dev_type != ESP_CODEC_DEV_TYPE_OUT)) {
        ESP_LOGE(TAG, "Get I2S bus information failed: invalid argument");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = _i2s_data_lock(i2s_data);
    if (ret != ESP_CODEC_DEV_OK) {
        /* _i2s_data_lock already logged */
        return ret;
    }
    bool is_playback = dev_type == ESP_CODEC_DEV_TYPE_OUT;
    i2s_stream_state_t *stream = _get_stream_state(i2s_data, is_playback);
    if (!_stream_has_bus_info(stream)) {
        _i2s_data_unlock(i2s_data);
        ESP_LOGE(TAG, "Get I2S bus information failed: stream has no committed bus");
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    *bus_info = stream->bus_info;
    _i2s_data_unlock(i2s_data);
    return ESP_CODEC_DEV_OK;
}

static int _i2s_data_get_mode(const audio_codec_data_if_t *h, esp_codec_dev_i2s_mode_t *in_mode, esp_codec_dev_i2s_mode_t *out_mode)
{
    i2s_data_t *i2s_data = (i2s_data_t *)h;
    if (i2s_data == NULL || (i2s_data->rx.handle == NULL && i2s_data->tx.handle == NULL)
        || in_mode == NULL || out_mode == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    *in_mode = ESP_CODEC_DEV_I2S_MODE_NONE;
    *out_mode = ESP_CODEC_DEV_I2S_MODE_NONE;
    if (i2s_data->rx.handle != NULL) {
        int ret = i2s_data_hw_get_mode(i2s_data->rx.handle, in_mode);
        if (ret != ESP_CODEC_DEV_OK) {
            return ret;
        }
        i2s_chan_handle_t peer_handle = NULL;
        if (i2s_data->tx.handle != NULL) {
            peer_handle = i2s_data->tx.handle;
        } else {
            peer_handle = _get_peer_channel(i2s_data->rx.handle);
        }
        if (peer_handle != NULL) {
            ret = i2s_data_hw_get_mode(peer_handle, out_mode);
            if (ret != ESP_CODEC_DEV_OK) {
                return ret;
            }
        }
    } else if (i2s_data->tx.handle != NULL) {
        int ret = i2s_data_hw_get_mode(i2s_data->tx.handle, out_mode);
        if (ret != ESP_CODEC_DEV_OK) {
            return ret;
        }
        /* rx.handle is NULL here (otherwise the first branch was taken). */
        i2s_chan_handle_t peer_handle = _get_peer_channel(i2s_data->tx.handle);
        if (peer_handle != NULL) {
            ret = i2s_data_hw_get_mode(peer_handle, in_mode);
            if (ret != ESP_CODEC_DEV_OK) {
                return ret;
            }
        }
    }
    return ESP_CODEC_DEV_OK;
}

/**
 * @brief  Create an I2S data interface
 *
 * @note  This function allocates the data interface instance and may allocate a
 *        shared per-port context. Registration in the global port list is
 *        thread-safe, but callers must not concurrently operate on the same
 *        returned data interface handle from multiple tasks.
 *
 * data_if creation matrix on the same I2S port:
 *
 * | Scenario                     | data_if count | A(tx, rx)    | B(tx, rx)    | Main use              |
 * |------------------------------|---------------|--------------|--------------|-----------------------|
 * | RX only && RX is master      | 2             | (NULL, RX)   | (TX, NULL)   | A: read, B: write     |
 * | RX only && RX is slave       | 2             | (TX, RX)     | (TX, NULL)   | A: read, B: write     |
 * | TX only && TX is master      | 2             | (TX, NULL)   | (NULL, RX)   | A: write, B: read     |
 * | TX only && TX is slave       | 2             | (TX, RX)     | (NULL, RX)   | A: write, B: read     |
 * | RX+TX full duplex            | 1             | (TX, RX)     | -            | A: read + write       |
 *
 * Note:
 * - data_if count: means the number of data_if instances on the same I2S port.
 * - A(tx, rx): the first audio_codec_new_i2s_data() call, with (tx_handle, rx_handle) assignment.
 * - B(tx, rx): the second audio_codec_new_i2s_data() call, with (tx_handle, rx_handle) assignment.
 * - Main use: A: read means the first data_if instance is used for read, B: write means the second data_if instance is used for write.
 */
const audio_codec_data_if_t *audio_codec_new_i2s_data(audio_codec_i2s_cfg_t *i2s_cfg)
{
    i2s_data_t *i2s_data = calloc(1, sizeof(i2s_data_t));
    if (i2s_data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate the I2S data interface instance");
        return NULL;
    }
    i2s_data->base.open = _i2s_data_open;
    i2s_data->base.is_open = _i2s_data_is_open;
    i2s_data->base.enable = _i2s_data_enable;
    i2s_data->base.read = _i2s_data_read;
    i2s_data->base.write = _i2s_data_write;
    i2s_data->base.set_fmt = _i2s_data_set_fmt;
    i2s_data->base.close = _i2s_data_close;
    i2s_data->base.get_mode = _i2s_data_get_mode;
    i2s_data->base.get_fmt = _i2s_data_get_fmt;
    i2s_data->base.set_map_query = _i2s_data_set_map_query;
    i2s_data->base.get_bus_info = _i2s_data_get_bus_info;
    int ret = _i2s_data_open(&i2s_data->base, i2s_cfg, sizeof(audio_codec_i2s_cfg_t));
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to open the I2S data interface, ret=%d", ret);
        free(i2s_data);
        return NULL;
    }
    return &i2s_data->base;
}
