/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2s_common.h"

#include "audio_codec_data_if.h"
#include "esp_codec_dev_os.h"
#include "esp_codec_dev_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define DEFAULT_WAIT_TIMEOUT  (1000)

typedef struct i2s_data_t i2s_data_t;
typedef struct i2s_port_group_t i2s_port_group_t;

/**
 * @brief  I2S TX/RX stream runtime state
 *
 *         app_fs, bus_info and map_query carry their own validity: a committed
 *         application format always has a non-zero channel count, a committed
 *         bus has a non-zero total_slot, and a registered map query has a
 *         resolve callback. All are cleared by resetting the stream state.
 */
typedef struct {
    void                        *handle;           /*!< Channel handle for TX or RX */
    bool                         req_enable;       /*!< User explicitly requested enable */
    uint8_t                      ext_ref_count;    /*!< Peer refs keeping this stream enabled */
    i2s_data_t                  *peer_ref_target;  /*!< Instance this stream holds one peer ref on */
    bool                         disable_pending;  /*!< Disable deferred until peer side can stop */
    bool                         auto_paired;      /*!< Handle was auto-filled from its peer channel */
    esp_codec_dev_sample_info_t  app_fs;           /*!< Last application format committed for this stream */
    esp_codec_dev_bus_info_t     bus_info;         /*!< Last committed STD/TDM hardware bus;
                                                        zero when no reliable committed bus exists */
    esp_codec_dev_map_query_t    map_query;        /*!< Directional device-map query copy */
} i2s_stream_state_t;

/**
 * @brief  I2S data interface instance
 */
struct i2s_data_t {
    audio_codec_data_if_t  base;        /*!< Base data interface */
    bool                   is_open;     /*!< True after open/config lifecycle completes */
    bool                   closing;     /*!< True once close begins and new operations must stop */
    uint8_t                port;        /*!< I2S port number */
    i2s_clock_src_t        clk_src;     /*!< I2S clock source */
    i2s_port_group_t      *port_group;  /*!< Shared per-port group */
    i2s_data_t            *next;        /*!< Next instance in the same port group */
    i2s_stream_state_t     tx;          /*!< TX stream runtime state */
    i2s_stream_state_t     rx;          /*!< RX stream runtime state */
};

/**
 * @brief  Shared state for all I2S data interfaces on one port
 */
struct i2s_port_group_t {
    uint8_t                       port;       /*!< I2S port number */
    uint8_t                       ref_count;  /*!< Number of instances using this group */
    esp_codec_dev_mutex_handle_t  mutex;      /*!< Protects instances and capability indexes */
    i2s_data_t                   *instances;  /*!< All instances in this port group */
    i2s_data_t                   *duplex;     /*!< First instance with explicit TX and RX handles */
    i2s_data_t                   *rx_only;    /*!< First instance with only an explicit RX handle */
    i2s_data_t                   *tx_only;    /*!< First instance with only an explicit TX handle */
    i2s_port_group_t             *next;       /*!< Next port group in the global list */
};

/**
 * @brief  Stream context used by a planner-backed bus apply
 */
typedef struct {
    i2s_data_t *i2s_data;     /*!< I2S data interface owning the stream */
    bool        is_playback;  /*!< True for TX and false for RX */
} i2s_reconfig_stream_ctx_t;

/**
 * @brief  Peer and active stream contexts for a planner-backed apply
 *
 *         Field names follow codec_dev_i2s_stream_id_t apply roles. For self-duplex,
 *         peer maps to RX and active maps to TX so apply_plan stays peer-first.
 */
typedef struct {
    i2s_reconfig_stream_ctx_t  peer;    /*!< Peer stream reconfig context */
    i2s_reconfig_stream_ctx_t  active;  /*!< Active stream reconfig context */
} i2s_reconfig_ctx_t;

/**
 * @brief  Peer reference changes made by one tracked acquire operation
 */
