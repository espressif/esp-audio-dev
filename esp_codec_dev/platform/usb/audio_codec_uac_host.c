/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_cpu.h"
#include "esp_check.h"
#include "esp_log.h"

#include "usb/uac_host.h"
#include "audio_codec_uac_priv.h"

#define UAC_DEFAULT_CONNECT_TIMEOUT_MS  (5000)
#define UAC_DEFAULT_BUFFER_SIZE         (16 * 1024)
#define UAC_DEFAULT_BUFFER_THRESHOLD    (4 * 1024)
#define UAC_DEFAULT_RW_TIMEOUT_MS       (1000)
#define UAC_CAPS_MAX                    (32)
#define UAC_RESOLVE_POLL_MS             (20)
#define UAC_DEFAULT_MUTEX_TIMEOUT_MS    (1000)

#ifdef CONFIG_CODEC_UAC_MAX_DEVICES
#define UAC_MAX_DEVICES  CONFIG_CODEC_UAC_MAX_DEVICES
#else
#define UAC_MAX_DEVICES  (2)
#endif  /* CONFIG_CODEC_UAC_MAX_DEVICES */

typedef enum {
    UAC_ROLE_RX = 0,
    UAC_ROLE_TX,
    UAC_ROLE_MAX,
} codec_uac_role_t;

typedef struct {
    uint8_t                     addr;
    uint8_t                     iface_num;
    bool                        connected;
    bool                        opened;
    bool                        started;
    bool                        caps_cached;
    uac_host_device_handle_t    handle;
    uac_host_dev_alt_param_t    selected_alt;
    esp_codec_dev_capability_t  caps[UAC_CAPS_MAX];
    uac_host_dev_alt_param_t    cap_alt[UAC_CAPS_MAX];
    uint8_t                     cap_alt_setting[UAC_CAPS_MAX];
    int                         cap_count;
} codec_uac_role_state_t;

typedef struct {
    bool                    in_use;
    uint8_t                 addr;
    codec_uac_role_state_t  role[UAC_ROLE_MAX];
} uac_device_t;

typedef struct {
    int                         refs;
    bool                        installed;
    uint8_t                     max_devices;
    SemaphoreHandle_t           lock;
    esp_codec_dev_uac_cfg_t     cfg;
    uac_device_t                devices[UAC_MAX_DEVICES];
    void                       *host_event_ctx;
    audio_codec_uac_event_cb_t  host_event_cb;
} codec_uac_ctx_t;

static const char *TAG = "CODEC_UAC_HOST";

static const uint32_t s_common_sample_rates[] = {
    8000, 11025, 12000, 16000, 22050, 24000,
    32000, 44100, 48000, 64000, 88200, 96000,
};

static codec_uac_ctx_t s_uac;

static inline bool lock_ctx(void)
{
    if (s_uac.lock == NULL) {
        return true;
    }
    if (xSemaphoreTake(s_uac.lock, pdMS_TO_TICKS(UAC_DEFAULT_MUTEX_TIMEOUT_MS)) == pdPASS) {
        return true;
    }
    ESP_LOGE(TAG, "Lock UAC context mutex timeout");
    return false;
}

static inline void unlock_ctx(void)
{
    if (s_uac.lock) {
        xSemaphoreGive(s_uac.lock);
    }
}

static inline void clear_role_caps(codec_uac_role_state_t *st)
{
    st->caps_cached = false;
    st->cap_count = 0;
    memset(st->caps, 0, sizeof(st->caps));
    memset(st->cap_alt, 0, sizeof(st->cap_alt));
    memset(st->cap_alt_setting, 0, sizeof(st->cap_alt_setting));
}

