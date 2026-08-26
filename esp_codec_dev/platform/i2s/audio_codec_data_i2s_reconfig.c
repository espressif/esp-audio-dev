/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include "esp_log.h"

#include "audio_codec_data_i2s_priv.h"
#include "codec_dev_i2s_bus.h"

/**
 * @brief  Negotiated buses and apply plan for one duplex reconfiguration
 */
typedef struct {
    codec_dev_i2s_candidate_t      tx_candidate;   /*!< Selected TX bus */
    codec_dev_i2s_candidate_t      rx_candidate;   /*!< Selected RX bus */
    codec_dev_i2s_reconfig_plan_t  apply_plan;     /*!< Peer-first hardware plan */
    i2s_reconfig_ctx_t             apply_context;  /*!< Streams mapped to plan roles */
} i2s_duplex_reconfig_t;

/**
 * @brief  State required to apply or roll back one idle clock peer transaction
 *
 *         An idle peer is the opposite-direction I2S channel on the same port. It is already
 *         initialized as a master (so it may drive BCLK, WS, or MCLK), but it has no running
 *         application request: either it has never been opened, or it is not being kept running.
 *         It therefore does not join duplex negotiation. The active stream still cannot ignore it,
 *         because a master peer that is left on a different frame geometry would clock the bus
 *         incorrectly. The idle peer keeps its own I2S mode and matches the active frame width:
 *         STD always uses two slots and widens them, TDM copies the active slot geometry. It is
 *         configured first, then referenced so the clocks stay up, then the active stream is applied.
 *
 *         A running peer (valid app_fs and kept running) is not idle: both sides negotiate one
 *         duplex bus. An initialized slave peer is also not idle: it does not provide clocks, so
 *         the active stream uses the active-only path.
 */
typedef struct {
    i2s_data_t                           *peer_data;          /*!< Idle peer data interface */
    bool                                  peer_is_playback;   /*!< Direction of the idle peer */
    codec_dev_i2s_stream_reconfig_plan_t  peer_plan;          /*!< Peer old and target bus */
    i2s_reconfig_stream_ctx_t             peer_context;       /*!< Hardware context for the peer */
    i2s_peer_ref_change_t                 peer_ref_change;    /*!< Ref changes made after peer apply */
    bool                                  peer_plan_applied;  /*!< True after peer plan succeeds */
    bool                                  peer_ref_acquired;  /*!< True after tracked acquire succeeds */
} i2s_idle_peer_reconfig_t;

static const char *TAG = "I2S_RECONFIG";

static inline const char *_bus_dir_name(bool is_playback)
{
    return is_playback ? "TX" : "RX";
}

static inline const char *_bus_mode_name(esp_codec_dev_i2s_mode_t mode)
{
    switch (mode) {
        case ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS:
            return "STD";
        case ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS:
            return "TDM";
        default:
            return "UNKNOWN";
    }
}

static void _log_app_to_bus(const char *op, const char *dir,
                            const esp_codec_dev_sample_info_t *app, const esp_codec_dev_bus_info_t *bus)
{
    ESP_LOGI(TAG, "%s %s: app ch=%u mask=0x%x bits=%u -> total=%u slot=%u data=%u mask=0x%x frame=%u",
             op, dir, app->channel, app->channel_mask, app->bits_per_sample,
             bus->total_slot, bus->slot_bit, bus->data_bit, bus->slot_mask, bus->total_frame_bits);
}

static void _log_reconfig_plan(const char *role, bool is_playback,
                               const codec_dev_i2s_stream_reconfig_plan_t *plan)
{
    const esp_codec_dev_bus_info_t *old_bus = &plan->old_bus;
    const esp_codec_dev_bus_info_t *new_bus = &plan->new_bus;
    ESP_LOGI(TAG, "Reconfig %s %s: %s enabled=%s total=%u->%u slot=%u->%u mask=0x%x->0x%x frame=%u->%u",
             role, _bus_dir_name(is_playback),
             codec_dev_i2s_stream_plan_needs_apply(plan) ? "yes" : "no",
             plan->was_enabled ? "yes" : "no",
             old_bus->total_slot, new_bus->total_slot, old_bus->slot_bit, new_bus->slot_bit,
             old_bus->slot_mask, new_bus->slot_mask, old_bus->total_frame_bits, new_bus->total_frame_bits);
}

static inline i2s_reconfig_stream_ctx_t _make_stream_context(i2s_data_t *i2s_data, bool is_playback)
{
    i2s_reconfig_stream_ctx_t ctx = {
        .i2s_data = i2s_data,
        .is_playback = is_playback,
    };
    return ctx;
}

