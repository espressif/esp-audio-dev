/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_idf_version.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "hal/i2s_ll.h"
#include "soc/soc_caps.h"
#include "esp_private/rtc_clk.h"
#include "unity.h"
#include "esp_pm.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "audio_hw_alc.h"
#include "test_board.h"
#include "test_board_periph.h"
#include "test_codec_print.h"
#include "esp_codec_dev_os.h"
#include "audio_codec_data_if.h"

static const char *TAG_CODEC_DEV_UT = "CODEC_DEV_UT";

extern const uint8_t music_pcm_start[] asm("_binary_16k_mono_16bit_pcm_start");
extern const uint8_t music_pcm_end[] asm("_binary_16k_mono_16bit_pcm_end");

typedef struct {
    int64_t  abs_sum;
    int64_t  corr[16];
    int      sample_count;
    int      peak_abs;
} s3_loopback_channel_stat_t;

static void test_check_data_layout(esp_codec_dev_handle_t dev, uint32_t expected_order, const char *stage)
{
    esp_codec_dev_channel_map_t layout = {0};
    int ret = esp_codec_dev_get_data_layout(dev, &layout);
    TEST_ESP_OK(ret);
    uint32_t order_u32 = layout.value;
    ESP_LOGI(TAG_CODEC_DEV_UT, "%s: get_data_layout -> 0x%lX", stage, (unsigned long)order_u32);
    if (expected_order != 0) {
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(expected_order, order_u32, stage);
    } else {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0, order_u32, stage);
    }
}

static void test_check_data_layout_allow_not_support(esp_codec_dev_handle_t dev, uint32_t expected_order, const char *stage)
{
    esp_codec_dev_channel_map_t layout = {0};
    int ret = esp_codec_dev_get_data_layout(dev, &layout);
    if (ret == ESP_CODEC_DEV_NOT_SUPPORT) {
        ESP_LOGW(TAG_CODEC_DEV_UT, "%s: get_data_layout NOT_SUPPORT", stage);
        return;
    }
    TEST_ESP_OK(ret);
    uint32_t order_u32 = layout.value;
    ESP_LOGI(TAG_CODEC_DEV_UT, "%s: get_data_layout -> 0x%lX", stage, (unsigned long)order_u32);
    if (expected_order != 0) {
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(expected_order, order_u32, stage);
    } else {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0, order_u32, stage);
    }
}

static void fill_s3_es7210_ref_tone(int16_t *data, int frame_count)
{
    static const int16_t sine_1k_16k[16] = {
        0, 6270, 11585, 15137, 16384, 15137, 11585, 6270,
        0, -6270, -11585, -15137, -16384, -15137, -11585, -6270,
    };
    for (int i = 0; i < frame_count; i++) {
        data[i] = (int16_t)((sine_1k_16k[i & 0x0F] * 500) / 16384);
    }
}

static void analyze_s3_es7210_ref_capture(const int16_t *data, int frame_count, int frame_offset,
                                          s3_loopback_channel_stat_t stat[3])
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

