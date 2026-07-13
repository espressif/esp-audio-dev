/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_idf_version.h"
#include "esp_log.h"
#include "unity.h"

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "test_board_periph.h"
#include "test_codec_print.h"
#include "esp_codec_dev_os.h"

#if CONFIG_IDF_TARGET_ESP32

#if defined(CONFIG_CODEC_ES7243E_SUPPORT) && defined(CONFIG_CODEC_ES8311_SUPPORT)

typedef struct {
    const audio_codec_data_if_t *out_data_if;
    const audio_codec_data_if_t *in_data_if;
    const audio_codec_ctrl_if_t *out_ctrl_if;
    const audio_codec_ctrl_if_t *in_ctrl_if;
    const audio_codec_gpio_if_t *gpio_if;
    const audio_codec_if_t      *out_codec_if;
    const audio_codec_if_t      *in_codec_if;
    esp_codec_dev_handle_t       play_dev;
    esp_codec_dev_handle_t       record_dev;
} lyrat_mini_ctx_t;

#define I2S_CLK_SRC  I2S_CLK_SRC_APLL

typedef struct {
    int64_t  abs_sum;
    int64_t  corr[16];
    int      sample_count;
    int      peak_abs;
} lyrat_mini_loopback_channel_stat_t;

static void fill_lyrat_mini_ref_tone(int16_t *data, int frame_count)
{
    static const int16_t sine_1k_16k[16] = {
        0, 6270, 11585, 15137, 16384, 15137, 11585, 6270,
        0, -6270, -11585, -15137, -16384, -15137, -11585, -6270,
    };
    for (int i = 0; i < frame_count; i++) {
        data[i] = (int16_t)((sine_1k_16k[i & 0x0F] * 500) / 16384);
    }
}

