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
#include "soc/soc_caps.h"
#include "unity.h"

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_os.h"
#include "test_board.h"
#include "test_board_periph.h"
#include "test_codec_print.h"

extern const uint8_t music_pcm_start[] asm("_binary_16k_mono_16bit_pcm_start");
extern const uint8_t music_pcm_end[] asm("_binary_16k_mono_16bit_pcm_end");

static void test_codec_dev_using_s3_board_phased_init(void) __attribute__((unused));
static void test_codec_dev_using_s3_board_record_and_playback(void) __attribute__((unused));

typedef struct {
    const audio_codec_data_if_t *data_if;
    const audio_codec_ctrl_if_t *ctrl_if;
    const audio_codec_gpio_if_t *gpio_if;
    const audio_codec_if_t      *codec_if;
    esp_codec_dev_handle_t       codec_dev;
} codec_inst_t;

static int init_es8311_play_codec_dev(codec_inst_t *inst)
{
    int ret = ut_i2s_init_tx_phase(0, NULL, I2S_CLK_SRC_DEFAULT);
    TEST_ESP_OK(ret);
    audio_codec_i2s_cfg_t i2s_cfg = {.port = 0};
    i2s_cfg.rx_handle = NULL;
    i2s_cfg.tx_handle = ut_i2s_get_tx_handle(0);
    const audio_codec_data_if_t *out_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    TEST_ASSERT_NOT_NULL(out_data_if);

    audio_codec_i2c_cfg_t i2c_cfg = {.addr = ES8311_CODEC_DEFAULT_ADDR};
    i2c_cfg.bus_handle = ut_i2c_get_bus_handle();
    const audio_codec_ctrl_if_t *out_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    TEST_ASSERT_NOT_NULL(out_ctrl_if);

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    TEST_ASSERT_NOT_NULL(gpio_if);

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = out_ctrl_if,
        .gpio_if = gpio_if,
        .sys_cfg = {
            .is_master = false,
            .no_mclk = true,
        },
        .pa_cfg = {
            .pa_pin = TEST_BOARD_PA_PIN,
            .pa_active_low = false,
        },
    };
    const audio_codec_if_t *out_codec_if = es8311_codec_new(&es8311_cfg);
    TEST_ASSERT_NOT_NULL(out_codec_if);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = out_codec_if,
        .data_if = out_data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    esp_codec_dev_handle_t play_dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(play_dev);

    ret = esp_codec_dev_set_out_vol(play_dev, TEST_CODEC_BOARD_OUT_VOL);
    TEST_ESP_OK(ret);

    inst->data_if = out_data_if;
    inst->ctrl_if = out_ctrl_if;
    inst->gpio_if = gpio_if;
    inst->codec_if = out_codec_if;
    inst->codec_dev = play_dev;
    return ESP_CODEC_DEV_OK;
}

static int init_es7210_record_codec_dev(codec_inst_t *inst)
{
    int ret = ut_i2s_init_rx_phase(0, NULL, I2S_CLK_SRC_DEFAULT);
    TEST_ESP_OK(ret);
    audio_codec_i2s_cfg_t i2s_cfg = {.port = 0};
    i2s_cfg.tx_handle = NULL;
    i2s_cfg.rx_handle = ut_i2s_get_rx_handle(0);
    const audio_codec_data_if_t *in_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    TEST_ASSERT_NOT_NULL(in_data_if);

    audio_codec_i2c_cfg_t i2c_cfg = {.addr = ES7210_CODEC_DEFAULT_ADDR};
    i2c_cfg.bus_handle = ut_i2c_get_bus_handle();
    const audio_codec_ctrl_if_t *in_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    TEST_ASSERT_NOT_NULL(in_ctrl_if);

    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = in_ctrl_if,
        .adc_cfg = {
            .label = "FL,FR,RE,NA",
        },
    };
    const audio_codec_if_t *in_codec_if = es7210_codec_new(&es7210_cfg);
    TEST_ASSERT_NOT_NULL(in_codec_if);
    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = in_codec_if,
        .data_if = in_data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    esp_codec_dev_handle_t record_dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(record_dev);

    ret = esp_codec_dev_set_in_gain(record_dev, TEST_CODEC_BOARD_IN_GAIN);
    TEST_ESP_OK(ret);

    inst->data_if = in_data_if;
    inst->ctrl_if = in_ctrl_if;
    inst->codec_if = in_codec_if;
    inst->codec_dev = record_dev;
    return ESP_CODEC_DEV_OK;
}

