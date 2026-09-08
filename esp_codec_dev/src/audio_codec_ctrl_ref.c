/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "esp_cpu.h"
#include "esp_log.h"

#include "audio_codec_ctrl_ref.h"
#include "esp_codec_dev_os.h"

static const char *TAG = "ADEV_CTRL_REF";

#define REF_LOCK_TIMEOUT_MS  (1000)

/**
 * @brief  Codec device reference list node
 */
typedef struct audio_codec_ctrl_ref_node {
    audio_codec_ctrl_info_t           info;       /*!< Control interface identity */
    uint8_t                           ref_count;  /*!< Reference count */
    struct audio_codec_ctrl_ref_node *next;       /*!< Next node in the list */
} audio_codec_ctrl_ref_node_t;

static audio_codec_ctrl_ref_node_t *s_ctrl_ref_list;
static esp_codec_dev_mutex_handle_t s_ctrl_ref_mutex;

static bool ctrl_info_equal(const audio_codec_ctrl_info_t *a, const audio_codec_ctrl_info_t *b)
{
    if (a->type != b->type) {
        return false;
    }
    switch (a->type) {
        case AUDIO_CODEC_CTRL_I2C:
            return a->i2c.addr == b->i2c.addr && a->i2c.bus_handle == b->i2c.bus_handle;
        case AUDIO_CODEC_CTRL_SPI:
            return a->spi.cs_pin == b->spi.cs_pin;
        default:
            return false;
    }
}

static esp_codec_dev_mutex_handle_t audio_codec_ctrl_ref_get_mutex(void)
{
    if (s_ctrl_ref_mutex != NULL) {
        return s_ctrl_ref_mutex;
    }

    esp_codec_dev_mutex_handle_t mutex = esp_codec_dev_mutex_create();
    if (mutex == NULL) {
        return NULL;
    }

    /* esp_cpu_compare_and_set operates on 32-bit values (32-bit pointer targets). */
    if (esp_cpu_compare_and_set((volatile uint32_t *)&s_ctrl_ref_mutex,
                                (uint32_t)NULL,
                                (uint32_t)mutex)) {
        return mutex;
    }

    esp_codec_dev_mutex_destroy(mutex);
    return s_ctrl_ref_mutex;
}

static audio_codec_ctrl_ref_node_t *audio_codec_ctrl_ref_find_node(const audio_codec_ctrl_info_t *info)
{
    audio_codec_ctrl_ref_node_t *p = s_ctrl_ref_list;
    while (p) {
        if (ctrl_info_equal(&p->info, info)) {
            return p;
        }
        p = p->next;
    }
    return NULL;
}

int audio_codec_ctrl_ref_acquire(const audio_codec_ctrl_info_t *info)
{
    if (info == NULL) {
        return -1;
    }
    esp_codec_dev_mutex_handle_t mutex = audio_codec_ctrl_ref_get_mutex();
    if (mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create codec ctrl ref mutex");
        return -1;
    }
    if (esp_codec_dev_mutex_lock(mutex, REF_LOCK_TIMEOUT_MS) != 0) {
        ESP_LOGE(TAG, "Failed to lock codec ctrl ref list");
        return -1;
    }
    int ref_count = 0;
    audio_codec_ctrl_ref_node_t *p = audio_codec_ctrl_ref_find_node(info);
    if (p) {
        if (p->ref_count < UINT8_MAX) {
            p->ref_count++;
        }
        ref_count = p->ref_count;
        goto unlock;
    }
    audio_codec_ctrl_ref_node_t *new_node = (audio_codec_ctrl_ref_node_t *)calloc(1, sizeof(audio_codec_ctrl_ref_node_t));
    if (new_node == NULL) {
        goto unlock;
    }
    memcpy(&new_node->info, info, sizeof(audio_codec_ctrl_info_t));
    new_node->ref_count = 1;
    new_node->next = s_ctrl_ref_list;
    s_ctrl_ref_list = new_node;
    ref_count = 1;
unlock:
    esp_codec_dev_mutex_unlock(mutex);
    return ref_count > 0 ? ref_count : -1;
}

int audio_codec_ctrl_ref_release(const audio_codec_ctrl_info_t *info)
{
    if (info == NULL) {
        ESP_LOGE(TAG, "Invalid info");
        return -1;
    }
    if (s_ctrl_ref_mutex == NULL) {
        ESP_LOGE(TAG, "Codec ctrl ref list is not initialized");
        return -1;
    }
    if (esp_codec_dev_mutex_lock(s_ctrl_ref_mutex, REF_LOCK_TIMEOUT_MS) != 0) {
        ESP_LOGE(TAG, "Failed to lock codec ctrl ref list");
        return -1;
    }
    int ref_count = -1;
    audio_codec_ctrl_ref_node_t *p = s_ctrl_ref_list;
    audio_codec_ctrl_ref_node_t *prev = NULL;
    while (p) {
        if (ctrl_info_equal(&p->info, info)) {
            if (p->ref_count > 0) {
                p->ref_count--;
            }
            ref_count = p->ref_count;
            if (p->ref_count == 0) {
                if (prev) {
                    prev->next = p->next;
                } else {
                    s_ctrl_ref_list = p->next;
                }
                free(p);
            }
            goto unlock;
        }
        prev = p;
        p = p->next;
    }
unlock:
    esp_codec_dev_mutex_unlock(s_ctrl_ref_mutex);
    return ref_count;
}