static void analyze_lyrat_mini_ref_capture(const int16_t *data, int frame_count, int frame_offset,
                                           lyrat_mini_loopback_channel_stat_t stat[2])
{
    static const int16_t sine_1k_16k[16] = {
        0, 6270, 11585, 15137, 16384, 15137, 11585, 6270,
        0, -6270, -11585, -15137, -16384, -15137, -11585, -6270,
    };
    for (int i = 0; i < frame_count; i++) {
        for (int ch = 0; ch < 2; ch++) {
            int sample = data[i * 2 + ch];
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

static void lyrat_mini_setup(lyrat_mini_ctx_t *ctx, bool rx_init_first)
{
    *ctx = (lyrat_mini_ctx_t) {0};
    // ESP32-LyraT-Mini (v1.2/v1.1) typical connections:
    // - I2C: SDA=GPIO18, SCL=GPIO23
    // - ES8311 (DAC) on I2S0: BCK=GPIO5,  WS=GPIO25, DOUT=GPIO26 (to codec), MCLK not used
    // - ES7243E (ADC) on I2S1: BCK=GPIO32, WS=GPIO33, DIN=GPIO36, MCLK=GPIO0
    ut_set_i2s_mode(I2S_COMM_MODE_STD, I2S_COMM_MODE_NONE);
    ut_i2s_set_rx_init_first(rx_init_first);

    codec_i2c_pin_t i2c_pin = {.sda = 18, .scl = 23};
    int ret = ut_i2c_init(0, &i2c_pin);
    TEST_ESP_OK(ret);

    codec_i2s_pin_t i2s_out_pin = {.mclk = -1, .bclk = 5, .ws = 25, .dout = 26, .din = -1};
    ret = ut_i2s_init(0, &i2s_out_pin, I2S_CLK_SRC);
    TEST_ESP_OK(ret);

    ut_set_i2s_mode(I2S_COMM_MODE_STD, I2S_COMM_MODE_STD);
    codec_i2s_pin_t i2s_in_pin = {.mclk = 0, .bclk = 32, .ws = 33, .dout = -1, .din = 36};
    ret = ut_i2s_init(1, &i2s_in_pin, I2S_CLK_SRC_DEFAULT);
    TEST_ESP_OK(ret);

    audio_codec_i2s_cfg_t i2s_out_cfg = {
        .port = 0,
        .rx_handle = NULL,
        .tx_handle = ut_i2s_get_tx_handle(0),
        .clk_src = I2S_CLK_SRC,
    };
    ctx->out_data_if = audio_codec_new_i2s_data(&i2s_out_cfg);
    TEST_ASSERT_NOT_NULL(ctx->out_data_if);

    audio_codec_i2s_cfg_t i2s_in_cfg = {
        .port = 1,
        .rx_handle = ut_i2s_get_rx_handle(1),
        .tx_handle = NULL,
    };
    ctx->in_data_if = audio_codec_new_i2s_data(&i2s_in_cfg);
    TEST_ASSERT_NOT_NULL(ctx->in_data_if);

    audio_codec_i2c_cfg_t i2c_cfg = {.addr = ES8311_CODEC_DEFAULT_ADDR};
    i2c_cfg.bus_handle = ut_i2c_get_bus_handle();
    ctx->out_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    TEST_ASSERT_NOT_NULL(ctx->out_ctrl_if);

    i2c_cfg.addr = ES7243E_CODEC_DEFAULT_ADDR;
    ctx->in_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    TEST_ASSERT_NOT_NULL(ctx->in_ctrl_if);

    ctx->gpio_if = audio_codec_new_gpio();
    TEST_ASSERT_NOT_NULL(ctx->gpio_if);

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = ctx->out_ctrl_if,
        .gpio_if = ctx->gpio_if,
        .sys_cfg = {
            .is_master = false,
            .no_mclk = true,
        },
        .dac_cfg = {
            .ref_enable = true,
        },
        .pa_cfg = {
            .pa_pin = 21,
            .pa_active_low = false,
        },
    };
    ctx->out_codec_if = es8311_codec_new(&es8311_cfg);
    TEST_ASSERT_NOT_NULL(ctx->out_codec_if);

    es7243e_codec_cfg_t es7243e_cfg = {
        .ctrl_if = ctx->in_ctrl_if,
        .adc_cfg = {
            .label = "RE,FL",
        },
    };
    ctx->in_codec_if = es7243e_codec_new(&es7243e_cfg);
    TEST_ASSERT_NOT_NULL(ctx->in_codec_if);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = ctx->out_codec_if,
        .data_if = ctx->out_data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    ctx->play_dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(ctx->play_dev);

    dev_cfg.codec_if = ctx->in_codec_if;
    dev_cfg.data_if = ctx->in_data_if;
    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
    ctx->record_dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(ctx->record_dev);

    ret = esp_codec_dev_set_out_vol(ctx->play_dev, 10.0);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_set_in_gain(ctx->record_dev, 20.0);
    TEST_ESP_OK(ret);
}

static void lyrat_mini_teardown(lyrat_mini_ctx_t *ctx)
{
    if (ctx->play_dev) {
        esp_codec_dev_delete(ctx->play_dev);
    }
    if (ctx->record_dev) {
        esp_codec_dev_delete(ctx->record_dev);
    }
    if (ctx->out_codec_if) {
        audio_codec_delete_codec_if(ctx->out_codec_if);
    }
    if (ctx->in_codec_if) {
        audio_codec_delete_codec_if(ctx->in_codec_if);
    }
    if (ctx->in_ctrl_if) {
        audio_codec_delete_ctrl_if(ctx->in_ctrl_if);
    }
    if (ctx->out_ctrl_if) {
        audio_codec_delete_ctrl_if(ctx->out_ctrl_if);
    }
    if (ctx->gpio_if) {
        audio_codec_delete_gpio_if(ctx->gpio_if);
    }
    if (ctx->in_data_if) {
        audio_codec_delete_data_if(ctx->in_data_if);
    }
    if (ctx->out_data_if) {
        audio_codec_delete_data_if(ctx->out_data_if);
    }

    ut_i2c_deinit(0);
    ut_i2s_deinit(0);
    ut_i2s_deinit(1);
}

static void open_codec_dev(esp_codec_dev_handle_t dev, esp_codec_dev_sample_info_t *fs)
{
    int ret = esp_codec_dev_open(dev, fs);
    TEST_ESP_OK(ret);
}

static void verify_record_label_layout(esp_codec_dev_handle_t record_dev)
{
    char label[16] = {0};
    int ret = esp_codec_dev_get_data_layout_label(record_dev, label, sizeof(label));
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_OK, ret);
    TEST_ASSERT_EQUAL_STRING("RE,FL", label);
    ret = esp_codec_dev_get_data_layout_label(record_dev, label, 3);
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_INVALID_ARG, ret);
    ret = esp_codec_dev_set_data_layout_label(record_dev, "");
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_INVALID_ARG, ret);
    ret = esp_codec_dev_set_data_layout_label(record_dev, "RE,RE");
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_INVALID_ARG, ret);

    ret = esp_codec_dev_set_data_layout_label(record_dev, "FL,RE");
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_OK, ret);
    esp_codec_dev_channel_map_t order = {0};
    ret = esp_codec_dev_get_data_layout(record_dev, &order);
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_OK, ret);
    TEST_ASSERT_EQUAL_HEX32(ESP_CODEC_DEV_CHANNEL_MAP(1, 3, 0, 0, 0, 0, 0, 0), order.value);
}

