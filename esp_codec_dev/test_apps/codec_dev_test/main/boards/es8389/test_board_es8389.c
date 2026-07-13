/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_idf_version.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "soc/soc_caps.h"
#include "esp_timer.h"
#include "unity.h"

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_os.h"
#include "test_board.h"
#include "test_board_periph.h"
#include "test_codec_print.h"

extern const uint8_t music_pcm_start[] asm("_binary_16k_mono_16bit_pcm_start");
extern const uint8_t music_pcm_end[] asm("_binary_16k_mono_16bit_pcm_end");

static const char *TAG = "TEST_BOARD_ES8389";

typedef struct {
    const audio_codec_data_if_t *data_if;
    const audio_codec_ctrl_if_t *ctrl_if;
    const audio_codec_gpio_if_t *gpio_if;
    const audio_codec_if_t      *codec_if;
    esp_codec_dev_handle_t       codec_dev;
} codec_inst_t;

typedef struct {
    int64_t  abs_sum;
    int64_t  corr[16];
    int      sample_count;
    int      peak_abs;
} es8389_loopback_channel_stat_t;

#if CONFIG_CODEC_ES8389_SUPPORT
/**
 * ES8389 on a custom board: full-duplex loop (read ADC -> write DAC).
 * I2S MCLK not routed to GPIO (-1); codec uses BCLK-derived clock (no_mclk).
 */
#if CONFIG_IDF_TARGET_ESP32S3
#define ES8389_TEST_BOARD_I2S_MCK_PIN       (-1)
#define ES8389_TEST_BOARD_I2S_BCK_PIN       (17)
#define ES8389_TEST_BOARD_I2S_DATA_WS_PIN   (15)
#define ES8389_TEST_BOARD_I2S_DATA_OUT_PIN  (16)
#define ES8389_TEST_BOARD_I2S_DATA_IN_PIN   (7)
#define ES8389_TEST_BOARD_PA                (6)
#define ES8389_TEST_BOARD_I2C_SCL_PIN       (8)
#define ES8389_TEST_BOARD_I2C_SDA_PIN       (18)
#define APP_I2S_CLK_SRC_TX_DEFAULT          (I2S_CLK_SRC_XTAL)
#define APP_I2S_CLK_SRC_RX_DEFAULT          (I2S_CLK_SRC_XTAL)
#else
#define ES8389_TEST_BOARD_I2S_MCK_PIN       (-1)
#define ES8389_TEST_BOARD_I2S_BCK_PIN       (3)
#define ES8389_TEST_BOARD_I2S_DATA_WS_PIN   (4)
#define ES8389_TEST_BOARD_I2S_DATA_OUT_PIN  (5)
#define ES8389_TEST_BOARD_I2S_DATA_IN_PIN   (6)
#define ES8389_TEST_BOARD_PA                (7)
#define ES8389_TEST_BOARD_I2C_SCL_PIN       (1)
#define ES8389_TEST_BOARD_I2C_SDA_PIN       (0)
#define APP_I2S_CLK_SRC_TX_DEFAULT          (I2S_CLK_SRC_APLL)
#define APP_I2S_CLK_SRC_RX_DEFAULT          (I2S_CLK_SRC_XTAL)
#endif  /* CONFIG_IDF_TARGET_ESP32S3 */

#if CONFIG_IDF_TARGET_ESP32S31
/* Temporary: S31-Korvo needs a slower I2C clock for reliable codec access. */
#define ES8389_TEST_BOARD_I2C_SCL_SPEED_HZ  (10000)
#else
#define ES8389_TEST_BOARD_I2C_SCL_SPEED_HZ  (0)
#endif  /* CONFIG_IDF_TARGET_ESP32S31 */

static const codec_i2c_pin_t s_es8389_i2c_pin = {
    .scl = ES8389_TEST_BOARD_I2C_SCL_PIN,
    .sda = ES8389_TEST_BOARD_I2C_SDA_PIN,
};

static const codec_i2s_pin_t s_es8389_i2s_pin = {
    .mclk = ES8389_TEST_BOARD_I2S_MCK_PIN,
    .bclk = ES8389_TEST_BOARD_I2S_BCK_PIN,
    .ws   = ES8389_TEST_BOARD_I2S_DATA_WS_PIN,
    .dout = ES8389_TEST_BOARD_I2S_DATA_OUT_PIN,
    .din  = ES8389_TEST_BOARD_I2S_DATA_IN_PIN,
};