typedef struct {
    i2s_data_t *previous_target;        /*!< Target held before acquisition */
    i2s_data_t *acquired_target;        /*!< Target selected by acquisition */
    bool        reference_changed;      /*!< True when the target link or count changed */
    bool        self_paired;            /*!< True when no external target owns the clock */
    bool        target_was_enabled;     /*!< Hardware enabled state before acquisition */
    bool        target_enable_changed;  /*!< True when acquisition enabled the target */
} i2s_peer_ref_change_t;

static inline bool _stream_has_app_fs(const i2s_stream_state_t *stream)
{
    return stream != NULL && stream->app_fs.channel != 0;
}

static inline i2s_stream_state_t *_get_stream_state(i2s_data_t *i2s_data, bool is_playback)
{
    return is_playback ? &i2s_data->tx : &i2s_data->rx;
}

static inline i2s_chan_handle_t _get_channel_handle(i2s_data_t *i2s_data, bool is_playback)
{
    i2s_stream_state_t *stream = _get_stream_state(i2s_data, is_playback);
    return (i2s_chan_handle_t)stream->handle;
}

static inline bool _has_explicit_stream(i2s_data_t *i2s_data, bool is_playback)
{
    i2s_stream_state_t *stream = _get_stream_state(i2s_data, is_playback);
    return stream->handle != NULL && !stream->auto_paired;
}

static inline esp_codec_dev_sample_info_t *_get_stream_app_fs(i2s_data_t *i2s_data, bool is_playback)
{
    i2s_stream_state_t *stream = _get_stream_state(i2s_data, is_playback);
    return &stream->app_fs;
}

static inline bool _stream_has_bus_info(const i2s_stream_state_t *stream)
{
    return stream != NULL && stream->bus_info.total_slot != 0;
}

static inline bool _stream_has_map_query(const i2s_stream_state_t *stream)
{
    return stream != NULL && stream->map_query.resolve_cb != NULL;
}

static inline bool _should_keep_running(i2s_data_t *i2s_data, bool is_playback)
{
    i2s_stream_state_t *stream = _get_stream_state(i2s_data, is_playback);
    return stream->req_enable || stream->ext_ref_count > 0;
}

/**
 * 1. Only master mode can output MCLK/BCLK/WS and drive sample-rate/bit-width/channel settings.
 * 2. At most one master can exist on the same port.
 * 3. In slave mode, bclk_div >= 4 is required for better stability, and if TX is slave, bclk_div >= 6 is required for better stability.
 *
 * As for following cases:
 * 1. tx_chan_cfg.role is slave, rx_chan_cfg.role is master
 * 2. tx_chan_cfg.role is master, rx_chan_cfg.role is slave
 * 3. tx_chan_cfg.role is slave, rx_chan_cfg.role is slave
 * 4. tx_chan_cfg.role is master, rx_chan_cfg.role is master, this must be duplex_mode, or else will cause timing conflict
 *    1. IDF >= v5.5.2, this will be automatically detected and the later inited will be forced to slave mode
 *    2. IDF <= v5.5.1, will force rx to slave mode, but info.role not updated to slave
 *
 * Note: If user set tx/rx as slave, then this can not change fs, because slave can not output BCLK/WS.
 *       But if we ensure need use both tx and rx, it is a good idea to set role by ourselves.
 */
static inline bool _is_master(i2s_chan_handle_t channel)
{
    i2s_chan_info_t info = {0};
    if (i2s_channel_get_info(channel, &info) != ESP_OK) {
        return false;
    }
    return info.role == I2S_ROLE_MASTER;
}

static inline bool _is_initialized(i2s_chan_handle_t channel)
{
    i2s_chan_info_t info = {0};
    if (i2s_channel_get_info(channel, &info) != ESP_OK) {
        return false;
    }
    return info.mode != I2S_COMM_MODE_NONE;
}

static inline bool _is_enabled(i2s_chan_handle_t channel)
{
    i2s_chan_info_t info = {0};
    if (i2s_channel_get_info(channel, &info) != ESP_OK) {
        return false;
    }
    return info.is_enabled;
}

static inline i2s_chan_handle_t _get_peer_channel(i2s_chan_handle_t channel)
{
    i2s_chan_info_t info = {0};
    if (i2s_channel_get_info(channel, &info) != ESP_OK) {
        return NULL;
    }
    return info.pair_chan;
}