static void test_codec_dev_using_esp32_lyrat_mini_es7243e(void)
{
    lyrat_mini_ctx_t ctx = {0};
    lyrat_mini_setup(&ctx, false);

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 48000,
        .channel = 2,
        .bits_per_sample = 32,
        .channel_mask = BIT(0) | BIT(1),
        .mclk_multiple = 256,
    };
    open_codec_dev(ctx.play_dev, &fs);
    open_codec_dev(ctx.record_dev, &fs);
    verify_record_label_layout(ctx.record_dev);

    esp_codec_dev_channel_map_t order = {
        .value = ESP_CODEC_DEV_CHANNEL_MAP(2, 1, 0, 0, 0, 0, 0, 0),
    };
    esp_codec_dev_set_data_layout(ctx.play_dev, &order);

    int data_size = 240 * 3 * fs.channel * (fs.bits_per_sample >> 3);
    uint8_t *data = (uint8_t *)malloc(data_size + 2);
    TEST_ASSERT_NOT_NULL(data);
    int limit_size = 10 * fs.sample_rate * fs.channel * (fs.bits_per_sample >> 3);
    int got_size = 0;
    int bytes_per_second = fs.sample_rate * fs.channel * (fs.bits_per_sample >> 3);
    int dump_target_size = bytes_per_second * 3;  // keep first 3s MIC data for host-side analysis
    if (dump_target_size > limit_size) {
        dump_target_size = limit_size;
    }
    uint8_t *dump_buf = (uint8_t *)malloc(dump_target_size);
    TEST_ASSERT_NOT_NULL(dump_buf);
    int dump_written = 0;
    int ret = 0;
    while (got_size < limit_size) {
        ret = esp_codec_dev_read(ctx.record_dev, data, data_size);
        TEST_ESP_OK(ret);
        if (dump_written < dump_target_size) {
            int cpy = data_size;
            if (cpy > (dump_target_size - dump_written)) {
                cpy = dump_target_size - dump_written;
            }
            memcpy(dump_buf + dump_written, data, cpy);
            dump_written += cpy;
        }
        // test_print_pcm_s16_head(data, 4);
        ret = esp_codec_dev_write(ctx.play_dev, data, data_size);
        TEST_ESP_OK(ret);
        got_size += data_size;
    }
    free(data);

    ret = esp_codec_dev_close(ctx.play_dev);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_close(ctx.record_dev);
    TEST_ESP_OK(ret);
    lyrat_mini_teardown(&ctx);
    free(dump_buf);
}

