/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdint.h>

#include "esp_cpu.h"

#include "esp_codec_dev_os.h"
#include "esp_codec_dev_types.h"
#include "audio_codec_hw_proc.h"

#define ESP_AUDIO_HW_PROC_LOCK_TIMEOUT_MS  (1000)

static esp_codec_dev_mutex_handle_t s_esp_audio_hw_proc_mutex;

static esp_codec_dev_mutex_handle_t esp_audio_hw_proc_get_mutex(void)
{
    if (s_esp_audio_hw_proc_mutex != NULL) {
        return s_esp_audio_hw_proc_mutex;
    }

    esp_codec_dev_mutex_handle_t mutex = esp_codec_dev_mutex_create();
    if (mutex == NULL) {
        return NULL;
    }

    /* Same pattern as codec_ref_mgr: CAS for concurrent first create */
    if (esp_cpu_compare_and_set((volatile uint32_t *)&s_esp_audio_hw_proc_mutex,
                                (uint32_t)NULL,
                                (uint32_t)mutex)) {
        return mutex;
    }

    esp_codec_dev_mutex_destroy(mutex);
    return s_esp_audio_hw_proc_mutex;
}

int esp_audio_hw_proc_lock(void)
{
    esp_codec_dev_mutex_handle_t mutex = esp_audio_hw_proc_get_mutex();
    if (mutex == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }
    if (esp_codec_dev_mutex_lock(mutex, ESP_AUDIO_HW_PROC_LOCK_TIMEOUT_MS) != 0) {
        return ESP_CODEC_DEV_TIMEOUT;
    }
    return ESP_CODEC_DEV_OK;
}

void esp_audio_hw_proc_unlock(void)
{
    esp_codec_dev_mutex_unlock(s_esp_audio_hw_proc_mutex);
}

int esp_audio_hw_proc_check_open(const audio_codec_if_t *codec_if)
{
    if (codec_if->hw_base.is_open &&
        codec_if->hw_base.is_open(&codec_if->hw_base) == false) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    return ESP_CODEC_DEV_OK;
}

int esp_audio_hw_proc_resolve(esp_codec_dev_handle_t dev, esp_audio_hw_proc_get_ops_fn get_ops,
                              const audio_codec_if_t **codec_if, const void **ops)
{
    if (dev == NULL || get_ops == NULL || codec_if == NULL || ops == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    const audio_codec_if_t *cif = esp_audio_hw_proc_get_codec_if(dev);
    if (cif == NULL || cif->hw_proc == NULL) {
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    const void *proc_ops = get_ops(cif->hw_proc);
    if (proc_ops == NULL) {
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }

    *codec_if = cif;
    *ops = proc_ops;
    return ESP_CODEC_DEV_OK;
}