static inline void _clear_pending(i2s_data_t *i2s_data, bool is_playback)
{
    i2s_stream_state_t *stream = _get_stream_state(i2s_data, is_playback);
    stream->disable_pending = false;
}

static inline void _set_pending(i2s_data_t *i2s_data, bool is_playback)
{
    i2s_stream_state_t *stream = _get_stream_state(i2s_data, is_playback);
    stream->disable_pending = true;
}

/**
 * @brief  Program the I2S driver with a sample format
 *
 * @note  The channel is disabled while it is reconfigured and re-enabled afterwards only when it
 *         was running on entry
 *
 * @param[in]  channel      Channel handle to reconfigure
 * @param[in]  is_playback  True for TX and false for RX
 * @param[in]  slot_bits    Slot bit width requested by the bus plan
 * @param[in]  clk_src      Clock source to use
 * @param[in]  fs           Sample format to program
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Slot width or channel count cannot be expressed in this mode
 *       - ESP_CODEC_DEV_DRV_ERR      I2S driver call failed
 */
int i2s_data_hw_set_fs(i2s_chan_handle_t channel, bool is_playback, int slot_bits, i2s_clock_src_t clk_src,
                       const esp_codec_dev_sample_info_t *fs);

/**
 * @brief  Enable or disable one channel of an instance
 *
 * @note  Does nothing when the channel already is in the requested state
 *
 * @param[in]  i2s_data     I2S data interface instance
 * @param[in]  is_playback  True for TX and false for RX
 * @param[in]  enable       True to enable the channel and false to disable it
 *
 * @return
 *       - ESP_CODEC_DEV_OK         On success
 *       - ESP_CODEC_DEV_NOT_FOUND  Instance has no handle for this direction
 *       - ESP_CODEC_DEV_DRV_ERR    I2S driver call failed
 */
int i2s_data_hw_enable(i2s_data_t *i2s_data, bool is_playback, bool enable);

/**
 * @brief  Read the bus geometry currently programmed on a channel
 *
 * @param[in]   channel   Channel handle to query
 * @param[out]  bus_info  Populated on success; undefined on error
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  channel or bus_info is NULL
 *       - ESP_CODEC_DEV_WRONG_STATE  Channel is not configured yet
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Communication mode carries no slot geometry
 *       - ESP_CODEC_DEV_DRV_ERR      I2S driver query failed
 */
int i2s_data_hw_get_bus_info(i2s_chan_handle_t channel, esp_codec_dev_bus_info_t *bus_info);

/**
 * @brief  Read the sample format currently programmed on a channel
 *
 * @param[in]   channel  Channel handle to query
 * @param[out]  fs       Populated on success; undefined on error
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_WRONG_STATE  Channel is not configured yet
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Communication mode is not handled
 *       - ESP_CODEC_DEV_DRV_ERR      I2S driver query failed
 */
int i2s_data_hw_get_fs(i2s_chan_handle_t channel, esp_codec_dev_sample_info_t *fs);

/**
 * @brief  Read the communication mode currently programmed on a channel
 *
 * @param[in]   channel  Channel handle to query
 * @param[out]  mode     Set to ESP_CODEC_DEV_I2S_MODE_NONE when the mode is unknown
 *
 * @return
 *       - ESP_CODEC_DEV_OK       On success
 *       - ESP_CODEC_DEV_DRV_ERR  I2S driver query failed
 */
int i2s_data_hw_get_mode(i2s_chan_handle_t channel, esp_codec_dev_i2s_mode_t *mode);

/**
 * @brief  Take the process-wide port-group list lock
 *
 * @note  Protects the global group list and each group's ref_count. Do not use this
 *         lock to mutate a group's instance list or peer refs.
 *
 * @return
 *       - ESP_CODEC_DEV_OK       On success
 *       - ESP_CODEC_DEV_NO_MEM   List mutex allocation failed
 *       - ESP_CODEC_DEV_TIMEOUT  Mutex was not acquired in time
 */