static esp_err_t ensure_ctx(void)
{
    if (s_uac.lock != NULL) {
        return ESP_OK;
    }
    if (s_uac.max_devices <= 0 || s_uac.max_devices > UAC_MAX_DEVICES) {
        s_uac.max_devices = UAC_MAX_DEVICES;
    }
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    if (mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (esp_cpu_compare_and_set((volatile uint32_t *)&s_uac.lock,
                                (uint32_t)NULL,
                                (uint32_t)mutex)) {
        return ESP_OK;
    }
    vSemaphoreDelete(mutex);
    return ESP_ERR_NO_MEM;
}

static int find_device_by_addr_locked(uint8_t addr)
{
    for (int i = 0; i < s_uac.max_devices; i++) {
        if (s_uac.devices[i].in_use && s_uac.devices[i].addr == addr) {
            return i;
        }
    }
    return -1;
}

static int alloc_device_locked(uint8_t addr)
{
    for (int i = 0; i < s_uac.max_devices; i++) {
        if (!s_uac.devices[i].in_use) {
            uac_device_t *dev = &s_uac.devices[i];
            memset(dev, 0, sizeof(*dev));
            dev->in_use = true;
            dev->addr = addr;
            return i;
        }
    }
    return -1;
}

static bool device_has_roles_locked(const uac_device_t *dev, esp_codec_dev_type_t dev_type)
{
    bool ok = true;
    if (dev_type & ESP_CODEC_DEV_TYPE_IN) {
        ok = ok && dev->role[UAC_ROLE_RX].connected;
    }
    if (dev_type & ESP_CODEC_DEV_TYPE_OUT) {
        ok = ok && dev->role[UAC_ROLE_TX].connected;
    }
    return ok;
}

static void free_device_if_idle_locked(uac_device_t *dev)
{
    for (int r = 0; r < UAC_ROLE_MAX; r++) {
        if (dev->role[r].connected || dev->role[r].handle != NULL) {
            return;
        }
    }
    memset(dev, 0, sizeof(*dev));
}

static esp_codec_dev_type_t device_connected_type_locked(const uac_device_t *dev)
{
    esp_codec_dev_type_t dev_type = ESP_CODEC_DEV_TYPE_NONE;
    if (dev->role[UAC_ROLE_RX].connected) {
        dev_type |= ESP_CODEC_DEV_TYPE_IN;
    }
    if (dev->role[UAC_ROLE_TX].connected) {
        dev_type |= ESP_CODEC_DEV_TYPE_OUT;
    }
    return dev_type;
}

static bool device_is_connected_locked(const uac_device_t *dev)
{
    return device_connected_type_locked(dev) != ESP_CODEC_DEV_TYPE_NONE;
}

static void dispatch_uac_event(esp_codec_dev_uac_event_type_t event, uint8_t addr,
                               esp_codec_dev_type_t dev_type)
{
    esp_codec_dev_uac_event_info_t info = {
        .event = event,
        .addr = addr,
        .dev_type = dev_type,
    };
    audio_codec_uac_event_cb_t host_cb = s_uac.host_event_cb;
    void *host_ctx = s_uac.host_event_ctx;
    esp_codec_dev_uac_event_cb_t app_cb = s_uac.cfg.event_cb;
    void *app_ctx = s_uac.cfg.event_ctx;

    if (host_cb != NULL) {
        host_cb(&info, host_ctx);
    }
    if (app_cb != NULL) {
        app_cb(&info, app_ctx);
    }
}

static void clear_role_state_locked(codec_uac_role_state_t *st)
{
    st->connected = false;
    st->opened = false;
    st->started = false;
    st->handle = NULL;
    memset(&st->selected_alt, 0, sizeof(st->selected_alt));
    clear_role_caps(st);
}

static bool disconnect_device_locked(uac_device_t *dev)
{
    if (!device_is_connected_locked(dev)) {
        return false;
    }
    for (int r = 0; r < UAC_ROLE_MAX; r++) {
        clear_role_state_locked(&dev->role[r]);
    }
    free_device_if_idle_locked(dev);
    return true;
}

static uac_host_device_handle_t get_active_handle_locked(const uac_binding_t *b, codec_uac_role_t role)
{
    if (b == NULL || !b->resolved) {
        return NULL;
    }
    int slot = find_device_by_addr_locked(b->resolved_addr);
    if (slot < 0) {
        return NULL;
    }
    codec_uac_role_state_t *st = &s_uac.devices[slot].role[role];
    if (!st->connected || !st->opened || !st->started) {
        return NULL;
    }
    return st->handle;
}

static bool update_role_connected_locked(uac_device_t *dev, codec_uac_role_t role,
                                         uint8_t addr, uint8_t iface_num, bool connected,
                                         esp_codec_dev_uac_event_type_t *out_event,
                                         uint8_t *out_addr, esp_codec_dev_type_t *out_dev_type)
{
    codec_uac_role_state_t *st = &dev->role[role];
    bool was_connected = device_is_connected_locked(dev);
    esp_codec_dev_type_t prev_type = device_connected_type_locked(dev);

    if (connected) {
        if (!st->started) {
            clear_role_caps(st);
        }
        st->addr = addr;
        st->iface_num = iface_num;
        st->connected = true;
        if (!was_connected) {
            *out_event = ESP_CODEC_DEV_UAC_EVENT_CONNECTED;
            *out_addr = addr;
            *out_dev_type = device_connected_type_locked(dev);
            return true;
        }
        return false;
    }

    if (!st->connected && st->handle == NULL) {
        return false;
    }

    clear_role_state_locked(st);
    free_device_if_idle_locked(dev);
    if (was_connected && !device_is_connected_locked(dev)) {
        *out_event = ESP_CODEC_DEV_UAC_EVENT_DISCONNECTED;
        *out_addr = addr;
        *out_dev_type = prev_type;
        return true;
    }
    return false;
}

static void driver_event_cb(uint8_t addr, uint8_t iface_num, const uac_host_driver_event_t event, void *arg)
{
    (void)arg;
    codec_uac_role_t role;
    if (event == UAC_HOST_DRIVER_EVENT_RX_CONNECTED) {
        role = UAC_ROLE_RX;
    } else if (event == UAC_HOST_DRIVER_EVENT_TX_CONNECTED) {
        role = UAC_ROLE_TX;
    } else {
        return;
    }

    if (!lock_ctx()) {
        return;
    }
    int slot = find_device_by_addr_locked(addr);
    if (slot < 0) {
        slot = alloc_device_locked(addr);
    }
    if (slot < 0) {
        unlock_ctx();
        ESP_LOGW(TAG, "No free slot for UAC device addr %u (max %d)", addr, s_uac.max_devices);
        return;
    }
    uac_device_t *dev = &s_uac.devices[slot];
    uint8_t out_addr = addr;
    esp_codec_dev_uac_event_type_t out_event = ESP_CODEC_DEV_UAC_EVENT_CONNECTED;
    esp_codec_dev_type_t out_dev_type = ESP_CODEC_DEV_TYPE_NONE;
    bool notify = update_role_connected_locked(dev, role, addr, iface_num, true,
                                               &out_event, &out_addr, &out_dev_type);
    unlock_ctx();
    ESP_LOGI(TAG, "UAC %s connected, addr %u iface %u (slot %d)",
             role == UAC_ROLE_RX ? "RX" : "TX", addr, iface_num, slot);
    if (notify) {
        dispatch_uac_event(out_event, out_addr, out_dev_type);
    }
}

static void device_event_cb(uac_host_device_handle_t handle, const uac_host_device_event_t event, void *arg)
{
    (void)arg;
    if (event != UAC_HOST_DRIVER_EVENT_DISCONNECTED && event != UAC_HOST_DEVICE_EVENT_TRANSFER_ERROR) {
        return;
    }
    bool close_needed = false;
    uac_host_device_handle_t close_handle = handle;
    uint8_t out_addr = 0;
    esp_codec_dev_type_t out_dev_type = ESP_CODEC_DEV_TYPE_NONE;
    bool notify_disconnect = false;

    if (!lock_ctx()) {
        return;
    }
    for (int i = 0; i < s_uac.max_devices; i++) {
        uac_device_t *dev = &s_uac.devices[i];
        if (!dev->in_use) {
            continue;
        }
        for (int r = 0; r < UAC_ROLE_MAX; r++) {
            if (dev->role[r].handle != handle) {
                continue;
            }
            out_addr = dev->addr;
            out_dev_type = device_connected_type_locked(dev);
            close_needed = true;
            notify_disconnect = disconnect_device_locked(dev);
            break;
        }
        if (close_needed) {
            break;
        }
    }
    unlock_ctx();

    if (close_needed) {
        uac_host_device_close(close_handle);
    }
    if (notify_disconnect) {
        dispatch_uac_event(ESP_CODEC_DEV_UAC_EVENT_DISCONNECTED, out_addr, out_dev_type);
    }
}

static esp_err_t ensure_driver_installed(void)
{
    esp_err_t ret = ensure_ctx();
    if (ret != ESP_OK) {
        return ret;
    }
    if (!lock_ctx()) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_uac.installed) {
        unlock_ctx();
        return ESP_OK;
    }
    unlock_ctx();

    uac_host_driver_config_t cfg = {
        .create_background_task = true,
        .task_priority = CONFIG_CODEC_UAC_DRV_PRIORITY,
        .stack_size = CONFIG_CODEC_UAC_DRV_STACK_SIZE,
#if CONFIG_FREERTOS_UNICORE
        .core_id = 0,
#else
        .core_id = CONFIG_CODEC_UAC_DRV_CORE_ID,
#endif  /* CONFIG_FREERTOS_UNICORE */
        .callback = driver_event_cb,
        .callback_arg = NULL,
    };
    ret = uac_host_install(&cfg);
    ESP_RETURN_ON_ERROR(ret, TAG, "Install UAC host driver failed");

    if (!lock_ctx()) {
        uac_host_uninstall();
        return ESP_ERR_TIMEOUT;
    }
    s_uac.installed = true;
    unlock_ctx();
    return ESP_OK;
}