static void test_codec_dev_using_s3_board_es7210_ref_signal(void)
{
    ut_set_i2s_mode(I2S_COMM_MODE_TDM, I2S_COMM_MODE_TDM);

    int ret = ut_i2c_init(0, NULL);
    TEST_ESP_OK(ret);
    ut_i2s_set_rx_init_first(false);
    ret = ut_i2s_init(0, NULL, I2S_CLK_SRC_DEFAULT);
    TEST_ESP_OK(ret);

    audio_codec_i2s_cfg_t i2s_cfg = {
        .tx_handle = NULL,
        .rx_handle = ut_i2s_get_rx_handle(0),
    };
    const audio_codec_data_if_t *in_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    TEST_ASSERT_NOT_NULL(in_data_if);
    i2s_cfg.rx_handle = NULL;
    i2s_cfg.tx_handle = ut_i2s_get_tx_handle(0);
    const audio_codec_data_if_t *out_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    TEST_ASSERT_NOT_NULL(out_data_if);

    audio_codec_i2c_cfg_t i2c_cfg = {.addr = ES8311_CODEC_DEFAULT_ADDR};
    i2c_cfg.bus_handle = ut_i2c_get_bus_handle();
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
        .pa_cfg = {
            .pa_pin = TEST_BOARD_PA_PIN,
            .pa_active_low = false,
        },
    };
    const audio_codec_if_t *out_codec_if =
        audio_codec_new("es8311", &es8311_cfg, sizeof(es8311_cfg));
    TEST_ASSERT_NOT_NULL(out_codec_if);
    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = in_ctrl_if,
        .sys_cfg = {
            .is_master = false,
            .no_mclk = false,
        },
        .adc_cfg = {
            .label = "FL,FR,RE,NA",
        },
    };
    const audio_codec_if_t *in_codec_if =
        audio_codec_new("es7210", &es7210_cfg, sizeof(es7210_cfg));
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

    ret = esp_codec_dev_set_out_vol(play_dev, 90);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_set_in_gain(record_dev, TEST_CODEC_BOARD_IN_GAIN);
    TEST_ESP_OK(ret);

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
    ret = esp_codec_dev_set_data_layout_label(record_dev, "FL,RE,FR");
    TEST_ESP_OK(ret);
    char label[16] = {0};
    ret = esp_codec_dev_get_data_layout_label(record_dev, label, sizeof(label));
    TEST_ESP_OK(ret);
    TEST_ASSERT_EQUAL_STRING("FL,RE,FR", label);
    ret = esp_codec_dev_open(play_dev, &play_fs);
    TEST_ESP_OK(ret);

    const int chunk_frames = 1024;
    const int play_bytes = chunk_frames * sizeof(int16_t);
    const int record_bytes = chunk_frames * 3 * sizeof(int16_t);
    int16_t *play_buf = (int16_t *)malloc(play_bytes);
    int16_t *record_buf = (int16_t *)malloc(record_bytes);
    TEST_ASSERT_NOT_NULL(play_buf);
    TEST_ASSERT_NOT_NULL(record_buf);
    fill_s3_es7210_ref_tone(play_buf, chunk_frames);

    int frame_offset = 0;
    s3_loopback_channel_stat_t stat[3] = {0};
    const int total_frames = 3 * play_fs.sample_rate;
    const int warmup_frames = play_fs.sample_rate / 2;
    ESP_LOGI(TAG_CODEC_DEV_UT, "Start ES7210 reference capture: play 1kHz on ES8311 ch1, analyze FL/RE/FR");
    while (frame_offset < total_frames) {
        ret = esp_codec_dev_write(play_dev, play_buf, play_bytes);
        TEST_ESP_OK(ret);
        ret = esp_codec_dev_read(record_dev, record_buf, record_bytes);
        TEST_ESP_OK(ret);
        if (frame_offset >= warmup_frames) {
            analyze_s3_es7210_ref_capture(record_buf, chunk_frames, frame_offset - warmup_frames, stat);
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
    ESP_LOGI(TAG_CODEC_DEV_UT, "ES7210 ref capture FL: samples=%d avg_abs=%d peak_abs=%d tone_level=%d",
             stat[0].sample_count, avg_abs[0], stat[0].peak_abs, tone_level[0]);
    ESP_LOGI(TAG_CODEC_DEV_UT, "ES7210 ref capture RE: samples=%d avg_abs=%d peak_abs=%d tone_level=%d",
             stat[1].sample_count, avg_abs[1], stat[1].peak_abs, tone_level[1]);
    ESP_LOGI(TAG_CODEC_DEV_UT, "ES7210 ref capture FR: samples=%d avg_abs=%d peak_abs=%d tone_level=%d",
             stat[2].sample_count, avg_abs[2], stat[2].peak_abs, tone_level[2]);
    bool has_samples = stat[0].sample_count > 0 && stat[1].sample_count > 0 && stat[2].sample_count > 0;
    bool ref_has_signal = avg_abs[1] > 100;
    bool ref_has_tone = tone_level[1] > 100;
    bool ref_not_clipped = stat[1].peak_abs < 32760;

    free(play_buf);
    free(record_buf);
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

    TEST_ASSERT_MESSAGE(has_samples, "No ES7210 reference samples analyzed");
    TEST_ASSERT_MESSAGE(ref_has_signal, "ES7210 middle reference channel is too quiet");
    TEST_ASSERT_MESSAGE(ref_has_tone, "ES7210 middle reference channel did not capture the played 1kHz tone");
    TEST_ASSERT_MESSAGE(ref_not_clipped, "ES7210 middle reference channel is clipped");
}

#if CONFIG_PM_ENABLE
static void esp_enable_pm_with_freq(int min_freq, int max_freq)
{
    esp_pm_config_t pm_config = {
        .max_freq_mhz = max_freq,
        .min_freq_mhz = min_freq,
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
}
#endif  /* CONFIG_PM_ENABLE */

static void test_codec_dev_using_s3_board(bool use_xtal)
{
#if SOC_I2S_SUPPORTS_TDM
    ut_set_i2s_mode(I2S_COMM_MODE_TDM, I2S_COMM_MODE_TDM);
#else
    ut_set_i2s_mode(I2S_COMM_MODE_STD, I2S_COMM_MODE_STD);
#endif  /* SOC_I2S_SUPPORTS_TDM */
    i2s_clock_src_t clk_src = I2S_CLK_SRC_DEFAULT;
    if (use_xtal) {
#if SOC_I2S_SUPPORTS_XTAL && CONFIG_PM_ENABLE
        clk_src = I2S_CLK_SRC_XTAL;
        esp_enable_pm_with_freq(40, 40);
#endif  /* SOC_I2S_SUPPORTS_XTAL && CONFIG_PM_ENABLE */
    } else {
        ut_i2s_set_rx_init_first(false);
    }
    // Need install driver (i2c and i2s) firstly
    int ret = ut_i2c_init(0, NULL);
    TEST_ESP_OK(ret);
    ret = ut_i2s_init(0, NULL, clk_src);
    TEST_ESP_OK(ret);
    // Do initialize of related interface: data_if, ctrl_if and gpio_if
    audio_codec_i2s_cfg_t i2s_cfg = {.port = 0};
    if (use_xtal) {
        i2s_cfg.clk_src = clk_src;
    }

    i2s_cfg.tx_handle = NULL;
    i2s_cfg.rx_handle = ut_i2s_get_rx_handle(0);
    const audio_codec_data_if_t *in_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    TEST_ASSERT_NOT_NULL(in_data_if);

    i2s_cfg.rx_handle = NULL;
    i2s_cfg.tx_handle = ut_i2s_get_tx_handle(0);
    const audio_codec_data_if_t *out_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    TEST_ASSERT_NOT_NULL(out_data_if);

    audio_codec_i2c_cfg_t i2c_cfg = {.addr = ES8311_CODEC_DEFAULT_ADDR};
    i2c_cfg.bus_handle = ut_i2c_get_bus_handle();
    const audio_codec_ctrl_if_t *out_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    TEST_ASSERT_NOT_NULL(out_ctrl_if);

    i2c_cfg.addr = ES7210_CODEC_DEFAULT_ADDR;
    const audio_codec_ctrl_if_t *in_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    TEST_ASSERT_NOT_NULL(in_ctrl_if);

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    TEST_ASSERT_NOT_NULL(gpio_if);
    // New output codec interface
    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = out_ctrl_if,
        .gpio_if = gpio_if,
        .sys_cfg = {
            .is_master = false,
            .no_mclk = false,
        },
        .pa_cfg = {
            .pa_pin = TEST_BOARD_PA_PIN,
            .pa_active_low = false,
        },
    };
    const audio_codec_if_t *out_codec_if = es8311_codec_new(&es8311_cfg);
    TEST_ASSERT_NOT_NULL(out_codec_if);
    // New input codec interface
    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = in_ctrl_if,
        .sys_cfg = {
            .is_master = false,
            .no_mclk = false,
        },
        .adc_cfg = {
            .label = "FL,FR,RE,NA",
        },
    };
    const audio_codec_if_t *in_codec_if = es7210_codec_new(&es7210_cfg);
    TEST_ASSERT_NOT_NULL(in_codec_if);
    // New output codec device
    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = out_codec_if,
        .data_if = out_data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    esp_codec_dev_handle_t play_dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(play_dev);
    // New input codec device
    dev_cfg.codec_if = in_codec_if;
    dev_cfg.data_if = in_data_if;
    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
    esp_codec_dev_handle_t record_dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(record_dev);

    ret = esp_codec_dev_set_out_vol(play_dev, TEST_CODEC_BOARD_OUT_VOL);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_set_in_gain(record_dev, TEST_CODEC_BOARD_IN_GAIN);
    TEST_ESP_OK(ret);

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 48000,
        .channel = 4,
        .bits_per_sample = 16,
        .mclk_multiple = ut_i2s_is_rx_init_first() ? 384 : 256,
        .channel_mask = 0x0F,
    };

    char label[16] = {0};
    /* Pre-open: I2S already init as 4-slot TDM on RX path */
    test_check_data_layout(record_dev, ESP_CODEC_DEV_CHANNEL_MAP(1, 3, 2, 4, 0, 0, 0, 0), "record before open");
    ret = esp_codec_dev_get_data_layout_label(record_dev, label, sizeof(label));
    TEST_ESP_OK(ret);
    TEST_ASSERT_EQUAL_STRING("FL,RE,FR,NA", label);
    test_check_data_layout_allow_not_support(play_dev, 0, "play before open");

    ret = esp_codec_dev_open(record_dev, &fs);
    TEST_ESP_OK(ret);
    test_check_data_layout(record_dev, ESP_CODEC_DEV_CHANNEL_MAP(1, 3, 2, 4, 0, 0, 0, 0), "record after open");
    ret = esp_codec_dev_get_data_layout_label(record_dev, label, sizeof(label));
    TEST_ESP_OK(ret);
    TEST_ASSERT_EQUAL_STRING("FL,RE,FR,NA", label);

    fs.channel = 2;
    fs.channel_mask = BIT(0) | BIT(1);
    ret = esp_codec_dev_open(play_dev, &fs);
    TEST_ESP_OK(ret);
    test_check_data_layout(play_dev, ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0), "play after open");

    int data_size = 240 * 3 * fs.channel * (fs.bits_per_sample >> 3);
    uint8_t *data = (uint8_t *)malloc(data_size);
    int limit_size = 10 * fs.sample_rate * fs.channel * (fs.bits_per_sample >> 3);
    int got_size = 0;

    audio_hw_alc_handle_t alc = NULL;
    audio_hw_alc_new(in_codec_if, &alc);
    if (alc != NULL) {
        int ret = 0;
        audio_alc_cfg_t alc_cfg = DEFAULT_ALC_CONFIG();
        ret |= audio_hw_alc_init(alc, &alc_cfg);
        ret |= audio_hw_alc_set_channel_mask(alc, BIT(1));
        ret |= audio_hw_alc_set_gain(alc, alc_cfg.target_gain);
        ret |= audio_hw_alc_set_channel_mask(alc, 0x00);  // Disable ALC
        TEST_ESP_OK(ret);
    }
    esp_codec_dev_channel_map_t invalid_order = {
        .value = ESP_CODEC_DEV_CHANNEL_MAP(9, 0, 0, 0, 0, 0, 0, 0),
    };
    ret = esp_codec_dev_set_data_layout(record_dev, &invalid_order);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, ret);

    /* Memory layout FL,RE => logical ch1->slot1, ch3->slot2 */
    esp_codec_dev_channel_map_t order = {
        .value = ESP_CODEC_DEV_CHANNEL_MAP(1, 3, 0, 0, 0, 0, 0, 0),
    };
    ret = esp_codec_dev_set_data_layout(record_dev, &order);
    TEST_ESP_OK(ret);
    test_check_data_layout(record_dev, ESP_CODEC_DEV_CHANNEL_MAP(1, 3, 0, 0, 0, 0, 0, 0), "record after set_data_layout");
    ret = esp_codec_dev_get_data_layout_label(record_dev, label, sizeof(label));
    TEST_ESP_OK(ret);
    TEST_ASSERT_EQUAL_STRING("FL,RE", label);
    order.value = ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0);
    ret = esp_codec_dev_set_data_layout(play_dev, &order);
    TEST_ESP_OK(ret);
    test_check_data_layout(play_dev, ESP_CODEC_DEV_CHANNEL_MAP(1, 2, 0, 0, 0, 0, 0, 0), "play after set_data_layout");

    // After I2S enable, data will be received, but codec not ready yet, so better wait for a while
    esp_codec_dev_sleep(200);
    // Playback the recording content directly
    while (got_size < limit_size) {
        ret = esp_codec_dev_read(record_dev, data, data_size);
        test_print_pcm_s16_head(data, 4);
        TEST_ESP_OK(ret);
        ret = esp_codec_dev_write(play_dev, data, data_size);
        TEST_ESP_OK(ret);
        int max_sample, min_sample;
        codec_max_sample(data, data_size, &max_sample, &min_sample);
        // Verify recording data not constant
        TEST_ASSERT(max_sample > min_sample);
        got_size += data_size;
    }
    free(data);

    ret = esp_codec_dev_close(play_dev);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_close(record_dev);
    TEST_ESP_OK(ret);
    esp_codec_dev_delete(play_dev);
    esp_codec_dev_delete(record_dev);

    if (alc != NULL) {
        audio_hw_alc_delete(alc);
    }

    // Delete codec interface
    audio_codec_delete_codec_if(in_codec_if);
    audio_codec_delete_codec_if(out_codec_if);
    // Delete codec control interface
    audio_codec_delete_ctrl_if(in_ctrl_if);
    audio_codec_delete_ctrl_if(out_ctrl_if);
    audio_codec_delete_gpio_if(gpio_if);
    // Delete codec data interface
    audio_codec_delete_data_if(in_data_if);
    audio_codec_delete_data_if(out_data_if);

    ut_i2c_deinit(0);
    ut_i2s_deinit(0);

