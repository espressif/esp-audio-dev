/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "esp_clk_tree.h"
#include "hal/clk_tree_hal.h"
#include "hal/i2s_types.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_common.h"
#include "esp_cpu.h"
#include "esp_log.h"

#include "audio_codec_data_if.h"
#if CONFIG_IDF_TARGET_ESP32
#include "codec_dev_data_cvt.h"
#endif  /* CONFIG_IDF_TARGET_ESP32 */
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_os.h"

static const char *TAG = "I2S_IF";

#define DEFAULT_WAIT_TIMEOUT    (1000)
#define DISABLED_CHAN_SLEEP_MS  (10)
#define I2S_DATA_MAX_CHANNELS   (8)

typedef struct i2s_data_t i2s_data_t;
typedef struct i2s_port_group_t i2s_port_group_t;

/**
 * @brief  I2S TX/RX stream runtime state
 */
typedef struct {
    void                        *handle;           /*!< Channel handle for TX or RX */
    bool                         req_enable;       /*!< User explicitly requested enable */
    uint8_t                      ext_ref_count;    /*!< Peer refs keeping this stream enabled */
    bool                         peer_linked;      /*!< This stream has acquired one peer ref */
    bool                         disable_pending;  /*!< Disable deferred until peer side can stop */
    bool                         auto_paired;      /*!< Handle was auto-filled from its peer channel */
    esp_codec_dev_sample_info_t  fs;               /*!< Active sample format */
} i2s_stream_state_t;

/**
 * @brief  I2S data interface instance
 */
struct i2s_data_t {
    audio_codec_data_if_t  base;             /*!< Base data interface */
    bool                   is_open;          /*!< True after open/config lifecycle completes */
    uint8_t                port;             /*!< I2S port number */
    uint16_t               total_slot_bits;  /*!< Total bus slot width negotiated for the pair */
    i2s_clock_src_t        clk_src;          /*!< I2S clock source */
    i2s_port_group_t      *port_group;       /*!< Shared per-port group */
    i2s_data_t            *next;             /*!< Next instance in the same port group */
    i2s_stream_state_t     tx;               /*!< TX stream runtime state */
    i2s_stream_state_t     rx;               /*!< RX stream runtime state */
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
    i2s_port_group_t             *next;       /*!< Next port group in the global registry */
};

/**
 * I2S data interfaces that share one I2S port coordinate through this process-wide
 * registry. The list is protected by s_i2s_port_group_list_mutex.
 */
static i2s_port_group_t *s_i2s_port_group_list = NULL;
static esp_codec_dev_mutex_handle_t s_i2s_port_group_list_mutex = NULL;

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

