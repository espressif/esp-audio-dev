/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdio.h>

#include "sdkconfig.h"
#include "esp_bit_defs.h"
#include "unity.h"

#include "my_codec.h"
#include "esp_codec_dev_vol.h"
#include "esp_codec_dev_defaults.h"
#include "audio_hw_alc.h"
#include "audio_hw_drc.h"
#include "audio_hw_eq.h"
#include "audio_hw_line.h"
#include "audio_hw_mute.h"

// Customized volume curve taken from android framework
static esp_codec_dev_vol_map_t volume_maps[] = {
    {.vol = 1, .db_value = -49.5},
    {.vol = 33, .db_value = -33.5},
    {.vol = 66, .db_value = -17.0},
    {.vol = 100, .db_value = 0.0},
};

/**
 * Test case for esp_codec_dev API using customized interface
 */
static void test_esp_codec_dev_api(void)
{
    const audio_codec_ctrl_if_t *ctrl_if = my_codec_ctrl_new();
    TEST_ASSERT_NOT_NULL(ctrl_if);
    my_codec_ctrl_t *codec_ctrl = (my_codec_ctrl_t *)ctrl_if;
    const audio_codec_data_if_t *data_if = my_codec_data_new();
    TEST_ASSERT_NOT_NULL(data_if);
    my_codec_data_t *codec_data = (my_codec_data_t *)data_if;
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    TEST_ASSERT_NOT_NULL(gpio_if);
    my_codec_cfg_t codec_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .hw_gain = {
            .pa_voltage = 3.3,
            .codec_dac_voltage = 3.3,  // PA and codec use same voltage
            .pa_gain = 10.0,           // PA gain 10db
        }};
    const audio_codec_if_t *codec_if = my_codec_new(&codec_cfg);
    TEST_ASSERT_NOT_NULL(codec_if);
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(codec_if);

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .sample_rate = 48000,
        .channel = 2,
    };
    int ret = esp_codec_dev_open(dev, &fs);
    TEST_ESP_OK(ret);

    esp_codec_dev_vol_curve_t vol_curve = {
        .count = sizeof(volume_maps) / sizeof(esp_codec_dev_vol_map_t),
        .vol_map = volume_maps,
    };
    // Test for volume curve settings
    ret = esp_codec_dev_set_vol_curve(dev, &vol_curve);
    TEST_ESP_OK(ret);

    // Test for volume setting, considering volume curve
    ret = esp_codec_dev_set_out_vol(dev, 33.0);
    TEST_ESP_OK(ret);
    TEST_ASSERT_EQUAL(43, codec_ctrl->reg[MY_CODEC_REG_VOL]);
    ret = esp_codec_dev_set_out_vol(dev, 66.0);
    TEST_ESP_OK(ret);
    TEST_ASSERT_EQUAL(27, codec_ctrl->reg[MY_CODEC_REG_VOL]);

    // Test for mute setting
    ret = esp_codec_dev_set_out_mute(dev, true);
    TEST_ESP_OK(ret);
    TEST_ASSERT_EQUAL(1, codec_ctrl->reg[MY_CODEC_REG_MUTE]);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_set_out_mute(dev, false);
    TEST_ASSERT_EQUAL(0, codec_ctrl->reg[MY_CODEC_REG_MUTE]);

    // Test for microphone gain
    ret = esp_codec_dev_set_in_gain(dev, 20.0);
    TEST_ESP_OK(ret);
    TEST_ASSERT_EQUAL(20, codec_ctrl->reg[MY_CODEC_REG_MIC_GAIN]);
    ret = esp_codec_dev_set_in_gain(dev, 40.0);
    TEST_ESP_OK(ret);
    TEST_ASSERT_EQUAL(40, codec_ctrl->reg[MY_CODEC_REG_MIC_GAIN]);

    // Test for microphone mute setting
    ret = esp_codec_dev_set_in_mute(dev, true);
    TEST_ESP_OK(ret);
    TEST_ASSERT_EQUAL(1, codec_ctrl->reg[MY_CODEC_REG_MIC_MUTE]);
    ret = esp_codec_dev_set_in_mute(dev, false);
    TEST_ESP_OK(ret);
    TEST_ASSERT_EQUAL(0, codec_ctrl->reg[MY_CODEC_REG_MIC_MUTE]);

    // Test for read data
    uint8_t *data = (uint8_t *)calloc(1, 512);
    TEST_ASSERT_NOT_NULL(data);
    ret = esp_codec_dev_read(dev, data, 256);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_read(dev, data + 256, 256);
    TEST_ESP_OK(ret);
    for (int i = 0; i < 512; i++) {
        uint8_t v = (uint8_t)i;
        TEST_ASSERT_EQUAL(v, data[i]);
    }
    // Test for write data
    ret = esp_codec_dev_write(dev, data, 512);
    TEST_ESP_OK(ret);
    TEST_ASSERT_EQUAL(512, codec_data->write_idx);

    esp_codec_dev_close(dev);

    // Test for volume curve settings
    ret = esp_codec_dev_set_vol_curve(dev, &vol_curve);
    TEST_ASSERT(ret == 0);

    // Test for volume setting
    ret = esp_codec_dev_set_out_vol(dev, 30.0);
    TEST_ESP_OK(ret);
    // Test for mute setting
    ret = esp_codec_dev_set_out_mute(dev, true);
    TEST_ESP_OK(ret);
    // Test for microphone gain
    ret = esp_codec_dev_set_in_gain(dev, 20.0);
    TEST_ESP_OK(ret);

    // Test for microphone mute setting
    ret = esp_codec_dev_set_in_mute(dev, true);
    TEST_ESP_OK(ret);

    // APP need fail after close
    // Test for read data
    ret = esp_codec_dev_read(dev, data, 256);
    TEST_ASSERT(ret != ESP_CODEC_DEV_OK);
    // Test for write data
    ret = esp_codec_dev_write(dev, data, 512);
    TEST_ASSERT(ret != ESP_CODEC_DEV_OK);

    // Test for volume interface
    const audio_codec_vol_if_t *vol_if = my_codec_vol_new();
    my_codec_vol_t *codec_vol = (my_codec_vol_t *)vol_if;
    TEST_ASSERT_NOT_NULL(data_if);
    // Should set vol handler before usage
    ret = esp_codec_dev_set_vol_handler(dev, vol_if);
    TEST_ESP_OK(ret);

    ret = esp_codec_dev_set_out_vol(dev, 40.0);
    TEST_ESP_OK(ret);
    // Calculated from volume curve
    TEST_ASSERT_EQUAL(-30.0, codec_vol->vol_db);
    // Reopen device
    ret = esp_codec_dev_open(dev, &fs);
    TEST_ESP_OK(ret);
    TEST_ASSERT_EQUAL(true, codec_vol->is_open);

    ret = esp_codec_dev_write(dev, data, 512);
    TEST_ESP_OK(ret);
    TEST_ASSERT_EQUAL(512, codec_vol->process_len);
    esp_codec_dev_close(dev);
    TEST_ESP_OK(ret);
    TEST_ASSERT_EQUAL(false, codec_vol->is_open);

    free(data);
    // Delete codec dev handle
    esp_codec_dev_delete(dev);
    // Delete codec interface
    audio_codec_delete_codec_if(codec_if);
    // Delete codec control interface
    audio_codec_delete_ctrl_if(ctrl_if);
    // Delete codec data interface
    audio_codec_delete_data_if(data_if);
    audio_codec_delete_vol_if(vol_if);
    // Delete GPIO interface
    audio_codec_delete_gpio_if(gpio_if);
}