static void fill_es8389_ref_tone(int16_t *data, int frame_count)
{
    static const int16_t sine_1k_16k[16] = {
        0, 6270, 11585, 15137, 16384, 15137, 11585, 6270,
        0, -6270, -11585, -15137, -16384, -15137, -11585, -6270,
    };
    for (int i = 0; i < frame_count; i++) {
        data[i] = (int16_t)((sine_1k_16k[i & 0x0F] * 500) / 16384);
    }
}

static void analyze_es8389_ref_capture(const int16_t *data, int frame_count, int frame_offset,
                                       es8389_loopback_channel_stat_t stat[3])
{
    static const int16_t sine_1k_16k[16] = {
        0, 6270, 11585, 15137, 16384, 15137, 11585, 6270,
        0, -6270, -11585, -15137, -16384, -15137, -11585, -6270,
    };
    for (int i = 0; i < frame_count; i++) {
        for (int ch = 0; ch < 3; ch++) {
            int sample = data[i * 3 + ch];
            int abs_sample = sample >= 0 ? sample : -sample;
            stat[ch].abs_sum += abs_sample;
            if (abs_sample > stat[ch].peak_abs) {
                stat[ch].peak_abs = abs_sample;
            }
            for (int phase = 0; phase < 16; phase++) {
                stat[ch].corr[phase] += (int64_t)sample * sine_1k_16k[(frame_offset + i + phase) & 0x0F];
            }
            stat[ch].sample_count++;
        }
    }
}

static void deinit_codec_inst(codec_inst_t *inst, bool has_gpio)
{
    if (inst == NULL) {
        return;
    }
    if (inst->codec_dev) {
        esp_codec_dev_delete(inst->codec_dev);
    }
    if (inst->codec_if) {
        audio_codec_delete_codec_if(inst->codec_if);
    }
    if (inst->data_if) {
        audio_codec_delete_data_if(inst->data_if);
    }
    if (inst->ctrl_if) {
        audio_codec_delete_ctrl_if(inst->ctrl_if);
    }
    if (has_gpio && inst->gpio_if) {
        audio_codec_delete_gpio_if(inst->gpio_if);
    }
    memset(inst, 0, sizeof(codec_inst_t));
}

static int init_es8389_play_codec_dev(codec_inst_t *inst)
{
    int ret = ut_i2s_init_tx_phase(0, (codec_i2s_pin_t *)&s_es8389_i2s_pin, APP_I2S_CLK_SRC_TX_DEFAULT);
    TEST_ESP_OK(ret);

    audio_codec_i2s_cfg_t i2s_cfg = {.port = 0};
    i2s_cfg.rx_handle = NULL;
    i2s_cfg.tx_handle = ut_i2s_get_tx_handle(0);
    i2s_cfg.clk_src = APP_I2S_CLK_SRC_TX_DEFAULT;
    const audio_codec_data_if_t *out_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    TEST_ASSERT_NOT_NULL(out_data_if);

    audio_codec_i2c_cfg_t i2c_cfg = {
        .addr = ES8389_CODEC_DEFAULT_ADDR,
        .bus_handle = ut_i2c_get_bus_handle(),
        .clock_speed_hz = ES8389_TEST_BOARD_I2C_SCL_SPEED_HZ,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    TEST_ASSERT_NOT_NULL(ctrl_if);

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    TEST_ASSERT_NOT_NULL(gpio_if);

    es8389_codec_cfg_t es8389_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .sys_cfg = {
            .is_master = false,
            .no_mclk = true,
        },
        .adc_cfg = {
            .digital_mic = false,
            .label = "FL,FR,RE",
        },
        .dac_cfg = {
            .ref_enable = false,
        },
        .pa_cfg = {
            .pa_pin = ES8389_TEST_BOARD_PA,
            .pa_active_low = false,
            .hw_gain = {
                .pa_voltage = 5.0f,
                .codec_dac_voltage = 3.3f,
                .pa_gain = 0.0f,
            },
        },
    };
    const audio_codec_if_t *codec_if = es8389_codec_new(&es8389_cfg);
    TEST_ASSERT_NOT_NULL(codec_if);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = codec_if,
        .data_if = out_data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    esp_codec_dev_handle_t play_dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(play_dev);
    ret = esp_codec_dev_set_out_vol(play_dev, TEST_CODEC_BOARD_OUT_VOL);
    TEST_ESP_OK(ret);

    inst->data_if = out_data_if;
    inst->ctrl_if = ctrl_if;
    inst->gpio_if = gpio_if;
    inst->codec_if = codec_if;
    inst->codec_dev = play_dev;
    return ESP_CODEC_DEV_OK;
}

