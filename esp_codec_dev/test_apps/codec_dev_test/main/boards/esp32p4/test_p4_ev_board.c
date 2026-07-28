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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_idf_version.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "unity.h"

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "audio_codec_if.h"
#include "test_board.h"
#include "test_board_periph.h"
#include "test_codec_print.h"

static const char *TAG = "CODEC_DEV_P4_EV_BOARD";

/* External MCLK pin for multi-codec tests (same GPIO as LP_I2S MCK on P4 EV board). */
#define TEST_BOARD_LP_I2S_MCK_PIN  (13)

typedef struct {
    const audio_codec_data_if_t *data_if;
    const audio_codec_ctrl_if_t *ctrl_if;
    const audio_codec_gpio_if_t *gpio_if;
    const audio_codec_if_t      *codec_if;
    esp_codec_dev_handle_t       codec_dev;
} codec_es8311_inst_t;

typedef struct {
    int64_t  abs_sum;
    int64_t  corr[16];
    int      sample_count;
    int      peak_abs;
} loopback_channel_stat_t;

typedef struct {
    const char                    *name;
    const char                    *listen_note;
    uint16_t                       count;
    const esp_codec_dev_vol_map_t *vol_map;
} volume_curve_case_t;

static const esp_codec_dev_vol_map_t linear_like_curve[] = {
    {.vol = 1, .db_value = -49.5f},
    {.vol = 33, .db_value = -33.5f},
    {.vol = 66, .db_value = -17.0f},
    {.vol = 100, .db_value = 0.0f},
};

static const esp_codec_dev_vol_map_t balanced_exp_curve[] = {
    {.vol = 1, .db_value = -58.0f},
    {.vol = 10, .db_value = -54.0f},
    {.vol = 30, .db_value = -44.0f},
    {.vol = 50, .db_value = -31.0f},
    {.vol = 70, .db_value = -18.0f},
    {.vol = 90, .db_value = -6.0f},
    {.vol = 100, .db_value = 0.0f},
};

static const esp_codec_dev_vol_map_t strong_exp_curve[] = {
    {.vol = 1, .db_value = -64.0f},
    {.vol = 10, .db_value = -60.0f},
    {.vol = 30, .db_value = -50.0f},
    {.vol = 50, .db_value = -36.0f},
    {.vol = 70, .db_value = -20.0f},
    {.vol = 90, .db_value = -7.0f},
    {.vol = 100, .db_value = 0.0f},
};

static const volume_curve_case_t volume_curve_cases[] = {
    {
        .name = "linear_like",
        .listen_note = "reference curve; compare with current default feel",
        .vol_map = linear_like_curve,
        .count = sizeof(linear_like_curve) / sizeof(linear_like_curve[0]),
    },
    {
        .name = "balanced_exp",
        .listen_note = "listen for finer low-level control and faster upper-end lift",
        .vol_map = balanced_exp_curve,
        .count = sizeof(balanced_exp_curve) / sizeof(balanced_exp_curve[0]),
    },
    {
        .name = "strong_exp",
        .listen_note = "listen for stronger contrast between mid and high volumes",
        .vol_map = strong_exp_curve,
        .count = sizeof(strong_exp_curve) / sizeof(strong_exp_curve[0]),
    },
};

static int multiple_es8311_init_i2s(bool separate_channels, i2s_role_t tx_role, i2s_role_t rx_role,
                                    bool use_external_mclk);