static void test_codec_dev_using_esp32_lyrat_mini_es7243e_mono(void)
{
    lyrat_mini_ctx_t ctx = {0};
    lyrat_mini_setup(&ctx, true);

    // 44100 and 48000 have noise issue, maybe due to mclk_multiple
    const int sample_rates[] = {16000, 32000, 48000};
    const int bits_list[] = {16, 24, 32};

    for (int r = 0; r < (int)(sizeof(sample_rates) / sizeof(sample_rates[0])); r++) {
        for (int b = 0; b < (int)(sizeof(bits_list) / sizeof(bits_list[0])); b++) {
            esp_codec_dev_sample_info_t fs = {
                .sample_rate = sample_rates[r],
                .channel = 2,
                .bits_per_sample = bits_list[b],
                .channel_mask = BIT(0),
                .mclk_multiple = 384,
            };
            ESP_LOGI("TEST_BOARD_HW1", "Mono loopback start: sample_rate=%d, bits=%d",
                     fs.sample_rate, fs.bits_per_sample);

            fs.channel_mask = BIT(0);
            open_codec_dev(ctx.play_dev, &fs);
            fs.channel_mask = BIT(1);
            open_codec_dev(ctx.record_dev, &fs);

            int data_size = 240 * fs.channel * (fs.bits_per_sample >> 3);
            uint8_t *data = (uint8_t *)malloc(data_size);
            TEST_ASSERT_NOT_NULL(data);
            int limit_size = 5 * fs.sample_rate * (fs.bits_per_sample >> 3);
            int got_size = 0;
            int ret = 0;
            while (got_size < limit_size) {
                ret = esp_codec_dev_read(ctx.record_dev, data, data_size);
                TEST_ESP_OK(ret);
                // test_print_pcm_s16_head(data, 4);
                ret = esp_codec_dev_write(ctx.play_dev, data, data_size);
                TEST_ESP_OK(ret);
                got_size += data_size;
            }
            ret = esp_codec_dev_close(ctx.play_dev);
            TEST_ESP_OK(ret);
            ret = esp_codec_dev_close(ctx.record_dev);
            TEST_ESP_OK(ret);
            free(data);
        }
    }
    lyrat_mini_teardown(&ctx);
}

static void test_codec_dev_using_esp32_lyrat_mini_es7243e_ref_signal(void)
{
    lyrat_mini_ctx_t ctx = {0};
    lyrat_mini_setup(&ctx, false);

    esp_codec_dev_sample_info_t play_fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = BIT(0),
        .mclk_multiple = 256,
    };
    esp_codec_dev_sample_info_t record_fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = BIT(0) | BIT(1),
        .mclk_multiple = 256,
    };
    int ret = esp_codec_dev_open(ctx.record_dev, &record_fs);
    TEST_ESP_OK(ret);
    esp_codec_dev_set_in_channel_gain(ctx.record_dev, BIT(0), 30.0);
    char label[16] = {0};
    ret = esp_codec_dev_get_data_layout_label(ctx.record_dev, label, sizeof(label));
    TEST_ESP_OK(ret);
    TEST_ASSERT_EQUAL_STRING("RE,FL", label);
    ret = esp_codec_dev_open(ctx.play_dev, &play_fs);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_set_out_vol(ctx.play_dev, 40);
    TEST_ESP_OK(ret);

    const int chunk_frames = 1024;
    const int play_bytes = chunk_frames * sizeof(int16_t);
    const int record_bytes = chunk_frames * 2 * sizeof(int16_t);
    int16_t *play_buf = (int16_t *)malloc(play_bytes);
    int16_t *record_buf = (int16_t *)malloc(record_bytes);
    TEST_ASSERT_NOT_NULL(play_buf);
    TEST_ASSERT_NOT_NULL(record_buf);
    fill_lyrat_mini_ref_tone(play_buf, chunk_frames);

    int frame_offset = 0;
    lyrat_mini_loopback_channel_stat_t stat[2] = {0};
    const int total_frames = 3 * play_fs.sample_rate;
    const int warmup_frames = play_fs.sample_rate / 2;
    ESP_LOGI("TEST_BOARD_HW1", "Start ES7243E reference capture: play 1kHz on ES8311 ch1, analyze RE/FL");
    while (frame_offset < total_frames) {
        ret = esp_codec_dev_write(ctx.play_dev, play_buf, play_bytes);
        TEST_ESP_OK(ret);
        ret = esp_codec_dev_read(ctx.record_dev, record_buf, record_bytes);
        TEST_ESP_OK(ret);
        if (frame_offset >= warmup_frames) {
            analyze_lyrat_mini_ref_capture(record_buf, chunk_frames, frame_offset - warmup_frames, stat);
        }
        frame_offset += chunk_frames;
    }

    int avg_abs[2] = {0};
    int tone_level[2] = {0};
    for (int ch = 0; ch < 2; ch++) {
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
    ESP_LOGI("TEST_BOARD_HW1", "ES7243E ref capture RE: samples=%d avg_abs=%d peak_abs=%d tone_level=%d",
             stat[0].sample_count, avg_abs[0], stat[0].peak_abs, tone_level[0]);
    ESP_LOGI("TEST_BOARD_HW1", "ES7243E ref capture FL: samples=%d avg_abs=%d peak_abs=%d tone_level=%d",
             stat[1].sample_count, avg_abs[1], stat[1].peak_abs, tone_level[1]);
    bool has_samples = stat[0].sample_count > 0 && stat[1].sample_count > 0;
    bool ref_has_signal = avg_abs[0] > 20;
    bool ref_has_tone = tone_level[0] > 20;
    bool ref_not_clipped = stat[0].peak_abs < 32760;

    free(play_buf);
    free(record_buf);
    ret = esp_codec_dev_close(ctx.play_dev);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_close(ctx.record_dev);
    TEST_ESP_OK(ret);
    lyrat_mini_teardown(&ctx);

    TEST_ASSERT_MESSAGE(has_samples, "No ES7243E reference samples analyzed");
    TEST_ASSERT_MESSAGE(ref_has_signal, "ES7243E reference channel is too quiet");
    TEST_ASSERT_MESSAGE(ref_has_tone, "ES7243E reference channel did not capture the played 1kHz tone");
    TEST_ASSERT_MESSAGE(ref_not_clipped, "ES7243E reference channel is clipped");
}

