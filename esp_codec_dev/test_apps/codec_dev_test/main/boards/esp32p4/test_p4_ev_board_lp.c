/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/lp_i2s.h"
#include "driver/lp_i2s_std.h"
#include "driver/lp_i2s_vad.h"
#include "driver/uart.h"
#include "esp_bit_defs.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "soc/lp_i2s_struct.h"
#include "soc/soc_caps.h"
#include "unity.h"

#include "esp_codec_dev_defaults.h"
#include "audio_codec_if.h"
#include "test_board_periph.h"
#include "test_codec_print.h"
#include "boards/esp32p4/test_p4_ev_board_lp.h"

static const char *TAG = "CODEC_DEV_P4_EV_LP";

#if SOC_LP_I2S_SUPPORTED && SOC_LP_VAD_SUPPORTED && defined(CONFIG_CODEC_ES8311_SUPPORT)

#define ENABLE_EXTERNAL_PIN_TEST  0

#if ENABLE_EXTERNAL_PIN_TEST
#define TEST_BOARD_LP_I2S_WS_PIN   (2)
#define TEST_BOARD_LP_I2S_BCK_PIN  (3)
#define TEST_BOARD_LP_I2S_DIN_PIN  (4)
#define TEST_BOARD_LP_I2S_MCK_PIN  (5)
#else
#define TEST_BOARD_LP_I2S_BCK_PIN  (12)
#define TEST_BOARD_LP_I2S_WS_PIN   (10)
#define TEST_BOARD_LP_I2S_DIN_PIN  (11)
#define TEST_BOARD_LP_I2S_MCK_PIN  (13)
#endif  /* ENABLE_EXTERNAL_PIN_TEST */

