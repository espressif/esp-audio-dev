/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdint.h>
#include <stdlib.h>

#include "sdkconfig.h"
#include "soc/soc_caps.h"
#include "unity.h"

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_os.h"
#include "test_board_periph.h"
#include "test_codec_print.h"

#if defined(CONFIG_CODEC_ES8311_SUPPORT) && defined(CONFIG_CODEC_ES7210_SUPPORT) && SOC_I2S_SUPPORTS_TDM

/**
 * Chatbot board (ESP32-S31) with ES8311 + ES7210.
 * Pins from schematic; do not reuse TEST_BOARD_* from test_board.h.
 */
#if CONFIG_IDF_TARGET_ESP32S3
#define CHATBOT_I2C_SDA_PIN   (17)
#define CHATBOT_I2C_SCL_PIN   (18)
#define CHATBOT_I2S_BCK_PIN   (9)
#define CHATBOT_I2S_MCLK_PIN  (16)
#define CHATBOT_I2S_DIN_PIN   (10)
#define CHATBOT_I2S_DOUT_PIN  (8)
#define CHATBOT_I2S_WS_PIN    (45)
#define CHATBOT_PA_PIN        (48)

#define CHATBOT_I2S_CLK_SRC_TX_DEFAULT  (I2S_CLK_SRC_DEFAULT)
#define CHATBOT_I2S_CLK_SRC_RX_DEFAULT  (I2S_CLK_SRC_DEFAULT)
#else
#define CHATBOT_I2C_SDA_PIN   (0)
#define CHATBOT_I2C_SCL_PIN   (1)
#define CHATBOT_I2S_MCLK_PIN  (14)
#define CHATBOT_I2S_BCK_PIN   (13)
#define CHATBOT_I2S_WS_PIN    (16)
#define CHATBOT_I2S_DOUT_PIN  (15)  /* I2S_SDOUT: ESP -> codec */
#define CHATBOT_I2S_DIN_PIN   (35)  /* I2S_SDIN:  codec -> ESP */
#define CHATBOT_PA_PIN        (12)

#define CHATBOT_I2S_CLK_SRC_TX_DEFAULT  (I2S_CLK_SRC_APLL)
#define CHATBOT_I2S_CLK_SRC_RX_DEFAULT  (I2S_CLK_SRC_DEFAULT)
#endif  /* CONFIG_IDF_TARGET_ESP32S3 */

extern const uint8_t music_pcm_start[] asm("_binary_16k_mono_16bit_pcm_start");
extern const uint8_t music_pcm_end[] asm("_binary_16k_mono_16bit_pcm_end");

/* Temporary: S31 needs a slower I2C clock for reliable codec access. */
#define CHATBOT_I2C_SCL_SPEED_HZ  (400000)

static const codec_i2c_pin_t s_chatbot_i2c_pin = {
    .scl = CHATBOT_I2C_SCL_PIN,
    .sda = CHATBOT_I2C_SDA_PIN,
};

static const codec_i2s_pin_t s_chatbot_i2s_pin = {
    .mclk = CHATBOT_I2S_MCLK_PIN,
    .bclk = CHATBOT_I2S_BCK_PIN,
    .ws   = CHATBOT_I2S_WS_PIN,
    .dout = CHATBOT_I2S_DOUT_PIN,
    .din  = CHATBOT_I2S_DIN_PIN,
};

/**
 * Duplex TDM: play 4 slots / mask BIT(0)|BIT(2), record 6 slots / mask BIT(0)|BIT(1).
 * Only verifies that capture data is not constant via codec_max_sample.
 */
