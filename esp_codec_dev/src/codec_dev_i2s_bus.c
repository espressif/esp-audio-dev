/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"

#include "codec_dev_i2s_bus.h"
#include "codec_dev_map.h"

typedef int (*codec_dev_i2s_candidate_cb_t)(void *ctx, const codec_dev_i2s_candidate_t *candidate, bool *stop);

/**
 * @brief  Iteration state shared by the per-mode candidate generators
 */
typedef struct {
    codec_dev_i2s_candidate_cb_t  on_candidate;  /*!< Visitor supplied by the caller */
    void                         *ctx;           /*!< Visitor context */
    bool                          stop;          /*!< Visitor asked to end the iteration */
    uint8_t                       emitted;       /*!< Number of legal candidates found so far */
} codec_dev_bus_emit_ctx_t;

/**
 * @brief  Visitor state used to capture the first visited candidate
 */
typedef struct {
    codec_dev_i2s_candidate_t *out;    /*!< Destination for the narrowest candidate */
    bool                       found;  /*!< True once out holds a candidate */
} codec_dev_bus_first_ctx_t;

/**
 * @brief  Visitor state used to find a peer candidate with an exact total_frame_bits
 */
typedef struct {
    uint16_t                   target_total_frame_bits;  /*!< Total frame bits the peer has to match */
    codec_dev_i2s_candidate_t *out;                      /*!< Destination for the matching candidate */
    bool                       found;                    /*!< True once out holds a candidate */
} codec_dev_bus_total_frame_match_ctx_t;

/**
 * @brief  Visitor state driving the TX side of the duplex intersection
 */
typedef struct {
    const codec_dev_i2s_request_t *rx_request;    /*!< Peer request re-iterated per TX candidate */
    codec_dev_i2s_candidate_t     *tx_candidate;  /*!< Destination for the selected TX candidate */
    codec_dev_i2s_candidate_t     *rx_candidate;  /*!< Destination for the selected RX candidate */
    bool                           found;         /*!< True once outputs hold a common total_frame_bits */
} codec_dev_bus_duplex_match_ctx_t;

static const char *TAG = "ADEV_BUS";
static const uint8_t s_legal_slot_bits[] = {8, 16, 24, 32};

static int codec_dev_bus_for_each_candidate(const codec_dev_i2s_request_t *request,
                                            codec_dev_i2s_candidate_cb_t on_candidate, void *ctx);

static inline uint16_t codec_dev_bus_make_low_mask(uint8_t bit_count)
{
    if (bit_count == 0) {
        return 0;
    }
    if (bit_count >= 16) {
        return UINT16_MAX;
    }
    return (uint16_t)((1U << bit_count) - 1U);
}

static inline uint16_t codec_dev_bus_get_app_slot_mask(const esp_codec_dev_sample_info_t *sample_info)
{
    if (sample_info->channel_mask != 0) {
        return sample_info->channel_mask;
    }
    return codec_dev_bus_make_low_mask(sample_info->channel);
}

static inline bool codec_dev_bus_is_supported_mode(esp_codec_dev_i2s_mode_t mode)
{
    return mode == ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS || mode == ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS;
}

static inline bool codec_dev_bus_is_valid_bus_info(const esp_codec_dev_bus_info_t *bus)
{
    return bus != NULL &&
           codec_dev_bus_is_supported_mode(bus->mode) &&
           bus->sample_rate != 0 &&
           bus->total_slot != 0 &&
           bus->total_slot <= ESP_CODEC_DEV_MAX_BUS_SLOT &&
           codec_dev_i2s_is_legal_slot_bit(bus->slot_bit) &&
           bus->data_bit != 0 &&
           bus->data_bit <= bus->slot_bit &&
           bus->slot_mask != 0 &&
           bus->total_frame_bits == (uint16_t)(bus->total_slot * bus->slot_bit);
}