void test_input_mirror_fake_input_keeps_latest_data(void)
{
    const audio_codec_ctrl_if_t *ctrl_if = my_codec_ctrl_new();
    TEST_ASSERT_NOT_NULL(ctrl_if);
    const audio_codec_data_if_t *data_if = my_codec_data_new();
    TEST_ASSERT_NOT_NULL(data_if);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    TEST_ASSERT_NOT_NULL(gpio_if);
    my_codec_cfg_t codec_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
    };
    const audio_codec_if_t *codec_if = my_codec_new(&codec_cfg);
    TEST_ASSERT_NOT_NULL(codec_if);
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(dev);

    uint8_t read_data[600];
    uint8_t cache_data[600];
    int bytes_read = -1;
    int ret = esp_codec_dev_mirror_cfg(dev, 512);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_mirror_read(dev, cache_data, sizeof(cache_data), 0, &bytes_read);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_TIMEOUT, ret);
    TEST_ASSERT_EQUAL(0, bytes_read);

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 8,
        .sample_rate = 8000,
        .channel = 1,
    };
    ret = esp_codec_dev_open(dev, &fs);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_mirror_cfg(dev, 0);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, ret);
    /* Repeated cfg should be idempotent. */
    ret = esp_codec_dev_mirror_cfg(dev, 512);
    TEST_ESP_OK(ret);

    ret = esp_codec_dev_mirror_read(dev, NULL, sizeof(cache_data), 0, &bytes_read);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, ret);
    ret = esp_codec_dev_mirror_read(dev, cache_data, 0, 0, &bytes_read);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, ret);
    ret = esp_codec_dev_mirror_read(dev, cache_data, sizeof(cache_data), 0, NULL);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, ret);

    ret = esp_codec_dev_read(dev, read_data, sizeof(read_data));
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_mirror_read(dev, cache_data, 512, 0, &bytes_read);
    TEST_ESP_OK(ret);
    TEST_ASSERT_EQUAL(512, bytes_read);
    for (int i = 0; i < bytes_read; i++) {
        TEST_ASSERT_EQUAL_UINT8(read_data[i + 88], cache_data[i]);
    }

    ret = esp_codec_dev_mirror_read(dev, cache_data, 16, 0, &bytes_read);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_TIMEOUT, ret);
    TEST_ASSERT_EQUAL(0, bytes_read);
    ret = esp_codec_dev_close(dev);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_mirror_read(dev, cache_data, 16, 0, &bytes_read);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_WRONG_STATE, ret);
    TEST_ASSERT_EQUAL(0, bytes_read);

    fs.bits_per_sample = 16;
    fs.sample_rate = 16000;
    ret = esp_codec_dev_open(dev, &fs);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_mirror_cfg(dev, 512);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_close(dev);
    TEST_ESP_OK(ret);
    ret = esp_codec_dev_mirror_read(dev, cache_data, 16, 0, &bytes_read);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_WRONG_STATE, ret);

    esp_codec_dev_delete(dev);
    audio_codec_delete_codec_if(codec_if);
    audio_codec_delete_ctrl_if(ctrl_if);
    audio_codec_delete_data_if(data_if);
    audio_codec_delete_gpio_if(gpio_if);
}