static inline void _commit_stream_state(i2s_data_t *i2s_data, bool is_playback,
                                        const esp_codec_dev_sample_info_t *app_fs,
                                        const esp_codec_dev_bus_info_t *bus)
{
    i2s_stream_state_t *stream = _get_stream_state(i2s_data, is_playback);
    stream->app_fs = *app_fs;
    stream->bus_info = *bus;
}

static inline void _invalidate_stream_bus(i2s_data_t *i2s_data, bool is_playback)
{
    i2s_stream_state_t *stream = _get_stream_state(i2s_data, is_playback);
    memset(&stream->bus_info, 0, sizeof(stream->bus_info));
}

/* Mono requests keep the stereo STD/TDM frame width while selecting only the left slot. */
static inline void _normalize_request_sample_info(const esp_codec_dev_sample_info_t *app_fs,
                                                  esp_codec_dev_sample_info_t *normalized_fs)
{
    *normalized_fs = *app_fs;
    if (normalized_fs->channel == 1) {
        normalized_fs->channel = 2;
        normalized_fs->channel_mask = 0x01;
    } else if (normalized_fs->channel_mask == 0) {
        if (normalized_fs->channel >= 16) {
            normalized_fs->channel_mask = 0xFFFF;
        } else {
            normalized_fs->channel_mask = (uint16_t)((1U << normalized_fs->channel) - 1U);
        }
    }
}

static int _prepare_stream_request(i2s_data_t *i2s_data, bool is_playback,
                                   const esp_codec_dev_sample_info_t *app_fs,
                                   codec_dev_i2s_request_t *request)
{
    esp_codec_dev_bus_info_t current_bus = {0};
    i2s_stream_state_t *stream = _get_stream_state(i2s_data, is_playback);
    /* Invariant: on success the mode is always STD_PHILIPS or TDM_PHILIPS, because
       i2s_data_hw_get_bus_info() rejects every other comm mode through its default branch.
       Callers rely on this to skip a mode check before handing the request to the bus planner. */
    int ret = i2s_data_hw_get_bus_info(_get_channel_handle(i2s_data, is_playback), &current_bus);
    if (ret != ESP_CODEC_DEV_OK) {
        /* i2s_data_hw_get_bus_info already logged */
        return ret;
    }
    memset(request, 0, sizeof(*request));
    _normalize_request_sample_info(app_fs, &request->sample_info);
    request->mode = current_bus.mode;
    request->dev_type = is_playback ? ESP_CODEC_DEV_TYPE_OUT : ESP_CODEC_DEV_TYPE_IN;
    if (current_bus.mode == ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS && _stream_has_map_query(stream)) {
        request->map_query = &stream->map_query;
    }
    return ESP_CODEC_DEV_OK;
}

static int _get_configured_stream_bus(i2s_data_t *i2s_data, bool is_playback, esp_codec_dev_bus_info_t *bus_info)
{
    i2s_stream_state_t *stream = _get_stream_state(i2s_data, is_playback);
    if (_stream_has_bus_info(stream)) {
        *bus_info = stream->bus_info;
        return ESP_CODEC_DEV_OK;
    }
    return i2s_data_hw_get_bus_info(_get_channel_handle(i2s_data, is_playback), bus_info);
}

static int _build_stream_reconfig_plan(i2s_data_t *i2s_data, bool is_playback,
                                       const esp_codec_dev_bus_info_t *target_bus,
                                       codec_dev_i2s_stream_reconfig_plan_t *stream_plan)
{
    esp_codec_dev_bus_info_t configured_bus = {0};
    int ret = _get_configured_stream_bus(i2s_data, is_playback, &configured_bus);
    if (ret != ESP_CODEC_DEV_OK) {
        /* _get_configured_stream_bus already logged */
        return ret;
    }
    stream_plan->old_bus = configured_bus;
    stream_plan->new_bus = *target_bus;
    stream_plan->was_enabled = _is_enabled(_get_channel_handle(i2s_data, is_playback));
    return ESP_CODEC_DEV_OK;
}