static int codec_dev_bus_validate_request(const codec_dev_i2s_request_t *request)
{
    if (request == NULL ||
        (request->dev_type != ESP_CODEC_DEV_TYPE_IN && request->dev_type != ESP_CODEC_DEV_TYPE_OUT) ||
        codec_dev_bus_is_supported_mode(request->mode) == false ||
        request->sample_info.channel == 0 || request->sample_info.channel > ESP_CODEC_DEV_MAX_BUS_SLOT ||
        request->sample_info.bits_per_sample == 0 || request->sample_info.bits_per_sample > 32 ||
        request->sample_info.sample_rate == 0) {
        ESP_LOGE(TAG, "Validate request failed: request fields are invalid");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    uint16_t app_slot_mask = codec_dev_bus_get_app_slot_mask(&request->sample_info);
    if (request->mode == ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS) {
        if (request->sample_info.channel <= 2) {
            if ((app_slot_mask & ~codec_dev_bus_make_low_mask(CODEC_DEV_I2S_STD_TOTAL_SLOT)) != 0) {
                ESP_LOGE(TAG, "Validate request failed: STD slot mask 0x%04x is invalid", app_slot_mask);
                return ESP_CODEC_DEV_INVALID_ARG;
            }
        } else if (app_slot_mask != codec_dev_bus_make_low_mask(request->sample_info.channel)) {
            /* STD packs every channel into the same two widened slots, so the bus cannot express a
               selection of them. Only the full set can be carried; anything narrower would otherwise be
               accepted here and then silently widened back to both slots.
               Note: reaching this path from a test needs codec_dev_i2s_bus.h, which is component-private,
               so the mock and board tests cannot cover it. */
            ESP_LOGW(TAG, "Validate request: partial STD channel selection is not supported");
            return ESP_CODEC_DEV_NOT_SUPPORT;
        }
    } else if ((app_slot_mask & ~codec_dev_bus_make_low_mask(request->sample_info.channel)) != 0) {
        ESP_LOGE(TAG, "Validate request failed: TDM slot mask 0x%04x exceeds channel count %u",
                 app_slot_mask, request->sample_info.channel);
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (request->map_query != NULL && request->map_query->resolve_cb == NULL) {
        ESP_LOGE(TAG, "Validate request failed: map query resolve callback is NULL");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    return ESP_CODEC_DEV_OK;
}

static inline int codec_dev_bus_emit_candidate(codec_dev_bus_emit_ctx_t *emit,
                                               const codec_dev_i2s_candidate_t *candidate)
{
    emit->emitted++;
    return emit->on_candidate(emit->ctx, candidate, &emit->stop);
}

static int codec_dev_bus_fill_candidate(codec_dev_i2s_candidate_t *candidate,
                                        const codec_dev_i2s_request_t *request,
                                        uint8_t total_slot,
                                        uint8_t slot_bit,
                                        uint8_t data_bit,
                                        uint16_t slot_mask,
                                        const esp_codec_dev_channel_map_t *mapping)
{
    if (candidate == NULL || request == NULL) {
        ESP_LOGE(TAG, "Fill candidate failed: invalid argument");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    memset(candidate, 0, sizeof(*candidate));
    candidate->bus.mode = request->mode;
    candidate->bus.sample_rate = request->sample_info.sample_rate;
    candidate->bus.mclk_multiple = request->sample_info.mclk_multiple;
    candidate->bus.total_slot = total_slot;
    candidate->bus.slot_bit = slot_bit;
    candidate->bus.data_bit = data_bit;
    candidate->bus.slot_mask = slot_mask;
    candidate->bus.total_frame_bits = (uint16_t)(total_slot * slot_bit);
    if (mapping != NULL) {
        candidate->mapping = *mapping;
    }
    return ESP_CODEC_DEV_OK;
}

static int codec_dev_bus_query_mapping(const codec_dev_i2s_request_t *request,
                                       uint8_t total_slot,
                                       esp_codec_dev_channel_map_t *mapping)
{
    if (request == NULL || request->map_query == NULL || request->map_query->resolve_cb == NULL ||
        mapping == NULL || total_slot == 0 || total_slot > ESP_CODEC_DEV_MAX_MAP_CHANNELS) {
        ESP_LOGE(TAG, "Query mapping failed: invalid argument");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = request->map_query->resolve_cb(request->map_query->ctx, request->dev_type, total_slot, mapping);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGD(TAG, "Query mapping: callback failed for %u slots, ret=%d", total_slot, ret);
        return ret;
    }
    if (codec_dev_map_validate_device(mapping) != ESP_CODEC_DEV_OK ||
        codec_dev_map_count(mapping) != total_slot) {
        ESP_LOGE(TAG, "Query mapping failed: callback returned an invalid %u-slot map", total_slot);
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    return ESP_CODEC_DEV_OK;
}

static int codec_dev_bus_get_original_logical_selection(const codec_dev_i2s_request_t *request,
                                                        esp_codec_dev_channel_map_t *memory_map)
{
    esp_codec_dev_channel_map_t device_map = {0};
    int ret = codec_dev_bus_query_mapping(request, request->sample_info.channel, &device_map);
    if (ret != ESP_CODEC_DEV_OK) {
        /* codec_dev_bus_query_mapping already logged */
        return ret;
    }
    uint16_t app_mask = codec_dev_bus_get_app_slot_mask(&request->sample_info);
    if (request->dev_type == ESP_CODEC_DEV_TYPE_OUT) {
        return codec_dev_map_from_mask(app_mask, (uint8_t)codec_dev_map_count(&device_map), memory_map);
    }
    return codec_dev_map_slots_to_memory(&device_map, app_mask, memory_map);
}

static int codec_dev_bus_generate_std_candidates(const codec_dev_i2s_request_t *request,
                                                 codec_dev_bus_emit_ctx_t *emit)
{
    uint16_t min_frame_bits = (uint16_t)(request->sample_info.channel * request->sample_info.bits_per_sample);
    uint16_t slot_mask = 0;
    if (request->sample_info.channel > 2) {
        slot_mask = 0x03;
    } else {
        slot_mask = (uint16_t)(codec_dev_bus_get_app_slot_mask(&request->sample_info) & 0x03);
    }
    uint8_t data_bit = request->sample_info.bits_per_sample;

    // STD always runs two slots, so carrying more than two channels means packing several samples into
    // each slot. The data width then has to fill the pair exactly, and still be a width the hardware
    // accepts, otherwise the frame silently transfers only part of every slot.
    if (request->sample_info.channel > CODEC_DEV_I2S_STD_TOTAL_SLOT) {
        if ((min_frame_bits % CODEC_DEV_I2S_STD_TOTAL_SLOT) != 0) {
            ESP_LOGW(TAG, "Generate STD candidates: frame width %u cannot use two slots", min_frame_bits);
            return ESP_CODEC_DEV_NOT_SUPPORT;
        }
        data_bit = (uint8_t)(min_frame_bits / CODEC_DEV_I2S_STD_TOTAL_SLOT);
        if (codec_dev_i2s_is_legal_slot_bit(data_bit) == false) {
            ESP_LOGW(TAG, "Generate STD candidates: packed data width %u is unsupported", data_bit);
            return ESP_CODEC_DEV_NOT_SUPPORT;
        }
    }

    for (size_t i = 0; i < sizeof(s_legal_slot_bits) && !emit->stop; i++) {
        uint8_t slot_bit = s_legal_slot_bits[i];
        // A mono request only needs half the frame, so the frame test alone can still leave a slot too
        // narrow to hold one sample.
        if ((uint16_t)(CODEC_DEV_I2S_STD_TOTAL_SLOT * slot_bit) < min_frame_bits || slot_bit < data_bit) {
            continue;
        }
        codec_dev_i2s_candidate_t candidate = {0};
        int ret = codec_dev_bus_fill_candidate(&candidate, request, CODEC_DEV_I2S_STD_TOTAL_SLOT, slot_bit,
                                               data_bit, slot_mask, NULL);
        if (ret != ESP_CODEC_DEV_OK) {
            /* codec_dev_bus_fill_candidate already logged */
            return ret;
        }
        ret = codec_dev_bus_emit_candidate(emit, &candidate);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "Generate STD candidates: visitor failed, ret=%d", ret);
            return ret;
        }
    }
    return ESP_CODEC_DEV_OK;
}

static int codec_dev_bus_generate_mapping_aware_tdm_candidates(const codec_dev_i2s_request_t *request,
                                                               codec_dev_bus_emit_ctx_t *emit)
{
    if (codec_dev_i2s_is_legal_slot_bit(request->sample_info.bits_per_sample) == false) {
        ESP_LOGW(TAG, "Generate TDM candidates: sample width %u is unsupported",
                 request->sample_info.bits_per_sample);
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    esp_codec_dev_channel_map_t memory_map = {0};
    int ret = codec_dev_bus_get_original_logical_selection(request, &memory_map);
    if (ret != ESP_CODEC_DEV_OK) {
        /* codec_dev_bus_get_original_logical_selection already logged */
        return ret;
    }
    uint16_t channel_mask = 0;
    if (memory_map.value != 0) {
        ret = codec_dev_map_to_mask(&memory_map, ESP_CODEC_DEV_MAX_MAP_CHANNELS, &channel_mask);
        if (ret != ESP_CODEC_DEV_OK) {
            return ret;
        }
    }
    for (uint8_t total_slot = request->sample_info.channel;
         total_slot <= ESP_CODEC_DEV_MAX_MAP_CHANNELS && !emit->stop; total_slot++) {
        esp_codec_dev_channel_map_t candidate_device = {0};
        ret = codec_dev_bus_query_mapping(request, total_slot, &candidate_device);
        if (ret == ESP_CODEC_DEV_NOT_SUPPORT) {
            ESP_LOGD(TAG, "Skip TDM candidate: total=%u reason=query", total_slot);
            continue;
        }
        if (ret != ESP_CODEC_DEV_OK) {
            /* codec_dev_bus_query_mapping already logged */
            return ret;
        }
        uint8_t logical_channel_count = (uint8_t)codec_dev_map_count(&candidate_device);
        if ((channel_mask & ~codec_dev_bus_make_low_mask(logical_channel_count)) != 0) {
            ESP_LOGD(TAG, "Skip TDM candidate: total=%u reason=logical-mask", total_slot);
            continue;
        }
        uint16_t candidate_slot_mask = 0;
        ret = codec_dev_map_memory_to_slots(&candidate_device, &memory_map, &candidate_slot_mask);
        if (ret == ESP_CODEC_DEV_NOT_SUPPORT) {
            ESP_LOGD(TAG, "Skip TDM candidate: total=%u reason=slot-mask", total_slot);
            continue;
        }
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Generate TDM candidates failed: logical map conversion returned %d", ret);
            return ret;
        }
        esp_codec_dev_channel_map_t verify_memory = {0};
        ret = codec_dev_map_slots_to_memory(&candidate_device, candidate_slot_mask, &verify_memory);
        if (ret == ESP_CODEC_DEV_NOT_SUPPORT) {
            ESP_LOGD(TAG, "Skip TDM candidate: total=%u reason=verify", total_slot);
            continue;
        }
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Generate TDM candidates failed: slot map verification returned %d", ret);
            return ret;
        }
        if (verify_memory.value != memory_map.value) {
            ESP_LOGD(TAG, "Skip TDM candidate: total=%u reason=mask-roundtrip", total_slot);
            continue;
        }
        codec_dev_i2s_candidate_t candidate = {0};
        ret = codec_dev_bus_fill_candidate(&candidate, request, total_slot,
                                           request->sample_info.bits_per_sample,
                                           request->sample_info.bits_per_sample,
                                           candidate_slot_mask, &candidate_device);
        if (ret != ESP_CODEC_DEV_OK) {
            /* codec_dev_bus_fill_candidate already logged */
            return ret;
        }
        ret = codec_dev_bus_emit_candidate(emit, &candidate);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "Generate TDM candidates: visitor failed, ret=%d", ret);
            return ret;
        }
    }
    return ESP_CODEC_DEV_OK;
}