static int init_es8389_record_codec_dev(codec_inst_t *inst)
{
    int ret = ut_i2s_init_rx_phase(0, (codec_i2s_pin_t *)&s_es8389_i2s_pin, APP_I2S_CLK_SRC_RX_DEFAULT);
    TEST_ESP_OK(ret);

    audio_codec_i2s_cfg_t i2s_cfg = {.port = 0};
    i2s_cfg.tx_handle = NULL;
    i2s_cfg.rx_handle = ut_i2s_get_rx_handle(0);
    i2s_cfg.clk_src = APP_I2S_CLK_SRC_RX_DEFAULT;
    const audio_codec_data_if_t *in_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    TEST_ASSERT_NOT_NULL(in_data_if);

    audio_codec_i2c_cfg_t i2c_cfg = {
        .addr = ES8389_CODEC_DEFAULT_ADDR,
        .bus_handle = ut_i2c_get_bus_handle(),
        .clock_speed_hz = ES8389_TEST_BOARD_I2C_SCL_SPEED_HZ,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    TEST_ASSERT_NOT_NULL(ctrl_if);

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    TEST_ASSERT_NOT_NULL(gpio_if);

    es8389_codec_cfg_t es8389_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .sys_cfg = {
            .is_master = false,
            .no_mclk = true,
        },
        .adc_cfg = {
            .digital_mic = false,
            .label = "FL,FR,RE",
        },
        .dac_cfg = {
            .ref_enable = false,
        },
        .pa_cfg = {
            .pa_pin = -1,
        },
    };
    const audio_codec_if_t *codec_if = es8389_codec_new(&es8389_cfg);
    TEST_ASSERT_NOT_NULL(codec_if);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = codec_if,
        .data_if = in_data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    esp_codec_dev_handle_t record_dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(record_dev);
    ret = esp_codec_dev_set_in_gain(record_dev, TEST_CODEC_BOARD_IN_GAIN);
    TEST_ESP_OK(ret);

    inst->data_if = in_data_if;
    inst->ctrl_if = ctrl_if;
    inst->gpio_if = gpio_if;
    inst->codec_if = codec_if;
    inst->codec_dev = record_dev;
    return ESP_CODEC_DEV_OK;
}

static void pcm_mono_to_stereo_16bit(const uint8_t *src, int src_size, uint8_t *dst, int dst_size)
{
    TEST_ASSERT_NOT_NULL(src);
    TEST_ASSERT_NOT_NULL(dst);
    TEST_ASSERT((src_size & 0x1) == 0);
    TEST_ASSERT(dst_size >= src_size * 2);
    const int16_t *src16 = (const int16_t *)src;
    int16_t *dst16 = (int16_t *)dst;
    int samples = src_size >> 1;
    for (int i = 0; i < samples; i++) {
        dst16[i * 2] = src16[i];
        dst16[i * 2 + 1] = src16[i];
    }
}