TEST_CASE("esp codec dev test using ESP32-LyraT-Mini (ES7243E record -> ES8311 playback)", "[lyrat_mini][duplex]")
{
    test_codec_dev_using_esp32_lyrat_mini_es7243e();
}

TEST_CASE("esp codec dev mono record and playback using ESP32-LyraT-Mini", "[lyrat_mini][duplex]")
{
    test_codec_dev_using_esp32_lyrat_mini_es7243e_mono();
}

TEST_CASE("ES7243E reference loopback capture using ESP32-LyraT-Mini", "[lyrat_mini][loopback]")
{
    test_codec_dev_using_esp32_lyrat_mini_es7243e_ref_signal();
}

#endif  /* defined(CONFIG_CODEC_ES7243E_SUPPORT) && defined(CONFIG_CODEC_ES8311_SUPPORT) */

#if defined(CONFIG_CODEC_ES8388_SUPPORT)

typedef struct {
    const audio_codec_data_if_t *out_data_if;
    const audio_codec_data_if_t *in_data_if;
    const audio_codec_ctrl_if_t *ctrl_if;
    const audio_codec_gpio_if_t *gpio_if;
    const audio_codec_if_t      *codec_if;
    esp_codec_dev_handle_t       play_dev;
    esp_codec_dev_handle_t       record_dev;
} es8388_dev_ctx_t;

static void esp32_es8388_setup(es8388_dev_ctx_t *ctx)
{
    *ctx = (es8388_dev_ctx_t) {0};

    ut_set_i2s_mode(I2S_COMM_MODE_STD, I2S_COMM_MODE_STD);
    ut_i2s_set_rx_init_first(false);

    codec_i2c_pin_t i2c_pin = {
        .sda = 18,
        .scl = 23,
    };
    int ret = ut_i2c_init(0, &i2c_pin);
    TEST_ESP_OK(ret);

    codec_i2s_pin_t i2s_pin = {
        .mclk = 0,
        .bclk = 5,
        .ws = 25,
        .dout = 26,
        .din = 35,
    };
    ret = ut_i2s_init(0, &i2s_pin, I2S_CLK_SRC_PLL_160M);
    TEST_ESP_OK(ret);

    audio_codec_i2s_cfg_t i2s_out_cfg = {
        .port = 0,
        .tx_handle = ut_i2s_get_tx_handle(0),
        .clk_src = I2S_CLK_SRC_PLL_160M,
    };
    ctx->out_data_if = audio_codec_new_i2s_data(&i2s_out_cfg);
    TEST_ASSERT_NOT_NULL(ctx->out_data_if);

    audio_codec_i2s_cfg_t i2s_in_cfg = {
        .port = 0,
        .rx_handle = ut_i2s_get_rx_handle(0),
        .clk_src = I2S_CLK_SRC_PLL_160M,
    };
    ctx->in_data_if = audio_codec_new_i2s_data(&i2s_in_cfg);
    TEST_ASSERT_NOT_NULL(ctx->in_data_if);

    audio_codec_i2c_cfg_t i2c_cfg = {
        .addr = ES8388_CODEC_DEFAULT_ADDR,
    };
    i2c_cfg.bus_handle = ut_i2c_get_bus_handle();
    ctx->ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    TEST_ASSERT_NOT_NULL(ctx->ctrl_if);

    ctx->gpio_if = audio_codec_new_gpio();
    TEST_ASSERT_NOT_NULL(ctx->gpio_if);

    es8388_codec_cfg_t es8388_cfg = {
        .ctrl_if = ctx->ctrl_if,
        .gpio_if = ctx->gpio_if,
        .sys_cfg = {
            .is_master = false,
            .no_mclk = false,
        },
        .pa_cfg = {
            .pa_pin = 21,
            .pa_active_low = false,
        },
    };
    ctx->codec_if = es8388_codec_new(&es8388_cfg);
    TEST_ASSERT_NOT_NULL(ctx->codec_if);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = ctx->codec_if,
        .data_if = ctx->out_data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    ctx->play_dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(ctx->play_dev);

    dev_cfg.codec_if = ctx->codec_if;
    dev_cfg.data_if = ctx->in_data_if;
    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
    ctx->record_dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(ctx->record_dev);

    ret = esp_codec_dev_set_out_vol(ctx->play_dev, 30.0);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_set_in_gain(ctx->record_dev, 24.0);
    TEST_ESP_OK(ret);
}

