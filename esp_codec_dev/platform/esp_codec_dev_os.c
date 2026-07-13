/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_check.h"
#include "esp_log.h"

#include "esp_codec_dev_os.h"

static const char *TAG = "CODEC_DEV_OS";

esp_codec_dev_mutex_handle_t esp_codec_dev_mutex_create(void)
{
    esp_codec_dev_mutex_handle_t mutex = (esp_codec_dev_mutex_handle_t)xSemaphoreCreateMutex();
    if (mutex == NULL) {
        ESP_LOGE(TAG, "No memory for mutex");
    }
    return mutex;
}

int esp_codec_dev_mutex_lock(esp_codec_dev_mutex_handle_t mutex, int timeout_ms)
{
    ESP_RETURN_ON_FALSE(mutex, -1, TAG, "Invalid mutex");
    if (xSemaphoreTake((SemaphoreHandle_t)mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGE(TAG, "Fail to lock mutex, timeout_ms=%d", timeout_ms);
        return -1;
    }
    return 0;
}

int esp_codec_dev_mutex_unlock(esp_codec_dev_mutex_handle_t mutex)
{
    ESP_RETURN_ON_FALSE(mutex, -1, TAG, "Invalid mutex");
    return xSemaphoreGive((SemaphoreHandle_t)mutex) ? 0 : -1;
}

void esp_codec_dev_mutex_destroy(esp_codec_dev_mutex_handle_t mutex)
{
    if (mutex == NULL) {
        return;
    }
    vSemaphoreDelete((SemaphoreHandle_t)mutex);
}

void esp_codec_dev_sleep(int ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}