static void test_case_es8389_separate_play_record_play_with_isolated_interfaces(void)
{
    int ret = ut_i2c_init(0, (codec_i2c_pin_t *)&s_es8389_i2c_pin);
    TEST_ESP_OK(ret);
    ut_set_i2s_mode(I2S_COMM_MODE_STD, I2S_COMM_MODE_STD);
    ret = ut_i2s_init_channel(0);
    TEST_ESP_OK(ret);

    const int sample_rate = 16000;
    const int bits_per_sample = 16;
    const int channel = 2;
    const int bytes_per_sample = bits_per_sample >> 3;
    const int total_bytes = 5 * sample_rate * channel * bytes_per_sample;
    const int chunk_bytes = 240 * 3 * channel * bytes_per_sample;

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = sample_rate,
        .channel = channel,
        .bits_per_sample = bits_per_sample,
        .mclk_multiple = 256,
        .channel_mask = BIT(0) | BIT(1),
    };

    codec_inst_t play_inst = {0};
    ret = init_es8389_play_codec_dev(&play_inst);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_open(play_inst.codec_dev, &fs);
    TEST_ESP_OK(ret);

    uint8_t *stereo_chunk = (uint8_t *)malloc(chunk_bytes);
    TEST_ASSERT_NOT_NULL(stereo_chunk);
    int pcm_size = (int)(music_pcm_end - music_pcm_start);
    int pcm_offset = 0;
    int played = 0;
    while (played < total_bytes) {
        int once = (total_bytes - played > chunk_bytes) ? chunk_bytes : (total_bytes - played);
        int mono_once = once / 2;
        if (pcm_offset + mono_once > pcm_size) {
            pcm_offset = 0;
        }
        pcm_mono_to_stereo_16bit(music_pcm_start + pcm_offset, mono_once, stereo_chunk, once);
        ret = esp_codec_dev_write(play_inst.codec_dev, stereo_chunk, once);
        TEST_ESP_OK(ret);
        pcm_offset += mono_once;
        played += once;
    }
    free(stereo_chunk);
    ret = esp_codec_dev_close(play_inst.codec_dev);
    TEST_ESP_OK(ret);
    deinit_codec_inst(&play_inst, true);

    uint8_t *record_buf = (uint8_t *)heap_caps_malloc(total_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (record_buf == NULL) {
        record_buf = (uint8_t *)malloc(total_bytes);
    }
    TEST_ASSERT_NOT_NULL(record_buf);

    codec_inst_t record_inst = {0};
    ret = init_es8389_record_codec_dev(&record_inst);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_open(record_inst.codec_dev, &fs);
    TEST_ESP_OK(ret);
    esp_codec_dev_sleep(100);
    int recorded = 0;
    while (recorded < total_bytes) {
        int once = (total_bytes - recorded > chunk_bytes) ? chunk_bytes : (total_bytes - recorded);
        ret = esp_codec_dev_read(record_inst.codec_dev, record_buf + recorded, once);
        test_print_pcm_s16_head(record_buf + recorded, 4);
        TEST_ESP_OK(ret);
        int max_sample = 0;
        int min_sample = 0;
        codec_max_sample(record_buf + recorded, once, &max_sample, &min_sample);
        TEST_ASSERT(max_sample > min_sample);
        recorded += once;
    }
    ret = esp_codec_dev_close(record_inst.codec_dev);
    TEST_ESP_OK(ret);
    deinit_codec_inst(&record_inst, true);

    ret = init_es8389_play_codec_dev(&play_inst);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_open(play_inst.codec_dev, &fs);
    TEST_ESP_OK(ret);
    played = 0;
    while (played < total_bytes) {
        int once = (total_bytes - played > chunk_bytes) ? chunk_bytes : (total_bytes - played);
        ret = esp_codec_dev_write(play_inst.codec_dev, record_buf + played, once);
        TEST_ESP_OK(ret);
        played += once;
    }
    ret = esp_codec_dev_close(play_inst.codec_dev);
    TEST_ESP_OK(ret);
    deinit_codec_inst(&play_inst, true);

    free(record_buf);
    TEST_ESP_OK(ut_i2s_deinit(0));
    TEST_ESP_OK(ut_i2c_deinit(0));
    ut_clr_i2s_mode();
}