static void lp_vad_test(lp_i2s_chan_handle_t rx_chan)
{
    lp_vad_init_config_t init_config = {
        .lp_i2s_chan = rx_chan,
        .vad_config = {
            .init_frame_num = 100,
            .min_energy_thresh = 100,
            .skip_band_energy_thresh = false,
            .speak_activity_thresh = 10,
            .non_speak_activity_thresh = 30,
            .min_speak_activity_thresh = 3,
            .max_speak_activity_thresh = 100,
        },
    };
    vad_unit_handle_t vad_unit = NULL;
    esp_err_t ret = lp_i2s_vad_new_unit(0, &init_config, &vad_unit);
    TEST_ESP_OK(ret);
    TEST_ASSERT_NOT_NULL(vad_unit);

    ret = lp_i2s_vad_enable(vad_unit);
    TEST_ESP_OK(ret);

    int count = 0;
    while (count++ < 50) {
        ESP_LOGI(TAG, "LP VAD test (vad_flag=%u, energy_enough=%u)",
                 LP_I2S.vad_result.vad_flag, LP_I2S.vad_result.energy_enough);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    count = 0;
    while (count++ < 5) {
        ESP_LOGI(TAG, "Entering light sleep, waiting for VAD trigger...");
        uart_wait_tx_done(0, pdMS_TO_TICKS(1000));
        esp_sleep_enable_vad_wakeup();
        ret = esp_light_sleep_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_light_sleep_start failed: %s", esp_err_to_name(ret));
        }

        int cause = esp_sleep_get_wakeup_causes();
        if (cause & BIT(ESP_SLEEP_WAKEUP_VAD)) {
            ESP_LOGI(TAG, "VAD triggered: wakeup cause is ESP_SLEEP_WAKEUP_VAD");
        } else {
            ESP_LOGW(TAG, "VAD not triggered, wakeup cause: %d", cause);
        }
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    TEST_ESP_OK(lp_i2s_vad_disable(vad_unit));
    TEST_ESP_OK(lp_i2s_vad_del_unit(vad_unit));
}

void test_p4_ev_board_lp_i2s_rx_es8311_record_vad(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ut_ledc_output_pwm(16000 * 256, TEST_BOARD_LP_I2S_MCK_PIN);
    /* 1) Init LP_I2S RX only. Driver on current IDF only supports LP_I2S slave role. */
    lp_i2s_chan_handle_t lp_rx_chan = NULL;
    lp_i2s_chan_config_t lp_cfg = {
        .id = 0,
        .role = I2S_ROLE_SLAVE,
        .threshold = 512,
    };
    esp_err_t ret = lp_i2s_new_channel(&lp_cfg, NULL, &lp_rx_chan);
    TEST_ESP_OK(ret);
    TEST_ASSERT_NOT_NULL(lp_rx_chan);

    lp_i2s_std_config_t lp_std_cfg = {
        .pin_cfg = {
            .bck = TEST_BOARD_LP_I2S_BCK_PIN,
            .ws = TEST_BOARD_LP_I2S_WS_PIN,
            .din = TEST_BOARD_LP_I2S_DIN_PIN,
        },
        .slot_cfg = LP_I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    };
    lp_std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
    lp_std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    TEST_ESP_OK(lp_i2s_channel_init_std_mode(lp_rx_chan, &lp_std_cfg));
    TEST_ESP_OK(lp_i2s_channel_enable(lp_rx_chan));

    /* 2) Init ES8311 and enable ADC for capture. */
    codec_i2c_pin_t i2c_pin = {
        .scl = 8,
        .sda = 7,
    };
    ret = ut_i2c_init(0, &i2c_pin);
    TEST_ESP_OK(ret);
    audio_codec_i2c_cfg_t i2c_cfg = {.addr = ES8311_CODEC_DEFAULT_ADDR};
    i2c_cfg.bus_handle = ut_i2c_get_bus_handle();
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    TEST_ASSERT_NOT_NULL(ctrl_if);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    TEST_ASSERT_NOT_NULL(gpio_if);

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .sys_cfg = {
            .is_master = true,
            .no_mclk = false,
        },
        .adc_cfg = {
            .digital_mic = false,
            .label = "FL,RE",
        },
        .dac_cfg = {
            .ref_enable = false,
        },
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es8311_cfg);
    TEST_ASSERT_NOT_NULL(codec_if);

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 2,
        .channel_mask = BIT(0) | BIT(1),
        .bits_per_sample = 16,
        .mclk_multiple = 256,
    };
    TEST_ESP_OK(codec_if->hw_base.set_fs(&codec_if->hw_base, &fs, ESP_CODEC_DEV_TYPE_IN));
    TEST_ASSERT_NOT_NULL(codec_if->adc_if);
    TEST_ESP_OK(codec_if->adc_if->ops.enable(codec_if, true));
    TEST_ESP_OK(codec_if->adc_if->ops.set_vol(codec_if, BIT(0) | BIT(1), 10));

    /* 3) Read and print 5 seconds LP_I2S capture data. */
    uint8_t rx_buf[1024];
    lp_i2s_trans_t trans = {
        .buffer = rx_buf,
        .buflen = sizeof(rx_buf),
    };
    int bytes_target = fs.sample_rate * fs.channel * (fs.bits_per_sample >> 3) * 5;
    int bytes_total = 0;
    ESP_LOGI(TAG, "Start LP_I2S capture for 5 seconds");
    while (bytes_total < bytes_target) {
        TEST_ESP_OK(lp_i2s_channel_read_until_bytes(lp_rx_chan, &trans));
        bytes_total += trans.received_size;
        test_print_pcm_s16_head(rx_buf, 4);
    }

    lp_vad_test(lp_rx_chan);

    TEST_ESP_OK(codec_if->adc_if->ops.enable(codec_if, false));
    TEST_ESP_OK(lp_i2s_channel_disable(lp_rx_chan));
    TEST_ESP_OK(lp_i2s_del_channel(lp_rx_chan));
    audio_codec_delete_codec_if(codec_if);
    audio_codec_delete_gpio_if(gpio_if);
    audio_codec_delete_ctrl_if(ctrl_if);
    ut_ledc_deinit(TEST_BOARD_LP_I2S_MCK_PIN);
    ut_i2c_deinit(0);
}

// TEST_CASE("LP I2S RX ES8311 record and LP VAD test use P4_EV_BOARD 16000Hz", "[p4_ev][lp_i2s]")
// {
//     test_p4_ev_board_lp_i2s_rx_es8311_record_vad();
// }

#endif  /* SOC_LP_I2S_SUPPORTED && SOC_LP_VAD_SUPPORTED && defined(CONFIG_CODEC_ES8311_SUPPORT) */