static int codec_dev_bus_generate_legacy_tdm_candidates(const codec_dev_i2s_request_t *request,
                                                        codec_dev_bus_emit_ctx_t *emit)
{
    uint16_t app_slot_mask = codec_dev_bus_get_app_slot_mask(&request->sample_info);

    for (size_t i = 0; i < sizeof(s_legal_slot_bits) && !emit->stop; i++) {
        uint8_t slot_bit = s_legal_slot_bits[i];
        if (slot_bit < request->sample_info.bits_per_sample) {
            continue;
        }
        codec_dev_i2s_candidate_t candidate = {0};
        int ret = codec_dev_bus_fill_candidate(&candidate, request, request->sample_info.channel,
                                               slot_bit, request->sample_info.bits_per_sample,
                                               app_slot_mask, NULL);
        if (ret != ESP_CODEC_DEV_OK) {
            /* codec_dev_bus_fill_candidate already logged */
            return ret;
        }
        ret = codec_dev_bus_emit_candidate(emit, &candidate);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "Generate legacy TDM candidates: visitor failed, ret=%d", ret);
            return ret;
        }
    }
    return ESP_CODEC_DEV_OK;
}

static int codec_dev_bus_select_first_candidate(void *ctx, const codec_dev_i2s_candidate_t *candidate, bool *stop)
{
    codec_dev_bus_first_ctx_t *first_ctx = (codec_dev_bus_first_ctx_t *)ctx;
    *first_ctx->out = *candidate;
    first_ctx->found = true;
    *stop = true;
    return ESP_CODEC_DEV_OK;
}