static void test_case_es8389_record_while_playing_with_isolated_interfaces(void)
{
    int ret = ut_i2c_init(0, (codec_i2c_pin_t *)&s_es8389_i2c_pin);
    TEST_ESP_OK(ret);
    ut_set_i2s_mode(I2S_COMM_MODE_STD, I2S_COMM_MODE_STD);
    ret = ut_i2s_init_channel(0);
    TEST_ESP_OK(ret);

    const int sample_rate = 16000;
    const int bits_per_sample = 16;
    const int channel = 2;
    const int bytes_per_sample = bits_per_sample >> 3;
    const int total_bytes = 5 * sample_rate * channel * bytes_per_sample;
    const int chunk_bytes = 240 * 3 * channel * bytes_per_sample;

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = sample_rate,
        .channel = channel,
        .bits_per_sample = bits_per_sample,
        .mclk_multiple = 256,
        .channel_mask = BIT(0) | BIT(1),
    };

    codec_inst_t play_inst = {0};
    codec_inst_t record_inst = {0};
    ret = init_es8389_play_codec_dev(&play_inst);
    TEST_ESP_OK(ret);
    ret = init_es8389_record_codec_dev(&record_inst);
    TEST_ESP_OK(ret);

    ret = esp_codec_dev_open(play_inst.codec_dev, &fs);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_open(record_inst.codec_dev, &fs);
    TEST_ESP_OK(ret);
    esp_codec_dev_sleep(100);

    uint8_t *stereo_chunk = (uint8_t *)malloc(chunk_bytes);
    TEST_ASSERT_NOT_NULL(stereo_chunk);

    int processed = 0;
    while (processed < total_bytes) {
        int once = (total_bytes - processed > chunk_bytes) ? chunk_bytes : (total_bytes - processed);
        ret = esp_codec_dev_read(record_inst.codec_dev, stereo_chunk, once);
        test_print_pcm_s16_head(stereo_chunk, 4);
        TEST_ESP_OK(ret);
        int max_sample = 0;
        int min_sample = 0;
        codec_max_sample(stereo_chunk, once, &max_sample, &min_sample);
        TEST_ASSERT(max_sample > min_sample);
        processed += once;
        ret = esp_codec_dev_write(play_inst.codec_dev, stereo_chunk, once);
        TEST_ESP_OK(ret);
    }
    free(stereo_chunk);

    ret = esp_codec_dev_close(record_inst.codec_dev);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_close(play_inst.codec_dev);
    TEST_ESP_OK(ret);
    deinit_codec_inst(&record_inst, true);
    deinit_codec_inst(&play_inst, true);

    TEST_ESP_OK(ut_i2s_deinit(0));
    TEST_ESP_OK(ut_i2c_deinit(0));
    ut_clr_i2s_mode();
}
#endif  /* CONFIG_CODEC_ES8389_SUPPORT */

#if CONFIG_CODEC_ES8389_SUPPORT

static void record_and_play_test(esp_codec_dev_handle_t record_dev, esp_codec_dev_handle_t play_dev)
{
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = 0x01,
        .mclk_multiple = 256,
    };

    int ret = esp_codec_dev_open(play_dev, &fs);
    TEST_ESP_OK(ret);
    esp_codec_dev_write(play_dev, (void *)music_pcm_start, music_pcm_end - music_pcm_start);
    esp_codec_dev_close(play_dev);

    int size = 5 * fs.sample_rate * (fs.bits_per_sample >> 3);
    uint8_t *data = (uint8_t *)malloc(size);
    ret = esp_codec_dev_open(record_dev, &fs);
    TEST_ESP_OK(ret);
    esp_codec_dev_read(record_dev, data, size);
    esp_codec_dev_close(record_dev);

    ret = esp_codec_dev_open(play_dev, &fs);
    TEST_ESP_OK(ret);
    esp_codec_dev_write(play_dev, data, size);
    esp_codec_dev_close(play_dev);
    free(data);
}