static void test_esp_codec_dev_wrong_argument(void)
{
    const audio_codec_ctrl_if_t *ctrl_if = my_codec_ctrl_new();
    TEST_ASSERT_NOT_NULL(ctrl_if);
    const audio_codec_data_if_t *data_if = my_codec_data_new();
    TEST_ASSERT_NOT_NULL(data_if);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    TEST_ASSERT_NOT_NULL(gpio_if);
    my_codec_cfg_t codec_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
    };
    const audio_codec_if_t *codec_if = my_codec_new(&codec_cfg);
    TEST_ASSERT_NOT_NULL(codec_if);

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT_NOT_NULL(codec_if);

    esp_codec_dev_handle_t dev_bad = esp_codec_dev_new(NULL);
    TEST_ASSERT(dev_bad == NULL);
    dev_cfg.data_if = NULL;
    dev_bad = esp_codec_dev_new(&dev_cfg);
    TEST_ASSERT(dev_bad == NULL);

    int ret = esp_codec_dev_open(dev, NULL);
    TEST_ASSERT(ret != ESP_CODEC_DEV_OK);

    esp_codec_dev_vol_curve_t vol_curve = {
        .count = 2,
    };
    // Test for volume curve settings
    ret = esp_codec_dev_set_vol_curve(dev, &vol_curve);
    TEST_ASSERT(ret != ESP_CODEC_DEV_OK);

    // Test for volume setting
    ret = esp_codec_dev_set_out_vol(NULL, 50.0);
    TEST_ASSERT(ret != ESP_CODEC_DEV_OK);

    // Test for mute setting
    ret = esp_codec_dev_set_out_mute(NULL, true);
    TEST_ASSERT(ret != ESP_CODEC_DEV_OK);

    // Test for microphone gain
    ret = esp_codec_dev_set_in_gain(NULL, 20.0);
    TEST_ASSERT(ret != ESP_CODEC_DEV_OK);

    // Test for microphone mute setting
    ret = esp_codec_dev_set_in_mute(NULL, true);
    TEST_ASSERT(ret != ESP_CODEC_DEV_OK);

    // Test for read data
    uint8_t data[16];
    ret = esp_codec_dev_read(dev, NULL, sizeof(data));
    TEST_ASSERT(ret != ESP_CODEC_DEV_OK);
    ret = esp_codec_dev_read(dev, data, sizeof(data));
    TEST_ASSERT(ret != ESP_CODEC_DEV_OK);
    ret = esp_codec_dev_read(NULL, NULL, sizeof(data));
    TEST_ASSERT(ret != ESP_CODEC_DEV_OK);

    // Test for write data
    ret = esp_codec_dev_write(dev, data, sizeof(data));
    TEST_ASSERT(ret != ESP_CODEC_DEV_OK);
    ret = esp_codec_dev_write(NULL, data, sizeof(data));
    TEST_ASSERT(ret != ESP_CODEC_DEV_OK);
    ret = esp_codec_dev_write(dev, NULL, sizeof(data));
    TEST_ASSERT(ret != ESP_CODEC_DEV_OK);

    esp_codec_dev_close(dev);
    // Delete codec dev handle
    esp_codec_dev_delete(dev);
    // Delete codec interface
    audio_codec_delete_codec_if(codec_if);
    // Delete codec control interface
    audio_codec_delete_ctrl_if(ctrl_if);
    // Delete codec data interface
    audio_codec_delete_data_if(data_if);
    // Delete GPIO interface
    audio_codec_delete_gpio_if(gpio_if);
}