static uint8_t requested_channels(const esp_codec_dev_sample_info_t *fs)
{
    if (fs->channel_mask) {
        uint16_t mask = fs->channel_mask;
        uint8_t count = 0;
        while (mask) {
            count += mask & 1;
            mask >>= 1;
        }
        return count;
    }
    return fs->channel;
}

static bool sample_freq_matches(const uac_host_dev_alt_param_t *alt, uint32_t rate)
{
    if (alt->sample_freq_type == 0) {
        return rate >= alt->sample_freq_lower && rate <= alt->sample_freq_upper;
    }
    uint8_t freq_num = alt->sample_freq_type;
    if (freq_num > UAC_FREQ_NUM_MAX) {
        freq_num = UAC_FREQ_NUM_MAX;
    }
    for (int i = 0; i < freq_num; i++) {
        if (alt->sample_freq[i] == rate) {
            return true;
        }
    }
    return false;
}

static bool cap_equals(const esp_codec_dev_capability_t *a, const esp_codec_dev_capability_t *b)
{
    return a->dev_type == b->dev_type &&
           a->mode == b->mode &&
           a->fixed.channel == b->fixed.channel &&
           a->fixed.bits_per_sample == b->fixed.bits_per_sample &&
           a->fixed.sample_rate == b->fixed.sample_rate;
}

static int copy_caps_to_user(const codec_uac_role_state_t *st, esp_codec_dev_type_t dev_type,
                             esp_codec_dev_capability_t *caps, int *count)
{
    int required = st->cap_count;
    if (caps == NULL || *count == 0) {
        *count = required;
        return ESP_CODEC_DEV_OK;
    }
    if (*count < required) {
        *count = required;
        return ESP_CODEC_DEV_NO_MEM;
    }
    for (int i = 0; i < required; i++) {
        caps[i] = st->caps[i];
        caps[i].dev_type = dev_type;
    }
    *count = required;
    return ESP_CODEC_DEV_OK;
}