static void test_case_es8389_record_while_playing_full_duplex_custom_pins(void)
{
    ut_set_i2s_mode(I2S_COMM_MODE_TDM, I2S_COMM_MODE_TDM);

    int ret = ut_i2c_init(0, (codec_i2c_pin_t *)&s_es8389_i2c_pin);
    TEST_ESP_OK(ret);
    ret = ut_i2s_init(0, (codec_i2s_pin_t *)&s_es8389_i2s_pin, APP_I2S_CLK_SRC_RX_DEFAULT);
    TEST_ESP_OK(ret);

    audio_codec_i2s_cfg_t i2s_cfg = {
        .rx_handle = ut_i2s_get_rx_handle(0),
        .tx_handle = ut_i2s_get_tx_handle(0),
        .clk_src = APP_I2S_CLK_SRC_RX_DEFAULT,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    TEST_ASSERT_NOT_NULL(data_if);

    audio_codec_i2c_cfg_t i2c_cfg = {
        .addr = ES8389_CODEC_DEFAULT_ADDR,
        .bus_handle = ut_i2c_get_bus_handle(),
        .clock_speed_hz = ES8389_TEST_BOARD_I2C_SCL_SPEED_HZ,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    TEST_ASSERT_NOT_NULL(ctrl_if);

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    TEST_ASSERT_NOT_NULL(gpio_if);

    es8389_codec_cfg_t es8389_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .sys_cfg = {
            .is_master = false,
            .no_mclk = true,
        },
        .adc_cfg = {
            .digital_mic = false,
            .label = "FL,FR,RE",
        },
        .dac_cfg = {
            .ref_enable = true,
        },
        .pa_cfg = {
            .pa_pin = ES8389_TEST_BOARD_PA,
            .pa_active_low = false,
            .hw_gain = {
                .pa_voltage = 5.0f,
                .codec_dac_voltage = 3.3f,
                .pa_gain = 0.0f,
            },
        },
    };
    const audio_codec_if_t *codec_if = es8389_codec_new(&es8389_cfg);
    TEST_ASSERT_NOT_NULL(codec_if);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = codec_if,
        .data_if = data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    esp_codec_dev_handle_t play_dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(play_dev);

    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
    esp_codec_dev_handle_t record_dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(record_dev);

    ret = esp_codec_dev_set_out_vol(play_dev, TEST_CODEC_BOARD_OUT_VOL);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_set_in_gain(record_dev, TEST_CODEC_BOARD_IN_GAIN);
    TEST_ESP_OK(ret);

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 48000,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = 0x03,
        .mclk_multiple = 256,
    };

    record_and_play_test(record_dev, play_dev);

    fs.channel = 4;
    fs.channel_mask = 0x0F;
    ret = esp_codec_dev_open(record_dev, &fs);
    TEST_ESP_OK(ret);
    /*
     * Regression: open record first, wait, then open play on a shared codec_if.
     * Opening play reconfigs I2S and briefly stops the clock; after the gap the
     * chip may enter analog standby. Without bias restore in set_fs, duplex
     * read/write fails. The delay is required so the codec is stable before the
     * clock drop. Different channel counts (4ch record -> 2ch play) force set_fmt
     * reconfiguration rather than a no-op path.
     */
    esp_codec_dev_sleep(100);

    fs.channel = 2;
    fs.channel_mask = 0x03;
    ret = esp_codec_dev_open(play_dev, &fs);
    TEST_ESP_OK(ret);

    esp_codec_dev_channel_map_t order = {
        .value = ESP_CODEC_DEV_CHANNEL_MAP(1, 3, 2, 4, 0, 0, 0, 0),
    };
    ret = esp_codec_dev_set_data_layout(play_dev, &order);
    TEST_ESP_OK(ret);

    const int chunk = fs.sample_rate * fs.channel * (fs.bits_per_sample >> 3) / 20;
    uint8_t *data = (uint8_t *)malloc(chunk);
    TEST_ASSERT_NOT_NULL(data);
    const int limit_bytes = 10 * fs.sample_rate * fs.channel * (fs.bits_per_sample >> 3);
    int got = 0;
    while (got < limit_bytes) {
        ret = esp_codec_dev_read(record_dev, data, chunk);
        TEST_ESP_OK(ret);
        test_print_pcm_s16_head(data, 4);
        ret = esp_codec_dev_write(play_dev, data, chunk);
        TEST_ESP_OK(ret);
        int max_sample = 0;
        int min_sample = 0;
        codec_max_sample(data, chunk, &max_sample, &min_sample);
        TEST_ASSERT(max_sample > min_sample);
        got += chunk;
    }
    free(data);

    ret = esp_codec_dev_close(record_dev);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_close(play_dev);
    TEST_ESP_OK(ret);

    esp_codec_dev_delete(record_dev);
    esp_codec_dev_delete(play_dev);
    audio_codec_delete_codec_if(codec_if);
    audio_codec_delete_ctrl_if(ctrl_if);
    audio_codec_delete_gpio_if(gpio_if);
    audio_codec_delete_data_if(data_if);
    ut_i2c_deinit(0);
    TEST_ESP_OK(ut_i2s_deinit(0));
}