int i2s_data_port_list_lock(void);

/**
 * @brief  Release the process-wide port-group list lock
 */
void i2s_data_port_list_unlock(void);

/**
 * @brief  Take a port group's mutex
 *
 * @note  Protects instances, capability indexes and peer refs of this group. Does not
 *         take the list lock. When the group might be freed, hold the list lock until
 *         this call succeeds, then release the list lock.
 *
 * @param[in]  group  Port group to lock
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  group is NULL
 *       - ESP_CODEC_DEV_WRONG_STATE  group has no mutex
 *       - ESP_CODEC_DEV_TIMEOUT      Mutex was not acquired in time
 */
int i2s_data_port_group_lock(i2s_port_group_t *group);

/**
 * @brief  Release a port group's mutex
 *
 * @note  Call only after a successful i2s_data_port_group_lock()
 *
 * @param[in]  group  Port group to unlock
 */
void i2s_data_port_group_unlock(i2s_port_group_t *group);

/**
 * @brief  Get the group of a port, creating it on first use
 *
 * @note  Takes one reference on the returned group; release it with i2s_data_port_group_release()
 *
 * @param[in]  port  I2S port number
 *
 * @return
 *       - NULL    List lock or allocation failed
 *       - Others  Port group holding one reference for the caller
 */
i2s_port_group_t *i2s_data_port_group_acquire(uint8_t port);

/**
 * @brief  Give back one reference on a port group
 *
 * @note  Frees the group once the last reference is gone; takes the list lock internally
 *
 * @param[in]  group  Port group, may be NULL
 */
void i2s_data_port_group_release(i2s_port_group_t *group);

/**
 * @brief  Give back one reference on a port group with the list lock already held
 *
 * @param[in]  group  Port group, may be NULL
 */
void i2s_data_port_group_release_locked(i2s_port_group_t *group);

/**
 * @brief  Add an instance to a port group
 *
 * @note  Caller must hold the group lock
 *
 * @param[in]  group     Port group to add to
 * @param[in]  i2s_data  I2S data interface instance
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  group or i2s_data is NULL
 */
int i2s_data_port_group_add_instance(i2s_port_group_t *group, i2s_data_t *i2s_data);

/**
 * @brief  Remove an instance from a port group
 *
 * @note  Caller must hold the group lock. Every peer ref the group holds on the instance is
 *         given back before it leaves the list
 *
 * @param[in]  group     Port group to remove from
 * @param[in]  i2s_data  I2S data interface instance
 */
void i2s_data_port_group_remove_instance(i2s_port_group_t *group, i2s_data_t *i2s_data);

/**
 * @brief  Rebuild the duplex, rx_only and tx_only shortcuts of a port group
 *
 * @note  Caller must hold the group lock
 *
 * @param[in]  group  Port group to refresh
 */
void i2s_data_port_group_refresh_capability_index(i2s_port_group_t *group);

/**
 * @brief  Find another instance in the group that owns the opposite direction
 *
 * @note  Caller must hold the group lock; the returned pointer is valid while it is held
 *
 * @param[in]  group        Port group to search
 * @param[in]  self         Instance to exclude from the search
 * @param[in]  is_playback  Direction of the caller: true for TX and false for RX
 *
 * @return
 *       - NULL    No other instance covers the opposite direction
 *       - Others  Peer instance
 */
i2s_data_t *i2s_data_port_group_find_peer(i2s_port_group_t *group, i2s_data_t *self, bool is_playback);

/**
 * @brief  Resolve the instance a peer ref should be taken on
 *
 * @note  Falls back to the instance itself when it owns both directions
 *
 * @param[in]   i2s_data     I2S data interface instance
 * @param[in]   is_playback  Direction of the caller: true for TX and false for RX
 * @param[out]  self_paired  Set to true when the target is the caller's own auto-paired channel
 *
 * @return
 *       - NULL    No channel of the opposite direction is reachable
 *       - Others  Instance the peer ref applies to
 */
i2s_data_t *i2s_data_port_get_peer_ref_target(i2s_data_t *i2s_data, bool is_playback, bool *self_paired);