static inline esp_codec_dev_sample_info_t *_get_channel_fs(i2s_data_t *i2s_data, bool is_playback)
{
    i2s_stream_state_t *stream = _get_stream_state(i2s_data, is_playback);
    return &stream->fs;
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

static inline bool _i2s_data_is_open(const audio_codec_data_if_t *h)
{
    i2s_data_t *i2s_data = (i2s_data_t *)h;
    if (i2s_data) {
        return i2s_data->is_open;
    }
    return false;
}

static inline void _clear_all_pending(i2s_data_t *i2s_data)
{
    i2s_data->tx.disable_pending = false;
    i2s_data->rx.disable_pending = false;
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

static const char *_i2s_mode_to_str(i2s_comm_mode_t mode)
{
    switch (mode) {
        case I2S_COMM_MODE_NONE:
            return "NONE";
        case I2S_COMM_MODE_STD:
            return "STD";
#if SOC_I2S_SUPPORTS_TDM
        case I2S_COMM_MODE_TDM:
            return "TDM";
#endif  /* SOC_I2S_SUPPORTS_TDM */
#if SOC_I2S_SUPPORTS_PDM
        case I2S_COMM_MODE_PDM:
            return "PDM";
#endif  /* SOC_I2S_SUPPORTS_PDM */
        default:
            return "UNKNOWN";
    }
}

// IDF >= v5.5.2 supports lazy init and can automatically form full-duplex mode.
static inline void _show_channel_info(i2s_chan_handle_t channel)
{
    i2s_chan_info_t info = {0};
    if (i2s_channel_get_info(channel, &info) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get I2S channel info");
        return;
    }
    ESP_LOGI(TAG, "I2S channel info: enabled=%d, clk_src=%d, sclk_hz=%" PRIu32 ", mclk_hz=%" PRIu32 ", bclk_hz=%" PRIu32 ", mode_cfg=%p",
             info.is_enabled, (int)info.clk_src, info.sclk_hz, info.mclk_hz, info.bclk_hz, info.mode_cfg);
    ESP_LOGI(TAG, "I2S channel info: total_dma_buf_size=%" PRIu32,
             info.total_dma_buf_size);
    ESP_LOGI(TAG, "I2S channel info: port=%d, role=%s, dir=%s, mode=%s, pair_chan=%p",
             info.id, info.role == I2S_ROLE_MASTER ? "MASTER" : "SLAVE",
             info.dir == I2S_DIR_TX ? "TX" : "RX", _i2s_mode_to_str(info.mode), info.pair_chan);
}

static void _check_mclk_jitter(int mode, void *clk_cfg)
{
    uint32_t source_clk = 0;
    uint32_t mclk = 0;
    i2s_clock_src_t clk_src __attribute__((unused)) = I2S_CLK_SRC_DEFAULT;
    if (I2S_CLK_SRC_DEFAULT == 0) {
        // ESP32P4 I2S_CLK_SRC_DEFAULT is zero, so can not get source_clk, skip check
        return;
    }
    switch (mode) {
        case I2S_COMM_MODE_STD: {
            i2s_std_clk_config_t *std_clk_cfg = (i2s_std_clk_config_t *)clk_cfg;
            clk_src = std_clk_cfg->clk_src;
            esp_clk_tree_src_get_freq_hz(std_clk_cfg->clk_src, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &source_clk);
            mclk = std_clk_cfg->sample_rate_hz * std_clk_cfg->mclk_multiple;
            break;
        }
#if SOC_I2S_SUPPORTS_TDM
        case I2S_COMM_MODE_TDM: {
            i2s_tdm_clk_config_t *tdm_clk_cfg = (i2s_tdm_clk_config_t *)clk_cfg;
            clk_src = tdm_clk_cfg->clk_src;
            esp_clk_tree_src_get_freq_hz(tdm_clk_cfg->clk_src, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &source_clk);
            mclk = tdm_clk_cfg->sample_rate_hz * tdm_clk_cfg->mclk_multiple;
            break;
        }
#endif  /* SOC_I2S_SUPPORTS_TDM */
#if SOC_I2S_SUPPORTS_PDM
        case I2S_COMM_MODE_PDM: {
            i2s_pdm_tx_clk_config_t *pdm_clk_cfg = (i2s_pdm_tx_clk_config_t *)clk_cfg;
            clk_src = pdm_clk_cfg->clk_src;
            esp_clk_tree_src_get_freq_hz(pdm_clk_cfg->clk_src, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &source_clk);
            mclk = pdm_clk_cfg->sample_rate_hz * pdm_clk_cfg->mclk_multiple;
            break;
        }
#endif  /* SOC_I2S_SUPPORTS_PDM */
        default: {
            ESP_LOGE(TAG, "Unsupported mode: %d", mode);
            return;
        }
    }
#if SOC_I2S_SUPPORTS_APLL
    if (clk_src == I2S_CLK_SRC_APLL) {
        source_clk = clk_hal_apll_get_freq_hz();
    }
#endif  /* SOC_I2S_SUPPORTS_APLL */
    {
        float_t jitter = (float_t)source_clk / (float_t)mclk - (uint32_t)(source_clk / mclk);
        jitter = (0.5f - fabsf(0.5f - jitter)) / 0.5f;
        jitter = jitter * 100.0f;
        int jitter_pct = (int)(jitter + 0.5f);
        if (jitter_pct > 10) {
            ESP_LOGW(TAG, "MCLK jitter ratio: %d%%, source_clk: %" PRIu32 ", mclk: %" PRIu32, jitter_pct, source_clk, mclk);
        } else {
            ESP_LOGI(TAG, "MCLK jitter ratio: %d%%, source_clk: %" PRIu32 ", mclk: %" PRIu32, jitter_pct, source_clk, mclk);
        }
    }
}

static esp_codec_dev_mutex_handle_t _port_group_get_registry_mutex(void)
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

static int _port_group_registry_lock(void)
{
    esp_codec_dev_mutex_handle_t mutex = _port_group_get_registry_mutex();
    if (mutex == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }
    int ret = esp_codec_dev_mutex_lock(mutex, DEFAULT_WAIT_TIMEOUT);
    if (ret != ESP_CODEC_DEV_OK) {
        return ESP_CODEC_DEV_TIMEOUT;
    }
    return ESP_CODEC_DEV_OK;
}

static int _port_group_registry_unlock(void)
{
    esp_codec_dev_mutex_handle_t mutex = s_i2s_port_group_list_mutex;
    if (mutex == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = esp_codec_dev_mutex_unlock(mutex);
    if (ret != ESP_CODEC_DEV_OK) {
        return ESP_CODEC_DEV_DRV_ERR;
    }
    return ESP_CODEC_DEV_OK;
}

static i2s_port_group_t *_port_group_acquire(uint8_t port)
{
    if (_port_group_registry_lock() != ESP_CODEC_DEV_OK) {
        return NULL;
    }
    i2s_port_group_t *cur = s_i2s_port_group_list;
    while (cur != NULL) {
        if (cur->port == port) {
            cur->ref_count++;
            _port_group_registry_unlock();
            return cur;
        }
        cur = cur->next;
    }
    i2s_port_group_t *group = calloc(1, sizeof(i2s_port_group_t));
    if (group == NULL) {
        ESP_LOGE(TAG, "Failed to allocate port group");
        _port_group_registry_unlock();
        return NULL;
    }
    group->mutex = esp_codec_dev_mutex_create();
    if (group->mutex == NULL) {
        ESP_LOGE(TAG, "Failed to allocate port mutex");
        free(group);
        _port_group_registry_unlock();
        return NULL;
    }
    group->port = port;
    group->ref_count = 1;
    group->next = s_i2s_port_group_list;
    s_i2s_port_group_list = group;
    _port_group_registry_unlock();
    return group;
}

static void _port_group_release(i2s_port_group_t *group)
{
    if (group == NULL) {
        return;
    }
    if (_port_group_registry_lock() != ESP_CODEC_DEV_OK) {
        return;
    }
    if (group->ref_count > 0) {
        group->ref_count--;
    }
    if (group->ref_count > 0) {
        _port_group_registry_unlock();
        return;
    }
    i2s_port_group_t *prev = NULL;
    i2s_port_group_t *cur = s_i2s_port_group_list;
    while (cur != NULL) {
        if (cur == group) {
            if (prev == NULL) {
                s_i2s_port_group_list = cur->next;
            } else {
                prev->next = cur->next;
            }
            if (cur->instances != NULL) {
                ESP_LOGW(TAG, "Port group %d is released with dangling instances", cur->port);
            }
            esp_codec_dev_mutex_destroy(cur->mutex);
            free(cur);
            _port_group_registry_unlock();
            return;
        }
        prev = cur;
        cur = cur->next;
    }
    _port_group_registry_unlock();
}

static void _port_group_refresh_capability_index(i2s_port_group_t *group)
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

static int _port_group_add_instance(i2s_port_group_t *group, i2s_data_t *i2s_data)
{
    if (group == NULL || i2s_data == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    i2s_data->next = group->instances;
    group->instances = i2s_data;
    _port_group_refresh_capability_index(group);
    return ESP_CODEC_DEV_OK;
}

static void _port_group_remove_instance(i2s_port_group_t *group, i2s_data_t *i2s_data)
{
    if (group == NULL || i2s_data == NULL) {
        return;
    }
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
            _port_group_refresh_capability_index(group);
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

static i2s_data_t *_port_group_find_peer(i2s_port_group_t *group, i2s_data_t *self, bool is_playback)
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

static i2s_data_t *_get_peer_ref_target(i2s_data_t *i2s_data, bool is_playback, bool *self_paired)
{
    i2s_data_t *peer_data = _port_group_find_peer(i2s_data->port_group, i2s_data, is_playback);
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

static inline int _i2s_lock(i2s_data_t *i2s_data)
{
    if (i2s_data == NULL || i2s_data->port_group == NULL || i2s_data->port_group->mutex == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = esp_codec_dev_mutex_lock(i2s_data->port_group->mutex, DEFAULT_WAIT_TIMEOUT);
    if (ret != ESP_CODEC_DEV_OK) {
        return ESP_CODEC_DEV_TIMEOUT;
    }
    return ESP_CODEC_DEV_OK;
}

static inline int _i2s_unlock(i2s_data_t *i2s_data)
{
    if (i2s_data == NULL || i2s_data->port_group == NULL || i2s_data->port_group->mutex == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = esp_codec_dev_mutex_unlock(i2s_data->port_group->mutex);
    if (ret != ESP_CODEC_DEV_OK) {
        return ESP_CODEC_DEV_DRV_ERR;
    }
    return ESP_CODEC_DEV_OK;
}

static int _i2s_drv_enable(i2s_data_t *i2s_data, bool is_playback, bool enable)
{
    i2s_chan_handle_t channel = _get_channel_handle(i2s_data, is_playback);
    if (channel == NULL) {
        return ESP_CODEC_DEV_NOT_FOUND;
    }
    if (_is_enabled(channel) == enable) {
        return ESP_CODEC_DEV_OK;
    }
    int ret;
    if (enable) {
        ret = i2s_channel_enable(channel);
    } else {
        ret = i2s_channel_disable(channel);
    }
    return ret == ESP_OK ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}

static int _acquire_peer_ref(i2s_data_t *i2s_data, bool is_playback, bool enable_hw)
{
    bool self_paired = false;
    i2s_data_t *peer_data = _get_peer_ref_target(i2s_data, is_playback, &self_paired);
    if (peer_data == NULL) {
        return ESP_CODEC_DEV_OK;
    }
    i2s_chan_handle_t peer_channel = _get_channel_handle(peer_data, !is_playback);
    if (peer_channel == NULL || !_is_master(peer_channel)) {
        return ESP_CODEC_DEV_OK;
    }
    if (self_paired) {
        if (enable_hw && !_is_enabled(peer_channel)) {
            return _i2s_drv_enable(peer_data, !is_playback, true);
        }
        return ESP_CODEC_DEV_OK;
    }
    i2s_stream_state_t *active_stream = _get_stream_state(i2s_data, is_playback);
    i2s_stream_state_t *peer_stream = _get_stream_state(peer_data, !is_playback);
    bool added_ref = false;
    if (!active_stream->peer_linked) {
        peer_stream->ext_ref_count++;
        active_stream->peer_linked = true;
        added_ref = true;
    }
    if (enable_hw && !_is_enabled(peer_channel)) {
        int ret = _i2s_drv_enable(peer_data, !is_playback, true);
        if (ret != ESP_CODEC_DEV_OK && added_ref) {
            if (peer_stream->ext_ref_count > 0) {
                peer_stream->ext_ref_count--;
            }
            active_stream->peer_linked = false;
        }
        return ret;
    }
    return ESP_CODEC_DEV_OK;
}

static int _release_peer_ref(i2s_data_t *i2s_data, bool is_playback, bool disable_if_unused)
{
    bool self_paired = false;
    i2s_data_t *peer_data = _get_peer_ref_target(i2s_data, is_playback, &self_paired);
    if (peer_data == NULL) {
        return ESP_CODEC_DEV_OK;
    }
    if (self_paired) {
        if (disable_if_unused) {
            i2s_chan_handle_t peer_channel = _get_channel_handle(peer_data, !is_playback);
            if (peer_channel && !_should_keep_running(peer_data, !is_playback)) {
                return _i2s_drv_enable(peer_data, !is_playback, false);
            }
        }
        return ESP_CODEC_DEV_OK;
    }

    i2s_stream_state_t *active_stream = _get_stream_state(i2s_data, is_playback);
    if (!active_stream->peer_linked) {
        return ESP_CODEC_DEV_OK;
    }
    active_stream->peer_linked = false;
    i2s_stream_state_t *peer_stream = _get_stream_state(peer_data, !is_playback);
    if (peer_stream->ext_ref_count > 0) {
        peer_stream->ext_ref_count--;
    }
    if (disable_if_unused) {
        i2s_chan_handle_t peer_channel = _get_channel_handle(peer_data, !is_playback);
        if (peer_channel && !_should_keep_running(peer_data, !is_playback)) {
            return _i2s_drv_enable(peer_data, !is_playback, false);
        }
    }
    return ESP_CODEC_DEV_OK;
}

/**
 * @brief  Set I2S driver sample format (sample rate, bit width, slot configuration)
 * @note  The channel must be disabled before reconfiguring. If it was enabled
 *         before calling this function, the caller is responsible for re-enabling
 *         it after reconfiguration completes.
 */
static int _set_drv_fs(i2s_chan_handle_t channel, bool is_playback, uint8_t slot_bits, i2s_clock_src_t clk_src, const esp_codec_dev_sample_info_t *fs)
{
    i2s_chan_info_t chan_info = {0};
    int ret = ESP_CODEC_DEV_OK;
    if (i2s_channel_get_info(channel, &chan_info) != ESP_OK) {
        return ESP_CODEC_DEV_DRV_ERR;
    }
    if (chan_info.is_enabled) {
        ESP_LOGD(TAG, "Channel is enabled, need disable first");
        ret = i2s_channel_disable(channel);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to disable channel");
            return ESP_CODEC_DEV_DRV_ERR;
        }
    }
    int effective_mclk = fs->mclk_multiple ? fs->mclk_multiple : I2S_MCLK_MULTIPLE_256;
    int bclk_div = effective_mclk / (fs->channel * slot_bits);
    if (chan_info.dir == I2S_DIR_TX && !_is_master(channel)) {
        int need_mclk_multiple = fs->channel * slot_bits * 6;
        if (bclk_div < 6) {
            ESP_LOGW(TAG, "BCLK division %d is too small for TX slave; increase MCLK multiple to %d",
                     bclk_div, need_mclk_multiple);
        }
    }
    switch (chan_info.mode) {
        case I2S_COMM_MODE_STD: {
            // Support the following cases:
            // 1. 2ch && mask < 0x03 : use mono mode
            // 2. 2ch && mask >= 0x03: use stereo mode
            // 3. 4ch                : use stereo mode, and data_bits = slot_bits = 2 * bits_per_sample
            uint8_t data_bits = fs->bits_per_sample;
            int slot_mask = (fs->channel > 2) ? I2S_STD_SLOT_BOTH : fs->channel_mask;
            slot_mask = slot_mask & I2S_STD_SLOT_BOTH;
            i2s_slot_mode_t slot_mode = (slot_mask == I2S_STD_SLOT_BOTH) ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO;
            // STD use 2ch 32bit to get 4ch 16bit data
            if (fs->channel > 2) {
#if SOC_I2S_HW_VERSION_2
                ESP_LOGW(TAG, "TDM mode is recommended for 4-channel 16-bit data; avoid using STD mode with 2-channel 32-bit data");
#endif  /* SOC_I2S_HW_VERSION_2 */
                // Convert to 2ch 32bit from 4ch 16bit
                slot_bits = slot_bits * fs->channel / 2;
                data_bits = slot_bits;
            }
            i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(data_bits, slot_mode);
            slot_cfg.slot_mask = slot_mask;
            slot_cfg.slot_mode = slot_mode;
            slot_cfg.data_bit_width = data_bits;
            slot_cfg.slot_bit_width = slot_bits;
            slot_cfg.ws_width = slot_bits;

            i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(fs->sample_rate);
            if (clk_src) {
                clk_cfg.clk_src = clk_src;
            }
            if (fs->mclk_multiple) {
                clk_cfg.mclk_multiple = fs->mclk_multiple;
            }
            if (slot_bits == 24 && (clk_cfg.mclk_multiple % 3) != 0) {
                clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;
            }
            ret = i2s_channel_reconfig_std_slot(channel, &slot_cfg);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to reconfigure STD slot, ret=%d", ret);
                return ESP_CODEC_DEV_DRV_ERR;
            }
            ret = i2s_channel_reconfig_std_clock(channel, &clk_cfg);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to reconfigure STD clock, ret=%d", ret);
                return ESP_CODEC_DEV_DRV_ERR;
            }

            ESP_LOGI(TAG, "I2S Driver mode(STD, %s), data_bit: %d, slot_bit: %d, ws_width: %" PRIu32 ", slot_mode: %s, slot_mask: 0x%x",
                     chan_info.dir == I2S_DIR_RX ? "RX" : "TX", (int)slot_cfg.data_bit_width, (int)slot_cfg.slot_bit_width, slot_cfg.ws_width,
                     slot_cfg.slot_mode == I2S_SLOT_MODE_MONO ? "MONO" : "STEREO", (int)slot_cfg.slot_mask);
            /* STD clk_cfg.bclk_div exists since IDF 5.5; log computed divider for all versions */
            ESP_LOGI(TAG, "I2S Driver mode(STD, %s), sample_rate_hz: %" PRIu32 ", mclk_multiple: %d, clk_src: %d, bclk_div: %d",
                     chan_info.dir == I2S_DIR_RX ? "RX" : "TX", clk_cfg.sample_rate_hz, (int)clk_cfg.mclk_multiple,
                     (int)clk_cfg.clk_src, bclk_div);
            _check_mclk_jitter(I2S_COMM_MODE_STD, &clk_cfg);
        }
        break;
#if SOC_I2S_SUPPORTS_PDM
        case I2S_COMM_MODE_PDM: {
            if (!is_playback) {
#if SOC_I2S_SUPPORTS_PDM_RX
                i2s_pdm_rx_clk_config_t clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(fs->sample_rate);
                if (clk_src) {
                    clk_cfg.clk_src = clk_src;
                }
                if (fs->mclk_multiple) {
                    clk_cfg.mclk_multiple = fs->mclk_multiple;
                }
                i2s_pdm_rx_slot_config_t slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(slot_bits, I2S_SLOT_MODE_STEREO);
                i2s_pdm_slot_mask_t slot_mask = fs->channel_mask ? (i2s_pdm_slot_mask_t)fs->channel_mask : I2S_PDM_SLOT_BOTH;
                // Stereo channel mask is ignored in driver, need use mono instead
                if (fs->channel_mask && fs->channel_mask < 3) {
                    slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
                }
                slot_cfg.slot_mask = slot_mask;
                if (slot_bits > fs->bits_per_sample) {
                    slot_cfg.data_bit_width = fs->bits_per_sample;
                    slot_cfg.slot_bit_width = slot_bits;
                }
                ret = i2s_channel_reconfig_pdm_rx_clock(channel, &clk_cfg);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to reconfigure PDM RX clock");
                    return ESP_CODEC_DEV_DRV_ERR;
                }
                ret = i2s_channel_reconfig_pdm_rx_slot(channel, &slot_cfg);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to reconfigure PDM RX slot");
                    return ESP_CODEC_DEV_DRV_ERR;
                }
                ESP_LOGI(TAG, "PDM RX mode, data_bit: %d, slot_bit: %d, sample_rate: %d, slot_mask: 0x%x",
                         fs->bits_per_sample, slot_bits, (int)fs->sample_rate, (int)fs->channel_mask);
#else
                ESP_LOGE(TAG, "PDM RX is not supported");
                return ESP_CODEC_DEV_NOT_SUPPORT;
#endif  /* SOC_I2S_SUPPORTS_PDM_RX */
            } else {
#if SOC_I2S_SUPPORTS_PDM_TX
                i2s_pdm_tx_clk_config_t clk_cfg = I2S_PDM_TX_CLK_DEFAULT_CONFIG(fs->sample_rate);
                if (clk_src) {
                    clk_cfg.clk_src = clk_src;
                }
                if (fs->mclk_multiple) {
                    clk_cfg.mclk_multiple = fs->mclk_multiple;
                }
                clk_cfg.up_sample_fs = fs->sample_rate / 100;
                i2s_pdm_tx_slot_config_t slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(slot_bits, I2S_SLOT_MODE_STEREO);
                // Stereo channel mask is ignored, need use mono instead
                if (fs->channel_mask && fs->channel_mask < 3) {
                    slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
                }
#if SOC_I2S_HW_VERSION_1
                i2s_pdm_slot_mask_t slot_mask = fs->channel_mask ? (i2s_pdm_slot_mask_t)fs->channel_mask : I2S_PDM_SLOT_BOTH;
                slot_cfg.slot_mask = slot_mask;
#endif  /* SOC_I2S_HW_VERSION_1 */
                if (slot_bits > fs->bits_per_sample) {
                    slot_cfg.data_bit_width = fs->bits_per_sample;
                    slot_cfg.slot_bit_width = slot_bits;
                }
                ret = i2s_channel_reconfig_pdm_tx_clock(channel, &clk_cfg);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to reconfigure PDM TX clock");
                    return ESP_CODEC_DEV_DRV_ERR;
                }
                ret = i2s_channel_reconfig_pdm_tx_slot(channel, &slot_cfg);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to reconfigure PDM TX slot");
                    return ESP_CODEC_DEV_DRV_ERR;
                }
                ESP_LOGI(TAG, "PDM TX mode, data_bit: %d, slot_bit: %d, sample_rate: %d, slot_mask: 0x%x",
                         fs->bits_per_sample, slot_bits, (int)fs->sample_rate, (int)fs->channel_mask);
                _check_mclk_jitter(I2S_COMM_MODE_PDM, &clk_cfg);
#else
                ESP_LOGE(TAG, "PDM TX is not supported");
                return ESP_CODEC_DEV_NOT_SUPPORT;
#endif  /* SOC_I2S_SUPPORTS_PDM_TX */
            }
        }
        break;
#endif  /* SOC_I2S_SUPPORTS_PDM */
#if SOC_I2S_SUPPORTS_TDM
        case I2S_COMM_MODE_TDM: {
            if (fs->channel == 2 && fs->bits_per_sample == 32) {
                ESP_LOGW(TAG, "Using 32-bit samples to get 2-channel 16-bit data in TDM mode is not recommended; use 4-channel 16-bit instead");
            }
            i2s_tdm_clk_config_t clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(fs->sample_rate);
            if (clk_src) {
                clk_cfg.clk_src = clk_src;
            }
            if (fs->mclk_multiple) {
                clk_cfg.mclk_multiple = fs->mclk_multiple;
            }
            if (slot_bits == 24 && (clk_cfg.mclk_multiple % 3) != 0) {
                clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;
            }
            i2s_tdm_slot_config_t slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(
                slot_bits,
                I2S_SLOT_MODE_STEREO,
                (i2s_tdm_slot_mask_t)fs->channel_mask);
            slot_cfg.total_slot = fs->channel;
            slot_cfg.left_align = true;  // Use left align mode to keep the same with STD mode
            slot_cfg.data_bit_width = fs->bits_per_sample;
            slot_cfg.slot_bit_width = slot_bits;
            slot_cfg.ws_width = slot_bits * fs->channel / 2;
            ret = i2s_channel_reconfig_tdm_slot(channel, &slot_cfg);
            if (ret != ESP_OK) {
                return ESP_CODEC_DEV_DRV_ERR;
            }
            ret = i2s_channel_reconfig_tdm_clock(channel, &clk_cfg);
            if (ret != ESP_OK) {
                return ESP_CODEC_DEV_DRV_ERR;
            }
            ESP_LOGI(TAG, "I2S Driver mode(TDM, %s), data_bit: %d, slot_bit: %d, ws_width: %" PRIu32 ", total_slot: %" PRIu32 ", slot_mask: 0x%x",
                     chan_info.dir == I2S_DIR_RX ? "RX" : "TX", (int)slot_cfg.data_bit_width, (int)slot_cfg.slot_bit_width,
                     slot_cfg.ws_width, slot_cfg.total_slot, (int)slot_cfg.slot_mask);
            ESP_LOGI(TAG, "I2S Driver mode(TDM, %s), sample_rate_hz: %" PRIu32 ", mclk_multiple: %d, clk_src: %d, bclk_div: %" PRIu32,
                     chan_info.dir == I2S_DIR_RX ? "RX" : "TX", clk_cfg.sample_rate_hz, (int)clk_cfg.mclk_multiple,
                     (int)clk_cfg.clk_src, clk_cfg.bclk_div);
            _check_mclk_jitter(I2S_COMM_MODE_TDM, &clk_cfg);
        }
        break;
#endif  /* SOC_I2S_SUPPORTS_TDM */
        default:
            return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    _show_channel_info(channel);
    return ret;
}

#if SOC_I2S_HW_VERSION_1
static int _set_drv_fs_with_peer_sync(i2s_chan_handle_t active_channel, i2s_chan_handle_t peer_channel, bool is_playback,
                                      uint8_t slot_bits, i2s_clock_src_t clk_src, const esp_codec_dev_sample_info_t *fs)
{
    bool peer_reenable = false;
    // On shared-clock duplex ports, reconfiguring one side while the peer master keeps running can leave channels out of sync.
    if (peer_channel && peer_channel != active_channel && _is_master(peer_channel) && _is_enabled(peer_channel)) {
        int ret = i2s_channel_disable(peer_channel);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to disable peer master channel");
            return ESP_CODEC_DEV_DRV_ERR;
        }
        peer_reenable = true;
    }
    int ret = _set_drv_fs(active_channel, is_playback, slot_bits, clk_src, fs);
    if (ret != ESP_CODEC_DEV_OK) {
        if (peer_reenable) {
            if (i2s_channel_enable(peer_channel) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to restore peer master channel");
            }
        }
        return ret;
    }
    if (peer_reenable) {
        ret = i2s_channel_enable(peer_channel);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable peer master channel");
            return ESP_CODEC_DEV_DRV_ERR;
        }
    }
    return ESP_CODEC_DEV_OK;
}
#endif  /* SOC_I2S_HW_VERSION_1 */