int init_es8311_inst(codec_es8311_inst_t *inst, bool playback, const audio_codec_data_if_t *data_if,
                     bool codec_is_master, bool no_mclk)
{
    if (data_if == NULL) {
        // Make sure that only assign one handle if not shared data_if
        audio_codec_i2s_cfg_t i2s_cfg = {
            .rx_handle = playback == false ? ut_i2s_get_rx_handle(0) : NULL,
            .tx_handle = playback ? ut_i2s_get_tx_handle(0) : NULL,
        };
        inst->data_if = audio_codec_new_i2s_data(&i2s_cfg);
        TEST_ASSERT_NOT_NULL(inst->data_if);
        data_if = inst->data_if;
    }

    audio_codec_i2c_cfg_t i2c_cfg = {.addr = ES8311_CODEC_DEFAULT_ADDR};
    i2c_cfg.bus_handle = ut_i2c_get_bus_handle();
    inst->ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    TEST_ASSERT_NOT_NULL(inst->ctrl_if);

    inst->gpio_if = audio_codec_new_gpio();
    TEST_ASSERT_NOT_NULL(inst->gpio_if);
    // New output codec interface
    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = inst->ctrl_if,
        .gpio_if = inst->gpio_if,
        .sys_cfg = {
            .is_master = codec_is_master,
            .no_mclk = no_mclk,
        },
        .adc_cfg = {
            .label = "FL,RE",
        },
        .dac_cfg = {
            .ref_enable = true,
        },
        .pa_cfg = {
#if CONFIG_IDF_TARGET_ESP32P4
            .pa_pin = 53,
#else
            .pa_pin = 46,
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
            .pa_active_low = false,
        },
    };
    inst->codec_if = es8311_codec_new(&es8311_cfg);
    TEST_ASSERT_NOT_NULL(inst->codec_if);
    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = inst->codec_if,
        .data_if = data_if,
        .dev_type = playback ? ESP_CODEC_DEV_TYPE_OUT : ESP_CODEC_DEV_TYPE_IN,
    };
    inst->codec_dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(inst->codec_dev);

    esp_codec_dev_type_t caps_type = dev_cfg.dev_type;
    esp_codec_dev_capability_t caps[1] = {0};
    int count = 1;
    TEST_ESP_OK(esp_codec_dev_get_caps(inst->codec_dev, caps, &count));
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL(caps_type, caps[0].dev_type);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_CAPS_MODE_FLEXIBLE, caps[0].mode);
    if (caps_type == ESP_CODEC_DEV_TYPE_IN) {
        TEST_ASSERT_EQUAL(2, caps[0].flexible.max_channels);
    } else {
        TEST_ASSERT_EQUAL(1, caps[0].flexible.max_channels);
    }
    TEST_ASSERT_EQUAL(3, caps[0].flexible.bits_num);
    TEST_ASSERT_EQUAL(16, caps[0].flexible.bits_per_sample[0]);
    TEST_ASSERT_EQUAL(24, caps[0].flexible.bits_per_sample[1]);
    TEST_ASSERT_EQUAL(32, caps[0].flexible.bits_per_sample[2]);
    TEST_ASSERT_EQUAL(12, caps[0].flexible.sample_rate_num);
    TEST_ASSERT_EQUAL(8000, caps[0].flexible.sample_rates[0]);
    TEST_ASSERT_EQUAL(48000, caps[0].flexible.sample_rates[8]);
    TEST_ASSERT_EQUAL(96000, caps[0].flexible.sample_rates[11]);
    return 0;
}

void deinit_es8311_inst(codec_es8311_inst_t *inst)
{
    if (inst->codec_dev) {
        esp_codec_dev_delete(inst->codec_dev);
        inst->codec_dev = NULL;
    }
    if (inst->codec_if) {
        audio_codec_delete_codec_if(inst->codec_if);
        inst->codec_if = NULL;
    }
    if (inst->gpio_if) {
        audio_codec_delete_gpio_if(inst->gpio_if);
        inst->gpio_if = NULL;
    }
    if (inst->ctrl_if) {
        audio_codec_delete_ctrl_if(inst->ctrl_if);
        inst->ctrl_if = NULL;
    }
    if (inst->data_if) {
        audio_codec_delete_data_if(inst->data_if);
        inst->data_if = NULL;
    }
}