/**
 * @brief  Take a peer ref keeping the opposite-direction channel alive
 *
 * @note  Only master peers are referenced; a slave peer needs no ref and returns success.
 *         Caller must hold the port-group mutex. On failure the entry ref target and count are
 *         restored before returning, so the call is atomic from the caller's viewpoint.
 *
 * @param[in]   i2s_data     I2S data interface instance
 * @param[in]   is_playback  Direction of the caller: true for TX and false for RX
 * @param[in]   enable_peer  True to also enable the peer channel when it is idle
 * @param[out]  change       Optional snapshot of ref and enable changes; NULL when unused.
 *                           Non-NULL values are valid input for i2s_data_port_restore_peer_ref()
 *
 * @return
 *       - ESP_CODEC_DEV_OK       On success or when no peer ref is needed
 *       - ESP_CODEC_DEV_DRV_ERR  Enabling the peer channel failed
 */
int i2s_data_port_acquire_peer_ref(i2s_data_t *i2s_data, bool is_playback,
                                   bool enable_peer, i2s_peer_ref_change_t *change);

/**
 * @brief  Restore peer ref and enable state recorded by a tracked acquire
 *
 * @note  Caller must hold the port-group mutex. Does not modify req_enable or disable_pending.
 *         Call once for each successful tracked acquire that must be rolled back.
 *
 * @param[in]  i2s_data     I2S data interface instance
 * @param[in]  is_playback  Direction of the caller: true for TX and false for RX
 * @param[in]  change       Snapshot returned by i2s_data_port_acquire_peer_ref()
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success or when no restore is needed
 *       - ESP_CODEC_DEV_INVALID_ARG  change is NULL
 *       - ESP_CODEC_DEV_DRV_ERR      Restoring the peer enable state failed
 */
int i2s_data_port_restore_peer_ref(i2s_data_t *i2s_data, bool is_playback,
                                   const i2s_peer_ref_change_t *change);

/**
 * @brief  Give back the peer ref this stream holds
 *
 * @note  The ref is returned to the instance it was taken on, even when the group would now pick a
 *         different peer. Caller must hold the port-group mutex.
 *
 * @param[in]  i2s_data           I2S data interface instance
 * @param[in]  is_playback        Direction of the caller: true for TX and false for RX
 * @param[in]  disable_if_unused  True to disable the peer channel once nothing keeps it running
 *
 * @return
 *       - ESP_CODEC_DEV_OK       On success or when no peer ref was held
 *       - ESP_CODEC_DEV_DRV_ERR  Disabling the peer channel failed
 */
int i2s_data_port_release_peer_ref(i2s_data_t *i2s_data, bool is_playback, bool disable_if_unused);

/**
 * @brief  Resolve a bus for a requested sample format and apply it to the port
 *
 * @note  Reconfigures the peer channel as well when both directions must share one frame layout,
 *         and rolls the peer back when the active stream cannot be programmed
 * @note  Caller must hold the port-group mutex.
 *
 * @param[in]  i2s_data     I2S data interface instance
 * @param[in]  is_playback  Direction being configured: true for TX and false for RX
 * @param[in]  app_fs       Application-domain sample format being requested
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_NOT_SUPPORT  No bus can carry both directions
 *       - ESP_CODEC_DEV_DRV_ERR      Programming a channel failed
 */
int i2s_data_reconfig_apply_fs(i2s_data_t *i2s_data, bool is_playback, const esp_codec_dev_sample_info_t *app_fs);

/**
 * @brief  Negotiate and apply one bus for an instance owning both TX and RX
 *
 * @param[in]  i2s_data  I2S data interface instance with explicit TX and RX handles
 * @param[in]  app_fs    Application-domain sample format being requested
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  Instance does not own both directions
 *       - ESP_CODEC_DEV_NOT_SUPPORT  No bus can carry both directions
 *       - ESP_CODEC_DEV_DRV_ERR      Programming a channel failed
 */
int i2s_data_reconfig_apply_self_duplex(i2s_data_t *i2s_data, const esp_codec_dev_sample_info_t *app_fs);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
