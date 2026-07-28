/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "sdkconfig.h"
#include "esp_cpu.h"
#include "esp_log.h"

#include "esp_codec_dev.h"
#include "esp_codec_dev_uac.h"
#include "audio_codec_uac_priv.h"

static const char *TAG = "CODEC_UAC_MGR";

#ifdef CONFIG_CODEC_UAC_MAX_DEVICES
#define UAC_MGR_MAX_DEVICES  CONFIG_CODEC_UAC_MAX_DEVICES
#else
#define UAC_MGR_MAX_DEVICES  (2)
#endif  /* CONFIG_CODEC_UAC_MAX_DEVICES */

#define UAC_MGR_MAX_BINDINGS              (UAC_MGR_MAX_DEVICES * 2)
#define UAC_MGR_DEFAULT_MUTEX_TIMEOUT_MS  (1000)

typedef struct {
    esp_codec_dev_handle_t       dev;
    const audio_codec_if_t      *codec_if;
    const audio_codec_data_if_t *data_if;
    uac_binding_t               *binding;
    bool                         used;
} uac_dev_entry_t;

static bool s_installed;
static uac_dev_entry_t s_devs[UAC_MGR_MAX_BINDINGS];
static SemaphoreHandle_t s_mgr_lock;

static inline bool lock_mgr(void)
{
    if (s_mgr_lock == NULL) {
        return false;
    }
    if (xSemaphoreTake(s_mgr_lock, pdMS_TO_TICKS(UAC_MGR_DEFAULT_MUTEX_TIMEOUT_MS)) == pdPASS) {
        return true;
    }
    ESP_LOGE(TAG, "Lock UAC manager mutex timeout");
    return false;
}

static inline void unlock_mgr(void)
{
    if (s_mgr_lock) {
        xSemaphoreGive(s_mgr_lock);
    }
}

static int ensure_mgr_lock(void)
{
    if (s_mgr_lock != NULL) {
        return ESP_CODEC_DEV_OK;
    }
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    if (mutex == NULL) {
        ESP_LOGE(TAG, "No memory for manager lock");
        return ESP_CODEC_DEV_NO_MEM;
    }
    if (esp_cpu_compare_and_set((volatile uint32_t *)&s_mgr_lock,
                                (uint32_t)NULL,
                                (uint32_t)mutex)) {
        return ESP_CODEC_DEV_OK;
    }
    vSemaphoreDelete(mutex);
    return ESP_CODEC_DEV_OK;
}

static void uac_mgr_event(const esp_codec_dev_uac_event_info_t *info, void *ctx)
{
    (void)ctx;
    if (info->event != ESP_CODEC_DEV_UAC_EVENT_DISCONNECTED) {
        return;
    }
    if (!lock_mgr()) {
        return;
    }
    for (int i = 0; i < UAC_MGR_MAX_BINDINGS; i++) {
        uac_dev_entry_t *slot = &s_devs[i];
        if (!slot->used || slot->binding == NULL) {
            continue;
        }
        if (slot->binding->resolved && slot->binding->resolved_addr == info->addr) {
            slot->binding->resolved = false;
            slot->binding->resolved_addr = 0;
        }
    }
    unlock_mgr();
}

static void release_entry(uac_dev_entry_t *slot)
{
    if (slot->dev) {
        esp_codec_dev_close(slot->dev);
        esp_codec_dev_delete(slot->dev);
    }
    if (slot->data_if) {
        audio_codec_delete_data_if(slot->data_if);
    }
    if (slot->codec_if) {
        audio_codec_delete_codec_if(slot->codec_if);
    }
    if (slot->binding) {
        free(slot->binding);
    }
    memset(slot, 0, sizeof(*slot));
}

int esp_codec_dev_uac_install(const esp_codec_dev_uac_cfg_t *cfg)
{
    int ret = ensure_mgr_lock();
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    if (!lock_mgr()) {
        return ESP_CODEC_DEV_TIMEOUT;
    }
    if (s_installed) {
        unlock_mgr();
        ESP_LOGE(TAG, "UAC manager already installed");
        return ESP_CODEC_DEV_WRONG_STATE;
    }

    esp_err_t err = audio_codec_uac_host_init(cfg);
    if (err != ESP_OK) {
        unlock_mgr();
        ESP_LOGE(TAG, "Failed to init UAC host");
        return esp_err_to_codec_err(err);
    }
    err = audio_codec_uac_host_set_event_cb(uac_mgr_event, NULL);
    if (err != ESP_OK) {
        unlock_mgr();
        ESP_LOGE(TAG, "Failed to register UAC manager event callback");
        return esp_err_to_codec_err(err);
    }
    s_installed = true;
    unlock_mgr();
    return ESP_CODEC_DEV_OK;
}