static void test_esp_codec_dev_feature_should_not_support(void)
{
    const audio_codec_ctrl_if_t *ctrl_if = my_codec_ctrl_new();
    TEST_ASSERT_NOT_NULL(ctrl_if);
    const audio_codec_data_if_t *data_if = my_codec_data_new();
    TEST_ASSERT_NOT_NULL(data_if);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    TEST_ASSERT_NOT_NULL(gpio_if);
    my_codec_cfg_t codec_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
    };
    const audio_codec_if_t *codec_if = my_codec_new(&codec_cfg);
    TEST_ASSERT_NOT_NULL(codec_if);
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    int ret = 0;
    uint8_t data[16];
    // Input device should not support output function
    {
        esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
        TEST_ASSERT_NOT_NULL(codec_if);
        esp_codec_dev_vol_map_t vol_maps[2] = {
            {.vol = 0, .db_value = 0},
            {.vol = 100, .db_value = 100},
        };
        esp_codec_dev_vol_curve_t vol_curve = {
            .count = 2,
            .vol_map = vol_maps,
        };
        // Test for volume curve settings
        ret = esp_codec_dev_set_vol_curve(dev, &vol_curve);
        TEST_ASSERT(ret != ESP_CODEC_DEV_OK);

        // Test for volume setting
        ret = esp_codec_dev_set_out_vol(dev, 30.0);
        TEST_ASSERT(ret != ESP_CODEC_DEV_OK);

        // Test for mute setting
        ret = esp_codec_dev_set_out_mute(NULL, true);
        TEST_ASSERT(ret != ESP_CODEC_DEV_OK);

        // Test for write data
        ret = esp_codec_dev_write(dev, data, sizeof(data));
        TEST_ASSERT(ret != ESP_CODEC_DEV_OK);
        esp_codec_dev_close(dev);
        // Delete codec dev handle
        esp_codec_dev_delete(dev);
    }
    // Output device should not support input function
    {
        dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_OUT;
        esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
        TEST_ASSERT_NOT_NULL(dev);

        // Test for volume setting
        ret = esp_codec_dev_set_in_gain(dev, 20.0);
        TEST_ASSERT(ret != ESP_CODEC_DEV_OK);

        // Test for mute setting
        ret = esp_codec_dev_set_in_mute(dev, true);
        TEST_ASSERT(ret != ESP_CODEC_DEV_OK);

        // Test for write data
        ret = esp_codec_dev_read(dev, data, sizeof(data));
        TEST_ASSERT(ret != ESP_CODEC_DEV_OK);
        ret = esp_codec_dev_mirror_cfg(dev, sizeof(data));
        TEST_ASSERT_EQUAL(ESP_CODEC_DEV_NOT_SUPPORT, ret);
        int bytes_read = 0;
        ret = esp_codec_dev_mirror_read(dev, data, sizeof(data), 0, &bytes_read);
        TEST_ASSERT_EQUAL(ESP_CODEC_DEV_NOT_SUPPORT, ret);
        esp_codec_dev_close(dev);
        // Delete codec dev handle
        esp_codec_dev_delete(dev);
    }
    // Delete codec interface
    audio_codec_delete_codec_if(codec_if);
    // Delete codec control interface
    audio_codec_delete_ctrl_if(ctrl_if);
    // Delete codec data interface
    audio_codec_delete_data_if(data_if);
    // Delete GPIO interface
    audio_codec_delete_gpio_if(gpio_if);
}