static esp_err_t add_fixed_cap(codec_uac_role_state_t *st, const uac_host_dev_alt_param_t *alt,
                               uint8_t alt_setting, uint32_t sample_rate)
{
    esp_codec_dev_capability_t cap = {
        .mode = ESP_CODEC_DEV_CAPS_MODE_FIXED,
        .fixed = {
            .channel = alt->channels,
            .bits_per_sample = alt->bit_resolution,
            .sample_rate = sample_rate,
        },
    };
    for (int i = 0; i < st->cap_count; i++) {
        if (cap_equals(&st->caps[i], &cap)) {
            return ESP_OK;
        }
    }
    if (st->cap_count >= UAC_CAPS_MAX) {
        return ESP_ERR_NO_MEM;
    }
    st->caps[st->cap_count] = cap;
    st->cap_alt[st->cap_count] = *alt;
    st->cap_alt_setting[st->cap_count] = alt_setting;
    st->cap_count++;
    return ESP_OK;
}

static esp_err_t build_caps_from_handle(codec_uac_role_state_t *st, uac_host_device_handle_t handle)
{
    uac_host_dev_info_t info = {0};
    esp_err_t ret = uac_host_get_device_info(handle, &info);
    if (ret != ESP_OK) {
        return ret;
    }
    clear_role_caps(st);
    for (uint8_t alt_num = 1; alt_num <= info.iface_alt_num; alt_num++) {
        uac_host_dev_alt_param_t alt = {0};
        ret = uac_host_get_device_alt_param(handle, alt_num, &alt);
        if (ret != ESP_OK) {
            continue;
        }
        if (alt.sample_freq_type == 0) {
            for (int i = 0; i < sizeof(s_common_sample_rates) / sizeof(s_common_sample_rates[0]); i++) {
                uint32_t rate = s_common_sample_rates[i];
                if (rate >= alt.sample_freq_lower && rate <= alt.sample_freq_upper) {
                    ret = add_fixed_cap(st, &alt, alt_num, rate);
                    if (ret != ESP_OK) {
                        return ret;
                    }
                }
            }
        } else {
            uint8_t freq_num = alt.sample_freq_type;
            if (freq_num > UAC_FREQ_NUM_MAX) {
                freq_num = UAC_FREQ_NUM_MAX;
            }
            for (int i = 0; i < freq_num; i++) {
                ret = add_fixed_cap(st, &alt, alt_num, alt.sample_freq[i]);
                if (ret != ESP_OK) {
                    return ret;
                }
            }
        }
    }
    st->caps_cached = true;
    return st->cap_count > 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t select_alt(uac_host_device_handle_t handle,
                            const uac_host_dev_info_t *info,
                            const esp_codec_dev_sample_info_t *fs,
                            uac_host_dev_alt_param_t *selected)
{
    uint8_t channels = requested_channels(fs);
    for (uint8_t alt_num = 1; alt_num <= info->iface_alt_num; alt_num++) {
        uac_host_dev_alt_param_t alt = {0};
        esp_err_t ret = uac_host_get_device_alt_param(handle, alt_num, &alt);
        if (ret != ESP_OK) {
            continue;
        }
        if (alt.channels == channels &&
            alt.bit_resolution == fs->bits_per_sample &&
            sample_freq_matches(&alt, fs->sample_rate)) {
            *selected = alt;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t select_alt_from_cache(const codec_uac_role_state_t *st,
                                       const esp_codec_dev_sample_info_t *fs,
                                       uac_host_dev_alt_param_t *selected)
{
    uint8_t channels = requested_channels(fs);
    for (int i = 0; i < st->cap_count; i++) {
        const esp_codec_dev_capability_t *cap = &st->caps[i];
        if (cap->mode == ESP_CODEC_DEV_CAPS_MODE_FIXED &&
            cap->fixed.channel == channels &&
            cap->fixed.bits_per_sample == fs->bits_per_sample &&
            cap->fixed.sample_rate == fs->sample_rate) {
            *selected = st->cap_alt[i];
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_SUPPORTED;
}

static inline uint32_t size_per_second(const uac_host_dev_alt_param_t *alt, uint32_t sample_rate)
{
    uint32_t bytes_per_sample = alt->subframe_size ? alt->subframe_size : (alt->bit_resolution / 8);
    return sample_rate * alt->channels * bytes_per_sample;
}

static inline uint32_t cfg_buffer_size(void)
{
    return s_uac.cfg.host_buffer_size ? s_uac.cfg.host_buffer_size : UAC_DEFAULT_BUFFER_SIZE;
}

static uint32_t cfg_threshold(void)
{
    uint32_t buffer_size = cfg_buffer_size();
    uint32_t threshold = s_uac.cfg.buffer_threshold ? s_uac.cfg.buffer_threshold : UAC_DEFAULT_BUFFER_THRESHOLD;
    if (threshold > buffer_size) {
        threshold = buffer_size / 2;
    }
    return threshold;
}

static uint32_t cfg_timeout_ms(void)
{
    return s_uac.cfg.connect_timeout_ms ? s_uac.cfg.connect_timeout_ms : UAC_DEFAULT_CONNECT_TIMEOUT_MS;
}

/* Resolve a binding to a device slot (must hold lock). Caches the addr on success. */
static int resolve_slot_locked(uac_binding_t *b, esp_codec_dev_type_t dev_type)
{
    if (b->resolved) {
        int s = find_device_by_addr_locked(b->resolved_addr);
        if (s >= 0 && device_has_roles_locked(&s_uac.devices[s], dev_type)) {
            return s;
        }
        return -1;
    }

    int picked = -1;
    switch (b->mode) {
        case ESP_CODEC_DEV_UAC_SELECT_BY_ADDR: {
            int s = find_device_by_addr_locked(b->sel_addr);
            if (s >= 0 && device_has_roles_locked(&s_uac.devices[s], dev_type)) {
                picked = s;
            }
            break;
        }
        case ESP_CODEC_DEV_UAC_SELECT_FIRST:
        default:
            for (int i = 0; i < s_uac.max_devices; i++) {
                if (s_uac.devices[i].in_use && device_has_roles_locked(&s_uac.devices[i], dev_type)) {
                    picked = i;
                    break;
                }
            }
            break;
    }

    if (picked >= 0) {
        b->resolved = true;
        b->resolved_addr = s_uac.devices[picked].addr;
    }
    return picked;
}

/* Resolve a binding, waiting up to the connect timeout for a matching device. */
static int wait_and_resolve(uac_binding_t *b, esp_codec_dev_type_t dev_type)
{
    TickType_t start = xTaskGetTickCount();
    for (;;) {
        if (!lock_ctx()) {
            vTaskDelay(pdMS_TO_TICKS(UAC_RESOLVE_POLL_MS));
            continue;
        }
        int slot = resolve_slot_locked(b, dev_type);
        unlock_ctx();
        if (slot >= 0) {
            return slot;
        }
        if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(cfg_timeout_ms())) {
            return -1;
        }
        vTaskDelay(pdMS_TO_TICKS(UAC_RESOLVE_POLL_MS));
    }
}

static esp_err_t open_and_start(uac_device_t *dev, codec_uac_role_t role,
                                const esp_codec_dev_sample_info_t *req_fs)
{
    codec_uac_role_state_t snapshot = {0};
    if (!lock_ctx()) {
        return ESP_ERR_TIMEOUT;
    }
    snapshot = dev->role[role];
    unlock_ctx();
    if (snapshot.started) {
        return ESP_OK;
    }
    if (!snapshot.connected) {
        return ESP_ERR_INVALID_STATE;
    }
    if (req_fs->sample_rate == 0 || req_fs->bits_per_sample == 0 ||
        requested_channels(req_fs) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uac_host_device_config_t dev_cfg = {
        .addr = snapshot.addr,
        .iface_num = snapshot.iface_num,
        .buffer_size = cfg_buffer_size(),
        .buffer_threshold = cfg_threshold(),
        .callback = device_event_cb,
        .callback_arg = (void *)(intptr_t)role,
    };
    uac_host_device_handle_t handle = NULL;
    esp_err_t ret = uac_host_device_open(&dev_cfg, &handle);
    if (ret != ESP_OK) {
        if (lock_ctx()) {
            clear_role_caps(&dev->role[role]);
            unlock_ctx();
        }
        ESP_LOGE(TAG, "Open UAC device failed");
        return ret;
    }

    uac_host_dev_info_t info = {0};
    if (snapshot.caps_cached) {
        ret = select_alt_from_cache(&snapshot, req_fs, &snapshot.selected_alt);
    } else {
        ret = build_caps_from_handle(&snapshot, handle);
        if (ret == ESP_OK) {
            ret = select_alt_from_cache(&snapshot, req_fs, &snapshot.selected_alt);
        }
        if (ret != ESP_OK) {
            esp_err_t info_ret = uac_host_get_device_info(handle, &info);
            if (info_ret == ESP_OK) {
                ret = select_alt(handle, &info, req_fs, &snapshot.selected_alt);
            }
        }
    }
    if (ret != ESP_OK) {
        uac_host_device_close(handle);
        return ret;
    }

    uac_host_stream_config_t stream_cfg = {
        .channels = snapshot.selected_alt.channels,
        .bit_resolution = snapshot.selected_alt.bit_resolution,
        .sample_freq = req_fs->sample_rate,
        .flags = 0,
    };
    ret = uac_host_device_start(handle, &stream_cfg);
    if (ret != ESP_OK) {
        uac_host_device_close(handle);
        return ret;
    }

    if (!lock_ctx()) {
        uac_host_device_stop(handle);
        uac_host_device_close(handle);
        return ESP_ERR_TIMEOUT;
    }
    codec_uac_role_state_t *st = &dev->role[role];
    st->handle = handle;
    st->opened = true;
    st->started = true;
    st->selected_alt = snapshot.selected_alt;
    if (snapshot.caps_cached) {
        st->caps_cached = true;
        st->cap_count = snapshot.cap_count;
        memcpy(st->caps, snapshot.caps, sizeof(st->caps));
        memcpy(st->cap_alt, snapshot.cap_alt, sizeof(st->cap_alt));
        memcpy(st->cap_alt_setting, snapshot.cap_alt_setting, sizeof(st->cap_alt_setting));
    }
    unlock_ctx();
    ESP_LOGI(TAG, "UAC %s started, addr %u, %u Hz, %u-bit, %u ch, %lu B/s",
             role == UAC_ROLE_RX ? "RX" : "TX", snapshot.addr,
             (unsigned)req_fs->sample_rate,
             (unsigned)snapshot.selected_alt.bit_resolution,
             (unsigned)snapshot.selected_alt.channels,
             (unsigned long)size_per_second(&snapshot.selected_alt, req_fs->sample_rate));
    return ESP_OK;
}

static esp_err_t close_role(uac_device_t *dev, codec_uac_role_t role)
{
    uac_host_device_handle_t handle = NULL;
    bool started = false;
    if (!lock_ctx()) {
        return ESP_ERR_TIMEOUT;
    }
    codec_uac_role_state_t *st = &dev->role[role];
    handle = st->handle;
    started = st->started;
    st->handle = NULL;
    st->opened = false;
    st->started = false;
    memset(&st->selected_alt, 0, sizeof(st->selected_alt));
    free_device_if_idle_locked(dev);
    unlock_ctx();

    if (handle) {
        if (started) {
            uac_host_device_stop(handle);
        }
        return uac_host_device_close(handle);
    }
    return ESP_OK;
}

static esp_err_t ensure_role_caps(uac_device_t *dev, codec_uac_role_t role)
{
    codec_uac_role_state_t snapshot = {0};
    if (!lock_ctx()) {
        return ESP_CODEC_DEV_TIMEOUT;
    }
    snapshot = dev->role[role];
    unlock_ctx();
    if (snapshot.caps_cached) {
        return ESP_CODEC_DEV_OK;
    }
    if (!snapshot.connected) {
        return ESP_CODEC_DEV_NOT_FOUND;
    }

    uac_host_device_config_t dev_cfg = {
        .addr = snapshot.addr,
        .iface_num = snapshot.iface_num,
        .buffer_size = cfg_buffer_size(),
        .buffer_threshold = cfg_threshold(),
        .callback = device_event_cb,
        .callback_arg = (void *)(intptr_t)role,
    };

    uac_host_device_handle_t handle = NULL;
    esp_err_t ret = uac_host_device_open(&dev_cfg, &handle);
    if (ret != ESP_OK) {
        if (lock_ctx()) {
            clear_role_caps(&dev->role[role]);
            unlock_ctx();
        }
        return esp_err_to_codec_err(ret);
    }

    ret = build_caps_from_handle(&snapshot, handle);
    uac_host_device_close(handle);
    if (ret != ESP_OK) {
        return ret == ESP_ERR_NO_MEM ? ESP_CODEC_DEV_NO_MEM : esp_err_to_codec_err(ret);
    }

    if (!lock_ctx()) {
        return ESP_CODEC_DEV_TIMEOUT;
    }
    codec_uac_role_state_t *st = &dev->role[role];
    st->caps_cached = true;
    st->cap_count = snapshot.cap_count;
    memcpy(st->caps, snapshot.caps, sizeof(st->caps));
    memcpy(st->cap_alt, snapshot.cap_alt, sizeof(st->cap_alt));
    memcpy(st->cap_alt_setting, snapshot.cap_alt_setting, sizeof(st->cap_alt_setting));
    unlock_ctx();
    return ESP_CODEC_DEV_OK;
}

esp_err_t audio_codec_uac_get_dev_num(uint8_t *num)
{
    ESP_RETURN_ON_FALSE(num != NULL, ESP_ERR_INVALID_ARG, TAG, "num is NULL");
    *num = 0;
    if (!lock_ctx()) {
        return ESP_ERR_TIMEOUT;
    }
    for (int i = 0; i < s_uac.max_devices; i++) {
        if (s_uac.devices[i].in_use && device_is_connected_locked(&s_uac.devices[i])) {
            (*num)++;
        }
    }
    unlock_ctx();
    return ESP_OK;
}

esp_err_t audio_codec_uac_get_dev_info(uint8_t index, esp_codec_dev_uac_info_t *info)
{
    ESP_RETURN_ON_FALSE(info != NULL, ESP_ERR_INVALID_ARG, TAG, "info is NULL");
    if (!lock_ctx()) {
        return ESP_ERR_TIMEOUT;
    }
    uint8_t ordinal = 0;
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    for (int i = 0; i < s_uac.max_devices; i++) {
        uac_device_t *dev = &s_uac.devices[i];
        if (!dev->in_use || !device_is_connected_locked(dev)) {
            continue;
        }
        if (ordinal == index) {
            info->addr = dev->addr;
            info->dev_type = device_connected_type_locked(dev);
            ret = ESP_OK;
            break;
        }
        ordinal++;
    }
    unlock_ctx();
    return ret;
}

esp_err_t audio_codec_uac_host_set_event_cb(audio_codec_uac_event_cb_t cb, void *ctx)
{
    if (!lock_ctx()) {
        return ESP_ERR_TIMEOUT;
    }
    s_uac.host_event_cb = cb;
    s_uac.host_event_ctx = ctx;
    unlock_ctx();
    return ESP_OK;
}

esp_err_t audio_codec_uac_host_init(const esp_codec_dev_uac_cfg_t *cfg)
{
    esp_err_t ret = ensure_ctx();
    if (ret != ESP_OK) {
        return ret;
    }
    if (!lock_ctx()) {
        return ESP_ERR_TIMEOUT;
    }
    s_uac.refs++;
    if (cfg) {
        s_uac.cfg = *cfg;
    } else {
        memset(&s_uac.cfg, 0, sizeof(s_uac.cfg));
    }
    if (s_uac.cfg.max_devices > 0 && s_uac.cfg.max_devices <= UAC_MAX_DEVICES) {
        s_uac.max_devices = s_uac.cfg.max_devices;
    } else {
        s_uac.max_devices = UAC_MAX_DEVICES;
    }
    unlock_ctx();

    ret = ensure_driver_installed();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "UAC driver install deferred until USB host is ready");
    }
    return ESP_OK;
}

esp_err_t audio_codec_uac_host_deinit(void)
{
    if (!lock_ctx()) {
        return ESP_CODEC_DEV_TIMEOUT;
    }
    if (s_uac.refs > 0) {
        s_uac.refs--;
    }
    if (s_uac.refs > 0) {
        unlock_ctx();
        return ESP_CODEC_DEV_OK;
    }
    int max_devices = s_uac.max_devices;
    unlock_ctx();

    for (int i = 0; i < max_devices; i++) {
        for (int r = 0; r < UAC_ROLE_MAX; r++) {
            close_role(&s_uac.devices[i], (codec_uac_role_t)r);
        }
    }

    if (!lock_ctx()) {
        return ESP_CODEC_DEV_TIMEOUT;
    }
    bool installed = s_uac.installed;
    s_uac.installed = false;
    unlock_ctx();
    if (installed) {
        esp_err_t ret = uac_host_uninstall();
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            if (lock_ctx()) {
                s_uac.installed = true;
                unlock_ctx();
            }
            return esp_err_to_codec_err(ret);
        }
    }

    SemaphoreHandle_t lock = s_uac.lock;
    s_uac.lock = NULL;
    s_uac.refs = 0;
    s_uac.max_devices = 0;
    memset(s_uac.devices, 0, sizeof(s_uac.devices));
    memset(&s_uac.cfg, 0, sizeof(s_uac.cfg));
    s_uac.host_event_cb = NULL;
    s_uac.host_event_ctx = NULL;
    if (lock) {
        vSemaphoreDelete(lock);
    }
    return ESP_CODEC_DEV_OK;
}

esp_err_t audio_codec_uac_set_fmt(uac_binding_t *b, esp_codec_dev_type_t dev_type,
                                  const esp_codec_dev_sample_info_t *fs)
{
    (void)dev_type;
    ESP_RETURN_ON_FALSE(b != NULL && fs != NULL, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid set_fmt args");
    b->req_fs = *fs;
    return ESP_CODEC_DEV_OK;
}

esp_err_t audio_codec_uac_enable(uac_binding_t *b, esp_codec_dev_type_t dev_type, bool enable)
{
    ESP_RETURN_ON_FALSE(b != NULL, ESP_CODEC_DEV_INVALID_ARG, TAG, "binding NULL");
    if (!enable) {
        if (!lock_ctx()) {
            return ESP_CODEC_DEV_TIMEOUT;
        }
        int slot = b->resolved ? find_device_by_addr_locked(b->resolved_addr) : -1;
        unlock_ctx();
        if (slot >= 0) {
            uac_device_t *dev = &s_uac.devices[slot];
            if (dev_type & ESP_CODEC_DEV_TYPE_IN) {
                close_role(dev, UAC_ROLE_RX);
            }
            if (dev_type & ESP_CODEC_DEV_TYPE_OUT) {
                close_role(dev, UAC_ROLE_TX);
            }
        }
        return ESP_CODEC_DEV_OK;
    }

    esp_err_t ret = ensure_driver_installed();
    if (ret != ESP_OK) {
        return esp_err_to_codec_err(ret);
    }
    int slot = wait_and_resolve(b, dev_type);
    if (slot < 0) {
        return ESP_CODEC_DEV_NOT_FOUND;
    }
    uac_device_t *dev = &s_uac.devices[slot];
    if (dev_type & ESP_CODEC_DEV_TYPE_IN) {
        ret = open_and_start(dev, UAC_ROLE_RX, &b->req_fs);
        if (ret != ESP_OK) {
            return esp_err_to_codec_err(ret);
        }
    }
    if (dev_type & ESP_CODEC_DEV_TYPE_OUT) {
        ret = open_and_start(dev, UAC_ROLE_TX, &b->req_fs);
        if (ret != ESP_OK) {
            if (dev_type & ESP_CODEC_DEV_TYPE_IN) {
                close_role(dev, UAC_ROLE_RX);
            }
            return esp_err_to_codec_err(ret);
        }
    }
    return ESP_CODEC_DEV_OK;
}

esp_err_t audio_codec_uac_get_caps(uac_binding_t *b, esp_codec_dev_type_t dev_type,
                                   esp_codec_dev_capability_t *caps, int *count)
{
    if (b == NULL || count == NULL || *count < 0 ||
        dev_type == ESP_CODEC_DEV_TYPE_NONE ||
        (dev_type & ~(ESP_CODEC_DEV_TYPE_IN_OUT)) != 0) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    esp_err_t ret = ensure_driver_installed();
    if (ret != ESP_OK) {
        return esp_err_to_codec_err(ret);
    }
    int slot = wait_and_resolve(b, dev_type);
    if (slot < 0) {
        return ESP_CODEC_DEV_NOT_FOUND;
    }
    uac_device_t *dev = &s_uac.devices[slot];

    int cret = ESP_CODEC_DEV_OK;
    if (dev_type & ESP_CODEC_DEV_TYPE_IN) {
        cret = ensure_role_caps(dev, UAC_ROLE_RX);
        if (cret != ESP_CODEC_DEV_OK) {
            return cret;
        }
    }
    if (dev_type & ESP_CODEC_DEV_TYPE_OUT) {
        cret = ensure_role_caps(dev, UAC_ROLE_TX);
        if (cret != ESP_CODEC_DEV_OK) {
            return cret;
        }
    }

    codec_uac_role_state_t rx = {0};
    codec_uac_role_state_t tx = {0};
    if (!lock_ctx()) {
        return ESP_CODEC_DEV_TIMEOUT;
    }
    rx = dev->role[UAC_ROLE_RX];
    tx = dev->role[UAC_ROLE_TX];
    unlock_ctx();

    if (dev_type == ESP_CODEC_DEV_TYPE_IN) {
        return copy_caps_to_user(&rx, ESP_CODEC_DEV_TYPE_IN, caps, count);
    }
    if (dev_type == ESP_CODEC_DEV_TYPE_OUT) {
        return copy_caps_to_user(&tx, ESP_CODEC_DEV_TYPE_OUT, caps, count);
    }

    int required = rx.cap_count + tx.cap_count;
    if (caps == NULL || *count == 0) {
        *count = required;
        return ESP_CODEC_DEV_OK;
    }
    if (*count < required) {
        *count = required;
        return ESP_CODEC_DEV_NO_MEM;
    }
    int idx = 0;
    for (int i = 0; i < rx.cap_count; i++) {
        caps[idx] = rx.caps[i];
        caps[idx].dev_type = ESP_CODEC_DEV_TYPE_IN;
        idx++;
    }
    for (int i = 0; i < tx.cap_count; i++) {
        caps[idx] = tx.caps[i];
        caps[idx].dev_type = ESP_CODEC_DEV_TYPE_OUT;
        idx++;
    }
    *count = idx;
    return ESP_CODEC_DEV_OK;
}

bool audio_codec_uac_is_open(uac_binding_t *b)
{
    bool opened = false;
    if (b == NULL) {
        return false;
    }
    if (!lock_ctx()) {
        return false;
    }
    if (b->resolved) {
        int slot = find_device_by_addr_locked(b->resolved_addr);
        if (slot >= 0) {
            opened = s_uac.devices[slot].role[UAC_ROLE_RX].opened ||
                     s_uac.devices[slot].role[UAC_ROLE_TX].opened;
        }
    }
    unlock_ctx();
    return opened;
}

esp_err_t audio_codec_uac_read(uac_binding_t *b, uint8_t *data, int size)
{
    ESP_RETURN_ON_FALSE(b != NULL && data != NULL && size > 0, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid read args");
    if (!lock_ctx()) {
        return ESP_CODEC_DEV_TIMEOUT;
    }
    uac_host_device_handle_t handle = get_active_handle_locked(b, UAC_ROLE_RX);
    unlock_ctx();
    if (handle == NULL) {
        ESP_LOGE(TAG, "UAC RX not opened");
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    uint32_t bytes_read = 0;
    esp_err_t ret = uac_host_device_read(handle, data, (uint32_t)size, &bytes_read, pdMS_TO_TICKS(UAC_DEFAULT_RW_TIMEOUT_MS));
    if (ret != ESP_OK) {
        return esp_err_to_codec_err(ret);
    }
    return bytes_read == (uint32_t)size ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}

esp_err_t audio_codec_uac_write(uac_binding_t *b, uint8_t *data, int size)
{
    ESP_RETURN_ON_FALSE(b != NULL && data != NULL && size > 0, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid write args");
    if (!lock_ctx()) {
        return ESP_CODEC_DEV_TIMEOUT;
    }
    uac_host_device_handle_t handle = get_active_handle_locked(b, UAC_ROLE_TX);
    unlock_ctx();
    if (handle == NULL) {
        ESP_LOGE(TAG, "UAC TX not opened");
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    esp_err_t ret = uac_host_device_write(handle, data, (uint32_t)size, pdMS_TO_TICKS(UAC_DEFAULT_RW_TIMEOUT_MS));
    return esp_err_to_codec_err(ret);
}

esp_err_t audio_codec_uac_set_mute(uac_binding_t *b, esp_codec_dev_type_t dev_type, bool mute)
{
    ESP_RETURN_ON_FALSE(b != NULL, ESP_CODEC_DEV_INVALID_ARG, TAG, "binding NULL");
    codec_uac_role_t role = (dev_type & ESP_CODEC_DEV_TYPE_IN) ? UAC_ROLE_RX : UAC_ROLE_TX;
    if (!lock_ctx()) {
        return ESP_CODEC_DEV_TIMEOUT;
    }
    uac_host_device_handle_t handle = get_active_handle_locked(b, role);
    unlock_ctx();
    if (handle == NULL) {
        ESP_LOGE(TAG, "UAC device not opened");
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    esp_err_t ret = uac_host_device_set_mute(handle, mute);
    return esp_err_to_codec_err(ret);
}

esp_err_t audio_codec_uac_set_volume_db(uac_binding_t *b, esp_codec_dev_type_t dev_type, float db)
{
    ESP_RETURN_ON_FALSE(b != NULL, ESP_CODEC_DEV_INVALID_ARG, TAG, "binding NULL");
    codec_uac_role_t role = (dev_type & ESP_CODEC_DEV_TYPE_IN) ? UAC_ROLE_RX : UAC_ROLE_TX;
    if (!lock_ctx()) {
        return ESP_CODEC_DEV_TIMEOUT;
    }
    uac_host_device_handle_t handle = get_active_handle_locked(b, role);
    unlock_ctx();
    if (handle == NULL) {
        ESP_LOGE(TAG, "UAC device not opened");
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    uint8_t volume = 0;
    if (db >= 0.0f) {
        volume = 100;
    } else if (db > -50.0f) {
        volume = (uint8_t)((db + 50.0f) * 2.0f + 0.5f);
    }
    esp_err_t ret = uac_host_device_set_volume(handle, volume);
    return esp_err_to_codec_err(ret);
}
