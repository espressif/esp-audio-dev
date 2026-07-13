/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "unity.h"

#include "unity_test_utils.h"

#define TEST_MEMORY_LEAK_THRESHOLD  (400)

void setUp(void)
{
    unity_utils_record_free_mem();
}

void tearDown(void)
{
    unity_utils_evaluate_leaks_direct(TEST_MEMORY_LEAK_THRESHOLD);
    vTaskDelay(pdMS_TO_TICKS(100));
}

void app_main(void)
{
    unity_run_menu();
}