static void test_audio_codec_new_common_api(void)
{
    dummy_codec_cfg_t chip_cfg = {
        .pa_cfg = {
            .pa_pin = -1,
        },
    };
    audio_codec_cfg_t common_cfg = {
        .pa_cfg = {
            .pa_pin = -1,
        },
        .reset_cfg = {
            .reset_pin = -1,
        },
    };

    const audio_codec_if_t *codec_if = audio_codec_new("dummy", &chip_cfg, sizeof(chip_cfg));
    TEST_ASSERT_NOT_NULL(codec_if);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_codec_delete_codec_if(codec_if));

    codec_if = audio_codec_new("dummy", &common_cfg, sizeof(common_cfg));
    TEST_ASSERT_NOT_NULL(codec_if);

    TEST_ASSERT_NULL(audio_codec_new(NULL, &chip_cfg, sizeof(chip_cfg)));
    TEST_ASSERT_NULL(audio_codec_new("unknown", &chip_cfg, sizeof(chip_cfg)));
    TEST_ASSERT_NULL(audio_codec_new("dummy", NULL, sizeof(chip_cfg)));
    TEST_ASSERT_NULL(audio_codec_new("dummy", &chip_cfg, sizeof(chip_cfg) - 1));
    TEST_ASSERT_NULL(audio_codec_new("dummy", &common_cfg, sizeof(common_cfg) - 1));

    audio_hw_alc_handle_t alc = NULL;
    audio_hw_drc_handle_t drc = NULL;
    audio_hw_eq_handle_t eq = NULL;
    audio_hw_line_handle_t line = NULL;
    audio_hw_mute_handle_t mute = NULL;

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, audio_hw_alc_new(NULL, &alc));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, audio_hw_drc_new(NULL, &drc));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, audio_hw_eq_new(NULL, &eq));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, audio_hw_line_new(NULL, &line));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, audio_hw_mute_new(NULL, &mute));

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, audio_hw_alc_new(codec_if, NULL));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, audio_hw_drc_new(codec_if, NULL));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, audio_hw_eq_new(codec_if, NULL));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, audio_hw_line_new(codec_if, NULL));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, audio_hw_mute_new(codec_if, NULL));

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_NOT_SUPPORT, audio_hw_alc_new(codec_if, &alc));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_NOT_SUPPORT, audio_hw_drc_new(codec_if, &drc));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_NOT_SUPPORT, audio_hw_eq_new(codec_if, &eq));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_NOT_SUPPORT, audio_hw_line_new(codec_if, &line));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_NOT_SUPPORT, audio_hw_mute_new(codec_if, &mute));

    TEST_ASSERT_NULL(alc);
    TEST_ASSERT_NULL(drc);
    TEST_ASSERT_NULL(eq);
    TEST_ASSERT_NULL(line);
    TEST_ASSERT_NULL(mute);

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, audio_hw_alc_delete(alc));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, audio_hw_drc_delete(drc));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, audio_hw_eq_delete(eq));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, audio_hw_line_delete(line));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, audio_hw_mute_delete(mute));

    audio_codec_delete_codec_if(codec_if);
}