#if CONFIG_PM_ENABLE
    if (clk_src == I2S_CLK_SRC_XTAL) {
        esp_enable_pm_with_freq(240, 240);
    }
#endif  /* CONFIG_PM_ENABLE */
}

static void test_case_record_play_overlap(void)
{
    // Need install driver (i2c and i2s) firstly
    int ret = ut_i2c_init(0, NULL);
    TEST_ESP_OK(ret);
    ret = ut_i2s_init(0, NULL, I2S_CLK_SRC_DEFAULT);
    TEST_ESP_OK(ret);
    // Do initialize of related interface: data_if, ctrl_if and gpio_if
    audio_codec_i2s_cfg_t i2s_cfg = {
        .rx_handle = ut_i2s_get_rx_handle(0),
        .tx_handle = ut_i2s_get_tx_handle(0),
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    TEST_ASSERT_NOT_NULL(data_if);

    audio_codec_i2c_cfg_t i2c_cfg = {.addr = ES8311_CODEC_DEFAULT_ADDR};
    i2c_cfg.bus_handle = ut_i2c_get_bus_handle();
    const audio_codec_ctrl_if_t *out_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    TEST_ASSERT_NOT_NULL(out_ctrl_if);

    i2c_cfg.addr = ES7210_CODEC_DEFAULT_ADDR;
    const audio_codec_ctrl_if_t *in_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    TEST_ASSERT_NOT_NULL(in_ctrl_if);

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    TEST_ASSERT_NOT_NULL(gpio_if);
    // New output codec interface
    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = out_ctrl_if,
        .gpio_if = gpio_if,
        .sys_cfg = {
            .is_master = false,
            .no_mclk = false,
        },
        .pa_cfg = {
            .pa_pin = TEST_BOARD_PA_PIN,
            .pa_active_low = false,
        },
    };
    const audio_codec_if_t *out_codec_if = es8311_codec_new(&es8311_cfg);
    TEST_ASSERT_NOT_NULL(out_codec_if);
    // New input codec interface
    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = in_ctrl_if,
        .sys_cfg = {
            .is_master = false,
            .no_mclk = false,
        },
        .adc_cfg = {
            .label = "FL,FR,RE,NA",
        },
    };
    const audio_codec_if_t *in_codec_if = es7210_codec_new(&es7210_cfg);
    TEST_ASSERT_NOT_NULL(in_codec_if);
    // New output codec device
    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = out_codec_if,
        .data_if = data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    esp_codec_dev_handle_t play_dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(play_dev);
    // New input codec device
    dev_cfg.codec_if = in_codec_if;
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
    };

    int limit_size = 5 * fs.sample_rate * fs.channel * (fs.bits_per_sample >> 3);
    int size = 512;
    uint8_t *data = (uint8_t *)malloc(size);
    TEST_ASSERT_NOT_NULL(data);
    // Test playback continuous and record interrupt
    ESP_LOGI(TAG_CODEC_DEV_UT, "Test for playback continuous and record interrupt");
    ret = esp_codec_dev_open(play_dev, &fs);
    TEST_ESP_OK(ret);
    int loop_count = 5;
    for (int i = 0; i < loop_count; i++) {
        ESP_LOGI(TAG_CODEC_DEV_UT, "Loop %d/%d start", i, loop_count);
        int got_size = 0;
        ret = esp_codec_dev_open(record_dev, &fs);
        TEST_ESP_OK(ret);
        // The data of record will have pop issue when enable again, so need to wait for a while
        esp_codec_dev_sleep(150);
        int count = 0;
        while (got_size < limit_size) {
            ret = esp_codec_dev_read(record_dev, data, size);
            TEST_ESP_OK(ret);
            if (count++ <= 20) {
                test_print_pcm_s16_head(data, 8);
            }
            ret = esp_codec_dev_write(play_dev, data, size);
            TEST_ESP_OK(ret);
            got_size += size;
        }
        ret = esp_codec_dev_close(record_dev);
        TEST_ESP_OK(ret);
        memset(data, 0, size);
        ret = esp_codec_dev_write(play_dev, data, size);
        TEST_ESP_OK(ret);
        ESP_LOGI(TAG_CODEC_DEV_UT, "Loop %d/%d end\n", i, loop_count);
    }
    ret = esp_codec_dev_close(play_dev);
    TEST_ESP_OK(ret);

    ESP_LOGI(TAG_CODEC_DEV_UT, "Test for record continuous and play interrupt");
    ret = esp_codec_dev_open(record_dev, &fs);
    TEST_ESP_OK(ret);
    for (int i = 0; i < loop_count; i++) {
        ESP_LOGI(TAG_CODEC_DEV_UT, "Loop %d/%d start", i, loop_count);
        int got_size = 0;
        ret = esp_codec_dev_open(play_dev, &fs);
        TEST_ESP_OK(ret);
        while (got_size < limit_size) {
            ret = esp_codec_dev_read(record_dev, data, size);
            TEST_ESP_OK(ret);
            ret = esp_codec_dev_write(play_dev, data, size);
            TEST_ESP_OK(ret);
            got_size += size;
        }
        ret = esp_codec_dev_close(play_dev);
        TEST_ESP_OK(ret);

        memset(data, 0, size);
        ret = esp_codec_dev_read(record_dev, data, size);
        TEST_ESP_OK(ret);
        int max_sample, min_sample;
        codec_max_sample(data, size, &max_sample, &min_sample);
        // Verify recording data not constant
        TEST_ASSERT(max_sample > min_sample);
        ESP_LOGI(TAG_CODEC_DEV_UT, "Loop %d/%d end\n", i, loop_count);
    }
    ret = esp_codec_dev_close(record_dev);
    TEST_ESP_OK(ret);
    free(data);

    esp_codec_dev_delete(play_dev);
    esp_codec_dev_delete(record_dev);

    // Delete codec interface
    audio_codec_delete_codec_if(in_codec_if);
    audio_codec_delete_codec_if(out_codec_if);
    // Delete codec control interface
    audio_codec_delete_ctrl_if(in_ctrl_if);
    audio_codec_delete_ctrl_if(out_ctrl_if);
    audio_codec_delete_gpio_if(gpio_if);
    // Delete codec data interface
    audio_codec_delete_data_if(data_if);

    ut_i2c_deinit(0);
    ut_i2s_deinit(0);
}

