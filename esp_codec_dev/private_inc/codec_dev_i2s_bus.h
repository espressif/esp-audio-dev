/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_codec_dev_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define CODEC_DEV_I2S_STD_TOTAL_SLOT  (2)

/**
 * @brief  Stream role inside one apply operation
 *
 *         These values identify plan roles rather than TX or RX directions. The peer is processed before
 *         the active stream.
 */
typedef enum {
    CODEC_DEV_I2S_STREAM_PEER   = 0,  /*!< Peer stream on the shared port */
    CODEC_DEV_I2S_STREAM_ACTIVE = 1,  /*!< Stream that requested the change */
} codec_dev_i2s_stream_id_t;

/**
 * @brief  One direction's normalized planner input
 *
 *         sample_info keeps the caller's normalized application-domain request. The planner must not mutate it.
 *
 *         Contract:
 *         - channel and channel_mask are already normalized for planner input.
 *         - For RX, channel_mask selects physical slots in the normalized original frame.
 *         - For TX, channel_mask selects codec logical channels.
 *         - Mono requests are normalized before reaching this type. For example, app channel=1,
 *           mask=BIT(0) becomes planner channel=2, mask=BIT(0).
 *         - sample_info.channel represents the normalized original frame width used for candidate generation.
 *         - sample_info.mclk_multiple is part of the clock identity and must match in duplex mode.
 */
typedef struct {
    esp_codec_dev_i2s_mode_t         mode;         /*!< I2S mode used to generate candidates */
    esp_codec_dev_type_t             dev_type;     /*!< Stream direction */
    esp_codec_dev_sample_info_t      sample_info;  /*!< Normalized application sample request */
    const esp_codec_dev_map_query_t *map_query;    /*!< Optional device map query */
} codec_dev_i2s_request_t;

/**
 * @brief  One legal hardware bus candidate
 *
 *         mapping is meaningful only for mapping-aware TDM candidates.
 */
typedef struct {
    esp_codec_dev_bus_info_t     bus;      /*!< Candidate hardware bus configuration */
    esp_codec_dev_channel_map_t  mapping;  /*!< Device mapping for mapping-aware TDM */
} codec_dev_i2s_candidate_t;

/**
 * @brief  One direction's committed-to-target reconfiguration plan
 *
 * @note  codec_dev_i2s_apply_plan() stops a stream only when it is currently enabled and old_bus
 *         differs from new_bus.
 */
typedef struct {
    esp_codec_dev_bus_info_t  old_bus;      /*!< Bus configuration to restore on rollback */
    esp_codec_dev_bus_info_t  new_bus;      /*!< Target bus configuration */
    bool                      was_enabled;  /*!< Enable state before applying the plan */
} codec_dev_i2s_stream_reconfig_plan_t;

/**
 * @brief  Duplex apply plan with independent peer and active stream plans
 */
typedef struct {
    codec_dev_i2s_stream_reconfig_plan_t  peer;    /*!< Peer stream plan processed first */
    codec_dev_i2s_stream_reconfig_plan_t  active;  /*!< Active stream plan processed second */
} codec_dev_i2s_reconfig_plan_t;

/**
 * @brief  Injected hardware operations used to apply a plan
 */
typedef struct {
    int (*apply_bus)(void *ctx, codec_dev_i2s_stream_id_t stream,
                     const esp_codec_dev_bus_info_t *bus);  /*!< Apply one stream bus to hardware */
    int (*set_enabled)(void *ctx, codec_dev_i2s_stream_id_t stream,
                       bool enable);  /*!< Change one stream enable state */
} codec_dev_i2s_ops_t;

/**
 * @brief  Return whether a slot bit width is accepted by STD and TDM I2S hardware
 *
 * @param[in]  slot_bit  Slot bit width to inspect
 *
 * @return
 *       - true   Width is 8, 16, 24, or 32
 *       - false  Any other value
 */
static inline bool codec_dev_i2s_is_legal_slot_bit(uint8_t slot_bit)
{
    return slot_bit == 8 || slot_bit == 16 || slot_bit == 24 || slot_bit == 32;
}

/**
 * @brief  Tell whether a stream plan changes the bus and has to be applied
 *
 * @note  A zero-initialized plan describes an unused stream role and needs no apply.
 *
 * @param[in]  plan  Stream plan to inspect
 *
 * @return
 *       - true   old_bus and new_bus differ
 *       - false  plan is NULL or both buses are identical
 */
static inline bool codec_dev_i2s_stream_plan_needs_apply(const codec_dev_i2s_stream_reconfig_plan_t *plan)
{
    if (plan == NULL) {
        return false;
    }
    const esp_codec_dev_bus_info_t *old_bus = &plan->old_bus;
    const esp_codec_dev_bus_info_t *new_bus = &plan->new_bus;
    return old_bus->mode != new_bus->mode ||
           old_bus->sample_rate != new_bus->sample_rate ||
           old_bus->mclk_multiple != new_bus->mclk_multiple ||
           old_bus->total_slot != new_bus->total_slot ||
           old_bus->slot_bit != new_bus->slot_bit ||
           old_bus->data_bit != new_bus->data_bit ||
           old_bus->slot_mask != new_bus->slot_mask ||
           old_bus->total_frame_bits != new_bus->total_frame_bits;
}

/**
 * @brief  Resolve the narrowest legal bus candidate for one stream
 *
 * @param[in]   request    Normalized bus request
 * @param[out]  candidate  Narrowest legal candidate
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Candidate resolved
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid request or candidate
 *       - ESP_CODEC_DEV_NOT_SUPPORT  No legal candidate exists
 */
int codec_dev_i2s_resolve(const codec_dev_i2s_request_t *request, codec_dev_i2s_candidate_t *candidate);

/**
 * @brief  Negotiate TX/RX bus configurations with the same total_frame_bits
 *
 *         TX and RX must request equal sample rates and MCLK multiples. The selected buses have equal
 *         total_frame_bits, while their slot masks and channel mappings may differ.
 *
 * @param[in]   tx_request    Normalized TX bus request
 * @param[in]   rx_request    Normalized RX bus request
 * @param[out]  tx_candidate  Selected TX candidate
 * @param[out]  rx_candidate  Selected RX candidate
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Compatible candidates negotiated
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid request or output
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Clock mismatch or no common total_frame_bits
 */
int codec_dev_i2s_negotiate_duplex_bus(const codec_dev_i2s_request_t *tx_request,
                                       const codec_dev_i2s_request_t *rx_request,
                                       codec_dev_i2s_candidate_t *tx_candidate,
                                       codec_dev_i2s_candidate_t *rx_candidate);

/**
 * @brief  Apply a duplex reconfiguration plan
 *
 *         Stops an enabled stream before reconfiguring it, applies changed buses peer-then-active, and restores
 *         their original enable states. On failure, changed buses are restored in reverse order before enable states
 *         are restored.
 *
 * @param[in]  plan  Peer and active stream plans
 * @param[in]  ops   Hardware operations used to apply the plan
 * @param[in]  ctx   Caller-owned operation context
 *
 * @note  Injected apply_bus / set_enabled errors are returned unchanged. The codes below
 *        cover this function and the I2S operations used in this component.
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Plan applied
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid plan or operations
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Injected apply_bus rejected the bus
 *       - ESP_CODEC_DEV_NOT_FOUND    Injected set_enabled found no channel
 *       - ESP_CODEC_DEV_DRV_ERR      Injected apply_bus or set_enabled failed
 */
int codec_dev_i2s_apply_plan(const codec_dev_i2s_reconfig_plan_t *plan, const codec_dev_i2s_ops_t *ops, void *ctx);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