/**
 * Just set fs directly when:
 * 1. peer == NULL
 * 2. peer != NULL && !peer_enable
 * 3. peer != NULL && peer_enable && equal(sample_rate, mclk_multiple, total_bits)
 *
 * Otherwise, need expand to max bits using slot_bit_width:
 * 1. cur_total_bits < peer_total_bits: extend cur to max bits
 * 2. cur_total_bits > peer_total_bits: extend peer to max bits, need disable peer and enable it back after set fs
 */
static int _check_fs_compatible(i2s_data_t *i2s_data, bool is_playback, const esp_codec_dev_sample_info_t *fs)
{
    i2s_chan_handle_t active_channel = _get_channel_handle(i2s_data, is_playback);
    i2s_data_t *peer_data = _get_peer_ref_target(i2s_data, is_playback, NULL);
    // No peer channel, set fs directly
    if (peer_data == NULL) {
        int slot_bit = fs->bits_per_sample;
        int total_slot_bits = slot_bit * fs->channel;
        int ret = _set_drv_fs(active_channel, is_playback, slot_bit, i2s_data->clk_src, fs);
        if (ret != ESP_CODEC_DEV_OK) {
            return ret;
        }
        i2s_data->total_slot_bits = total_slot_bits;
        memcpy(_get_channel_fs(i2s_data, is_playback), fs, sizeof(esp_codec_dev_sample_info_t));
        ESP_LOGI(TAG, "No peer channel; set slot width to %d", slot_bit);
        return ESP_CODEC_DEV_OK;
    }

    ESP_LOGI(TAG, "Active handles (in:%p, out:%p), peer handles (in:%p, out:%p)",
             i2s_data->rx.handle, i2s_data->tx.handle, peer_data->rx.handle, peer_data->tx.handle);

    // peer == i2s_data || peer != i2s_data
    i2s_chan_handle_t peer_channel = _get_channel_handle(peer_data, !is_playback);
    i2s_stream_state_t *peer_stream = _get_stream_state(peer_data, !is_playback);
    bool peer_req_enable = peer_stream->req_enable;
    if (!peer_req_enable) {  // peer side is not explicitly enabled yet
        int slot_bit = fs->bits_per_sample;
        if (_is_master(peer_channel)) {
            if (_is_initialized(peer_channel)) {
                int ret = _set_drv_fs(peer_channel, !is_playback, slot_bit, i2s_data->clk_src, fs);
                if (ret != ESP_CODEC_DEV_OK) {
                    return ret;
                }
                ret = _acquire_peer_ref(i2s_data, is_playback, true);
                if (ret != ESP_CODEC_DEV_OK) {
                    return ret;
                }
                ESP_LOGI(TAG, "Peer channel is master and initialized; set slot width to %d", slot_bit);
            } else {
                ESP_LOGW(TAG, "Peer channel is master but not initialized; cannot apply the sample format to the active channel");
            }
        }
        int ret = _set_drv_fs(active_channel, is_playback, slot_bit, i2s_data->clk_src, fs);
        if (ret != ESP_CODEC_DEV_OK) {
            _release_peer_ref(i2s_data, is_playback, true);
            ESP_LOGE(TAG, "Failed to apply the sample format to the active channel");
        } else {
            esp_codec_dev_sample_info_t *peer_fs = _get_channel_fs(peer_data, !is_playback);
            memcpy(peer_fs, fs, sizeof(esp_codec_dev_sample_info_t));
            peer_data->total_slot_bits = slot_bit * peer_fs->channel;

            i2s_data->total_slot_bits = slot_bit * fs->channel;
            memcpy(_get_channel_fs(i2s_data, is_playback), fs, sizeof(esp_codec_dev_sample_info_t));
            ESP_LOGI(TAG, "Applied the sample format to the active channel, slot width=%d, total slot bits=%d",
                     slot_bit, i2s_data->total_slot_bits);
        }
        return ret;
    }

    // If use duplex mode, sample_rate and mclk_multiple must same
    esp_codec_dev_sample_info_t *peer_fs = _get_channel_fs(peer_data, !is_playback);
    if (fs->sample_rate != peer_fs->sample_rate || fs->mclk_multiple != peer_fs->mclk_multiple) {
        ESP_LOGE(TAG, "Conflicting duplex config: sample_rate(active:%d, peer:%d), mclk_multiple(active:%d, peer:%d)",
                 (int)fs->sample_rate, (int)peer_fs->sample_rate, fs->mclk_multiple, peer_fs->mclk_multiple);
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }

    int active_total_bits = fs->channel * fs->bits_per_sample;
    int peer_total_bits = peer_data->total_slot_bits;
    // Total_slot bits same, set directly
    if (active_total_bits == peer_total_bits) {
        int slot_bit = fs->bits_per_sample;
        int ret = 0;
#if SOC_I2S_HW_VERSION_1
        ret = _set_drv_fs_with_peer_sync(active_channel, peer_channel, is_playback, slot_bit, i2s_data->clk_src, fs);
#else
        ret = _set_drv_fs(active_channel, is_playback, slot_bit, i2s_data->clk_src, fs);
#endif  /* SOC_I2S_HW_VERSION_1 */
        if (ret == ESP_CODEC_DEV_OK) {
            i2s_data->total_slot_bits = active_total_bits;
            memcpy(_get_channel_fs(i2s_data, is_playback), fs, sizeof(esp_codec_dev_sample_info_t));
            ESP_LOGD(TAG, "Peer total bits: %d, active channel count: %d, active bits per sample: %d",
                     peer_total_bits, (int)fs->channel, (int)fs->bits_per_sample);
        } else {
            ESP_LOGE(TAG, "Failed to apply the sample format to the active channel");
        }
        return ret;
    }

    // If the total slot widths differ, widen one side so both channels end up
    // using the larger total slot width. For example, 32 vs 64 total bits must
    // be aligned to 64 total bits before both sides can run together.
    int max_total_bits = active_total_bits > peer_total_bits ? active_total_bits : peer_total_bits;
    if (active_total_bits < peer_total_bits) {
        // The active side is narrower than the peer, so widen the active side.
        // Example: peer_total_bits=64, active_total_bits=32.
        int slot_bit = max_total_bits / fs->channel;
        int ret = 0;
#if SOC_I2S_HW_VERSION_1
        ret = _set_drv_fs_with_peer_sync(active_channel, peer_channel, is_playback, slot_bit, i2s_data->clk_src, fs);
#else
        ret = _set_drv_fs(active_channel, is_playback, slot_bit, i2s_data->clk_src, fs);
#endif  /* SOC_I2S_HW_VERSION_1 */
        if (ret == ESP_CODEC_DEV_OK) {
            i2s_data->total_slot_bits = max_total_bits;
            memcpy(_get_channel_fs(i2s_data, is_playback), fs, sizeof(esp_codec_dev_sample_info_t));
            ESP_LOGD(TAG, "Peer total bits: %d, active channel count: %d, active bits per sample: %d",
                     peer_total_bits, (int)fs->channel, (int)fs->bits_per_sample);
            ESP_LOGI(TAG, "Extend %s total slot bits from %d to %d", is_playback ? "playback" : "record", active_total_bits, max_total_bits);
        } else {
            ESP_LOGE(TAG, "Failed to apply the sample format to the active channel");
        }
        return ret;
    } else {
        // The peer side is narrower than the active side, so widen the peer side first.
        // Example: peer_total_bits=32, active_total_bits=64.
        int slot_bit = max_total_bits / peer_fs->channel;
        int ret = _set_drv_fs(peer_channel, !is_playback, slot_bit, i2s_data->clk_src, peer_fs);
        if (ret != ESP_CODEC_DEV_OK) {
            return ret;
        }
        if (!_is_enabled(peer_channel)) {
            ret = _i2s_drv_enable(peer_data, !is_playback, true);
            if (ret != ESP_CODEC_DEV_OK) {
                return ret;
            }
        }
        slot_bit = fs->bits_per_sample;
#if SOC_I2S_HW_VERSION_1
        ret = _set_drv_fs_with_peer_sync(active_channel, peer_channel, is_playback, slot_bit, i2s_data->clk_src, fs);
#else
        ret = _set_drv_fs(active_channel, is_playback, slot_bit, i2s_data->clk_src, fs);
#endif  /* SOC_I2S_HW_VERSION_1 */
        if (ret == ESP_CODEC_DEV_OK) {
            i2s_data->total_slot_bits = max_total_bits;
            peer_data->total_slot_bits = max_total_bits;
            memcpy(_get_channel_fs(i2s_data, is_playback), fs, sizeof(esp_codec_dev_sample_info_t));
            ESP_LOGD(TAG, "Peer total bits: %d, active channel count: %d, active bits per sample: %d",
                     peer_total_bits, (int)fs->channel, (int)fs->bits_per_sample);
        } else {
            ESP_LOGE(TAG, "Failed to apply the sample format to the active channel");
        }
        ESP_LOGI(TAG, "Extend %s total slot bits from %d to %d", !is_playback ? "playback" : "record", peer_total_bits, max_total_bits);
        return ret;
    }
}