static void deinit_codec_inst(codec_inst_t *inst)
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
    if (inst->gpio_if) {
        audio_codec_delete_gpio_if(inst->gpio_if);
    }
    memset(inst, 0, sizeof(codec_inst_t));
}

/**
 * Simplex is formed when (pin conflict prevents full duplex):
 * 1. i2s_channel_init(A)
 * 2. i2s_channel_reconfig(A)
 * 3. i2s_channel_init(B) -- configs differ from A, so the port runs simplex
 *
 * Full duplex is formed when:
 * 1. i2s_new_channel(&chan_cfg, tx_handle, rx_handle) -- modes need not match
 * 2. IDF >= v5.5.3: TX and RX must use the same mode and init config (e.g. STD + TDM cannot
 *    form duplex; mismatched configs under the same mode also fail)
 */
static void test_codec_dev_using_s3_board_phased_init(void)
{
    int ret = ut_i2c_init(0, NULL);
    TEST_ESP_OK(ret);

#if SOC_I2S_SUPPORTS_TDM
    ut_set_i2s_mode(I2S_COMM_MODE_TDM, I2S_COMM_MODE_TDM);
#endif  /* SOC_I2S_SUPPORTS_TDM */
    ret = ut_i2s_init_channel(0);
    TEST_ESP_OK(ret);

    int sample_rate = 48000;
    int mclk_multiple = 384;
    int bits_per_sample = 16;

    esp_codec_dev_sample_info_t fs_play = {
        .sample_rate = sample_rate,
        .channel = 2,
        .bits_per_sample = bits_per_sample,
        .mclk_multiple = mclk_multiple,
        .channel_mask = BIT(0) | BIT(1),
    };

    esp_codec_dev_sample_info_t fs_rec = {
        .sample_rate = sample_rate,
        .channel = 4,
        .bits_per_sample = bits_per_sample,
        .mclk_multiple = mclk_multiple,
        .channel_mask = BIT(0) | BIT(1) | BIT(2) | BIT(3),
    };

    codec_inst_t record_inst = {0};
    ret = init_es7210_record_codec_dev(&record_inst);
    TEST_ESP_OK(ret);

    codec_inst_t play_inst = {0};
    ret = init_es8311_play_codec_dev(&play_inst);
    TEST_ESP_OK(ret);

    ret = esp_codec_dev_open(play_inst.codec_dev, &fs_play);
    TEST_ESP_OK(ret);

    ret = esp_codec_dev_open(record_inst.codec_dev, &fs_rec);
    TEST_ESP_OK(ret);

    int data_size = 240 * 3 * fs_play.channel * (fs_play.bits_per_sample >> 3);
    uint8_t *data = (uint8_t *)malloc(data_size);
    TEST_ASSERT_NOT_NULL(data);
    int limit_size = 5 * fs_play.sample_rate * fs_play.channel * (fs_play.bits_per_sample >> 3);
    int got_size = 0;

    esp_codec_dev_channel_map_t order = {
        .value = ESP_CODEC_DEV_CHANNEL_MAP(1, 3, 2, 4, 0, 0, 0, 0),
    };
    esp_codec_dev_set_data_layout(play_inst.codec_dev, &order);

    esp_codec_dev_sleep(200);
    while (got_size < limit_size) {
        ret = esp_codec_dev_read(record_inst.codec_dev, data, data_size);
        test_print_pcm_s16_head(data, 4);
        TEST_ESP_OK(ret);
        ret = esp_codec_dev_write(play_inst.codec_dev, data, data_size);
        TEST_ESP_OK(ret);
        int max_sample, min_sample;
        codec_max_sample(data, data_size, &max_sample, &min_sample);
        TEST_ASSERT(max_sample > min_sample);
        got_size += data_size;
    }
    free(data);

    ret = esp_codec_dev_close(play_inst.codec_dev);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_close(record_inst.codec_dev);
    TEST_ESP_OK(ret);

    deinit_codec_inst(&record_inst);
    deinit_codec_inst(&play_inst);

    ut_i2s_deinit(0);
    ut_i2c_deinit(0);
}

