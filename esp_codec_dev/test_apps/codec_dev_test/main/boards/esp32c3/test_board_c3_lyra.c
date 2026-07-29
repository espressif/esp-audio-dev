/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdint.h>
#include <stdlib.h>

#include "unity.h"
#include "esp_log.h"

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "dummy_codec.h"
#include "test_board_periph.h"
#include "test_codec_print.h"

#if CONFIG_CODEC_DATA_ADC_SUPPORT && SOC_ADC_SUPPORTED && SOC_I2S_SUPPORTS_PDM_TX

static const char *TAG = "TEST_C3_LYRA";

static void test_codec_dev_using_adc_mic(void)
{
    const char *fail_msg = NULL;
    const audio_codec_data_if_t *adc_data_if = NULL;
    const audio_codec_data_if_t *pdm_data_if = NULL;
    const audio_codec_gpio_if_t *gpio_if = NULL;
    const audio_codec_if_t *dummy_codec_if = NULL;
    esp_codec_dev_handle_t play_dev = NULL;
    esp_codec_dev_handle_t record_dev = NULL;
    uint8_t *data = NULL;
    bool record_opened = false;
    bool play_opened = false;

    audio_codec_adc_cfg_t adc_cfg = {
        .handle = NULL,
        .continuous_cfg = {
            .max_store_buf_size = 1024,
            .conv_frame_size = 256,
            .sample_freq_hz = 16000,
            .conv_mode = ADC_CONV_SINGLE_UNIT_1,
            .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
            .pattern_num = 1,
            .cfg_mode = AUDIO_CODEC_ADC_CFG_MODE_SINGLE_UNIT,
            .cfg.single_unit = {
                .unit_id = ADC_UNIT_1,
                .atten = ADC_ATTEN_DB_12,
                .bit_width = ADC_BITWIDTH_12,
                .channel_id = {0},
            },
        },
    };

    esp_codec_dev_cfg_t rec_dev_cfg = {
        .codec_if = NULL,
        .data_if = NULL,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 1,
        .bits_per_sample = 16,
    };

    adc_data_if = audio_codec_new_adc_data(&adc_cfg);
    if (adc_data_if == NULL) {
        fail_msg = "failed to create ADC data_if";
        goto cleanup;
    }

    rec_dev_cfg.data_if = adc_data_if;
    record_dev = esp_codec_dev_new(&rec_dev_cfg);
    if (record_dev == NULL) {
        fail_msg = "failed to create esp_codec_dev for ADC MIC";
        goto cleanup;
    }

    if (ut_i2s_init_pdm_out(0) != 0) {
        fail_msg = "failed to init I2S PDM output";
        goto cleanup;
    }

    audio_codec_i2s_cfg_t i2s_cfg = {
        .tx_handle = ut_i2s_get_tx_handle(0),
    };
    pdm_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (pdm_data_if == NULL) {
        fail_msg = "failed to create PDM output data_if";
        goto cleanup;
    }

    gpio_if = audio_codec_new_gpio();
    if (gpio_if == NULL) {
        fail_msg = "failed to create gpio_if";
        goto cleanup;
    }

    dummy_codec_cfg_t dummy_cfg = {
        .gpio_if = gpio_if,
        .pa_cfg = {
            .pa_pin = 1,
            .pa_active_low = false,
        },
    };
    dummy_codec_if = dummy_codec_new(&dummy_cfg);
    if (dummy_codec_if == NULL) {
        fail_msg = "failed to create dummy codec";
        goto cleanup;
    }

    esp_codec_dev_cfg_t play_dev_cfg = {
        .codec_if = dummy_codec_if,
        .data_if = pdm_data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };

    play_dev = esp_codec_dev_new(&play_dev_cfg);
    if (play_dev == NULL) {
        fail_msg = "failed to create esp_codec_dev for PDM speaker";
        goto cleanup;
    }

    int ret = -1;
    data = (uint8_t *)malloc(512);
    if (data == NULL) {
        fail_msg = "failed to allocate ADC read buffer";
        goto cleanup;
    }

    ret = esp_codec_dev_open(play_dev, &fs);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Open C3 Lyra PDM speaker failed: %d", ret);
        fail_msg = "failed to open PDM speaker codec device";
        goto cleanup;
    }
    play_opened = true;

    ret = esp_codec_dev_set_out_vol(play_dev, TEST_CODEC_BOARD_OUT_VOL);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Set C3 Lyra PDM speaker volume failed after open: %d", ret);
        fail_msg = "failed to set PDM speaker volume";
        goto cleanup;
    }

    ret = esp_codec_dev_open(record_dev, &fs);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Open C3 Lyra ADC MIC failed: %d", ret);
        fail_msg = "failed to open ADC MIC codec device";
        goto cleanup;
    }
    record_opened = true;

    for (int i = 0; i < 300; i++) {
        ret = esp_codec_dev_read(record_dev, data, 512);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Read C3 Lyra ADC MIC failed at round %d: %d", i, ret);
            fail_msg = "ADC MIC continuous read failed";
            goto cleanup;
        }
        int max_sample = 0;
        int min_sample = 0;
        codec_max_sample(data, 512, &max_sample, &min_sample);
        if (!(max_sample > min_sample)) {
            ESP_LOGE(TAG, "ADC MIC data looks constant at round %d, max:%d min:%d", i, max_sample, min_sample);
            fail_msg = "ADC MIC data is constant";
            goto cleanup;
        }
        ret = esp_codec_dev_write(play_dev, data, 512);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Write C3 Lyra PDM speaker failed at round %d: %d", i, ret);
            fail_msg = "PDM speaker playback failed";
            goto cleanup;
        }
    }

cleanup:
    if (record_opened) {
        int close_ret = esp_codec_dev_close(record_dev);
        if (close_ret != ESP_CODEC_DEV_OK && fail_msg == NULL) {
            ESP_LOGE(TAG, "Close C3 Lyra ADC MIC failed: %d", close_ret);
            fail_msg = "failed to close ADC MIC codec device";
        }
    }
    if (play_opened) {
        int close_ret = esp_codec_dev_close(play_dev);
        if (close_ret != ESP_CODEC_DEV_OK && fail_msg == NULL) {
            ESP_LOGE(TAG, "Close C3 Lyra PDM speaker failed: %d", close_ret);
            fail_msg = "failed to close PDM speaker codec device";
        }
    }
    if (record_dev) {
        esp_codec_dev_delete(record_dev);
    }
    if (play_dev) {
        esp_codec_dev_delete(play_dev);
    }
    if (adc_data_if) {
        audio_codec_delete_data_if(adc_data_if);
    }
    if (pdm_data_if) {
        audio_codec_delete_data_if(pdm_data_if);
    }
    if (dummy_codec_if) {
        audio_codec_delete_codec_if(dummy_codec_if);
    }
    if (gpio_if) {
        audio_codec_delete_gpio_if(gpio_if);
    }
    if (data) {
        free(data);
    }
    TEST_ESP_OK(ut_i2s_deinit(0));
    TEST_ASSERT_MESSAGE(fail_msg == NULL, fail_msg ? fail_msg : "unexpected failure");
}

TEST_CASE("esp codec dev ADC MIC record and PDM speaker play test (ESP32-C3-Lyra)", "[c3_lyra][duplex]")
{
    test_codec_dev_using_adc_mic();
}

#endif  /* CONFIG_CODEC_DATA_ADC_SUPPORT && SOC_ADC_SUPPORTED && SOC_I2S_SUPPORTS_PDM_TX */