static void test_esp_codec_dev_hw_proc_api(void)
{
    const audio_codec_ctrl_if_t *ctrl_if = my_codec_ctrl_new();
    TEST_ASSERT_NOT_NULL(ctrl_if);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    TEST_ASSERT_NOT_NULL(gpio_if);
    my_codec_cfg_t codec_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
    };
    const audio_codec_if_t *codec_if = my_codec_new(&codec_cfg);
    TEST_ASSERT_NOT_NULL(codec_if);
    const my_codec_proc_state_t *proc = my_codec_get_proc_state(codec_if);
    TEST_ASSERT_NOT_NULL(proc);

    audio_hw_alc_handle_t alc = NULL;
    audio_hw_drc_handle_t drc = NULL;
    audio_hw_eq_handle_t eq = NULL;
    audio_hw_line_handle_t line = NULL;
    audio_hw_mute_handle_t mute = NULL;

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_alc_new(codec_if, &alc));
    TEST_ASSERT_NOT_NULL(alc);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_drc_new(codec_if, &drc));
    TEST_ASSERT_NOT_NULL(drc);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_eq_new(codec_if, &eq));
    TEST_ASSERT_NOT_NULL(eq);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_line_new(codec_if, &line));
    TEST_ASSERT_NOT_NULL(line);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_mute_new(codec_if, &mute));
    TEST_ASSERT_NOT_NULL(mute);

    audio_alc_cfg_t alc_cfg = DEFAULT_ALC_CONFIG();
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_alc_init(alc, &alc_cfg));
    TEST_ASSERT_EQUAL(alc_cfg.min_gain, proc->alc_min_gain);
    TEST_ASSERT_EQUAL(alc_cfg.max_gain, proc->alc_max_gain);
    TEST_ASSERT_EQUAL(alc_cfg.target_gain, proc->alc_target_gain);
    TEST_ASSERT_EQUAL(alc_cfg.noise_gate_threshold, proc->alc_noise_gate);

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_alc_set_channel_mask(alc, BIT(1)));
    TEST_ASSERT_EQUAL(BIT(1), proc->alc_channel_mask);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_alc_set_gain(alc, -12.0f));
    TEST_ASSERT_EQUAL(-12.0f, proc->alc_target_gain);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_alc_set_noise_gate(alc, -45.0f));
    TEST_ASSERT_EQUAL(-45.0f, proc->alc_noise_gate);

    audio_drc_cfg_t drc_cfg = DEFAULT_DRC_CONFIG();
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_drc_init(drc, &drc_cfg));
    TEST_ASSERT_EQUAL(drc_cfg.min_gain, proc->drc_min_gain);
    TEST_ASSERT_EQUAL(drc_cfg.max_gain, proc->drc_max_gain);
    TEST_ASSERT_EQUAL(drc_cfg.offset_gain, proc->drc_offset_gain);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_drc_set_offset_gain(drc, -3.0f));
    TEST_ASSERT_EQUAL(-3.0f, proc->drc_offset_gain);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_drc_enable(drc, true));
    TEST_ASSERT_EQUAL(true, proc->drc_enabled);

    eq_para_t eq_para[] = {
        {.gain = 3, .frequency = 1000},
        {.gain = -2, .frequency = 4000},
    };
    audio_eq_cfg_t eq_cfg = {
        .para = eq_para,
        .filter_num = 2,
    };
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_eq_set_cfg(eq, &eq_cfg));
    TEST_ASSERT_EQUAL(2, proc->eq_filter_num);
    TEST_ASSERT_EQUAL(eq_para[0].gain, proc->eq_band[0].gain);
    TEST_ASSERT_EQUAL(eq_para[0].frequency, proc->eq_band[0].frequency);
    TEST_ASSERT_EQUAL(eq_para[1].gain, proc->eq_band[1].gain);
    TEST_ASSERT_EQUAL(eq_para[1].frequency, proc->eq_band[1].frequency);

    eq_para_t band = {.gain = 6, .frequency = 8000};
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_eq_set_band_para(eq, &band, 2));
    TEST_ASSERT_EQUAL(3, proc->eq_filter_num);
    TEST_ASSERT_EQUAL(band.gain, proc->eq_band[2].gain);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_eq_enable(eq, true));
    TEST_ASSERT_EQUAL(true, proc->eq_enabled);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_eq_dump_info(eq));

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_line_enable_in(line, true));
    TEST_ASSERT_EQUAL(true, proc->line_in_enabled);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_line_enable_out(line, false));
    TEST_ASSERT_EQUAL(false, proc->line_out_enabled);

    auto_mute_cfg_t amute_cfg = DEFAULT_AUTO_MUTE_CONFIG();
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_auto_mute_set_cfg(mute, &amute_cfg));
    TEST_ASSERT_EQUAL(amute_cfg.noise_gate, proc->auto_mute_noise_gate);
    TEST_ASSERT_EQUAL_FLOAT(amute_cfg.mute_vol, proc->auto_mute_vol);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_auto_mute_enable(mute, true));
    TEST_ASSERT_EQUAL(true, proc->auto_mute_enabled);

    soft_mute_cfg_t smute_cfg = DEFAULT_SOFT_MUTE_CONFIG();
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_soft_mute_set_cfg(mute, &smute_cfg));
    TEST_ASSERT_EQUAL(smute_cfg.ramp_rate, proc->soft_mute_ramp_rate);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_soft_mute_enable(mute, true));
    TEST_ASSERT_EQUAL(true, proc->soft_mute_enabled);

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_alc_delete(alc));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_drc_delete(drc));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_eq_delete(eq));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_line_delete(line));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_mute_delete(mute));

    audio_codec_delete_codec_if(codec_if);
    audio_codec_delete_ctrl_if(ctrl_if);
    audio_codec_delete_gpio_if(gpio_if);
}