static void test_case_chatbot_play_4ch_record_6ch_tdm(void)
{
    ut_set_i2s_mode(I2S_COMM_MODE_TDM, I2S_COMM_MODE_TDM);
    int ret = ut_i2c_init(0, (codec_i2c_pin_t *)&s_chatbot_i2c_pin);
    TEST_ESP_OK(ret);
    ret = ut_i2s_init_channel(0);
    TEST_ESP_OK(ret);

    /* TX and RX are brought up separately so each direction keeps its own clock source. */
    ret = ut_i2s_init_tx_phase(0, (codec_i2s_pin_t *)&s_chatbot_i2s_pin, CHATBOT_I2S_CLK_SRC_TX_DEFAULT);
    TEST_ESP_OK(ret);
    audio_codec_i2s_cfg_t out_i2s_cfg = {.port = 0};
    out_i2s_cfg.tx_handle = ut_i2s_get_tx_handle(0);
    out_i2s_cfg.rx_handle = NULL;
    out_i2s_cfg.clk_src = CHATBOT_I2S_CLK_SRC_TX_DEFAULT;
    const audio_codec_data_if_t *out_data_if = audio_codec_new_i2s_data(&out_i2s_cfg);
    TEST_ASSERT_NOT_NULL(out_data_if);

    ret = ut_i2s_init_rx_phase(0, (codec_i2s_pin_t *)&s_chatbot_i2s_pin, CHATBOT_I2S_CLK_SRC_RX_DEFAULT);
    TEST_ESP_OK(ret);
    audio_codec_i2s_cfg_t in_i2s_cfg = {.port = 0};
    in_i2s_cfg.tx_handle = NULL;
    in_i2s_cfg.rx_handle = ut_i2s_get_rx_handle(0);
    in_i2s_cfg.clk_src = CHATBOT_I2S_CLK_SRC_RX_DEFAULT;
    const audio_codec_data_if_t *in_data_if = audio_codec_new_i2s_data(&in_i2s_cfg);
    TEST_ASSERT_NOT_NULL(in_data_if);

    audio_codec_i2c_cfg_t i2c_cfg = {
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = ut_i2c_get_bus_handle(),
        .clock_speed_hz = CHATBOT_I2C_SCL_SPEED_HZ,
    };
    const audio_codec_ctrl_if_t *out_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    TEST_ASSERT_NOT_NULL(out_ctrl_if);

    i2c_cfg.addr = ES7210_CODEC_DEFAULT_ADDR;
    const audio_codec_ctrl_if_t *in_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    TEST_ASSERT_NOT_NULL(in_ctrl_if);

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    TEST_ASSERT_NOT_NULL(gpio_if);

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = out_ctrl_if,
        .gpio_if = gpio_if,
        .sys_cfg = {
            .is_master = false,
            .no_mclk = false,
        },
        .dac_cfg = {
            .ref_enable = true,
        },
        .pa_cfg = {
            .pa_pin = CHATBOT_PA_PIN,
            .pa_active_low = false,
        },
    };
    const audio_codec_if_t *out_codec_if = es8311_codec_new(&es8311_cfg);
    TEST_ASSERT_NOT_NULL(out_codec_if);

    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = in_ctrl_if,
        .sys_cfg = {
            .is_master = false,
            .no_mclk = false,
        },
    };
    const audio_codec_if_t *in_codec_if = es7210_codec_new(&es7210_cfg);
    TEST_ASSERT_NOT_NULL(in_codec_if);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = out_codec_if,
        .data_if = out_data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    esp_codec_dev_handle_t play_dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(play_dev);
    dev_cfg.codec_if = in_codec_if;
    dev_cfg.data_if = in_data_if;
    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
    esp_codec_dev_handle_t record_dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(record_dev);

    ret = esp_codec_dev_set_out_vol(play_dev, 80);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_set_in_gain(record_dev, TEST_CODEC_BOARD_IN_GAIN);
    TEST_ESP_OK(ret);

    esp_codec_dev_sample_info_t play_fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = BIT(0),
        .mclk_multiple = 384,
    };
    ret = esp_codec_dev_open(play_dev, &play_fs);
    TEST_ESP_OK(ret);
    printf("Play music start\n");
    ret = esp_codec_dev_write(play_dev, (uint8_t *)music_pcm_start, music_pcm_end - music_pcm_start);
    esp_codec_dev_sleep(200);
    esp_codec_dev_close(play_dev);
    // ch1 NA NA NA
    play_fs.channel = 4;
    play_fs.channel_mask = BIT(0) | BIT(2);
    ret = esp_codec_dev_open(play_dev, &play_fs);
    TEST_ESP_OK(ret);