static void test_case_es8389_dac_ref_loopback_custom_pins(void)
{
    ut_set_i2s_mode(I2S_COMM_MODE_TDM, I2S_COMM_MODE_TDM);

    int ret = ut_i2c_init(0, (codec_i2c_pin_t *)&s_es8389_i2c_pin);
    TEST_ESP_OK(ret);
    ret = ut_i2s_init(0, (codec_i2s_pin_t *)&s_es8389_i2s_pin, APP_I2S_CLK_SRC_RX_DEFAULT);
    TEST_ESP_OK(ret);

    audio_codec_i2s_cfg_t i2s_cfg = {
        .rx_handle = ut_i2s_get_rx_handle(0),
        .tx_handle = ut_i2s_get_tx_handle(0),
        .clk_src = APP_I2S_CLK_SRC_RX_DEFAULT,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    TEST_ASSERT_NOT_NULL(data_if);

    audio_codec_i2c_cfg_t i2c_cfg = {
        .addr = ES8389_CODEC_DEFAULT_ADDR,
        .bus_handle = ut_i2c_get_bus_handle(),
        .clock_speed_hz = ES8389_TEST_BOARD_I2C_SCL_SPEED_HZ,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    TEST_ASSERT_NOT_NULL(ctrl_if);

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    TEST_ASSERT_NOT_NULL(gpio_if);

    es8389_codec_cfg_t es8389_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .sys_cfg = {
            .is_master = false,
            .no_mclk = true,
        },
        .adc_cfg = {
            .digital_mic = false,
            .label = "FL,FR,RE,NA",
        },
        .dac_cfg = {
            .ref_enable = true,
        },
        .pa_cfg = {
            .pa_pin = ES8389_TEST_BOARD_PA,
            .pa_active_low = false,
            .hw_gain = {
                .pa_voltage = 5.0f,
                .codec_dac_voltage = 3.3f,
                .pa_gain = 0.0f,
            },
        },
    };
    const audio_codec_if_t *codec_if = es8389_codec_new(&es8389_cfg);
    TEST_ASSERT_NOT_NULL(codec_if);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = codec_if,
        .data_if = data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    esp_codec_dev_handle_t play_dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(play_dev);
    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
    esp_codec_dev_handle_t record_dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(record_dev);

    ret = esp_codec_dev_set_out_vol(play_dev, 90);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_set_in_gain(record_dev, TEST_CODEC_BOARD_IN_GAIN);
    TEST_ESP_OK(ret);

    char label[16] = {0};
    ret = esp_codec_dev_get_data_layout_label(record_dev, label, sizeof(label));
    TEST_ESP_OK(ret);
    TEST_ASSERT_EQUAL_STRING("FL,RE,FR,NA", label);

    esp_codec_dev_sample_info_t play_fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = BIT(0),
        .mclk_multiple = 256,
    };
    esp_codec_dev_sample_info_t record_fs = {
        .sample_rate = 16000,
        .channel = 4,
        .bits_per_sample = 16,
        .channel_mask = BIT(0) | BIT(1) | BIT(2),
        .mclk_multiple = 256,
    };
    ret = esp_codec_dev_open(record_dev, &record_fs);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_set_data_layout_label(record_dev, "FL,FR,RE");
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_get_data_layout_label(record_dev, label, sizeof(label));
    TEST_ESP_OK(ret);
    TEST_ASSERT_EQUAL_STRING("FL,FR,RE", label);
    ret = esp_codec_dev_open(play_dev, &play_fs);
    TEST_ESP_OK(ret);

    const int chunk_frames = 1024;
    const int play_bytes = chunk_frames * sizeof(int16_t);
    const int record_bytes = chunk_frames * 3 * sizeof(int16_t);
    int16_t *play_buf = (int16_t *)malloc(play_bytes);
    int16_t *record_buf = (int16_t *)malloc(record_bytes);
    TEST_ASSERT_NOT_NULL(play_buf);
    TEST_ASSERT_NOT_NULL(record_buf);

    int64_t times_start = esp_timer_get_time();
    while (esp_timer_get_time() - times_start < 10 * 1000 * 1000) {  // 10 seconds
        ret = esp_codec_dev_read(record_dev, record_buf, record_bytes);
        TEST_ESP_OK(ret);
        test_print_pcm_s16_head((uint8_t *)record_buf, 6);
    }

    fill_es8389_ref_tone(play_buf, chunk_frames);

    int frame_offset = 0;
    es8389_loopback_channel_stat_t stat[3] = {0};
    const int total_frames = 3 * play_fs.sample_rate;
    const int warmup_frames = play_fs.sample_rate / 2;
    ESP_LOGI(TAG, "Start ES8389 reference capture: play 1kHz on DAC ch1, analyze FL/FR/RE");
    while (frame_offset < total_frames) {
        ret = esp_codec_dev_write(play_dev, play_buf, play_bytes);
        TEST_ESP_OK(ret);
        ret = esp_codec_dev_read(record_dev, record_buf, record_bytes);
        TEST_ESP_OK(ret);
        if (frame_offset >= warmup_frames) {
            analyze_es8389_ref_capture(record_buf, chunk_frames, frame_offset - warmup_frames, stat);
        }
        frame_offset += chunk_frames;
    }

    int avg_abs[3] = {0};
    int tone_level[3] = {0};
    for (int ch = 0; ch < 3; ch++) {
        int64_t max_corr = 0;
        for (int i = 0; i < 16; i++) {
            int64_t corr_abs = stat[ch].corr[i] >= 0 ? stat[ch].corr[i] : -stat[ch].corr[i];
            if (corr_abs > max_corr) {
                max_corr = corr_abs;
            }
        }
        avg_abs[ch] = stat[ch].sample_count > 0 ? (int)(stat[ch].abs_sum / stat[ch].sample_count) : 0;
        tone_level[ch] = stat[ch].sample_count > 0 ? (int)(max_corr / ((int64_t)stat[ch].sample_count * 16384)) : 0;
    }
    ESP_LOGI(TAG, "ES8389 ref capture FL: samples=%d avg_abs=%d peak_abs=%d tone_level=%d",
             stat[0].sample_count, avg_abs[0], stat[0].peak_abs, tone_level[0]);
    ESP_LOGI(TAG, "ES8389 ref capture FR: samples=%d avg_abs=%d peak_abs=%d tone_level=%d",
             stat[1].sample_count, avg_abs[1], stat[1].peak_abs, tone_level[1]);
    ESP_LOGI(TAG, "ES8389 ref capture RE: samples=%d avg_abs=%d peak_abs=%d tone_level=%d",
             stat[2].sample_count, avg_abs[2], stat[2].peak_abs, tone_level[2]);
    bool has_samples = stat[0].sample_count > 0 && stat[1].sample_count > 0 && stat[2].sample_count > 0;
    bool ref_has_signal = avg_abs[2] > 100;
    bool ref_has_tone = tone_level[2] > 100;
    bool ref_not_clipped = stat[2].peak_abs < 32760;

    free(play_buf);
    free(record_buf);
    ret = esp_codec_dev_close(record_dev);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_close(play_dev);
    TEST_ESP_OK(ret);
    esp_codec_dev_delete(record_dev);
    esp_codec_dev_delete(play_dev);
    audio_codec_delete_codec_if(codec_if);
    audio_codec_delete_ctrl_if(ctrl_if);
    audio_codec_delete_gpio_if(gpio_if);
    audio_codec_delete_data_if(data_if);
    ut_i2c_deinit(0);
    TEST_ESP_OK(ut_i2s_deinit(0));

    TEST_ASSERT_MESSAGE(has_samples, "No ES8389 reference samples analyzed");
    TEST_ASSERT_MESSAGE(ref_has_signal, "ES8389 reference channel is too quiet");
    TEST_ASSERT_MESSAGE(ref_has_tone, "ES8389 reference channel did not capture the played 1kHz tone");
    TEST_ASSERT_MESSAGE(ref_not_clipped, "ES8389 reference channel is clipped");
}

TEST_CASE("es8389 separate play record play with isolated interfaces", "[es8389][duplex]")
{
    test_case_es8389_separate_play_record_play_with_isolated_interfaces();
}

TEST_CASE("es8389 record while playing with isolated interfaces", "[es8389][duplex]")
{
    test_case_es8389_record_while_playing_with_isolated_interfaces();
}

TEST_CASE("ES8389 record while playing (full duplex, custom pins)", "[es8389][duplex]")
{
    test_case_es8389_record_while_playing_full_duplex_custom_pins();
}

TEST_CASE("ES8389 DAC reference loopback capture (custom pins)", "[es8389][loopback]")
{
    test_case_es8389_dac_ref_loopback_custom_pins();
}
#endif  /* CONFIG_CODEC_ES8389_SUPPORT */