static void esp32_es8388_teardown(es8388_dev_ctx_t *ctx)
{
    if (ctx->play_dev) {
        esp_codec_dev_delete(ctx->play_dev);
    }
    if (ctx->record_dev) {
        esp_codec_dev_delete(ctx->record_dev);
    }
    if (ctx->codec_if) {
        audio_codec_delete_codec_if(ctx->codec_if);
    }
    if (ctx->ctrl_if) {
        audio_codec_delete_ctrl_if(ctx->ctrl_if);
    }
    if (ctx->gpio_if) {
        audio_codec_delete_gpio_if(ctx->gpio_if);
    }
    if (ctx->out_data_if) {
        audio_codec_delete_data_if(ctx->out_data_if);
    }
    if (ctx->in_data_if) {
        audio_codec_delete_data_if(ctx->in_data_if);
    }
    ut_i2c_deinit(0);
    ut_i2s_deinit(0);
}

static void test_codec_dev_using_esp32_es8388_record_and_playback(void)
{
    es8388_dev_ctx_t ctx = {0};
    esp32_es8388_setup(&ctx);

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 48000,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = BIT(0) | BIT(1),
        .mclk_multiple = 256,
    };

    int ret = esp_codec_dev_open(ctx.play_dev, &fs);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_open(ctx.record_dev, &fs);
    TEST_ESP_OK(ret);

    const int chunk_size = fs.sample_rate * fs.channel * (fs.bits_per_sample >> 3) / 20;
    uint8_t *data = (uint8_t *)malloc(chunk_size);
    TEST_ASSERT_NOT_NULL(data);

    int limit_size = 5 * fs.sample_rate * fs.channel * (fs.bits_per_sample >> 3);
    int got_size = 0;

    while (got_size < limit_size) {
        ret = esp_codec_dev_read(ctx.record_dev, data, chunk_size);
        TEST_ESP_OK(ret);
        test_print_pcm_s16_head(data, 4);
        ret = esp_codec_dev_write(ctx.play_dev, data, chunk_size);
        TEST_ESP_OK(ret);

        int16_t *pcm = (int16_t *)data;
        int sample_num = chunk_size / sizeof(int16_t);
        int max_sample = INT16_MIN;
        int min_sample = INT16_MAX;
        for (int i = 0; i < sample_num; i++) {
            if (pcm[i] > max_sample) {
                max_sample = pcm[i];
            }
            if (pcm[i] < min_sample) {
                min_sample = pcm[i];
            }
        }
        TEST_ASSERT(max_sample > min_sample);
        got_size += chunk_size;
    }

    ret = esp_codec_dev_close(ctx.play_dev);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_close(ctx.record_dev);
    TEST_ESP_OK(ret);
    free(data);
    esp32_es8388_teardown(&ctx);
}

TEST_CASE("esp codec dev record and playback using ES8388", "[esp32_lyrat][duplex][es8388]")
{
    test_codec_dev_using_esp32_es8388_record_and_playback();
}

#endif  /* defined(CONFIG_CODEC_ES8388_SUPPORT) */

#endif  /* CONFIG_IDF_TARGET_ESP32 */