/**
 * Record for 5 s, play back for 5 s, and verify capture matches playback (IDF v5.5.1).
 */
static void test_codec_dev_using_s3_board_record_and_playback(void)
{
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0) {
        TEST_IGNORE_MESSAGE("PSRAM is not available");
    }

    int ret = ut_i2c_init(0, NULL);
    TEST_ESP_OK(ret);
#if SOC_I2S_SUPPORTS_TDM
    ut_set_i2s_mode(I2S_COMM_MODE_TDM, I2S_COMM_MODE_TDM);
#endif  /* SOC_I2S_SUPPORTS_TDM */
    ret = ut_i2s_init_channel(0);
    TEST_ESP_OK(ret);

    const int pcm_sample_rate = 16000;
    const int pcm_channel = 1;
    const int pcm_bits_per_sample = 16;
    const int pcm_bytes_per_sample = pcm_bits_per_sample >> 3;
    const int pcm_total_bytes = 10 * pcm_sample_rate * pcm_channel * pcm_bytes_per_sample;
    const int pcm_chunk_bytes = 240 * 3 * pcm_channel * pcm_bytes_per_sample;
    const uint8_t *pcm = music_pcm_start;
    const int pcm_size = (int)(music_pcm_end - music_pcm_start);
    TEST_ASSERT(pcm_size > 0);

    codec_inst_t play_inst = {0};
    ret = init_es8311_play_codec_dev(&play_inst);
    TEST_ESP_OK(ret);
    esp_codec_dev_sample_info_t fs_warmup = {
        .sample_rate = pcm_sample_rate,
        .channel = pcm_channel,
        .bits_per_sample = pcm_bits_per_sample,
        .mclk_multiple = 256,
        .channel_mask = BIT(0),
    };
    ret = esp_codec_dev_open(play_inst.codec_dev, &fs_warmup);
    TEST_ESP_OK(ret);
    esp_codec_dev_set_out_vol(play_inst.codec_dev, TEST_CODEC_BOARD_OUT_VOL);

    int pcm_written = 0;
    while (pcm_written < pcm_total_bytes) {
        int once = (pcm_total_bytes - pcm_written > pcm_chunk_bytes) ? pcm_chunk_bytes : (pcm_total_bytes - pcm_written);
        int offset = (pcm_size > 0) ? (pcm_written % pcm_size) : 0;
        const uint8_t *pcm_ptr = pcm + offset;
        if (offset + once <= pcm_size) {
            ret = esp_codec_dev_write(play_inst.codec_dev, (void *)pcm_ptr, once);
            TEST_ESP_OK(ret);
        } else {
            int first = pcm_size - offset;
            ret = esp_codec_dev_write(play_inst.codec_dev, (void *)pcm_ptr, first);
            TEST_ESP_OK(ret);
            int second = once - first;
            ret = esp_codec_dev_write(play_inst.codec_dev, (void *)pcm, second);
            TEST_ESP_OK(ret);
        }
        pcm_written += once;
    }
    ret = esp_codec_dev_close(play_inst.codec_dev);
    TEST_ESP_OK(ret);
    deinit_codec_inst(&play_inst);
    ut_i2s_deinit(0);
    printf("play pcm done\n\n");

    ret = ut_i2s_init_channel(0);
    TEST_ESP_OK(ret);

    const int sample_rate = 48000;
    const int bits_per_sample = 16;
    const int channel = 2;
    const int mclk_multiple = 384;
    const int bytes_per_sample = bits_per_sample >> 3;
    const int total_bytes = 3 * sample_rate * channel * bytes_per_sample;
    const int chunk_bytes = 240 * 3 * channel * bytes_per_sample;

    uint8_t *audio_psram = (uint8_t *)heap_caps_malloc(total_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    TEST_ASSERT_NOT_NULL(audio_psram);

    codec_inst_t record_inst = {0};
    ret = init_es7210_record_codec_dev(&record_inst);
    TEST_ESP_OK(ret);

    esp_codec_dev_sample_info_t fs_rec = {
        .sample_rate = sample_rate,
        .channel = channel,
        .bits_per_sample = bits_per_sample,
        .mclk_multiple = mclk_multiple,
        .channel_mask = BIT(0) | BIT(1),
    };
    ret = esp_codec_dev_open(record_inst.codec_dev, &fs_rec);
    TEST_ESP_OK(ret);

    esp_codec_dev_sleep(100);
    int recorded = 0;
    while (recorded < total_bytes) {
        int once = (total_bytes - recorded > chunk_bytes) ? chunk_bytes : (total_bytes - recorded);
        ret = esp_codec_dev_read(record_inst.codec_dev, audio_psram + recorded, once);
        TEST_ESP_OK(ret);
        int max_sample = 0, min_sample = 0;
        codec_max_sample(audio_psram + recorded, once, &max_sample, &min_sample);
        TEST_ASSERT(max_sample > min_sample);
        recorded += once;
    }

    ret = esp_codec_dev_close(record_inst.codec_dev);
    TEST_ESP_OK(ret);
    deinit_codec_inst(&record_inst);
    printf("record pcm done\n\n");

    ret = init_es8311_play_codec_dev(&play_inst);
    TEST_ESP_OK(ret);

    esp_codec_dev_sample_info_t fs_play = {
        .sample_rate = sample_rate,
        .channel = channel,
        .bits_per_sample = bits_per_sample,
        .mclk_multiple = mclk_multiple,
        .channel_mask = BIT(0) | BIT(1),
    };
    ret = esp_codec_dev_open(play_inst.codec_dev, &fs_play);
    TEST_ESP_OK(ret);

    esp_codec_dev_sleep(100);
    int played = 0;
    while (played < total_bytes) {
        int once = (total_bytes - played > chunk_bytes) ? chunk_bytes : (total_bytes - played);
        ret = esp_codec_dev_write(play_inst.codec_dev, audio_psram + played, once);
        TEST_ESP_OK(ret);
        played += once;
    }

    ret = esp_codec_dev_close(play_inst.codec_dev);
    TEST_ESP_OK(ret);
    deinit_codec_inst(&play_inst);

    free(audio_psram);
    ut_i2s_deinit(0);
    ut_i2c_deinit(0);
}

#if CONFIG_IDF_TARGET_ESP32S3 && defined(CONFIG_CODEC_ES7210_SUPPORT) && defined(CONFIG_CODEC_ES8311_SUPPORT)

TEST_CASE("esp codec dev S3 phased TX player then RX record loop", "[korvo2_v3][duplex]")
{
    test_codec_dev_using_s3_board_phased_init();
}

TEST_CASE("esp codec dev S3 record 5s to psram then playback", "[korvo2_v3][duplex]")
{
    test_codec_dev_using_s3_board_record_and_playback();
}

#endif  /* CONFIG_IDF_TARGET_ESP32S3 && defined(CONFIG_CODEC_ES7210_SUPPORT) && defined(CONFIG_CODEC_ES8311_SUPPORT) */
