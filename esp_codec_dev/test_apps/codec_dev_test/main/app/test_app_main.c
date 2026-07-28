/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "unity.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "soc/soc_caps.h"
#if TEST_MEMORY_LEAK_ENABLE && defined(CONFIG_HEAP_TRACING)
#include "esp_heap_trace.h"
#endif  /* TEST_MEMORY_LEAK_ENABLE && defined(CONFIG_HEAP_TRACING) */

#include "unity_test_utils.h"
#include "test_board_periph.h"

static const char *TAG = "CODEC_DEV_TEST";

#define TEST_MEMORY_LEAK_THRESHOLD  (400)
#define TEST_MEMORY_LEAK_ENABLE     (0)

static void warmup_i2c(uint8_t port, codec_i2c_pin_t *i2c_pin)
{
    int ret = ut_i2c_init(port, i2c_pin);
    if (ret != 0) {
        ESP_LOGW(TAG, "Warm up I2C failed: %d", ret);
        return;
    }
    ut_i2c_deinit(port);
}

static void warmup_i2s(uint8_t port, codec_i2s_pin_t *i2s_pin)
{
    ut_set_i2s_mode(I2S_COMM_MODE_STD, I2S_COMM_MODE_STD);
    int ret = ut_i2s_init(port, i2s_pin, I2S_CLK_SRC_DEFAULT);
    if (ret != 0) {
        ESP_LOGW(TAG, "Warm up I2S failed: %d", ret);
    }
    ut_i2s_deinit(port);
    ut_clr_i2s_mode();
}

static void codec_dev_test_warmup(void)
{
#if CONFIG_IDF_TARGET_ESP32
    codec_i2c_pin_t i2c_pin = {.sda = 18, .scl = 23};
    codec_i2s_pin_t i2s_out_pin = {.mclk = -1, .bclk = 5, .ws = 25, .dout = 26, .din = -1};
    codec_i2s_pin_t i2s_in_pin = {.mclk = 0, .bclk = 32, .ws = 33, .dout = -1, .din = 36};
    warmup_i2c(0, &i2c_pin);
    warmup_i2s(0, &i2s_out_pin);
    warmup_i2s(1, &i2s_in_pin);
#elif CONFIG_IDF_TARGET_ESP32P4
    codec_i2c_pin_t i2c_pin = {.sda = 7, .scl = 8};
    codec_i2s_pin_t i2s_pin = {.mclk = 13, .bclk = 12, .ws = 10, .dout = 9, .din = 11};
    warmup_i2c(0, &i2c_pin);
    warmup_i2s(0, &i2s_pin);
    if (ut_ledc_output_pwm(16000 * 256, 13) == 0) {
        ut_ledc_deinit(13);
    }
#elif CONFIG_IDF_TARGET_ESP32C3 && SOC_I2S_SUPPORTS_PDM_TX
    if (ut_i2s_init_pdm_out(0) == 0) {
        ut_i2s_deinit(0);
    }
#else
    warmup_i2c(0, NULL);
    warmup_i2s(0, NULL);
#endif  /* CONFIG_IDF_TARGET_ESP32 */
}

void setUp(void)
{
#if TEST_MEMORY_LEAK_ENABLE && defined(CONFIG_HEAP_TRACING)
    unity_utils_setup_heap_record(256);
    TEST_ESP_OK(heap_trace_start(HEAP_TRACE_LEAKS));
#endif  /* TEST_MEMORY_LEAK_ENABLE && defined(CONFIG_HEAP_TRACING) */
    unity_utils_record_free_mem();
    esp_log_level_set("*", ESP_LOG_DEBUG);
}

void tearDown(void)
{
#if TEST_MEMORY_LEAK_ENABLE && defined(CONFIG_HEAP_TRACING)
    TEST_ESP_OK(heap_trace_stop());
    heap_trace_dump();
#endif  /* TEST_MEMORY_LEAK_ENABLE && defined(CONFIG_HEAP_TRACING) */
    unity_utils_evaluate_leaks_direct(TEST_MEMORY_LEAK_THRESHOLD);
    vTaskDelay(pdMS_TO_TICKS(100));
}

void app_main(void)
{
    codec_dev_test_warmup();
    unity_run_menu();
}
