/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_cpu.h"
#include "esp_log.h"

#include "audio_codec_data_i2s_priv.h"

static const char *TAG = "I2S_PORT";

/**
 * I2S data interfaces that share one I2S port coordinate through this process-wide
 * list. The list is protected by s_i2s_port_group_list_mutex.
 */
static i2s_port_group_t *s_i2s_port_group_list = NULL;
static esp_codec_dev_mutex_handle_t s_i2s_port_group_list_mutex = NULL;

/**
 * @brief  Give back the peer ref this stream holds, if any
 *
 * @note  The ref is recorded as the instance it was taken on, so it is always given back to that
 *         same instance even when the group has meanwhile picked a different peer for this
 *         direction. A recorded target is always still in the group: i2s_data_port_group_remove_instance()
 *         clears every link pointing at an instance before it leaves the list.
 */
static inline void _drop_peer_ref_link(i2s_stream_state_t *stream, bool is_playback)
{
    i2s_data_t *target = stream->peer_ref_target;
    if (target == NULL) {
        return;
    }
    stream->peer_ref_target = NULL;
    i2s_stream_state_t *target_stream = _get_stream_state(target, !is_playback);
    if (target_stream->ext_ref_count > 0) {
        target_stream->ext_ref_count--;
    }
}

static inline void _take_peer_ref_link(i2s_stream_state_t *stream, i2s_data_t *target, bool is_playback)
{
    i2s_stream_state_t *target_stream = _get_stream_state(target, !is_playback);
    target_stream->ext_ref_count++;
    stream->peer_ref_target = target;
}

static esp_codec_dev_mutex_handle_t _port_list_ensure_mutex(void)
{
    if (s_i2s_port_group_list_mutex != NULL) {
        return s_i2s_port_group_list_mutex;
    }
    esp_codec_dev_mutex_handle_t mutex = esp_codec_dev_mutex_create();
    if (mutex == NULL) {
        return NULL;
    }
    /* esp_cpu_compare_and_set operates on 32-bit values (32-bit pointer targets). */
    if (esp_cpu_compare_and_set((volatile uint32_t *)&s_i2s_port_group_list_mutex,
                                (uint32_t)NULL,
                                (uint32_t)mutex)) {
        return mutex;
    }
    esp_codec_dev_mutex_destroy(mutex);
    return s_i2s_port_group_list_mutex;
}