static int _i2s_data_open(const audio_codec_data_if_t *h, void *data_cfg, int cfg_size)
{
    i2s_data_t *i2s_data = (i2s_data_t *)h;
    if (h == NULL || data_cfg == NULL || cfg_size != sizeof(audio_codec_i2s_cfg_t)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    audio_codec_i2s_cfg_t *i2s_cfg = (audio_codec_i2s_cfg_t *)data_cfg;
    i2s_data->port = i2s_cfg->port;
    i2s_data->tx.handle = i2s_cfg->tx_handle;
    i2s_data->rx.handle = i2s_cfg->rx_handle;
    i2s_data->tx.auto_paired = false;
    i2s_data->rx.auto_paired = false;
    i2s_data->port_group = _port_group_acquire(i2s_data->port);
    if (i2s_data->port_group == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }
    int ret = _port_group_add_instance(i2s_data->port_group, i2s_data);
    if (ret != ESP_CODEC_DEV_OK) {
        _port_group_release(i2s_data->port_group);
        i2s_data->port_group = NULL;
        return ret;
    }
    if (i2s_data->rx.handle && i2s_data->tx.handle == NULL && !_is_master(i2s_data->rx.handle)) {
        i2s_data_t *peer_data = _port_group_find_peer(i2s_data->port_group, i2s_data, false);
        if (!peer_data) {
            // If use duplex mode, tx.handle is the pair of rx.handle, otherwise tx.handle is NULL
            i2s_data->tx.handle = _get_peer_channel(i2s_data->rx.handle);
            i2s_data->tx.auto_paired = i2s_data->tx.handle != NULL;
            ESP_LOGI(TAG, "Auto-paired output handle %p from input handle %p in duplex mode", i2s_data->tx.handle, i2s_data->rx.handle);
        }
    }
    // If RX is master, only open TX, then set fs for TX will not work, so get RX peer handle from TX
    if (i2s_data->tx.handle && i2s_data->rx.handle == NULL && !_is_master(i2s_data->tx.handle)) {
        i2s_data_t *peer_data = _port_group_find_peer(i2s_data->port_group, i2s_data, true);
        if (!peer_data) {
            // If use duplex mode, rx.handle is the pair of tx.handle, otherwise rx.handle is NULL
            i2s_data->rx.handle = _get_peer_channel(i2s_data->tx.handle);
            i2s_data->rx.auto_paired = i2s_data->rx.handle != NULL;
            ESP_LOGI(TAG, "Auto-paired input handle %p from output handle %p in duplex mode", i2s_data->rx.handle, i2s_data->tx.handle);
        }
    }
    _port_group_refresh_capability_index(i2s_data->port_group);
    ESP_LOGD(TAG, "I2S data handles: rx=%p, tx=%p, port=%d, self=%p",
             i2s_data->rx.handle, i2s_data->tx.handle, i2s_data->port, i2s_data);
    i2s_data->clk_src = (i2s_clock_src_t)i2s_cfg->clk_src;
    i2s_data->is_open = true;
    return ESP_CODEC_DEV_OK;
}

static int _check_peer_and_disable_data(i2s_data_t *i2s_data, i2s_data_t *peer_data, bool is_playback)
{
    i2s_stream_state_t *peer_stream = _get_stream_state(peer_data, !is_playback);
    bool peer_pending = peer_stream->disable_pending;
    bool peer_should_run = _should_keep_running(peer_data, !is_playback);
    if (peer_pending || !peer_should_run) {
        ESP_LOGI(TAG, "Disable peer channel (%s) because disable is pending", is_playback ? "in" : "out");
        int ret = _i2s_drv_enable(peer_data, !is_playback, false);
        if (ret != ESP_CODEC_DEV_OK) {
            return ret;
        }
        _clear_all_pending(peer_data);
    }
    return _i2s_drv_enable(i2s_data, is_playback, false);
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
        ret = _acquire_peer_ref(i2s_data, is_playback, true);
        if (ret != ESP_CODEC_DEV_OK) {
            active_stream->req_enable = false;
            return ret;
        }
        ret = _i2s_drv_enable(i2s_data, is_playback, true);
        if (ret != ESP_CODEC_DEV_OK) {
            _release_peer_ref(i2s_data, is_playback, true);
            active_stream->req_enable = false;
        }
    } else {
        active_stream->req_enable = false;
        ret = _release_peer_ref(i2s_data, is_playback, false);
        if (ret != ESP_CODEC_DEV_OK) {
            return ret;
        }
        if (_should_keep_running(i2s_data, is_playback)) {
            _clear_pending(i2s_data, is_playback);
            return ESP_CODEC_DEV_OK;
        }
        i2s_data_t *peer_data = _port_group_find_peer(i2s_data->port_group, i2s_data, is_playback);
        if (peer_data == NULL) {
            // If not have peer channel, disable current directly
            ret = _i2s_drv_enable(i2s_data, is_playback, false);
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
    int ret = _i2s_lock(i2s_data);
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
    int unlock_ret = _i2s_unlock(i2s_data);
    return ret == ESP_CODEC_DEV_OK ? unlock_ret : ret;
}

static bool _check_fs_param(esp_codec_dev_sample_info_t *fs)
{
    if (fs == NULL) {
        return false;
    }
    if (fs->channel > 16 || fs->channel == 0) {
        ESP_LOGE(TAG, "Channel count %d is not supported", fs->channel);
        return false;
    }
    if ((fs->channel_mask >= ESP_CODEC_DEV_MAKE_CHANNEL_MASK(fs->channel)) || fs->channel_mask == 0) {
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
    if (i2s_data == NULL || !_check_fs_param(fs)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (!i2s_data->is_open) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int ret = _i2s_lock(i2s_data);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    if (dev_type == ESP_CODEC_DEV_TYPE_IN_OUT) {
        if (i2s_data->tx.handle == NULL || i2s_data->rx.handle == NULL) {
            _i2s_unlock(i2s_data);
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        memcpy(&i2s_data->rx.fs, fs, sizeof(esp_codec_dev_sample_info_t));
        memcpy(&i2s_data->tx.fs, fs, sizeof(esp_codec_dev_sample_info_t));
        i2s_chan_handle_t channel = (i2s_chan_handle_t)i2s_data->tx.handle;
        ret = _set_drv_fs(channel, true, fs->bits_per_sample, i2s_data->clk_src, fs);
        if (ret != ESP_CODEC_DEV_OK) {
            _i2s_unlock(i2s_data);
            return ret;
        }
        channel = (i2s_chan_handle_t)i2s_data->rx.handle;
        ret = _set_drv_fs(channel, false, fs->bits_per_sample, i2s_data->clk_src, fs);
        i2s_data->total_slot_bits = fs->bits_per_sample * fs->channel;
    } else {
        bool is_playback = dev_type & ESP_CODEC_DEV_TYPE_OUT ? true : false;
        ret = _check_fs_compatible(i2s_data, is_playback, fs);
    }
    int unlock_ret = _i2s_unlock(i2s_data);
    return ret == ESP_CODEC_DEV_OK ? unlock_ret : ret;
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
    if (i2s_data->rx.fs.channel_mask < 0x03 && i2s_data->rx.fs.bits_per_sample <= 24) {
        int read_len = 0;  // Actual number of bytes read by I2S driver.
        if (i2s_data->rx.fs.bits_per_sample == 16) {
            read_len = size;
        } else if (i2s_data->rx.fs.bits_per_sample == 8) {
            read_len = size * 2;
        } else if (i2s_data->rx.fs.bits_per_sample == 24) {
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
        err = esp32_read_mono_fix(read_data, read_len, i2s_data->rx.fs.bits_per_sample, data, size);
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
    if (i2s_data->tx.fs.channel_mask < 0x03 && i2s_data->tx.fs.bits_per_sample <= 24) {
        int write_len = 0;
        uint8_t *write_data = NULL;
        int convert_ret = esp32_write_mono_fix(data, size, i2s_data->tx.fs.bits_per_sample, &write_data, &write_len);
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
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    i2s_port_group_t *port_group = i2s_data->port_group;
    _release_peer_ref(i2s_data, true, false);
    _release_peer_ref(i2s_data, false, false);
    i2s_data->rx = (i2s_stream_state_t) {0};
    i2s_data->tx = (i2s_stream_state_t) {0};
    i2s_data->total_slot_bits = 0;
    i2s_data->is_open = false;
    i2s_data->port_group = NULL;
    _port_group_remove_instance(port_group, i2s_data);
    _port_group_release(port_group);
    return ESP_CODEC_DEV_OK;
}

static int _i2s_data_get_mode_from_channel(i2s_chan_handle_t channel, esp_codec_dev_i2s_mode_t *mode)
{
    i2s_chan_info_t chan_info = {0};
    if (i2s_channel_get_info(channel, &chan_info) != ESP_OK) {
        *mode = ESP_CODEC_DEV_I2S_MODE_NONE;
        return ESP_CODEC_DEV_DRV_ERR;
    }
    switch (chan_info.mode) {
        case I2S_COMM_MODE_STD:
            *mode = ESP_CODEC_DEV_I2S_MODE_STD_PHILIPS;
            break;
#if SOC_I2S_SUPPORTS_PDM
        case I2S_COMM_MODE_PDM:
            if (chan_info.dir == I2S_DIR_RX) {
                *mode = ESP_CODEC_DEV_I2S_MODE_PDM_RX;
            } else {
                *mode = ESP_CODEC_DEV_I2S_MODE_PDM_TX;
            }
            break;
#endif  /* SOC_I2S_SUPPORTS_PDM */
#if SOC_I2S_SUPPORTS_TDM
        case I2S_COMM_MODE_TDM:
            *mode = ESP_CODEC_DEV_I2S_MODE_TDM_PHILIPS;
            break;
#endif  /* SOC_I2S_SUPPORTS_TDM */
        default:
            *mode = ESP_CODEC_DEV_I2S_MODE_NONE;
            break;
    }
    return ESP_CODEC_DEV_OK;
}

static int _i2s_parse_fs_from_channel_info(i2s_chan_handle_t channel, esp_codec_dev_sample_info_t *fs)
{
    i2s_chan_info_t info = {0};
    if (i2s_channel_get_info(channel, &info) != ESP_OK) {
        return ESP_CODEC_DEV_DRV_ERR;
    }
    if (info.mode == I2S_COMM_MODE_NONE || info.mode_cfg == NULL) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    memset(fs, 0, sizeof(*fs));
    switch (info.mode) {
        case I2S_COMM_MODE_STD: {
            const i2s_std_config_t *cfg = (const i2s_std_config_t *)info.mode_cfg;
            fs->sample_rate = cfg->clk_cfg.sample_rate_hz;
            fs->mclk_multiple = cfg->clk_cfg.mclk_multiple;
            fs->bits_per_sample = (uint8_t)cfg->slot_cfg.data_bit_width;
            fs->channel_mask = (uint16_t)cfg->slot_cfg.slot_mask;
            fs->channel = (cfg->slot_cfg.slot_mode == I2S_SLOT_MODE_MONO) ? 1 : 2;
            break;
        }
#if SOC_I2S_SUPPORTS_TDM
        case I2S_COMM_MODE_TDM: {
            const i2s_tdm_config_t *cfg = (const i2s_tdm_config_t *)info.mode_cfg;
            fs->sample_rate = cfg->clk_cfg.sample_rate_hz;
            fs->mclk_multiple = cfg->clk_cfg.mclk_multiple;
            fs->bits_per_sample = (uint8_t)cfg->slot_cfg.data_bit_width;
            fs->channel = (uint8_t)cfg->slot_cfg.total_slot;
            fs->channel_mask = (uint16_t)cfg->slot_cfg.slot_mask;
            break;
        }
#endif  /* SOC_I2S_SUPPORTS_TDM */
#if SOC_I2S_SUPPORTS_PDM
        case I2S_COMM_MODE_PDM: {
            if (info.dir == I2S_DIR_RX) {
#if SOC_I2S_SUPPORTS_PDM_RX
                const i2s_pdm_rx_config_t *cfg = (const i2s_pdm_rx_config_t *)info.mode_cfg;
                fs->sample_rate = cfg->clk_cfg.sample_rate_hz;
                fs->mclk_multiple = cfg->clk_cfg.mclk_multiple;
                fs->bits_per_sample = (uint8_t)cfg->slot_cfg.data_bit_width;
                fs->channel_mask = (uint16_t)cfg->slot_cfg.slot_mask;
                fs->channel = (cfg->slot_cfg.slot_mode == I2S_SLOT_MODE_MONO) ? 1 : 2;
#else
                return ESP_CODEC_DEV_NOT_SUPPORT;
#endif  /* SOC_I2S_SUPPORTS_PDM_RX */
            } else {
#if SOC_I2S_SUPPORTS_PDM_TX
                const i2s_pdm_tx_config_t *cfg = (const i2s_pdm_tx_config_t *)info.mode_cfg;
                fs->sample_rate = cfg->clk_cfg.sample_rate_hz;
                fs->mclk_multiple = cfg->clk_cfg.mclk_multiple;
                fs->bits_per_sample = (uint8_t)cfg->slot_cfg.data_bit_width;
                fs->channel = (cfg->slot_cfg.slot_mode == I2S_SLOT_MODE_MONO) ? 1 : 2;
#if SOC_I2S_HW_VERSION_1
                fs->channel_mask = (uint16_t)cfg->slot_cfg.slot_mask;
#else
                fs->channel_mask = (cfg->slot_cfg.slot_mode == I2S_SLOT_MODE_MONO) ? 0x01 : 0x03;
#endif  /* SOC_I2S_HW_VERSION_1 */
#else
                return ESP_CODEC_DEV_NOT_SUPPORT;
#endif  /* SOC_I2S_SUPPORTS_PDM_TX */
            }
            break;
        }
#endif  /* SOC_I2S_SUPPORTS_PDM */
        default:
            return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    return ESP_CODEC_DEV_OK;
}

static int _i2s_data_get_fmt(const audio_codec_data_if_t *h, esp_codec_dev_type_t dev_type, esp_codec_dev_sample_info_t *fs)
{
    i2s_data_t *i2s_data = (i2s_data_t *)h;
    if (i2s_data == NULL || fs == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    bool is_playback = (dev_type & ESP_CODEC_DEV_TYPE_OUT) != 0;
    esp_codec_dev_sample_info_t *cached = _get_channel_fs(i2s_data, is_playback);
    if (cached->channel != 0) {
        memcpy(fs, cached, sizeof(*fs));
        return ESP_CODEC_DEV_OK;
    }
    i2s_chan_handle_t channel = _get_channel_handle(i2s_data, is_playback);
    if (channel == NULL) {
        return ESP_CODEC_DEV_NOT_FOUND;
    }
    return _i2s_parse_fs_from_channel_info(channel, fs);
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
        int ret = _i2s_data_get_mode_from_channel(i2s_data->rx.handle, in_mode);
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
            ret = _i2s_data_get_mode_from_channel(peer_handle, out_mode);
            if (ret != ESP_CODEC_DEV_OK) {
                return ret;
            }
        }
    } else if (i2s_data->tx.handle != NULL) {
        int ret = _i2s_data_get_mode_from_channel(i2s_data->tx.handle, out_mode);
        if (ret != ESP_CODEC_DEV_OK) {
            return ret;
        }
        /* rx.handle is NULL here (otherwise the first branch was taken). */
        i2s_chan_handle_t peer_handle = _get_peer_channel(i2s_data->tx.handle);
        if (peer_handle != NULL) {
            ret = _i2s_data_get_mode_from_channel(peer_handle, in_mode);
            if (ret != ESP_CODEC_DEV_OK) {
                return ret;
            }
        }
    }
    return ESP_CODEC_DEV_OK;
}

static int _i2s_data_get_order(const audio_codec_data_if_t *h, uint8_t channel,
                               uint16_t channel_mask, esp_codec_dev_channel_map_t *map)
{
    if (h == NULL || map == NULL || channel == 0 || channel > I2S_DATA_MAX_CHANNELS || channel_mask == 0) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    uint16_t valid_mask = (uint16_t)((1U << channel) - 1U);
    if ((channel_mask & ~valid_mask) != 0) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    map->value = 0;
    uint8_t logical_channel = 1;
    for (uint8_t slot = 0; slot < channel; slot++) {
        if (channel_mask & (1U << slot)) {
            uint32_t shift = 4 * (logical_channel - 1);
            map->value |= (uint32_t)(slot + 1) << shift;
            logical_channel++;
        }
    }
    return ESP_CODEC_DEV_OK;
}

static int _i2s_data_get_channel_mask(const audio_codec_data_if_t *h, uint8_t channel,
                                      const esp_codec_dev_channel_map_t *map, uint16_t *channel_mask)
{
    if (h == NULL || channel_mask == NULL || map == NULL ||
        channel == 0 || channel > I2S_DATA_MAX_CHANNELS || map->value == 0) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    uint16_t mask = 0;
    for (uint8_t logical_channel = 1; logical_channel <= 8; logical_channel++) {
        uint8_t slot = (uint8_t)((map->value >> (4 * (logical_channel - 1))) & 0x0F);
        if (slot == 0) {
            continue;
        }
        if (slot == 0 || slot > channel) {
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        uint16_t bit = (uint16_t)(1U << (slot - 1));
        if (mask & bit) {
            return ESP_CODEC_DEV_INVALID_ARG;
        }
        mask |= bit;
    }

    *channel_mask = mask;
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
    i2s_data->base.get_order = _i2s_data_get_order;
    i2s_data->base.get_channel_mask = _i2s_data_get_channel_mask;
    int ret = _i2s_data_open(&i2s_data->base, i2s_cfg, sizeof(audio_codec_i2s_cfg_t));
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to open the I2S data interface, ret=%d", ret);
        free(i2s_data);
        return NULL;
    }
    return &i2s_data->base;
}