static void verify_record_label_layout(esp_codec_dev_handle_t record_dev)
{
    char label[16] = {0};
    int ret = esp_codec_dev_get_data_layout_label(record_dev, label, sizeof(label));
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_OK, ret);
    TEST_ASSERT_EQUAL_STRING("FL,RE", label);
    ret = esp_codec_dev_get_data_layout_label(record_dev, label, 3);
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_INVALID_ARG, ret);
    ret = esp_codec_dev_set_data_layout_label(record_dev, "");
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_INVALID_ARG, ret);
    ret = esp_codec_dev_set_data_layout_label(record_dev, "FL,FL");
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_INVALID_ARG, ret);

    ret = esp_codec_dev_set_data_layout_label(record_dev, "RE,FL");
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_OK, ret);
    esp_codec_dev_channel_map_t order = {0};
    ret = esp_codec_dev_get_data_layout(record_dev, &order);
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_OK, ret);
    TEST_ASSERT_EQUAL_HEX32(ESP_CODEC_DEV_CHANNEL_MAP(2, 1, 0, 0, 0, 0, 0, 0), order.value);
}

static void fill_dacr_loopback_tone(int16_t *data, int frame_count, int scale)
{
    static const int16_t sine_1k_16k[16] = {
        0, 6270, 11585, 15137, 16384, 15137, 11585, 6270,
        0, -6270, -11585, -15137, -16384, -15137, -11585, -6270,
    };
    for (int i = 0; i < frame_count; i++) {
        int16_t sample = (int16_t)((sine_1k_16k[i & 0x0F] * scale) / 100);
        data[i * 2] = sample;
        data[i * 2 + 1] = sample;
    }
}

static void analyze_dacr_loopback_capture(const int16_t *data, int frame_count, int frame_offset,
                                          loopback_channel_stat_t stat[2])
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

static float volume_curve_get_db(const esp_codec_dev_vol_map_t *vol_map, int count, int volume)
{
    if (volume <= 0) {
        return -96.0f;
    }
    if (count <= 0 || vol_map == NULL) {
        return 0.0f;
    }
    if (volume >= vol_map[count - 1].vol) {
        return vol_map[count - 1].db_value;
    }
    for (int i = 0; i < count - 1; i++) {
        if (volume < vol_map[i + 1].vol) {
            if (vol_map[i].vol != vol_map[i + 1].vol) {
                float ratio = (vol_map[i + 1].db_value - vol_map[i].db_value) /
                              (vol_map[i + 1].vol - vol_map[i].vol);
                return vol_map[i].db_value + (volume - vol_map[i].vol) * ratio;
            }
            break;
        }
    }
    return vol_map[count - 1].db_value;
}