static int _build_duplex_reconfig(const codec_dev_i2s_request_t *tx_request,
                                  const codec_dev_i2s_request_t *rx_request,
                                  i2s_data_t *tx_data, i2s_data_t *rx_data,
                                  bool active_is_playback, i2s_duplex_reconfig_t *result)
{
    memset(result, 0, sizeof(*result));
    int ret = codec_dev_i2s_negotiate_duplex_bus(tx_request, rx_request,
                                                 &result->tx_candidate, &result->rx_candidate);
    if (ret != ESP_CODEC_DEV_OK) {
        /* codec_dev_i2s_negotiate_duplex_bus already logged */
        return ret;
    }

    i2s_data_t *peer_data = active_is_playback ? rx_data : tx_data;
    i2s_data_t *active_data = active_is_playback ? tx_data : rx_data;
    const esp_codec_dev_bus_info_t *peer_bus =
        active_is_playback ? &result->rx_candidate.bus : &result->tx_candidate.bus;
    const esp_codec_dev_bus_info_t *active_bus =
        active_is_playback ? &result->tx_candidate.bus : &result->rx_candidate.bus;

    ret = _build_stream_reconfig_plan(peer_data, !active_is_playback, peer_bus, &result->apply_plan.peer);
    if (ret != ESP_CODEC_DEV_OK) {
        /* _build_stream_reconfig_plan already logged */
        return ret;
    }
    ret = _build_stream_reconfig_plan(active_data, active_is_playback, active_bus, &result->apply_plan.active);
    if (ret != ESP_CODEC_DEV_OK) {
        /* _build_stream_reconfig_plan already logged */
        return ret;
    }

    result->apply_context.peer = _make_stream_context(peer_data, !active_is_playback);
    result->apply_context.active = _make_stream_context(active_data, active_is_playback);
    return ESP_CODEC_DEV_OK;
}