esp_codec_dev_handle_t esp_codec_dev_uac_new_dev(const esp_codec_dev_uac_select_t *sel)
{
    if (sel == NULL) {
        ESP_LOGE(TAG, "Invalid argument");
        return NULL;
    }

    if (!lock_mgr()) {
        return NULL;
    }
    if (!s_installed) {
        unlock_mgr();
        ESP_LOGE(TAG, "UAC manager not installed");
        return NULL;
    }

    uac_dev_entry_t *slot = NULL;
    for (int i = 0; i < UAC_MGR_MAX_BINDINGS; i++) {
        if (!s_devs[i].used) {
            slot = &s_devs[i];
            break;
        }
    }
    if (slot == NULL) {
        unlock_mgr();
        ESP_LOGE(TAG, "No free device slot");
        return NULL;
    }
    slot->used = true;
    unlock_mgr();

    slot->binding = calloc(1, sizeof(uac_binding_t));
    if (slot->binding == NULL) {
        ESP_LOGE(TAG, "No memory for binding");
        if (lock_mgr()) {
            slot->used = false;
            unlock_mgr();
        }
        return NULL;
    }
    slot->binding->mode = sel->mode;
    slot->binding->sel_addr = sel->addr;
    slot->binding->dev_type = sel->dev_type;

    slot->codec_if = uac_codec_new(slot->binding);
    if (slot->codec_if == NULL) {
        ESP_LOGE(TAG, "Failed to create UAC codec interface");
        goto cleanup;
    }
    slot->data_if = audio_codec_new_uac_data(slot->binding);
    if (slot->data_if == NULL) {
        ESP_LOGE(TAG, "Failed to create UAC data interface");
        goto cleanup;
    }
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = sel->dev_type,
        .codec_if = slot->codec_if,
        .data_if = slot->data_if,
    };
    slot->dev = esp_codec_dev_new(&dev_cfg);
    if (slot->dev == NULL) {
        ESP_LOGE(TAG, "Failed to create codec device");
        goto cleanup;
    }
    return slot->dev;

cleanup:
    if (lock_mgr()) {
        release_entry(slot);
        unlock_mgr();
    }
    return NULL;
}

int esp_codec_dev_uac_del_dev(esp_codec_dev_handle_t dev)
{
    if (dev == NULL) {
        ESP_LOGE(TAG, "Invalid device handle");
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    if (!lock_mgr()) {
        return ESP_CODEC_DEV_TIMEOUT;
    }
    if (!s_installed) {
        unlock_mgr();
        ESP_LOGE(TAG, "UAC manager not installed");
        return ESP_CODEC_DEV_WRONG_STATE;
    }

    uac_dev_entry_t *slot = NULL;
    for (int i = 0; i < UAC_MGR_MAX_BINDINGS; i++) {
        if (s_devs[i].used && s_devs[i].dev == dev) {
            slot = &s_devs[i];
            break;
        }
    }
    if (slot == NULL) {
        unlock_mgr();
        ESP_LOGE(TAG, "UAC device not found");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    release_entry(slot);
    unlock_mgr();
    return ESP_CODEC_DEV_OK;
}

int esp_codec_dev_uac_get_num(uint8_t *num)
{
    if (num == NULL) {
        ESP_LOGE(TAG, "Invalid num pointer");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (!lock_mgr()) {
        return ESP_CODEC_DEV_TIMEOUT;
    }
    if (!s_installed) {
        unlock_mgr();
        ESP_LOGE(TAG, "UAC manager not installed");
        return ESP_CODEC_DEV_WRONG_STATE;
    }

    int ret = audio_codec_uac_get_dev_num(num);
    unlock_mgr();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fail to get UAC device number, ret=%d", ret);
        return esp_err_to_codec_err(ret);
    }
    return ESP_CODEC_DEV_OK;
}

int esp_codec_dev_uac_get_info(uint8_t index, esp_codec_dev_uac_info_t *info)
{
    if (info == NULL) {
        ESP_LOGE(TAG, "Invalid info pointer");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (!lock_mgr()) {
        return ESP_CODEC_DEV_TIMEOUT;
    }
    if (!s_installed) {
        unlock_mgr();
        ESP_LOGE(TAG, "UAC manager not installed");
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    unlock_mgr();

    int ret = esp_err_to_codec_err(audio_codec_uac_get_dev_info(index, info));
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Fail to get UAC device info, index=%u, ret=%d", index, ret);
    }
    return ret;
}

int esp_codec_dev_uac_uninstall(void)
{
    if (!lock_mgr()) {
        return ESP_CODEC_DEV_TIMEOUT;
    }
    if (!s_installed) {
        unlock_mgr();
        ESP_LOGE(TAG, "UAC manager not installed");
        return ESP_CODEC_DEV_WRONG_STATE;
    }

    for (int i = 0; i < UAC_MGR_MAX_BINDINGS; i++) {
        if (s_devs[i].used) {
            unlock_mgr();
            ESP_LOGE(TAG, "Derived UAC devices still active");
            return ESP_CODEC_DEV_WRONG_STATE;
        }
    }
    s_installed = false;
    unlock_mgr();

    esp_err_t ret = audio_codec_uac_host_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fail to deinit UAC host, ret=%d", (int)ret);
        return esp_err_to_codec_err(ret);
    }

    if (s_mgr_lock) {
        vSemaphoreDelete(s_mgr_lock);
        s_mgr_lock = NULL;
    }
    return ESP_CODEC_DEV_OK;
}