static int codec_dev_bus_match_total_frame_bits(void *ctx, const codec_dev_i2s_candidate_t *candidate, bool *stop)
{
    codec_dev_bus_total_frame_match_ctx_t *match_ctx = (codec_dev_bus_total_frame_match_ctx_t *)ctx;
    if (candidate->bus.total_frame_bits != match_ctx->target_total_frame_bits) {
        return ESP_CODEC_DEV_OK;
    }
    *match_ctx->out = *candidate;
    match_ctx->found = true;
    *stop = true;
    return ESP_CODEC_DEV_OK;
}

static int codec_dev_bus_match_duplex_candidate(void *ctx, const codec_dev_i2s_candidate_t *tx_candidate, bool *stop)
{
    codec_dev_bus_duplex_match_ctx_t *duplex_ctx = (codec_dev_bus_duplex_match_ctx_t *)ctx;
    codec_dev_bus_total_frame_match_ctx_t match_ctx = {
        .target_total_frame_bits = tx_candidate->bus.total_frame_bits,
        .out = duplex_ctx->rx_candidate,
    };
    int ret = codec_dev_bus_for_each_candidate(duplex_ctx->rx_request,
                                               codec_dev_bus_match_total_frame_bits, &match_ctx);
    if (ret != ESP_CODEC_DEV_OK) {
        /* codec_dev_bus_for_each_candidate already logged */
        return ret;
    }
    if (!match_ctx.found) {
        ESP_LOGD(TAG, "Skip duplex match: tx_total_frame_bits=%u rx_nomatch", tx_candidate->bus.total_frame_bits);
        return ESP_CODEC_DEV_OK;
    }
    *duplex_ctx->tx_candidate = *tx_candidate;
    duplex_ctx->found = true;
    *stop = true;
    return ESP_CODEC_DEV_OK;
}