static void multiple_es8311_play_volume_curve_run(void)
{
    int ret = multiple_es8311_init_i2s(false, I2S_ROLE_MASTER, I2S_ROLE_MASTER, false);
    TEST_ESP_OK(ret);

    codec_es8311_inst_t play_inst = {};
    ret = init_es8311_inst(&play_inst, true, NULL, false, true);
    TEST_ESP_OK(ret);

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = 0x03,
        .mclk_multiple = 256,
    };
    ret = esp_codec_dev_open(play_inst.codec_dev, &fs);
    TEST_ESP_OK(ret);

    const int chunk_frames = 1024;
    const int chunk_bytes = chunk_frames * fs.channel * (fs.bits_per_sample >> 3);
    const int repeat_per_step = 6;
    int16_t *play_buf = (int16_t *)malloc(chunk_bytes);
    TEST_ASSERT_NOT_NULL(play_buf);
    fill_dacr_loopback_tone(play_buf, chunk_frames, 100);

    for (size_t curve_idx = 0; curve_idx < sizeof(volume_curve_cases) / sizeof(volume_curve_cases[0]); curve_idx++) {
        const volume_curve_case_t *curve_case = &volume_curve_cases[curve_idx];
        esp_codec_dev_vol_curve_t curve = {
            .vol_map = (esp_codec_dev_vol_map_t *)curve_case->vol_map,
            .count = curve_case->count,
        };
        ret = esp_codec_dev_set_vol_curve(play_inst.codec_dev, &curve);
        TEST_ESP_OK(ret);
        ESP_LOGI(TAG, "Manual volume curve check: curve=%s, note=%s",
                 curve_case->name, curve_case->listen_note);
        for (int volume = 0; volume <= 100; volume += 10) {
            float db_value = volume_curve_get_db(curve_case->vol_map, curve_case->count, volume);
            ret = esp_codec_dev_set_out_vol(play_inst.codec_dev, volume);
            TEST_ESP_OK(ret);
            ESP_LOGI(TAG, "curve=%s volume=%d db=%.1f",
                     curve_case->name, volume, (double)db_value);
            for (int repeat = 0; repeat < repeat_per_step; repeat++) {
                ret = esp_codec_dev_write(play_inst.codec_dev, play_buf, chunk_bytes);
                TEST_ESP_OK(ret);
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    free(play_buf);
    ret = esp_codec_dev_close(play_inst.codec_dev);
    TEST_ESP_OK(ret);
    deinit_es8311_inst(&play_inst);
    ut_i2c_deinit(0);
    ut_i2s_deinit(0);
}

static void multiple_es8311_dacr_loopback_signal_run(void)
{
    int ret = multiple_es8311_init_i2s(false, I2S_ROLE_MASTER, I2S_ROLE_MASTER, false);
    TEST_ESP_OK(ret);

    codec_es8311_inst_t play_inst = {};
    ret = init_es8311_inst(&play_inst, true, NULL, false, true);
    TEST_ESP_OK(ret);
    codec_es8311_inst_t record_inst = {};
    ret = init_es8311_inst(&record_inst, false, NULL, false, true);
    TEST_ESP_OK(ret);

    ret = esp_codec_dev_set_out_vol(play_inst.codec_dev, 90);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_set_in_gain(record_inst.codec_dev, TEST_CODEC_BOARD_IN_GAIN);
    TEST_ESP_OK(ret);

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = 0x03,
        .mclk_multiple = 256,
    };
    ret = esp_codec_dev_open(record_inst.codec_dev, &fs);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_open(play_inst.codec_dev, &fs);
    TEST_ESP_OK(ret);

    const int chunk_frames = 1024;
    const int chunk_bytes = chunk_frames * fs.channel * (fs.bits_per_sample >> 3);
    int16_t *play_buf = (int16_t *)malloc(chunk_bytes);
    int16_t *record_buf = (int16_t *)malloc(chunk_bytes);
    TEST_ASSERT_NOT_NULL(play_buf);
    TEST_ASSERT_NOT_NULL(record_buf);
    fill_dacr_loopback_tone(play_buf, chunk_frames, 30);

    int frame_offset = 0;
    loopback_channel_stat_t stat[2] = {0};
    const int total_frames = fs.sample_rate * 3;
    const int warmup_frames = fs.sample_rate / 2;
    ESP_LOGI(TAG, "Start DACR loopback capture: play 1kHz tone on right channel, analyze both ADC channels");
    while (frame_offset < total_frames) {
        ret = esp_codec_dev_write(play_inst.codec_dev, play_buf, chunk_bytes);
        TEST_ESP_OK(ret);
        ret = esp_codec_dev_read(record_inst.codec_dev, record_buf, chunk_bytes);
        TEST_ESP_OK(ret);
        if (frame_offset >= warmup_frames) {
            analyze_dacr_loopback_capture(record_buf, chunk_frames, frame_offset - warmup_frames, stat);
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
    ESP_LOGI(TAG, "DACR loopback capture ch1: samples=%d avg_abs=%d peak_abs=%d tone_level=%d",
             stat[0].sample_count, avg_abs[0], stat[0].peak_abs, tone_level[0]);
    ESP_LOGI(TAG, "DACR loopback capture ch2: samples=%d avg_abs=%d peak_abs=%d tone_level=%d",
             stat[1].sample_count, avg_abs[1], stat[1].peak_abs, tone_level[1]);
    bool has_samples = stat[0].sample_count > 0 && stat[1].sample_count > 0;
    bool dacr_has_signal = avg_abs[1] > 100;
    bool dacr_has_tone = tone_level[1] > 100;
    bool dacr_not_clipped = stat[1].peak_abs < 32760;

    free(play_buf);
    free(record_buf);
    ret = esp_codec_dev_close(record_inst.codec_dev);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_close(play_inst.codec_dev);
    TEST_ESP_OK(ret);
    deinit_es8311_inst(&play_inst);
    deinit_es8311_inst(&record_inst);
    ut_i2c_deinit(0);
    ut_i2s_deinit(0);

    TEST_ASSERT_MESSAGE(has_samples, "No DACR loopback samples analyzed");
    TEST_ASSERT_MESSAGE(dacr_has_signal, "ADC second channel is too quiet for DACR loopback");
    TEST_ASSERT_MESSAGE(dacr_has_tone, "ADC second channel did not capture the played 1kHz DACR tone");
    TEST_ASSERT_MESSAGE(dacr_not_clipped, "DACR loopback capture is clipped");
}

static int multiple_es8311_init_i2s(bool separate_channels, i2s_role_t tx_role, i2s_role_t rx_role,
                                    bool use_external_mclk)
{
    ut_set_i2s_mode(I2S_COMM_MODE_STD, I2S_COMM_MODE_STD);
    int ret = 0;
#if CONFIG_IDF_TARGET_ESP32P4
    codec_i2c_pin_t i2c_pin = {
        .sda = 7,
        .scl = 8,
    };
    ret = ut_i2c_init(0, &i2c_pin);
    TEST_ESP_OK(ret);
    codec_i2s_pin_t i2s_pin = {
        .mclk = use_external_mclk ? -1 : 13,
        .bclk = 12,
        .ws = 10,
        .dout = 9,
        .din = 11,
    };
#else
    codec_i2c_pin_t i2c_pin = {
        .sda = 17,
        .scl = 18,
    };
    ret = ut_i2c_init(0, &i2c_pin);
    TEST_ESP_OK(ret);
    codec_i2s_pin_t i2s_pin = {
        .mclk = -1,
        .bclk = 9,
        .ws   = 45,
        .dout = 8,
        .din  = 10,
    };
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
    if (separate_channels) {
        ret = ut_i2s_init_single_channel(0, I2S_DIR_TX, tx_role);
        TEST_ESP_OK(ret);
        ret = ut_i2s_init_tx_phase(0, &i2s_pin, I2S_CLK_SRC_DEFAULT);
        TEST_ESP_OK(ret);
        ret = ut_i2s_init_single_channel(0, I2S_DIR_RX, rx_role);
        TEST_ESP_OK(ret);
        ret = ut_i2s_init_rx_phase(0, &i2s_pin, I2S_CLK_SRC_DEFAULT);
        TEST_ESP_OK(ret);
    } else {
        ret = ut_i2s_init(0, &i2s_pin, I2S_CLK_SRC_DEFAULT);
        TEST_ESP_OK(ret);
    }
    return ret;
}

static void multiple_es8311_run(bool reuse_data_if, bool separate_channels, i2s_role_t tx_role,
                                i2s_role_t rx_role, int sample_rate, bool codec_is_master, bool use_external_mclk)
{
    int ret = 0;
    bool no_mclk = codec_is_master ? false : true;
    if (use_external_mclk) {
        ret = ut_ledc_output_pwm(sample_rate * 256, TEST_BOARD_LP_I2S_MCK_PIN);
        TEST_ESP_OK(ret);
    }

    ret = multiple_es8311_init_i2s(separate_channels, tx_role, rx_role, use_external_mclk);
    TEST_ESP_OK(ret);

    const audio_codec_data_if_t *data_if = NULL;
    if (reuse_data_if) {
        audio_codec_i2s_cfg_t i2s_cfg = {
            .rx_handle = ut_i2s_get_rx_handle(0),
            .tx_handle = ut_i2s_get_tx_handle(0),
        };
        data_if = audio_codec_new_i2s_data(&i2s_cfg);
        TEST_ASSERT_NOT_NULL(data_if);
    }

    codec_es8311_inst_t play_inst = {};
    ret = init_es8311_inst(&play_inst, true, data_if, codec_is_master, no_mclk);
    TEST_ESP_OK(ret);
    codec_es8311_inst_t record_inst = {};
    ret = init_es8311_inst(&record_inst, false, data_if, codec_is_master, no_mclk);
    TEST_ESP_OK(ret);

    ret = esp_codec_dev_set_out_vol(play_inst.codec_dev, TEST_CODEC_BOARD_OUT_VOL);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_set_in_gain(record_inst.codec_dev, TEST_CODEC_BOARD_IN_GAIN);
    TEST_ESP_OK(ret);

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = sample_rate,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = 0x03,  // If get adc+dacR, need write data to dacR, defaults is dacL
        .mclk_multiple = 256,
    };
    // 64k memory to save recording data
    int limit_size = 5 * fs.sample_rate * fs.channel * (fs.bits_per_sample >> 3);
    uint8_t *data = (uint8_t *)malloc(limit_size);
    TEST_ASSERT_NOT_NULL(data);
    ret = esp_codec_dev_open(record_inst.codec_dev, &fs);
    ret = esp_codec_dev_open(play_inst.codec_dev, &fs);
    esp_codec_dev_close(record_inst.codec_dev);
    esp_codec_dev_close(play_inst.codec_dev);

    int each_size = 512;
    int read_count = limit_size / each_size;
    // Test playback continuous and record interrupt
    ESP_LOGI(TAG, "1: Record only test");
    ret = esp_codec_dev_open(record_inst.codec_dev, &fs);
    TEST_ESP_OK(ret);
    verify_record_label_layout(record_inst.codec_dev);
    int read_size = 0;
    for (int i = 0; i < read_count; i++, read_size += each_size) {
        ret = esp_codec_dev_read(record_inst.codec_dev, data + read_size, each_size);
        TEST_ESP_OK(ret);
        test_print_pcm_s16_head(data + read_size, 4);
    }

    ESP_LOGI(TAG, "\n\n");
    ESP_LOGI(TAG, "2: Play record");
    ret = esp_codec_dev_open(play_inst.codec_dev, &fs);
    TEST_ESP_OK(ret);
    read_size = 0;
    for (int i = 0; i < read_count; i++, read_size += each_size) {
        ret = esp_codec_dev_write(play_inst.codec_dev, data + read_size, each_size);
        TEST_ESP_OK(ret);
        if (i == (read_count / 2)) {
            ESP_LOGI(TAG, "Close recording during playback, make sure playback OK");
            esp_codec_dev_close(record_inst.codec_dev);
        }
    }
    ESP_LOGI(TAG, "\n\n");
    ESP_LOGI(TAG, "3: Open record during playback");
    ret = esp_codec_dev_open(record_inst.codec_dev, &fs);
    TEST_ESP_OK(ret);
    esp_rom_delay_us(1000 * 1000);  // Data will not stable if ADC power down and power up immediately
    read_size = 0;
    bool is_playback = true;
    for (int i = 0; i < read_count; i++, read_size += each_size) {
        ret = esp_codec_dev_read(record_inst.codec_dev, data + read_size, each_size);
        TEST_ESP_OK(ret);
        int max_sample, min_sample;
        test_print_pcm_s16_head(data + read_size, 4);
        codec_max_sample(data + read_size, each_size, &max_sample, &min_sample);
        // Verify recording data not constant
        TEST_ASSERT(max_sample > min_sample);
        if (fs.channel_mask == 0x03) {
            uint16_t *data16 = (uint16_t *)(data + read_size);
            for (int j = 0; j < each_size / 4 / 2; j++) {
                data16[j * 2 + 1] = data16[j * 2];
            }
        }
        if (is_playback) {
            ret = esp_codec_dev_write(play_inst.codec_dev, data + read_size, each_size);
            TEST_ESP_OK(ret);
        }
        if (i == (read_count / 2)) {
            ESP_LOGI(TAG, "Close playback during recording, make sure recording OK");
            esp_codec_dev_close(play_inst.codec_dev);
            is_playback = false;
        }
    }
    ret = esp_codec_dev_close(record_inst.codec_dev);
    TEST_ESP_OK(ret);

    free(data);
    deinit_es8311_inst(&play_inst);
    deinit_es8311_inst(&record_inst);
    if (data_if) {
        audio_codec_delete_data_if(data_if);
    }
    ut_i2c_deinit(0);
    ut_i2s_deinit(0);
    if (use_external_mclk) {
        ret = ut_ledc_deinit(TEST_BOARD_LP_I2S_MCK_PIN);
        TEST_ESP_OK(ret);
    }
}

// TEST_CASE("Multiple es8311 codec with separate TX/RX slave and codec master with external MCLK test use P4_EV_BOARD 16000Hz", "[p4_ev][multi_codec]")
// {
//     // Drive external MCLK with LEDC, let codec output BCLK/WS, and keep TX/RX channels in slave mode.
//     multiple_es8311_run(false, true, I2S_ROLE_SLAVE, I2S_ROLE_SLAVE, 16000, true, true);
// }

// TEST_CASE("Multiple es8311 codec with separate TX/RX slave and codec master without external MCLK test use P4_EV_BOARD 16000Hz", "[p4_ev][multi_codec]")
// {
//     // Keep TX/RX channels in slave mode and let codec run as master without driving external MCLK.
//     multiple_es8311_run(false, true, I2S_ROLE_SLAVE, I2S_ROLE_SLAVE, 16000, true, false);
// }

// FIXME(ES8311): Right channel silent after record close->open at 8 kHz; root cause TBD
TEST_CASE("Multiple es8311 codec test use P4_EV_BOARD 8000Hz", "[p4_ev][multi_codec]")
{
    // This test code only test multiple codec all related interface not shared
    // Actually can use share data_if, codec_if, gpio_if to save memory
    multiple_es8311_run(false, false, I2S_ROLE_MASTER, I2S_ROLE_MASTER, 8000, false, false);
}

TEST_CASE("Multiple es8311 codec reuse data_if test use P4_EV_BOARD 8000Hz", "[p4_ev][multi_codec]")
{
    // This test code only test multiple codec only share data_if
    // Actually can use share data_if, codec_if, gpio_if to save memory
    multiple_es8311_run(true, false, I2S_ROLE_MASTER, I2S_ROLE_MASTER, 8000, false, false);
}

TEST_CASE("Multiple es8311 codec reuse data_if test use P4_EV_BOARD 16000Hz", "[p4_ev][multi_codec]")
{
    // This test code only test multiple codec all related interface not shared
    // Actually can use share data_if, codec_if, gpio_if to save memory
    multiple_es8311_run(true, false, I2S_ROLE_MASTER, I2S_ROLE_MASTER, 16000, false, false);
}

TEST_CASE("ES8311 DACR loopback capture test use P4_EV_BOARD 16000Hz", "[p4_ev][loopback]")
{
    multiple_es8311_dacr_loopback_signal_run();
}

TEST_CASE("ES8311 playback volume curve manual check use P4_EV_BOARD 16000Hz", "[p4_ev][vol_curve][manual]")
{
    multiple_es8311_play_volume_curve_run();
}

TEST_CASE("Multiple es8311 codec with separate TX master and RX slave init test use P4_EV_BOARD 16000Hz", "[p4_ev][multi_codec]")
{
    // Allocate TX/RX channels independently, and create playback/record codec instances independently.
    multiple_es8311_run(false, true, I2S_ROLE_MASTER, I2S_ROLE_SLAVE, 16000, false, false);
}

TEST_CASE("Multiple es8311 codec with separate TX slave and RX master init test use P4_EV_BOARD 16000Hz", "[p4_ev][multi_codec]")
{
    // Allocate TX/RX channels independently, and create playback/record codec instances independently.
    multiple_es8311_run(false, true, I2S_ROLE_SLAVE, I2S_ROLE_MASTER, 16000, false, false);
}