static void _port_group_unlink_locked(i2s_port_group_t *group)
{
    i2s_port_group_t *prev = NULL;
    i2s_port_group_t *cur = s_i2s_port_group_list;
    while (cur != NULL) {
        if (cur == group) {
            if (prev == NULL) {
                s_i2s_port_group_list = cur->next;
            } else {
                prev->next = cur->next;
            }
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

/**
 * @brief  Drop every peer ref the group holds on one instance
 *
 * @note  Runs before the instance leaves the list so no member keeps a link to it. A surviving link
 *         would either be given back to freed memory or, once a new instance takes the same slot in
 *         the group, decrement the ref count of a stream that never handed one out.
 */
static void _port_group_clear_links_to_locked(i2s_port_group_t *group, i2s_data_t *target)
{
    i2s_data_t *cur = group->instances;
    while (cur != NULL) {
        if (cur != target) {
            if (cur->tx.peer_ref_target == target) {
                _drop_peer_ref_link(&cur->tx, true);
            }
            if (cur->rx.peer_ref_target == target) {
                _drop_peer_ref_link(&cur->rx, false);
            }
        }
        cur = cur->next;
    }
}

int i2s_data_port_acquire_peer_ref(i2s_data_t *i2s_data, bool is_playback,
                                   bool enable_peer, i2s_peer_ref_change_t *change)
{
    /* Callers that do not need the snapshot pass NULL; the body still needs the state to roll back. */
    i2s_peer_ref_change_t scratch;
    if (change == NULL) {
        change = &scratch;
    }
    memset(change, 0, sizeof(*change));
    i2s_stream_state_t *active_stream = _get_stream_state(i2s_data, is_playback);
    change->previous_target = active_stream->peer_ref_target;

    bool self_paired = false;
    i2s_data_t *peer_data = i2s_data_port_get_peer_ref_target(i2s_data, is_playback, &self_paired);
    if (peer_data == NULL) {
        return ESP_CODEC_DEV_OK;
    }
    i2s_chan_handle_t peer_channel = _get_channel_handle(peer_data, !is_playback);
    if (peer_channel == NULL || !_is_master(peer_channel)) {
        return ESP_CODEC_DEV_OK;
    }

    change->acquired_target = peer_data;
    change->self_paired = self_paired;
    change->target_was_enabled = _is_enabled(peer_channel);

    if (self_paired) {
        if (enable_peer && !change->target_was_enabled) {
            int ret = i2s_data_hw_enable(peer_data, !is_playback, true);
            if (ret != ESP_CODEC_DEV_OK) {
                /* i2s_data_hw_enable already logged */
                return ret;
            }
            change->target_enable_changed = true;
        }
        return ESP_CODEC_DEV_OK;
    }

    if (active_stream->peer_ref_target != peer_data) {
        /* Take the new target first so a later enable failure can restore the previous target. */
        _take_peer_ref_link(active_stream, peer_data, is_playback);
        change->reference_changed = true;
    }

    if (enable_peer && !change->target_was_enabled) {
        int ret = i2s_data_hw_enable(peer_data, !is_playback, true);
        if (ret != ESP_CODEC_DEV_OK) {
            if (change->reference_changed) {
                active_stream->peer_ref_target = NULL;
                i2s_stream_state_t *acquired_stream = _get_stream_state(peer_data, !is_playback);
                if (acquired_stream->ext_ref_count > 0) {
                    acquired_stream->ext_ref_count--;
                }
                if (change->previous_target != NULL) {
                    _take_peer_ref_link(active_stream, change->previous_target, is_playback);
                }
                change->reference_changed = false;
            }
            /* i2s_data_hw_enable already logged */
            return ret;
        }
        change->target_enable_changed = true;
    }

    if (change->reference_changed && change->previous_target != NULL) {
        i2s_stream_state_t *previous_stream = _get_stream_state(change->previous_target, !is_playback);
        if (previous_stream->ext_ref_count > 0) {
            previous_stream->ext_ref_count--;
        }
    }
    return ESP_CODEC_DEV_OK;
}

int i2s_data_port_restore_peer_ref(i2s_data_t *i2s_data, bool is_playback,
                                   const i2s_peer_ref_change_t *change)
{
    if (change == NULL) {
        ESP_LOGE(TAG, "Restore peer ref failed: change is NULL");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (i2s_data == NULL) {
        ESP_LOGE(TAG, "Restore peer ref failed: i2s_data is NULL");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    int ret = ESP_CODEC_DEV_OK;
    i2s_stream_state_t *active_stream = _get_stream_state(i2s_data, is_playback);

    if (change->reference_changed && !change->self_paired) {
        if (active_stream->peer_ref_target == change->acquired_target) {
            _drop_peer_ref_link(active_stream, is_playback);
        }
        if (change->previous_target != NULL) {
            _take_peer_ref_link(active_stream, change->previous_target, is_playback);
        }
    }

    if (change->target_enable_changed && change->acquired_target != NULL) {
        i2s_chan_handle_t peer_channel = _get_channel_handle(change->acquired_target, !is_playback);
        bool currently_enabled = peer_channel != NULL && _is_enabled(peer_channel);
        if (currently_enabled != change->target_was_enabled) {
            ret = i2s_data_hw_enable(change->acquired_target, !is_playback, change->target_was_enabled);
            if (ret != ESP_CODEC_DEV_OK) {
                ESP_LOGE(TAG, "Restore peer ref failed: restore enable returned %d", ret);
            }
        }
    }
    return ret;
}

int i2s_data_port_release_peer_ref(i2s_data_t *i2s_data, bool is_playback, bool disable_if_unused)
{
    i2s_stream_state_t *active_stream = _get_stream_state(i2s_data, is_playback);
    i2s_data_t *ref_target = active_stream->peer_ref_target;
    if (ref_target != NULL) {
        // Follow the recorded target rather than asking the group again: the ref belongs to that
        // instance even if the group would now pick another peer, or none at all.
        _drop_peer_ref_link(active_stream, is_playback);
        if (disable_if_unused) {
            i2s_chan_handle_t peer_channel = _get_channel_handle(ref_target, !is_playback);
            if (peer_channel && !_should_keep_running(ref_target, !is_playback)) {
                return i2s_data_hw_enable(ref_target, !is_playback, false);
            }
        }
        return ESP_CODEC_DEV_OK;
    }
    bool self_paired = false;
    i2s_data_t *peer_data = i2s_data_port_get_peer_ref_target(i2s_data, is_playback, &self_paired);
    if (peer_data == NULL || !self_paired) {
        return ESP_CODEC_DEV_OK;
    }
    if (disable_if_unused) {
        i2s_chan_handle_t peer_channel = _get_channel_handle(peer_data, !is_playback);
        if (peer_channel && !_should_keep_running(peer_data, !is_playback)) {
            return i2s_data_hw_enable(peer_data, !is_playback, false);
        }
    }
    return ESP_CODEC_DEV_OK;
}

int i2s_data_port_list_lock(void)
{
    esp_codec_dev_mutex_handle_t mutex = _port_list_ensure_mutex();
    if (mutex == NULL) {
        ESP_LOGE(TAG, "Lock port list failed: mutex allocation failed");
        return ESP_CODEC_DEV_NO_MEM;
    }
    int ret = esp_codec_dev_mutex_lock(mutex, DEFAULT_WAIT_TIMEOUT);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Lock port list failed: mutex timed out");
        return ESP_CODEC_DEV_TIMEOUT;
    }
    return ESP_CODEC_DEV_OK;
}

void i2s_data_port_list_unlock(void)
{
    esp_codec_dev_mutex_unlock(s_i2s_port_group_list_mutex);
}

int i2s_data_port_group_lock(i2s_port_group_t *group)
{
    if (group == NULL) {
        ESP_LOGE(TAG, "Lock port group failed: group is NULL");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (group->mutex == NULL) {
        ESP_LOGE(TAG, "Lock port group failed: group mutex is unavailable");
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = esp_codec_dev_mutex_lock(group->mutex, DEFAULT_WAIT_TIMEOUT);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Lock port group failed: mutex timed out");
        return ESP_CODEC_DEV_TIMEOUT;
    }
    return ESP_CODEC_DEV_OK;
}

void i2s_data_port_group_unlock(i2s_port_group_t *group)
{
    // Call only after a successful i2s_data_port_group_lock(); group->mutex is then valid.
    esp_codec_dev_mutex_unlock(group->mutex);
}

i2s_port_group_t *i2s_data_port_group_acquire(uint8_t port)
{
    if (i2s_data_port_list_lock() != ESP_CODEC_DEV_OK) {
        return NULL;
    }
    i2s_port_group_t *cur = s_i2s_port_group_list;
    while (cur != NULL) {
        if (cur->port == port) {
            cur->ref_count++;
            i2s_data_port_list_unlock();
            return cur;
        }
        cur = cur->next;
    }
    i2s_port_group_t *group = calloc(1, sizeof(i2s_port_group_t));
    if (group == NULL) {
        ESP_LOGE(TAG, "Failed to allocate port group");
        i2s_data_port_list_unlock();
        return NULL;
    }
    group->mutex = esp_codec_dev_mutex_create();
    if (group->mutex == NULL) {
        ESP_LOGE(TAG, "Failed to allocate port mutex");
        free(group);
        i2s_data_port_list_unlock();
        return NULL;
    }
    group->port = port;
    group->ref_count = 1;
    group->next = s_i2s_port_group_list;
    s_i2s_port_group_list = group;
    i2s_data_port_list_unlock();
    return group;
}

void i2s_data_port_group_release_locked(i2s_port_group_t *group)
{
    if (group == NULL) {
        return;
    }
    if (group->ref_count > 0) {
        group->ref_count--;
    }
    if (group->ref_count > 0) {
        return;
    }
    _port_group_unlink_locked(group);
    if (group->instances != NULL) {
        ESP_LOGW(TAG, "Port group %d is released with dangling instances", group->port);
    }
    esp_codec_dev_mutex_destroy(group->mutex);
    free(group);
}

void i2s_data_port_group_release(i2s_port_group_t *group)
{
    if (group == NULL) {
        return;
    }
    if (i2s_data_port_list_lock() != ESP_CODEC_DEV_OK) {
        return;
    }
    i2s_data_port_group_release_locked(group);
    i2s_data_port_list_unlock();
}

void i2s_data_port_group_refresh_capability_index(i2s_port_group_t *group)
{
    if (group == NULL) {
        return;
    }
    group->duplex = NULL;
    group->rx_only = NULL;
    group->tx_only = NULL;
    i2s_data_t *cur = group->instances;
    while (cur != NULL) {
        i2s_data_t *candidate = cur;
        bool has_tx = _has_explicit_stream(candidate, true);
        bool has_rx = _has_explicit_stream(candidate, false);
        if (has_tx && has_rx) {
            if (group->duplex == NULL) {
                group->duplex = candidate;
            }
        } else if (has_rx) {
            if (group->rx_only == NULL) {
                group->rx_only = candidate;
            }
        } else if (has_tx) {
            if (group->tx_only == NULL) {
                group->tx_only = candidate;
            }
        }
        cur = cur->next;
    }
}

int i2s_data_port_group_add_instance(i2s_port_group_t *group, i2s_data_t *i2s_data)
{
    if (group == NULL || i2s_data == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    i2s_data->next = group->instances;
    group->instances = i2s_data;
    i2s_data_port_group_refresh_capability_index(group);
    return ESP_CODEC_DEV_OK;
}

void i2s_data_port_group_remove_instance(i2s_port_group_t *group, i2s_data_t *i2s_data)
{
    if (group == NULL || i2s_data == NULL) {
        return;
    }
    _port_group_clear_links_to_locked(group, i2s_data);
    i2s_data_t *prev = NULL;
    i2s_data_t *cur = group->instances;
    while (cur != NULL) {
        if (cur == i2s_data) {
            if (prev == NULL) {
                group->instances = cur->next;
            } else {
                prev->next = cur->next;
            }
            i2s_data->next = NULL;
            i2s_data_port_group_refresh_capability_index(group);
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

i2s_data_t *i2s_data_port_group_find_peer(i2s_port_group_t *group, i2s_data_t *self, bool is_playback)
{
    if (group == NULL) {
        return NULL;
    }
    i2s_data_t *candidate = is_playback ? group->rx_only : group->tx_only;
    if (candidate && candidate != self) {
        return candidate;
    }
    candidate = group->duplex;
    if (candidate && candidate != self && (is_playback ? candidate->rx.handle : candidate->tx.handle)) {
        return candidate;
    }
    return NULL;
}

i2s_data_t *i2s_data_port_get_peer_ref_target(i2s_data_t *i2s_data, bool is_playback, bool *self_paired)
{
    i2s_data_t *peer_data = i2s_data_port_group_find_peer(i2s_data->port_group, i2s_data, is_playback);
    if (self_paired != NULL) {
        *self_paired = false;
    }
    if (peer_data != NULL) {
        return peer_data;
    }
    if (_has_explicit_stream(i2s_data, true) && _has_explicit_stream(i2s_data, false)) {
        return i2s_data;
    }
    i2s_stream_state_t *stream = _get_stream_state(i2s_data, is_playback);
    i2s_stream_state_t *peer_stream = _get_stream_state(i2s_data, !is_playback);
    if (stream->handle != NULL && !stream->auto_paired &&
        peer_stream->handle != NULL && peer_stream->auto_paired) {
        if (self_paired != NULL) {
            *self_paired = true;
        }
        return i2s_data;
    }
    return NULL;
}