static void test_esp_codec_dev_hw_proc_wrong_arg(void)
{
    const audio_codec_ctrl_if_t *ctrl_if = my_codec_ctrl_new();
    TEST_ASSERT_NOT_NULL(ctrl_if);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    TEST_ASSERT_NOT_NULL(gpio_if);
    my_codec_cfg_t codec_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
    };
    const audio_codec_if_t *codec_if = my_codec_new(&codec_cfg);
    TEST_ASSERT_NOT_NULL(codec_if);

    audio_hw_alc_handle_t alc = NULL;
    audio_hw_drc_handle_t drc = NULL;
    audio_hw_eq_handle_t eq = NULL;
    audio_hw_line_handle_t line = NULL;
    audio_hw_mute_handle_t mute = NULL;

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_alc_new(codec_if, &alc));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_drc_new(codec_if, &drc));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_eq_new(codec_if, &eq));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_line_new(codec_if, &line));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_mute_new(codec_if, &mute));

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, audio_hw_alc_init(NULL, NULL));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, audio_hw_alc_set_gain(NULL, 0.0f));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, audio_hw_drc_init(NULL, NULL));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, audio_hw_eq_set_cfg(NULL, NULL));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, audio_hw_line_enable_in(NULL, true));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, audio_hw_auto_mute_enable(NULL, true));

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_alc_delete(alc));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_drc_delete(drc));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_eq_delete(eq));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_line_delete(line));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, audio_hw_mute_delete(mute));

    codec_if->hw_base.close(&codec_if->hw_base);

    alc = NULL;
    drc = NULL;
    eq = NULL;
    line = NULL;
    mute = NULL;
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_WRONG_STATE, audio_hw_alc_new(codec_if, &alc));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_WRONG_STATE, audio_hw_drc_new(codec_if, &drc));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_WRONG_STATE, audio_hw_eq_new(codec_if, &eq));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_WRONG_STATE, audio_hw_line_new(codec_if, &line));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_WRONG_STATE, audio_hw_mute_new(codec_if, &mute));
    TEST_ASSERT_NULL(alc);
    TEST_ASSERT_NULL(drc);
    TEST_ASSERT_NULL(eq);
    TEST_ASSERT_NULL(line);
    TEST_ASSERT_NULL(mute);

    audio_codec_delete_codec_if(codec_if);
    audio_codec_delete_ctrl_if(ctrl_if);
    audio_codec_delete_gpio_if(gpio_if);
}

static void test_esp_codec_dev_vol_wrong_arg(void)
{
    TEST_ASSERT_EQUAL(0, esp_codec_dev_vol_calc_reg(NULL, 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, esp_codec_dev_vol_calc_db(NULL, 0));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, esp_codec_dev_vol_calc_hw_gain(NULL));
}

TEST_CASE("esp codec dev API test", "[mock][api]")
{
    test_esp_codec_dev_api();
}

TEST_CASE("esp codec dev wrong argument test", "[mock][api]")
{
    test_esp_codec_dev_wrong_argument();
}

TEST_CASE("esp codec dev feature should not support", "[mock][api]")
{
    test_esp_codec_dev_feature_should_not_support();
}

TEST_CASE("audio codec common new API test", "[mock][api]")
{
    test_audio_codec_new_common_api();
}

TEST_CASE("esp codec dev hw proc API test", "[mock][api][proc]")
{
    test_esp_codec_dev_hw_proc_api();
}

TEST_CASE("esp codec dev hw proc wrong argument test", "[mock][api][proc]")
{
    test_esp_codec_dev_hw_proc_wrong_arg();
}

TEST_CASE("esp codec dev volume helper wrong argument test", "[mock][api][vol]")
{
    test_esp_codec_dev_vol_wrong_arg();
}

TEST_CASE("input mirror - fake input keeps latest data", "[mock][api][mirror]")
{
    test_input_mirror_fake_input_keeps_latest_data();
}