static void test_case_playing_while_recording_use_tdm_mode(void)
{
#if SOC_I2S_SUPPORTS_TDM
    ut_set_i2s_mode(I2S_COMM_MODE_STD, I2S_COMM_MODE_TDM);
#else
    TEST_ESP_OK(-1);
#endif  /* SOC_I2S_SUPPORTS_TDM */
    // Need install driver (i2c and i2s) firstly
    int ret = ut_i2c_init(0, NULL);
    TEST_ESP_OK(ret);
    ret = ut_i2s_init(0, NULL, I2S_CLK_SRC_DEFAULT);
    TEST_ESP_OK(ret);
    // Do initialize of related interface: data_if, ctrl_if and gpio_if
    audio_codec_i2s_cfg_t i2s_cfg = {
        .rx_handle = ut_i2s_get_rx_handle(0),
        .tx_handle = ut_i2s_get_tx_handle(0),
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    TEST_ASSERT_NOT_NULL(data_if);

    audio_codec_i2c_cfg_t i2c_cfg = {.addr = ES8311_CODEC_DEFAULT_ADDR};
    i2c_cfg.bus_handle = ut_i2c_get_bus_handle();
    const audio_codec_ctrl_if_t *out_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    TEST_ASSERT_NOT_NULL(out_ctrl_if);

    i2c_cfg.addr = ES7210_CODEC_DEFAULT_ADDR;
    const audio_codec_ctrl_if_t *in_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    TEST_ASSERT_NOT_NULL(in_ctrl_if);

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    TEST_ASSERT_NOT_NULL(gpio_if);
    // New output codec interface via audio_codec_new() common cfg
    audio_codec_cfg_t es8311_common_cfg = {
        .ctrl_if = out_ctrl_if,
        .gpio_if = gpio_if,
        .sys_cfg = {
            .is_master = false,
            .no_mclk = false,
        },
        .pa_cfg = {
            .pa_pin = TEST_BOARD_PA_PIN,
            .pa_active_low = false,
        },
    };
    const audio_codec_if_t *out_codec_if =
        audio_codec_new("es8311", &es8311_common_cfg, sizeof(es8311_common_cfg));
    TEST_ASSERT_NOT_NULL(out_codec_if);
    // New input codec interface via audio_codec_new() common cfg
    audio_codec_cfg_t es7210_common_cfg = {
        .ctrl_if = in_ctrl_if,
        .sys_cfg = {
            .is_master = false,
            .no_mclk = false,
        },
        .adc_cfg = {
            .label = "FL,FR,RE,NA",
        },
    };
    const audio_codec_if_t *in_codec_if =
        audio_codec_new("es7210", &es7210_common_cfg, sizeof(es7210_common_cfg));
    TEST_ASSERT_NOT_NULL(in_codec_if);
    // New output codec device
    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = out_codec_if,
        .data_if = data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    esp_codec_dev_handle_t play_dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(play_dev);
    // New input codec device
    dev_cfg.codec_if = in_codec_if;
    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
    esp_codec_dev_handle_t record_dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(record_dev);

    ret = esp_codec_dev_set_out_vol(play_dev, TEST_CODEC_BOARD_OUT_VOL);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_set_in_gain(record_dev, TEST_CODEC_BOARD_IN_GAIN);
    TEST_ESP_OK(ret);
    // Play 16bits 2 channel
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 48000,
        .channel = 2,
        .bits_per_sample = 16,
    };
    ret = esp_codec_dev_open(play_dev, &fs);
    TEST_ESP_OK(ret);
    // Record 16bits 4 channel select channel 0 and 3
    fs.channel = 4;
    fs.channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(3);
    ret = esp_codec_dev_open(record_dev, &fs);
    TEST_ESP_OK(ret);
    uint8_t *data = (uint8_t *)malloc(512);
    int limit_size = 10 * fs.sample_rate * fs.channel * (fs.bits_per_sample >> 3);
    int got_size = 0;
    // Playback the recording content directly
    while (got_size < limit_size) {
        ret = esp_codec_dev_read(record_dev, data, 512);
        TEST_ESP_OK(ret);
        ret = esp_codec_dev_write(play_dev, data, 512);
        TEST_ESP_OK(ret);
        int max_sample, min_sample;
        codec_max_sample(data, 512, &max_sample, &min_sample);
        // Verify recording data not constant
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

    // Delete codec interface
    audio_codec_delete_codec_if(in_codec_if);
    audio_codec_delete_codec_if(out_codec_if);
    // Delete codec control interface
    audio_codec_delete_ctrl_if(in_ctrl_if);
    audio_codec_delete_ctrl_if(out_ctrl_if);
    audio_codec_delete_gpio_if(gpio_if);
    // Delete codec data interface
    audio_codec_delete_data_if(data_if);

    ut_i2c_deinit(0);
    ut_i2s_deinit(0);
    ut_clr_i2s_mode();
}

TEST_CASE("Record play overlap test", "[korvo2_v3][duplex]")
{
    test_case_record_play_overlap();
}

TEST_CASE("Playing while recording use TDM mode", "[korvo2_v3][duplex]")
{
    test_case_playing_while_recording_use_tdm_mode();
}

TEST_CASE("esp codec dev test using S3 board", "[korvo2_v3][duplex]")
{
    test_codec_dev_using_s3_board(false);
}

TEST_CASE("ES8311 output captured by ES7210 reference channel on S3 board", "[korvo2_v3][loopback]")
{
    test_codec_dev_using_s3_board_es7210_ref_signal();
}

#if SOC_I2S_SUPPORTS_XTAL && CONFIG_PM_ENABLE
TEST_CASE("esp codec dev test using S3 board with XTAL", "[korvo2_v3][duplex]")
{
    test_codec_dev_using_s3_board(true);
}
#endif  /* SOC_I2S_SUPPORTS_XTAL && CONFIG_PM_ENABLE */
