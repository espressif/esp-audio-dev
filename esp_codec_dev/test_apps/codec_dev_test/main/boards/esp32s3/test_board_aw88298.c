/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdint.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#include "unity.h"

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "test_board_periph.h"
#include "test_codec_print.h"

#if defined(CONFIG_CODEC_AW88298_SUPPORT) && SOC_I2S_SUPPORTS_TDM

static const char *TAG = "TEST_BOARD_AW88298";

/**
 * Custom board with AW88298 (and ES7210 on the same board).
 * Pins differ from Korvo2 V3; do not reuse TEST_BOARD_* from test_board.h.
 */
#define AW88298_TEST_I2C_SDA_PIN   (12)
#define AW88298_TEST_I2C_SCL_PIN   (11)
#define AW88298_TEST_I2S_MCLK_PIN  (0)
#define AW88298_TEST_I2S_WS_PIN    (33)
#define AW88298_TEST_I2S_BCK_PIN   (34)
#define AW88298_TEST_I2S_DOUT_PIN  (13)
#define AW88298_TEST_I2S_DIN_PIN   (14)

/* M5Stack CoreS3 power rail / IO expander (same I2C bus as AW88298) */
#define CORES3_AXP2101_ADDR         (0x34)
#define CORES3_AW9523B_ADDR         (0x58)  /* 8-bit write addr 0xB0 */
#define CORES3_AW9523B_REG_OUTPUT0  (0x02)
#define CORES3_AW9523B_REG_CONFIG0  (0x04)
#define CORES3_AW9523B_REG_GCR      (0x11)
#define CORES3_AW9523B_SPK_IO       (1 << 2)
#define CORES3_I2C_TIMEOUT_MS       (1000)
#define CORES3_POWER_SETTLE_MS      (100)
#define CORES3_SPK_IO_PULSE_MS      (50)

extern const uint8_t music_pcm_start[] asm("_binary_16k_mono_16bit_pcm_start");
extern const uint8_t music_pcm_end[] asm("_binary_16k_mono_16bit_pcm_end");

static void fill_aw88298_mono_tone(int16_t *data, int frame_count, int scale)
{
    static const int16_t sine_1k_16k[16] = {
        0, 6270, 11585, 15137, 16384, 15137, 11585, 6270,
        0, -6270, -11585, -15137, -16384, -15137, -11585, -6270,
    };
    for (int i = 0; i < frame_count; i++) {
        data[i] = (int16_t)((sine_1k_16k[i & 0x0F] * scale) / 100);
    }
}

/**
 * Enable CoreS3 audio supplies before AW88298/ES7210 init.
 * Mirrors m5stack_cores3 power_manager SPEAKER path + LDO enable.
 */