#if !CONFIG_IDF_TARGET_ESP32S3
    // If want to get mic from es8311, also need to enable the mic.
    out_codec_if->adc_if->ops.enable(out_codec_if, true);
#endif  /* CONFIG_IDF_TARGET_ESP32S3 */

    /* Record opens first so the duplex frame is sized to 6 * 16 bits. */
    esp_codec_dev_sample_info_t record_fs = {
        .sample_rate = 16000,
        .channel = 6,
        .bits_per_sample = 16,
#if CONFIG_IDF_TARGET_ESP32S3
        .channel_mask = BIT(0) | BIT(1),
#else
        .channel_mask = BIT(0) | BIT(2),
#endif  /* CONFIG_IDF_TARGET_ESP32S3 */
        .mclk_multiple = 384,
    };
    // ch1 ch3 ch5 ch2 ch4 NA
    ret = esp_codec_dev_open(record_dev, &record_fs);
    TEST_ESP_OK(ret);

    /* Active DMA channels follow the mask popcount, not total_slot. */
    const int chunk_frames = 240 * 3;
    const int play_active_ch = 2;
    const int record_active_ch = 2;
    const int bytes_per_sample = play_fs.bits_per_sample >> 3;
    int play_bytes = chunk_frames * play_active_ch * bytes_per_sample;
    int record_bytes = chunk_frames * record_active_ch * bytes_per_sample;
    uint8_t *data = (uint8_t *)malloc(record_bytes);
    TEST_ASSERT_NOT_NULL(data);
    int limit_size = 5 * record_fs.sample_rate * record_active_ch * bytes_per_sample;
    int got_size = 0;

    esp_codec_dev_sleep(200);
    while (got_size < limit_size) {
        ret = esp_codec_dev_read(record_dev, data, record_bytes);
        test_print_pcm_s16_head(data, 4);
        TEST_ESP_OK(ret);
        ret = esp_codec_dev_write(play_dev, data, play_bytes);
        TEST_ESP_OK(ret);
        int max_sample, min_sample;
        codec_max_sample(data, record_bytes, &max_sample, &min_sample);
        TEST_ASSERT(max_sample > min_sample);
        test_print_pcm_s16_head(data, 4);
        got_size += record_bytes;
    }
    free(data);

    ret = esp_codec_dev_close(play_dev);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_close(record_dev);
    TEST_ESP_OK(ret);
    esp_codec_dev_delete(play_dev);
    esp_codec_dev_delete(record_dev);

    audio_codec_delete_codec_if(in_codec_if);
    audio_codec_delete_codec_if(out_codec_if);
    audio_codec_delete_ctrl_if(in_ctrl_if);
    audio_codec_delete_ctrl_if(out_ctrl_if);
    audio_codec_delete_gpio_if(gpio_if);
    audio_codec_delete_data_if(in_data_if);
    audio_codec_delete_data_if(out_data_if);

    ut_i2c_deinit(0);
    ut_i2s_deinit(0);
    ut_clr_i2s_mode();
}

TEST_CASE("Chatbot play es8311 4ch and record es7210 6ch TDM", "[chatbot][duplex]")
{
    test_case_chatbot_play_4ch_record_6ch_tdm();
}

#endif  /* defined(CONFIG_CODEC_ES8311_SUPPORT) && defined(CONFIG_CODEC_ES7210_SUPPORT) && SOC_I2S_SUPPORTS_TDM */