static int codec_dev_bus_validate_reconfig_plan(const codec_dev_i2s_stream_reconfig_plan_t *plan)
{
    if (plan == NULL) {
        ESP_LOGE(TAG, "Validate reconfiguration plan failed: plan is NULL");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (!codec_dev_i2s_stream_plan_needs_apply(plan)) {
        return ESP_CODEC_DEV_OK;
    }
    if (!codec_dev_bus_is_valid_bus_info(&plan->old_bus) || !codec_dev_bus_is_valid_bus_info(&plan->new_bus)) {
        ESP_LOGE(TAG, "Validate reconfiguration plan failed: old or new bus is invalid");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    return ESP_CODEC_DEV_OK;
}

static int codec_dev_bus_validate_plan(const codec_dev_i2s_reconfig_plan_t *plan, const codec_dev_i2s_ops_t *ops)
{
    int ret = codec_dev_bus_validate_reconfig_plan(plan ? &plan->peer : NULL);
    if (ret != ESP_CODEC_DEV_OK) {
        /* codec_dev_bus_validate_reconfig_plan already logged */
        return ret;
    }
    ret = codec_dev_bus_validate_reconfig_plan(plan ? &plan->active : NULL);
    if (ret != ESP_CODEC_DEV_OK) {
        /* codec_dev_bus_validate_reconfig_plan already logged */
        return ret;
    }
    if (ops == NULL || ops->apply_bus == NULL || ops->set_enabled == NULL) {
        ESP_LOGE(TAG, "Validate plan failed: operation callbacks are missing");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    return ESP_CODEC_DEV_OK;
}

static void codec_dev_bus_restore_original_enable_states(const codec_dev_i2s_reconfig_plan_t *plan,
                                                         const codec_dev_i2s_ops_t *ops, void *ctx)
{
    if (codec_dev_i2s_stream_plan_needs_apply(&plan->peer) && plan->peer.was_enabled) {
        ops->set_enabled(ctx, CODEC_DEV_I2S_STREAM_PEER, true);
    }
    if (codec_dev_i2s_stream_plan_needs_apply(&plan->active) && plan->active.was_enabled) {
        ops->set_enabled(ctx, CODEC_DEV_I2S_STREAM_ACTIVE, true);
    }
}

static void codec_dev_bus_disable_affected_streams(const codec_dev_i2s_reconfig_plan_t *plan,
                                                   const codec_dev_i2s_ops_t *ops, void *ctx,
                                                   bool *peer_disabled_ok,
                                                   bool *active_disabled_ok)
{
    if (peer_disabled_ok != NULL) {
        *peer_disabled_ok = true;
    }
    if (active_disabled_ok != NULL) {
        *active_disabled_ok = true;
    }
    if (codec_dev_i2s_stream_plan_needs_apply(&plan->peer)) {
        bool disabled_ok = ops->set_enabled(ctx, CODEC_DEV_I2S_STREAM_PEER, false) == ESP_CODEC_DEV_OK;
        if (peer_disabled_ok != NULL) {
            *peer_disabled_ok = disabled_ok;
        }
    }
    if (codec_dev_i2s_stream_plan_needs_apply(&plan->active)) {
        bool disabled_ok = ops->set_enabled(ctx, CODEC_DEV_I2S_STREAM_ACTIVE, false) == ESP_CODEC_DEV_OK;
        if (active_disabled_ok != NULL) {
            *active_disabled_ok = disabled_ok;
        }
    }
}

static bool codec_dev_bus_restore_old_buses_reverse(const codec_dev_i2s_reconfig_plan_t *plan,
                                                    const codec_dev_i2s_ops_t *ops, void *ctx,
                                                    bool attempted_peer_apply_bus,
                                                    bool attempted_active_apply_bus,
                                                    bool peer_disabled_ok,
                                                    bool active_disabled_ok)
{
    bool restore_ok = true;
    if (attempted_active_apply_bus && codec_dev_i2s_stream_plan_needs_apply(&plan->active)) {
        if (!active_disabled_ok) {
            restore_ok = false;
        } else if (ops->apply_bus(ctx, CODEC_DEV_I2S_STREAM_ACTIVE, &plan->active.old_bus) != ESP_CODEC_DEV_OK) {
            restore_ok = false;
        }
    }
    if (attempted_peer_apply_bus && codec_dev_i2s_stream_plan_needs_apply(&plan->peer)) {
        if (!peer_disabled_ok) {
            restore_ok = false;
        } else if (ops->apply_bus(ctx, CODEC_DEV_I2S_STREAM_PEER, &plan->peer.old_bus) != ESP_CODEC_DEV_OK) {
            restore_ok = false;
        }
    }
    return restore_ok;
}

static int codec_dev_bus_for_each_candidate(const codec_dev_i2s_request_t *request,
                                            codec_dev_i2s_candidate_cb_t on_candidate, void *ctx)
{
    if (on_candidate == NULL) {
        ESP_LOGE(TAG, "Visit candidates failed: visitor is NULL");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = codec_dev_bus_validate_request(request);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    codec_dev_bus_emit_ctx_t emit = {
        .on_candidate = on_candidate,
        .ctx = ctx,
    };
    if (request->mode == ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS) {
        ret = codec_dev_bus_generate_std_candidates(request, &emit);
    } else if (request->mode == ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS && request->map_query != NULL &&
               request->map_query->resolve_cb != NULL) {
        ret = codec_dev_bus_generate_mapping_aware_tdm_candidates(request, &emit);
    } else {
        ret = codec_dev_bus_generate_legacy_tdm_candidates(request, &emit);
    }
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Visit candidates: generation failed, ret=%d", ret);
        return ret;
    }
    if (emit.emitted == 0) {
        ESP_LOGW(TAG, "Visit candidates: no legal candidate, mode=%d ch=%u bits=%u",
                 (int)request->mode, request->sample_info.channel, request->sample_info.bits_per_sample);
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    return ESP_CODEC_DEV_OK;
}

int codec_dev_i2s_resolve(const codec_dev_i2s_request_t *request, codec_dev_i2s_candidate_t *candidate)
{
    if (candidate == NULL) {
        ESP_LOGE(TAG, "Select candidate failed: candidate is NULL");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    codec_dev_bus_first_ctx_t first_ctx = {.out = candidate};
    int ret = codec_dev_bus_for_each_candidate(request, codec_dev_bus_select_first_candidate, &first_ctx);
    if (ret != ESP_CODEC_DEV_OK) {
        /* codec_dev_bus_for_each_candidate already logged */
        return ret;
    }
    return first_ctx.found ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_NOT_SUPPORT;
}

int codec_dev_i2s_negotiate_duplex_bus(const codec_dev_i2s_request_t *tx_request,
                                       const codec_dev_i2s_request_t *rx_request,
                                       codec_dev_i2s_candidate_t *tx_candidate,
                                       codec_dev_i2s_candidate_t *rx_candidate)
{
    if (tx_candidate == NULL || rx_candidate == NULL) {
        ESP_LOGE(TAG, "Negotiate duplex bus failed: output candidate is NULL");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = codec_dev_bus_validate_request(tx_request);
    if (ret != ESP_CODEC_DEV_OK) {
        /* codec_dev_bus_validate_request already logged */
        return ret;
    }
    ret = codec_dev_bus_validate_request(rx_request);
    if (ret != ESP_CODEC_DEV_OK) {
        /* codec_dev_bus_validate_request already logged */
        return ret;
    }
    if (tx_request->sample_info.sample_rate != rx_request->sample_info.sample_rate ||
        tx_request->sample_info.mclk_multiple != rx_request->sample_info.mclk_multiple) {
        ESP_LOGW(TAG, "Negotiate duplex bus: TX and RX clocks do not match");
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    memset(tx_candidate, 0, sizeof(*tx_candidate));
    memset(rx_candidate, 0, sizeof(*rx_candidate));
    /* Candidates are visited in ascending total_frame_bits order, so the first TX candidate that the
       RX side can match already has the minimum common total_frame_bits. */
    codec_dev_bus_duplex_match_ctx_t duplex_ctx = {
        .rx_request = rx_request,
        .tx_candidate = tx_candidate,
        .rx_candidate = rx_candidate,
    };
    ret = codec_dev_bus_for_each_candidate(tx_request, codec_dev_bus_match_duplex_candidate, &duplex_ctx);
    if (ret != ESP_CODEC_DEV_OK) {
        /* codec_dev_bus_for_each_candidate already logged */
        return ret;
    }
    if (!duplex_ctx.found) {
        ESP_LOGW(TAG, "Negotiate duplex bus: no common total_frame_bits, tx_ch=%u rx_ch=%u",
                 tx_request->sample_info.channel, rx_request->sample_info.channel);
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    return ESP_CODEC_DEV_OK;
}

int codec_dev_i2s_apply_plan(const codec_dev_i2s_reconfig_plan_t *plan, const codec_dev_i2s_ops_t *ops, void *ctx)
{
    int ret = codec_dev_bus_validate_plan(plan, ops);
    if (ret != ESP_CODEC_DEV_OK) {
        /* codec_dev_bus_validate_plan already logged */
        return ret;
    }

    const bool peer_needs_apply = codec_dev_i2s_stream_plan_needs_apply(&plan->peer);
    const bool active_needs_apply = codec_dev_i2s_stream_plan_needs_apply(&plan->active);
    bool attempted_peer_apply_bus = false;
    bool attempted_active_apply_bus = false;
    bool peer_disabled_ok = true;
    bool active_disabled_ok = true;

    if (peer_needs_apply && plan->peer.was_enabled) {
        ret = ops->set_enabled(ctx, CODEC_DEV_I2S_STREAM_PEER, false);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Apply plan failed: disable peer returned %d", ret);
            return ret;
        }
    }
    if (active_needs_apply && plan->active.was_enabled) {
        ret = ops->set_enabled(ctx, CODEC_DEV_I2S_STREAM_ACTIVE, false);
        if (ret != ESP_CODEC_DEV_OK) {
            /* The peer was already stopped above when it also needed reconfigure, and nothing has been
               applied yet, so the old geometry is still live and bringing both sides back up is safe.
               Without this the peer stays silent after a failed reconfigure and was_enabled is gone. */
            ESP_LOGE(TAG, "Apply plan failed: disable active returned %d", ret);
            codec_dev_bus_disable_affected_streams(plan, ops, ctx, NULL, NULL);
            goto restore_enable;
        }
    }

    if (peer_needs_apply) {
        attempted_peer_apply_bus = true;
        ret = ops->apply_bus(ctx, CODEC_DEV_I2S_STREAM_PEER, &plan->peer.new_bus);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Apply plan failed: apply peer bus returned %d", ret);
            goto rollback_bus;
        }
    }
    if (active_needs_apply) {
        attempted_active_apply_bus = true;
        ret = ops->apply_bus(ctx, CODEC_DEV_I2S_STREAM_ACTIVE, &plan->active.new_bus);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Apply plan failed: apply active bus returned %d", ret);
            goto rollback_bus;
        }
    }

    if (peer_needs_apply && plan->peer.was_enabled) {
        ret = ops->set_enabled(ctx, CODEC_DEV_I2S_STREAM_PEER, true);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Apply plan failed: enable peer returned %d", ret);
            goto rollback_bus;
        }
    }
    if (active_needs_apply && plan->active.was_enabled) {
        ret = ops->set_enabled(ctx, CODEC_DEV_I2S_STREAM_ACTIVE, true);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Apply plan failed: enable active returned %d", ret);
            goto rollback_bus;
        }
    }
    return ESP_CODEC_DEV_OK;

rollback_bus:
    codec_dev_bus_disable_affected_streams(plan, ops, ctx, &peer_disabled_ok, &active_disabled_ok);
    if (!codec_dev_bus_restore_old_buses_reverse(plan, ops, ctx,
                                                 attempted_peer_apply_bus, attempted_active_apply_bus,
                                                 peer_disabled_ok, active_disabled_ok)) {
        return ret;
    }

restore_enable:
    codec_dev_bus_restore_original_enable_states(plan, ops, ctx);
    return ret;
}