static esp_err_t cores3_enable_audio_power(i2c_master_bus_handle_t bus)
{
    i2c_master_dev_handle_t axp = NULL;
    i2c_master_dev_handle_t aw9523 = NULL;
    esp_err_t err;
    uint8_t data[2];
    uint8_t reg_val = 0;

    const i2c_device_config_t axp_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CORES3_AXP2101_ADDR,
        .scl_speed_hz = 400000,
    };
    err = i2c_master_bus_add_device(bus, &axp_cfg, &axp);
    if (err != ESP_OK) {
        return err;
    }

    /* AXP ALDO1 voltage / PA PVDD / 1V8 */
    data[0] = 0x92;
    data[1] = 0x0D;
    err = i2c_master_transmit(axp, data, sizeof(data), CORES3_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        goto cleanup_axp;
    }
    /* AXP ALDO2 voltage / Codec / 3V3 */
    data[0] = 0x93;
    data[1] = 0x1C;
    err = i2c_master_transmit(axp, data, sizeof(data), CORES3_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        goto cleanup_axp;
    }
    /* AXP ALDO3 voltage / Codec+Mic / 3V3 */
    data[0] = 0x94;
    data[1] = 0x1C;
    err = i2c_master_transmit(axp, data, sizeof(data), CORES3_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        goto cleanup_axp;
    }
    /* AXP LDO enable (same as CoreS3 power_manager_init) */
    data[0] = 0x90;
    data[1] = 0xBF;
    err = i2c_master_transmit(axp, data, sizeof(data), CORES3_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        goto cleanup_axp;
    }

    const i2c_device_config_t aw_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CORES3_AW9523B_ADDR,
        .scl_speed_hz = 400000,
    };
    err = i2c_master_bus_add_device(bus, &aw_cfg, &aw9523);
    if (err != ESP_OK) {
        goto cleanup_axp;
    }

    /* P0 push-pull (GCR bit4), matching CoreS3 setup_device */
    data[0] = CORES3_AW9523B_REG_GCR;
    data[1] = 0x10;
    err = i2c_master_transmit(aw9523, data, sizeof(data), CORES3_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        goto cleanup_aw;
    }

    /* CONFIG0: bit2 = 0 => output */
    data[0] = CORES3_AW9523B_REG_CONFIG0;
    err = i2c_master_transmit_receive(aw9523, &data[0], 1, &reg_val, 1, CORES3_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        goto cleanup_aw;
    }
    data[1] = (uint8_t)(reg_val & (uint8_t)~CORES3_AW9523B_SPK_IO);
    err = i2c_master_transmit(aw9523, data, sizeof(data), CORES3_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        goto cleanup_aw;
    }

    /* OUTPUT0: bit2 pulse low then high => speaker path enable */
    data[0] = CORES3_AW9523B_REG_OUTPUT0;
    err = i2c_master_transmit_receive(aw9523, &data[0], 1, &reg_val, 1, CORES3_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        goto cleanup_aw;
    }
    data[1] = (uint8_t)(reg_val & (uint8_t)~CORES3_AW9523B_SPK_IO);
    err = i2c_master_transmit(aw9523, data, sizeof(data), CORES3_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        goto cleanup_aw;
    }
    vTaskDelay(pdMS_TO_TICKS(CORES3_SPK_IO_PULSE_MS));
    data[1] = (uint8_t)(reg_val | CORES3_AW9523B_SPK_IO);
    err = i2c_master_transmit(aw9523, data, sizeof(data), CORES3_I2C_TIMEOUT_MS);

cleanup_aw:
    i2c_master_bus_rm_device(aw9523);
cleanup_axp:
    i2c_master_bus_rm_device(axp);
    return err;
}