static int _apply_stream_bus(i2s_data_t *i2s_data, bool is_playback, const esp_codec_dev_bus_info_t *bus)
{
    if (i2s_data == NULL || bus == NULL) {
        ESP_LOGE(TAG, "Apply stream bus failed: invalid argument");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    esp_codec_dev_sample_info_t hardware_fs = {0};
    hardware_fs.sample_rate = bus->sample_rate;
    hardware_fs.mclk_multiple = bus->mclk_multiple;
    hardware_fs.channel = bus->total_slot;
    hardware_fs.bits_per_sample = bus->data_bit;
    hardware_fs.channel_mask = bus->slot_mask;
    return i2s_data_hw_set_fs(_get_channel_handle(i2s_data, is_playback), is_playback, bus->slot_bit,
                              i2s_data->clk_src, &hardware_fs);
}

static int _i2s_reconfig_ops_apply_bus(void *ctx, codec_dev_i2s_stream_id_t stream,
                                       const esp_codec_dev_bus_info_t *bus)
{
    i2s_reconfig_ctx_t *reconfig_ctx = (i2s_reconfig_ctx_t *)ctx;
    i2s_reconfig_stream_ctx_t *stream_ctx =
        stream == CODEC_DEV_I2S_STREAM_PEER ? &reconfig_ctx->peer : &reconfig_ctx->active;
    return _apply_stream_bus(stream_ctx->i2s_data, stream_ctx->is_playback, bus);
}

static int _i2s_reconfig_ops_set_enabled(void *ctx, codec_dev_i2s_stream_id_t stream, bool enable)
{
    i2s_reconfig_ctx_t *reconfig_ctx = (i2s_reconfig_ctx_t *)ctx;
    i2s_reconfig_stream_ctx_t *stream_ctx =
        stream == CODEC_DEV_I2S_STREAM_PEER ? &reconfig_ctx->peer : &reconfig_ctx->active;
    return i2s_data_hw_enable(stream_ctx->i2s_data, stream_ctx->is_playback, enable);
}

static int _apply_reconfig_plan(const codec_dev_i2s_reconfig_plan_t *plan,
                                const i2s_reconfig_ctx_t *reconfig_context)
{
    if (reconfig_context == NULL) {
        ESP_LOGE(TAG, "Apply reconfig plan failed: context is NULL");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const codec_dev_i2s_ops_t ops = {
        .apply_bus = _i2s_reconfig_ops_apply_bus,
        .set_enabled = _i2s_reconfig_ops_set_enabled,
    };
    return codec_dev_i2s_apply_plan(plan, &ops, (void *)reconfig_context);
}

static int _apply_peer_stream_plan(const codec_dev_i2s_stream_reconfig_plan_t *stream_plan,
                                   i2s_reconfig_stream_ctx_t *stream_context)
{
    codec_dev_i2s_reconfig_plan_t plan = {0};
    i2s_reconfig_ctx_t reconfig_context = {0};
    plan.peer = *stream_plan;
    reconfig_context.peer = *stream_context;
    return _apply_reconfig_plan(&plan, &reconfig_context);
}

static int _apply_active_stream_plan(const codec_dev_i2s_stream_reconfig_plan_t *stream_plan,
                                     i2s_reconfig_stream_ctx_t *stream_context)
{
    codec_dev_i2s_reconfig_plan_t plan = {0};
    i2s_reconfig_ctx_t reconfig_context = {0};
    plan.active = *stream_plan;
    reconfig_context.active = *stream_context;
    return _apply_reconfig_plan(&plan, &reconfig_context);
}

static int _prepare_idle_peer_reconfig(i2s_data_t *peer_data, bool peer_is_playback,
                                       const esp_codec_dev_bus_info_t *active_bus,
                                       i2s_idle_peer_reconfig_t *txn)
{
    memset(txn, 0, sizeof(*txn));
    if (peer_data == NULL || active_bus == NULL) {
        ESP_LOGE(TAG, "Prepare idle peer failed: invalid argument");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    i2s_chan_handle_t peer_channel = _get_channel_handle(peer_data, peer_is_playback);
    if (peer_channel == NULL || !_is_master(peer_channel) || !_is_initialized(peer_channel)) {
        ESP_LOGE(TAG, "Prepare idle peer failed: peer is not an initialized master");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    esp_codec_dev_bus_info_t configured_peer_bus = {0};
    int ret = _get_configured_stream_bus(peer_data, peer_is_playback, &configured_peer_bus);
    if (ret == ESP_CODEC_DEV_NOT_SUPPORT) {
        ESP_LOGE(TAG, "Prepare idle peer failed: peer %s is not an STD or TDM stream",
                 _bus_dir_name(peer_is_playback));
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    if (ret != ESP_CODEC_DEV_OK) {
        /* _get_configured_stream_bus already logged */
        return ret;
    }

    esp_codec_dev_bus_info_t peer_target_bus = *active_bus;
    peer_target_bus.mode = configured_peer_bus.mode;
    /* Slot geometry is mode specific, so only the clock and the frame width carry over. Copying the
       active geometry verbatim would record a frame the peer mode cannot produce. */
    if (peer_target_bus.mode == ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS) {
        uint8_t std_slot_bit = (uint8_t)(active_bus->total_frame_bits / CODEC_DEV_I2S_STD_TOTAL_SLOT);
        if (!codec_dev_i2s_is_legal_slot_bit(std_slot_bit)) {
            ESP_LOGE(TAG, "Prepare idle peer failed: STD %s cannot build a %u-bit frame from two %u-bit slots",
                     _bus_dir_name(peer_is_playback), active_bus->total_frame_bits, std_slot_bit);
            return ESP_CODEC_DEV_NOT_SUPPORT;
        }
        peer_target_bus.total_slot = CODEC_DEV_I2S_STD_TOTAL_SLOT;
        peer_target_bus.slot_bit = std_slot_bit;
        peer_target_bus.data_bit = std_slot_bit;
        peer_target_bus.slot_mask = (1U << CODEC_DEV_I2S_STD_TOTAL_SLOT) - 1U;
        peer_target_bus.total_frame_bits = (uint16_t)(CODEC_DEV_I2S_STD_TOTAL_SLOT * std_slot_bit);
    }
    txn->peer_plan.old_bus = configured_peer_bus;
    txn->peer_plan.new_bus = peer_target_bus;
    txn->peer_plan.was_enabled = _is_enabled(peer_channel);

    txn->peer_data = peer_data;
    txn->peer_is_playback = peer_is_playback;
    txn->peer_context = _make_stream_context(peer_data, peer_is_playback);
    return ESP_CODEC_DEV_OK;
}

static int _rollback_idle_peer_reconfig(i2s_data_t *i2s_data, bool is_playback,
                                        i2s_idle_peer_reconfig_t *txn)
{
    int rollback_ret = ESP_CODEC_DEV_OK;
    if (txn->peer_ref_acquired) {
        int ret = i2s_data_port_restore_peer_ref(i2s_data, is_playback, &txn->peer_ref_change);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Rollback idle peer failed: restore peer ref returned %d", ret);
            rollback_ret = ret;
        }
        txn->peer_ref_acquired = false;
    }

    if (txn->peer_plan_applied && codec_dev_i2s_stream_plan_needs_apply(&txn->peer_plan)) {
        codec_dev_i2s_stream_reconfig_plan_t reverse_plan = {0};
        reverse_plan.old_bus = txn->peer_plan.new_bus;
        reverse_plan.new_bus = txn->peer_plan.old_bus;
        reverse_plan.was_enabled =
            _is_enabled(_get_channel_handle(txn->peer_data, txn->peer_is_playback));
        int ret = _apply_peer_stream_plan(&reverse_plan, &txn->peer_context);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Rollback idle peer failed: reverse apply returned %d", ret);
            _invalidate_stream_bus(txn->peer_data, txn->peer_is_playback);
            if (rollback_ret == ESP_CODEC_DEV_OK) {
                rollback_ret = ret;
            }
        }
    }
    return rollback_ret;
}

/**
 * Fallback used by modes the bus planner cannot describe, in practice PDM.
 *
 * PDM cannot run full duplex on one I2S port and cannot share the port with STD or TDM.
 * Deliberately leaves bus_info cleared: inventing STD/TDM geometry would make later
 * bus comparisons wrong.
 */
static int _apply_fs_without_planner(i2s_data_t *i2s_data, bool is_playback,
                                     const esp_codec_dev_sample_info_t *fs)
{
    ESP_LOGI(TAG, "Fallback without planner: port=%u active=%s",
             i2s_data->port, _bus_dir_name(is_playback));
    i2s_data_t *peer_data = i2s_data_port_get_peer_ref_target(i2s_data, is_playback, NULL);
    if (peer_data != NULL) {
        ESP_LOGE(TAG, "Fallback bus on port %d cannot share the port with a peer %s channel",
                 (int)i2s_data->port, _bus_dir_name(!is_playback));
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    i2s_chan_handle_t active_channel = _get_channel_handle(i2s_data, is_playback);
    bool was_enabled = _is_enabled(active_channel);
    int slot_bit = fs->bits_per_sample;
    int ret = i2s_data_hw_set_fs(active_channel, is_playback, slot_bit, i2s_data->clk_src, fs);
    if (was_enabled) {
        int enable_ret = i2s_data_hw_enable(i2s_data, is_playback, true);
        if (enable_ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Restore planner-less stream enable failed: ret=%d", enable_ret);
            _invalidate_stream_bus(i2s_data, is_playback);
            return ret != ESP_CODEC_DEV_OK ? ret : enable_ret;
        }
    }
    if (ret != ESP_CODEC_DEV_OK) {
        _invalidate_stream_bus(i2s_data, is_playback);
        /* i2s_data_hw_set_fs already logged */
        return ret;
    }
    _invalidate_stream_bus(i2s_data, is_playback);
    memcpy(_get_stream_app_fs(i2s_data, is_playback), fs, sizeof(esp_codec_dev_sample_info_t));
    ESP_LOGI(TAG, "No peer channel; set slot width to %d", slot_bit);
    return ESP_CODEC_DEV_OK;
}

static int _apply_fs_active_only(i2s_data_t *i2s_data, bool is_playback,
                                 const esp_codec_dev_sample_info_t *app_fs,
                                 const codec_dev_i2s_request_t *active_request)
{
    codec_dev_i2s_candidate_t active_candidate = {0};
    int ret = codec_dev_i2s_resolve(active_request, &active_candidate);
    if (ret != ESP_CODEC_DEV_OK) {
        /* codec_dev_i2s_resolve already logged */
        return ret;
    }

    codec_dev_i2s_reconfig_plan_t plan = {0};
    ret = _build_stream_reconfig_plan(i2s_data, is_playback, &active_candidate.bus, &plan.active);
    if (ret != ESP_CODEC_DEV_OK) {
        /* _build_stream_reconfig_plan already logged */
        return ret;
    }

    i2s_reconfig_ctx_t reconfig_context = {
        .active = _make_stream_context(i2s_data, is_playback),
    };
    _log_app_to_bus("Active-only", _bus_dir_name(is_playback),
                    &active_request->sample_info, &active_candidate.bus);
    _log_reconfig_plan("active", is_playback, &plan.active);
    ret = _apply_reconfig_plan(&plan, &reconfig_context);
    if (ret != ESP_CODEC_DEV_OK) {
        if (codec_dev_i2s_stream_plan_needs_apply(&plan.active)) {
            _invalidate_stream_bus(i2s_data, is_playback);
        }
        /* _apply_reconfig_plan already logged */
        return ret;
    }
    _commit_stream_state(i2s_data, is_playback, app_fs, &active_candidate.bus);
    return ESP_CODEC_DEV_OK;
}

static int _apply_fs_with_peer_request(i2s_data_t *i2s_data, bool is_playback,
                                       const esp_codec_dev_sample_info_t *app_fs,
                                       const codec_dev_i2s_request_t *active_request,
                                       i2s_data_t *peer_data)
{
    i2s_stream_state_t *peer_stream = _get_stream_state(peer_data, !is_playback);
    codec_dev_i2s_request_t peer_request = {0};
    int ret = _prepare_stream_request(peer_data, !is_playback, &peer_stream->app_fs, &peer_request);
    if (ret == ESP_CODEC_DEV_NOT_SUPPORT) {
        ESP_LOGE(TAG, "Cannot share port %d: running peer %s channel is not an STD or TDM stream",
                 (int)i2s_data->port, _bus_dir_name(!is_playback));
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    if (ret != ESP_CODEC_DEV_OK) {
        /* _prepare_stream_request already logged */
        return ret;
    }

    const codec_dev_i2s_request_t *tx_request = is_playback ? active_request : &peer_request;
    const codec_dev_i2s_request_t *rx_request = is_playback ? &peer_request : active_request;
    i2s_data_t *tx_data = is_playback ? i2s_data : peer_data;
    i2s_data_t *rx_data = is_playback ? peer_data : i2s_data;
    const esp_codec_dev_sample_info_t *tx_app_fs = is_playback ? app_fs : &peer_stream->app_fs;
    const esp_codec_dev_sample_info_t *rx_app_fs = is_playback ? &peer_stream->app_fs : app_fs;

    ESP_LOGI(TAG, "Negotiate duplex bus: port=%u active=%s mode=%s peer=%s",
             i2s_data->port, _bus_dir_name(is_playback), _bus_mode_name(active_request->mode),
             _bus_dir_name(!is_playback));
    i2s_duplex_reconfig_t duplex = {0};
    ret = _build_duplex_reconfig(tx_request, rx_request, tx_data, rx_data, is_playback, &duplex);
    if (ret != ESP_CODEC_DEV_OK) {
        /* _build_duplex_reconfig already logged */
        return ret;
    }
    ESP_LOGI(TAG, "Negotiate duplex bus: rate=%" PRIu32 " mclk=%d",
             tx_request->sample_info.sample_rate, tx_request->sample_info.mclk_multiple);
    _log_app_to_bus("Duplex", "TX", &tx_request->sample_info, &duplex.tx_candidate.bus);
    _log_app_to_bus("Duplex", "RX", &rx_request->sample_info, &duplex.rx_candidate.bus);
    _log_reconfig_plan("peer", !is_playback, &duplex.apply_plan.peer);
    _log_reconfig_plan("active", is_playback, &duplex.apply_plan.active);

    ret = _apply_reconfig_plan(&duplex.apply_plan, &duplex.apply_context);
    if (ret != ESP_CODEC_DEV_OK) {
        if (codec_dev_i2s_stream_plan_needs_apply(&duplex.apply_plan.peer)) {
            _invalidate_stream_bus(peer_data, !is_playback);
        }
        if (codec_dev_i2s_stream_plan_needs_apply(&duplex.apply_plan.active)) {
            _invalidate_stream_bus(i2s_data, is_playback);
        }
        /* _apply_reconfig_plan already logged */
        return ret;
    }
    _commit_stream_state(tx_data, true, tx_app_fs, &duplex.tx_candidate.bus);
    _commit_stream_state(rx_data, false, rx_app_fs, &duplex.rx_candidate.bus);
    return ESP_CODEC_DEV_OK;
}

static int _apply_fs_with_idle_peer(i2s_data_t *i2s_data, bool is_playback,
                                    const esp_codec_dev_sample_info_t *app_fs,
                                    const codec_dev_i2s_request_t *active_request,
                                    i2s_data_t *peer_data)
{
    codec_dev_i2s_candidate_t active_candidate = {0};
    int ret = codec_dev_i2s_resolve(active_request, &active_candidate);
    if (ret != ESP_CODEC_DEV_OK) {
        /* codec_dev_i2s_resolve already logged */
        return ret;
    }

    codec_dev_i2s_stream_reconfig_plan_t active_plan = {0};
    ret = _build_stream_reconfig_plan(i2s_data, is_playback, &active_candidate.bus, &active_plan);
    if (ret != ESP_CODEC_DEV_OK) {
        /* _build_stream_reconfig_plan already logged */
        return ret;
    }

    i2s_idle_peer_reconfig_t idle_txn = {0};
    ret = _prepare_idle_peer_reconfig(peer_data, !is_playback, &active_candidate.bus, &idle_txn);
    if (ret != ESP_CODEC_DEV_OK) {
        /* _prepare_idle_peer_reconfig already logged */
        return ret;
    }

    _log_app_to_bus("Preconfigure idle peer", _bus_dir_name(is_playback),
                    &active_request->sample_info, &active_candidate.bus);
    ESP_LOGD(TAG, "Preconfigure idle peer %s: need=%s total=%u->%u frame=%u->%u",
             _bus_dir_name(!is_playback),
             codec_dev_i2s_stream_plan_needs_apply(&idle_txn.peer_plan) ? "yes" : "no",
             idle_txn.peer_plan.old_bus.total_slot, idle_txn.peer_plan.new_bus.total_slot,
             idle_txn.peer_plan.old_bus.total_frame_bits, idle_txn.peer_plan.new_bus.total_frame_bits);

    ret = _apply_peer_stream_plan(&idle_txn.peer_plan, &idle_txn.peer_context);
    if (ret != ESP_CODEC_DEV_OK) {
        if (codec_dev_i2s_stream_plan_needs_apply(&idle_txn.peer_plan)) {
            _invalidate_stream_bus(peer_data, !is_playback);
        }
        ESP_LOGE(TAG, "Apply idle peer failed: active=%s peer=%s ret=%d",
                 _bus_dir_name(is_playback), _bus_dir_name(!is_playback), ret);
        return ret;
    }
    idle_txn.peer_plan_applied = true;

    ret = i2s_data_port_acquire_peer_ref(i2s_data, is_playback, true, &idle_txn.peer_ref_change);
    if (ret != ESP_CODEC_DEV_OK) {
        (void)_rollback_idle_peer_reconfig(i2s_data, is_playback, &idle_txn);
        /* i2s_data_port_acquire_peer_ref already logged */
        return ret;
    }
    idle_txn.peer_ref_acquired = true;

    i2s_reconfig_stream_ctx_t active_context = _make_stream_context(i2s_data, is_playback);
    _log_reconfig_plan("active", is_playback, &active_plan);
    ret = _apply_active_stream_plan(&active_plan, &active_context);
    if (ret != ESP_CODEC_DEV_OK) {
        if (codec_dev_i2s_stream_plan_needs_apply(&active_plan)) {
            _invalidate_stream_bus(i2s_data, is_playback);
        }
        (void)_rollback_idle_peer_reconfig(i2s_data, is_playback, &idle_txn);
        return ret;
    }

    _get_stream_state(peer_data, !is_playback)->bus_info = idle_txn.peer_plan.new_bus;
    _commit_stream_state(i2s_data, is_playback, app_fs, &active_candidate.bus);
    return ESP_CODEC_DEV_OK;
}

int i2s_data_reconfig_apply_fs(i2s_data_t *i2s_data, bool is_playback,
                               const esp_codec_dev_sample_info_t *app_fs)
{
    if (i2s_data == NULL || app_fs == NULL) {
        ESP_LOGE(TAG, "Apply fs failed: invalid argument");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    codec_dev_i2s_request_t active_request = {0};
    int ret = _prepare_stream_request(i2s_data, is_playback, app_fs, &active_request);
    if (ret == ESP_CODEC_DEV_NOT_SUPPORT) {
        /* PDM and any future mode i2s_data_hw_get_bus_info() cannot describe */
        return _apply_fs_without_planner(i2s_data, is_playback, app_fs);
    }
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;  /* helper already logged */
    }

    /* The peer is the opposite direction, resolved by i2s_data_port_get_peer_ref_target():
       1. another instance in the port group that covers that direction
       2. this instance itself when it explicitly owns both handles
       3. this instance itself when its own handle is explicit and the peer handle was auto-filled */
    i2s_data_t *peer_data = i2s_data_port_get_peer_ref_target(i2s_data, is_playback, NULL);
    i2s_stream_state_t *peer_stream = peer_data ? _get_stream_state(peer_data, !is_playback) : NULL;

    /* Peer is live, so both formats are real requests: negotiate one frame that carries both and roll
       the peer back when the active stream cannot be programmed. Reached when:
       1. the peer must keep running, either opened by the app or held by a peer ref
       2. the peer has a committed format of its own
       Role is deliberately not checked: whoever drives BCLK/WS, the two directions still share the
       frame, so a running slave peer constrains this stream just as much as a master would. */
    if (peer_data != NULL && _should_keep_running(peer_data, !is_playback) && _stream_has_app_fs(peer_stream)) {
        return _apply_fs_with_peer_request(i2s_data, is_playback, app_fs, &active_request, peer_data);
    }

    /* Peer carries no data yet but already owns the clock: it is the initialized master of this port,
       so this stream rides on its BCLK/WS. Widen the peer to the frame this stream needs and take a
       ref that keeps it enabled, otherwise the clock stops while the peer sits idle. Typical case:
       both channels are initialized up front and only one direction is opened. */
    if (peer_data != NULL) {
        i2s_chan_handle_t peer_channel = _get_channel_handle(peer_data, !is_playback);
        if (peer_channel != NULL && _is_master(peer_channel) && _is_initialized(peer_channel)) {
            return _apply_fs_with_idle_peer(i2s_data, is_playback, app_fs, &active_request, peer_data);
        }
    }

    /* Nothing on the peer side to align with:
       1. no peer instance on this port at all
       2. the peer channel is a slave: it follows the external clock and imposes no frame
       3. the peer is a master still in I2S_COMM_MODE_NONE: no slot geometry to match yet, it
          negotiates when its own open runs */
    return _apply_fs_active_only(i2s_data, is_playback, app_fs, &active_request);
}

int i2s_data_reconfig_apply_self_duplex(i2s_data_t *i2s_data, const esp_codec_dev_sample_info_t *app_fs)
{
    if (i2s_data == NULL || app_fs == NULL) {
        ESP_LOGE(TAG, "Apply self duplex failed: invalid argument");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (i2s_data->tx.handle == NULL || i2s_data->rx.handle == NULL) {
        ESP_LOGE(TAG, "Apply self duplex failed: instance does not own both directions");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    codec_dev_i2s_request_t tx_request = {0};
    codec_dev_i2s_request_t rx_request = {0};
    int ret = _prepare_stream_request(i2s_data, true, app_fs, &tx_request);
    if (ret == ESP_CODEC_DEV_NOT_SUPPORT || ret != ESP_CODEC_DEV_OK) {
        if (ret == ESP_CODEC_DEV_NOT_SUPPORT) {
            ESP_LOGE(TAG, "Apply self duplex failed: TX is not an STD or TDM stream");
        }
        return ret;
    }
    ret = _prepare_stream_request(i2s_data, false, app_fs, &rx_request);
    if (ret == ESP_CODEC_DEV_NOT_SUPPORT || ret != ESP_CODEC_DEV_OK) {
        if (ret == ESP_CODEC_DEV_NOT_SUPPORT) {
            ESP_LOGE(TAG, "Apply self duplex failed: RX is not an STD or TDM stream");
        }
        return ret;
    }

    /* RX uses the peer role only to preserve peer-first apply ordering; both streams belong to this instance. */
    i2s_duplex_reconfig_t duplex = {0};
    ret = _build_duplex_reconfig(&tx_request, &rx_request, i2s_data, i2s_data, true, &duplex);
    if (ret != ESP_CODEC_DEV_OK) {
        /* _build_duplex_reconfig already logged */
        return ret;
    }
    ESP_LOGI(TAG, "Negotiate self duplex: rate=%" PRIu32 " mclk=%d",
             tx_request.sample_info.sample_rate, tx_request.sample_info.mclk_multiple);
    _log_app_to_bus("Self-duplex", "TX", &tx_request.sample_info, &duplex.tx_candidate.bus);
    _log_app_to_bus("Self-duplex", "RX", &rx_request.sample_info, &duplex.rx_candidate.bus);
    _log_reconfig_plan("peer", false, &duplex.apply_plan.peer);
    _log_reconfig_plan("active", true, &duplex.apply_plan.active);

    ret = _apply_reconfig_plan(&duplex.apply_plan, &duplex.apply_context);
    if (ret != ESP_CODEC_DEV_OK) {
        if (codec_dev_i2s_stream_plan_needs_apply(&duplex.apply_plan.peer)) {
            _invalidate_stream_bus(i2s_data, false);
        }
        if (codec_dev_i2s_stream_plan_needs_apply(&duplex.apply_plan.active)) {
            _invalidate_stream_bus(i2s_data, true);
        }
        /* _apply_reconfig_plan already logged */
        return ret;
    }
    _commit_stream_state(i2s_data, true, app_fs, &duplex.tx_candidate.bus);
    _commit_stream_state(i2s_data, false, app_fs, &duplex.rx_candidate.bus);
    return ESP_CODEC_DEV_OK;
}