static void test_aw88298_tdm_mono_sine_play(void)
{
    ut_set_i2s_mode(I2S_COMM_MODE_TDM, I2S_COMM_MODE_NONE);

    codec_i2c_pin_t i2c_pin = {
        .sda = AW88298_TEST_I2C_SDA_PIN,
        .scl = AW88298_TEST_I2C_SCL_PIN,
    };
    int ret = ut_i2c_init(0, &i2c_pin);
    TEST_ESP_OK(ret);
    ret = cores3_enable_audio_power(ut_i2c_get_bus_handle());
    TEST_ESP_OK(ret);
    vTaskDelay(pdMS_TO_TICKS(CORES3_POWER_SETTLE_MS));

    codec_i2s_pin_t i2s_pin = {
        .mclk = -1,
        .bclk = AW88298_TEST_I2S_BCK_PIN,
        .ws = AW88298_TEST_I2S_WS_PIN,
        .dout = AW88298_TEST_I2S_DOUT_PIN,
        .din = AW88298_TEST_I2S_DIN_PIN,
    };
    ret = ut_i2s_init(0, &i2s_pin, I2S_CLK_SRC_DEFAULT);
    TEST_ESP_OK(ret);

    audio_codec_i2s_cfg_t i2s_cfg = {
        .rx_handle = NULL,
        .tx_handle = ut_i2s_get_tx_handle(0),
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    TEST_ASSERT_NOT_NULL(data_if);

    audio_codec_i2c_cfg_t i2c_cfg = {
        .addr = AW88298_CODEC_DEFAULT_ADDR,
        .bus_handle = ut_i2c_get_bus_handle(),
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    TEST_ASSERT_NOT_NULL(ctrl_if);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    TEST_ASSERT_NOT_NULL(gpio_if);

    aw88298_codec_cfg_t aw88298_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .pa_cfg = {
            .pa_pin = -1,
        },
        .reset_cfg = {
            .reset_pin = -1,
        },
    };
    const audio_codec_if_t *codec_if = aw88298_codec_new(&aw88298_cfg);
    TEST_ASSERT_NOT_NULL(codec_if);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = codec_if,
        .data_if = data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    esp_codec_dev_handle_t play_dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(play_dev);

    ret = esp_codec_dev_set_out_vol(play_dev, 50);
    TEST_ESP_OK(ret);

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 1,
        .bits_per_sample = 16,
        .channel_mask = 0x01,
        .mclk_multiple = 256,
    };
    ret = esp_codec_dev_open(play_dev, &fs);
    TEST_ESP_OK(ret);

    const int chunk_frames = 1024;
    const int chunk_bytes = chunk_frames * 1 * (fs.bits_per_sample >> 3);
    int16_t *play_buf = (int16_t *)malloc(chunk_bytes);
    TEST_ASSERT_NOT_NULL(play_buf);
    fill_aw88298_mono_tone(play_buf, chunk_frames, 80);

    const int total_frames = fs.sample_rate * 3;
    int frame_offset = 0;
    ESP_LOGI(TAG, "AW88298 TDM mono sine play start: 1kHz @ %d Hz, ch=%d",
             (int)fs.sample_rate, fs.channel);
    while (frame_offset < total_frames) {
        ret = esp_codec_dev_write(play_dev, play_buf, chunk_bytes);
        TEST_ESP_OK(ret);
        frame_offset += chunk_frames;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_codec_dev_close(play_dev);

    // play music
    vTaskDelay(pdMS_TO_TICKS(2000));
    fs.channel = 2;
    fs.channel_mask = 0x01;
    ret = esp_codec_dev_open(play_dev, &fs);
    TEST_ESP_OK(ret);
    esp_codec_dev_write(play_dev, (uint8_t *)music_pcm_start, (int)(music_pcm_end - music_pcm_start));

    free(play_buf);
    ret = esp_codec_dev_close(play_dev);
    TEST_ESP_OK(ret);
    esp_codec_dev_delete(play_dev);
    audio_codec_delete_codec_if(codec_if);
    audio_codec_delete_ctrl_if(ctrl_if);
    audio_codec_delete_gpio_if(gpio_if);
    audio_codec_delete_data_if(data_if);
    ut_i2c_deinit(0);
    ut_i2s_deinit(0);
    ut_clr_i2s_mode();
}

TEST_CASE("AW88298 TDM mono sine play", "[aw88298][play]")
{
    test_aw88298_tdm_mono_sine_play();
}

#if defined(CONFIG_CODEC_ES7210_SUPPORT)

static void test_aw88298_play_es7210_record(void)
{
    ut_set_i2s_mode(I2S_COMM_MODE_TDM, I2S_COMM_MODE_TDM);

    codec_i2c_pin_t i2c_pin = {
        .sda = AW88298_TEST_I2C_SDA_PIN,
        .scl = AW88298_TEST_I2C_SCL_PIN,
    };
    int ret = ut_i2c_init(0, &i2c_pin);
    TEST_ESP_OK(ret);
    ret = cores3_enable_audio_power(ut_i2c_get_bus_handle());
    TEST_ESP_OK(ret);
    vTaskDelay(pdMS_TO_TICKS(CORES3_POWER_SETTLE_MS));

    codec_i2s_pin_t i2s_pin = {
        .mclk = AW88298_TEST_I2S_MCLK_PIN,
        .bclk = AW88298_TEST_I2S_BCK_PIN,
        .ws = AW88298_TEST_I2S_WS_PIN,
        .dout = AW88298_TEST_I2S_DOUT_PIN,
        .din = AW88298_TEST_I2S_DIN_PIN,
    };
    ret = ut_i2s_init(0, &i2s_pin, I2S_CLK_SRC_DEFAULT);
    TEST_ESP_OK(ret);

    audio_codec_i2s_cfg_t i2s_cfg = {
        .rx_handle = ut_i2s_get_rx_handle(0),
        .tx_handle = ut_i2s_get_tx_handle(0),
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    TEST_ASSERT_NOT_NULL(data_if);

    audio_codec_i2c_cfg_t i2c_cfg = {
        .addr = AW88298_CODEC_DEFAULT_ADDR,
        .bus_handle = ut_i2c_get_bus_handle(),
    };
    const audio_codec_ctrl_if_t *out_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    TEST_ASSERT_NOT_NULL(out_ctrl_if);

    i2c_cfg.addr = ES7210_CODEC_DEFAULT_ADDR;
    const audio_codec_ctrl_if_t *in_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    TEST_ASSERT_NOT_NULL(in_ctrl_if);

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    TEST_ASSERT_NOT_NULL(gpio_if);

    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = in_ctrl_if,
        .adc_cfg = {
            .label = "FL,FR,RE,NA",
        },
    };
    const audio_codec_if_t *in_codec_if = es7210_codec_new(&es7210_cfg);
    TEST_ASSERT_NOT_NULL(in_codec_if);

    aw88298_codec_cfg_t aw88298_cfg = {
        .ctrl_if = out_ctrl_if,
        .gpio_if = gpio_if,
        .pa_cfg = {
            .pa_pin = -1,
        },
        .reset_cfg = {
            .reset_pin = -1,
        },
    };
    const audio_codec_if_t *out_codec_if = aw88298_codec_new(&aw88298_cfg);
    TEST_ASSERT_NOT_NULL(out_codec_if);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = out_codec_if,
        .data_if = data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    esp_codec_dev_handle_t play_dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(play_dev);

    dev_cfg.codec_if = in_codec_if;
    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
    esp_codec_dev_handle_t record_dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(record_dev);

    ret = esp_codec_dev_set_out_vol(play_dev, 20);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_set_in_gain(record_dev, 10.0f);
    TEST_ESP_OK(ret);

    esp_codec_dev_sample_info_t play_fs = {
        .sample_rate = 16000,
        .channel = 1,
        .bits_per_sample = 16,
        .channel_mask = 0x01,
        .mclk_multiple = 256,
    };
    esp_codec_dev_sample_info_t record_fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = 0x02,
        .mclk_multiple = 256,
    };

    ret = esp_codec_dev_open(record_dev, &record_fs);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_open(play_dev, &play_fs);
    TEST_ESP_OK(ret);
    vTaskDelay(pdMS_TO_TICKS(200));

    uint8_t *data = (uint8_t *)malloc(512);
    TEST_ASSERT_NOT_NULL(data);
    int limit_size = 10 * record_fs.sample_rate * record_fs.channel * (record_fs.bits_per_sample >> 3);
    int got_size = 0;
    ESP_LOGI(TAG, "ES7210 record then AW88298 play: record ch=%d mask=0x%x, play ch=%d mask=0x%x",
             record_fs.channel, record_fs.channel_mask, play_fs.channel, play_fs.channel_mask);
    /* Playback the recording content directly */
    while (got_size < limit_size) {
        ret = esp_codec_dev_read(record_dev, data, 512);
        TEST_ESP_OK(ret);
        ret = esp_codec_dev_write(play_dev, data, 512);
        TEST_ESP_OK(ret);
        int max_sample, min_sample;
        codec_max_sample(data, 512, &max_sample, &min_sample);
        /* Verify recording data not constant */
        TEST_ASSERT(max_sample > min_sample);
        got_size += 512;
    }

    free(data);
    ret = esp_codec_dev_close(play_dev);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_close(record_dev);
    TEST_ESP_OK(ret);
    esp_codec_dev_delete(play_dev);
    esp_codec_dev_delete(record_dev);
    audio_codec_delete_codec_if(out_codec_if);
    audio_codec_delete_codec_if(in_codec_if);
    audio_codec_delete_ctrl_if(out_ctrl_if);
    audio_codec_delete_ctrl_if(in_ctrl_if);
    audio_codec_delete_gpio_if(gpio_if);
    audio_codec_delete_data_if(data_if);
    ut_i2c_deinit(0);
    ut_i2s_deinit(0);
    ut_clr_i2s_mode();
}

TEST_CASE("AW88298 play recorded ES7210 audio", "[aw88298][es7210][play][record]")
{
    test_aw88298_play_es7210_record();
}

#endif  /* defined(CONFIG_CODEC_ES7210_SUPPORT) */

#endif  /* defined(CONFIG_CODEC_AW88298_SUPPORT) && SOC_I2S_SUPPORTS_TDM */
