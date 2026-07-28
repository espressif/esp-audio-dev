/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_idf_version.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "unity.h"

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_os.h"
#include "test_board.h"
#include "test_board_periph.h"
#include "test_codec_print.h"

#if CONFIG_IDF_TARGET_ESP32S3 && defined(CONFIG_CODEC_ES7210_SUPPORT) && defined(CONFIG_CODEC_ES8311_SUPPORT)

static const char *TAG = "CODEC_DEV_OPEN_ORDER";

typedef enum {
    CASE_INIT_TX_FIRST = 0,
    CASE_INIT_RX_FIRST,
} case_init_order_t;

typedef enum {
    CASE_NEW_PLAY_FIRST = 0,
    CASE_NEW_RECORD_FIRST,
} case_new_order_t;

typedef enum {
    CASE_OPEN_PLAY_FIRST = 0,
    CASE_OPEN_RECORD_FIRST,
} case_open_order_t;

typedef enum {
    CASE_CLOSE_PLAY_FIRST = 0,
    CASE_CLOSE_RECORD_FIRST,
} case_close_order_t;

typedef struct {
    const audio_codec_data_if_t *data_if;
    const audio_codec_ctrl_if_t *ctrl_if;
    const audio_codec_gpio_if_t *gpio_if;
    const audio_codec_if_t      *codec_if;
    esp_codec_dev_handle_t       codec_dev;
} codec_inst_t;

typedef struct {
    codec_inst_t  play_inst;
    codec_inst_t  record_inst;
} codec_case_ctx_t;

typedef struct {
    esp_codec_dev_handle_t  record_dev;
    esp_codec_dev_handle_t  play_dev;
    SemaphoreHandle_t       done;
    volatile bool           stop;
    int                     total_read;
    int                     ret;
    bool                    saw_active_data;
} mirror_ctx_t;

#define CHECK_CODEC_DEV_GOTO_OK(_ret)  do {                                                        \
    int __check_ret = (_ret);                                                                      \
    if (__check_ret != ESP_CODEC_DEV_OK) {                                                         \
        ESP_LOGE(TAG, "CHECK failed: %s => %d at %s:%d", #_ret, __check_ret, __func__, __LINE__);  \
        ret = __check_ret;                                                                         \
        goto cleanup;                                                                              \
    }                                                                                              \
} while (0)

#define MIRROR_READ_SIZE   (2048)
#define MIRROR_BUF_SIZE    (4096)
#define MIRROR_TIMEOUT_MS  (1000)
#define MIRROR_TASK_STACK  (configMINIMAL_STACK_SIZE * 4)

extern const uint8_t music_pcm_start[] asm("_binary_16k_mono_16bit_pcm_start");
extern const uint8_t music_pcm_end[] asm("_binary_16k_mono_16bit_pcm_end");

static void case_teardown(codec_case_ctx_t *ctx)
{
    const audio_codec_data_if_t *play_data_if = ctx->play_inst.data_if;
    const audio_codec_data_if_t *record_data_if = ctx->record_inst.data_if;
    if (ctx->play_inst.codec_dev) {
        esp_codec_dev_close(ctx->play_inst.codec_dev);
        esp_codec_dev_delete(ctx->play_inst.codec_dev);
    }
    if (ctx->record_inst.codec_dev) {
        esp_codec_dev_close(ctx->record_inst.codec_dev);
        esp_codec_dev_delete(ctx->record_inst.codec_dev);
    }
    if (ctx->play_inst.codec_if) {
        audio_codec_delete_codec_if(ctx->play_inst.codec_if);
    }
    if (ctx->record_inst.codec_if) {
        audio_codec_delete_codec_if(ctx->record_inst.codec_if);
    }
    if (ctx->record_inst.ctrl_if) {
        audio_codec_delete_ctrl_if(ctx->record_inst.ctrl_if);
    }
    if (ctx->play_inst.ctrl_if) {
        audio_codec_delete_ctrl_if(ctx->play_inst.ctrl_if);
    }
    if (ctx->play_inst.gpio_if) {
        audio_codec_delete_gpio_if(ctx->play_inst.gpio_if);
    }
    if (record_data_if && record_data_if != play_data_if) {
        audio_codec_delete_data_if(record_data_if);
    }
    if (play_data_if) {
        audio_codec_delete_data_if(play_data_if);
    }
    memset(ctx, 0, sizeof(*ctx));
    ut_i2s_deinit(0);
    ut_i2c_deinit(0);
    ut_clr_i2s_mode();
}

static int case_setup_i2s(case_init_order_t init_order)
{
    int ret = ut_i2c_init(0, NULL);
    if (ret != ESP_OK) {
        return ret;
    }
#if SOC_I2S_SUPPORTS_TDM
    ut_set_i2s_mode(I2S_COMM_MODE_STD, I2S_COMM_MODE_TDM);
#else
    ut_set_i2s_mode(I2S_COMM_MODE_STD, I2S_COMM_MODE_STD);
#endif  /* SOC_I2S_SUPPORTS_TDM */
    ret = ut_i2s_init_channel(0);
    if (ret != ESP_OK) {
        return ret;
    }
    if (init_order == CASE_INIT_TX_FIRST) {
        ret = ut_i2s_init_tx_phase(0, NULL, I2S_CLK_SRC_DEFAULT);
        if (ret != ESP_OK) {
            return ret;
        }
        ret = ut_i2s_init_rx_phase(0, NULL, I2S_CLK_SRC_DEFAULT);
        if (ret != ESP_OK) {
            return ret;
        }
    } else {
        ret = ut_i2s_init_rx_phase(0, NULL, I2S_CLK_SRC_DEFAULT);
        if (ret != ESP_OK) {
            return ret;
        }
        ret = ut_i2s_init_tx_phase(0, NULL, I2S_CLK_SRC_DEFAULT);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    return ESP_CODEC_DEV_OK;
}

static const audio_codec_data_if_t *create_shared_data_if(void)
{
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = 0,
        .rx_handle = ut_i2s_get_rx_handle(0),
        .tx_handle = ut_i2s_get_tx_handle(0),
        .clk_src = I2S_CLK_SRC_DEFAULT,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    return data_if;
}

static int init_play_inst_with_data_if(codec_inst_t *inst, const audio_codec_data_if_t *data_if)
{
    if (inst == NULL || data_if == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    inst->data_if = data_if;

    audio_codec_i2c_cfg_t i2c_cfg = {.addr = ES8311_CODEC_DEFAULT_ADDR};
    i2c_cfg.bus_handle = ut_i2c_get_bus_handle();
    inst->ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (inst->ctrl_if == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }

    inst->gpio_if = audio_codec_new_gpio();
    if (inst->gpio_if == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = inst->ctrl_if,
        .gpio_if = inst->gpio_if,
        .sys_cfg = {
            .is_master = false,
            .no_mclk = false,
        },
        .pa_cfg = {
            .pa_pin = TEST_BOARD_PA_PIN,
            .pa_active_low = false,
        },
    };
    inst->codec_if = es8311_codec_new(&es8311_cfg);
    if (inst->codec_if == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = inst->codec_if,
        .data_if = inst->data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    inst->codec_dev = esp_codec_dev_new(&dev_cfg);
    if (inst->codec_dev == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }
    if (esp_codec_dev_set_out_vol(inst->codec_dev, TEST_CODEC_BOARD_OUT_VOL) != ESP_CODEC_DEV_OK) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    return ESP_CODEC_DEV_OK;
}

static int init_play_inst(codec_inst_t *inst)
{
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = 0,
        .rx_handle = NULL,
        .tx_handle = ut_i2s_get_tx_handle(0),
        .clk_src = I2S_CLK_SRC_DEFAULT,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (data_if == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }
    return init_play_inst_with_data_if(inst, data_if);
}

static int init_record_inst_with_data_if(codec_inst_t *inst, const audio_codec_data_if_t *data_if)
{
    if (inst == NULL || data_if == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    inst->data_if = data_if;

    audio_codec_i2c_cfg_t i2c_cfg = {.addr = ES7210_CODEC_DEFAULT_ADDR};
    i2c_cfg.bus_handle = ut_i2c_get_bus_handle();
    inst->ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (inst->ctrl_if == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }

    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = inst->ctrl_if,
        .sys_cfg = {
            .is_master = false,
            .no_mclk = false,
        },
        .adc_cfg = {
            .label = "FL,FR,RE,NA",
        },
    };
    inst->codec_if = es7210_codec_new(&es7210_cfg);
    if (inst->codec_if == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = inst->codec_if,
        .data_if = inst->data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    inst->codec_dev = esp_codec_dev_new(&dev_cfg);
    if (inst->codec_dev == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }
    if (esp_codec_dev_set_in_gain(inst->codec_dev, TEST_CODEC_BOARD_IN_GAIN) != ESP_CODEC_DEV_OK) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    if (esp_codec_dev_set_in_channel_gain(inst->codec_dev, BIT(2), 30.0) != ESP_CODEC_DEV_OK) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    return ESP_CODEC_DEV_OK;
}

static int init_record_inst(codec_inst_t *inst)
{
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = 0,
        .rx_handle = ut_i2s_get_rx_handle(0),
        .tx_handle = NULL,
        .clk_src = I2S_CLK_SRC_DEFAULT,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (data_if == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }
    return init_record_inst_with_data_if(inst, data_if);
}

static esp_codec_dev_sample_info_t make_play_fs_2ch_16bit(case_init_order_t init_order)
{
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
        .mclk_multiple = init_order == CASE_INIT_RX_FIRST ? 384 : 256,
        .channel_mask = BIT(0) | BIT(1),
    };
    return fs;
}

static esp_codec_dev_sample_info_t make_record_fs_4ch_16bit(case_init_order_t init_order)
{
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 4,
        .bits_per_sample = 16,
        .mclk_multiple = init_order == CASE_INIT_RX_FIRST ? 384 : 256,
        .channel_mask = BIT(0) | BIT(2),
    };
    return fs;
}

static esp_codec_dev_sample_info_t make_fs_2ch_32bit(case_init_order_t init_order)
{
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 32,
        .mclk_multiple = init_order == CASE_INIT_RX_FIRST ? 384 : 256,
        .channel_mask = BIT(0) | BIT(1),
    };
    return fs;
}

static int open_play(codec_case_ctx_t *ctx, const esp_codec_dev_sample_info_t *fs)
{
    if (ctx == NULL || fs == NULL || ctx->play_inst.codec_dev == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    esp_codec_dev_sample_info_t fs_local = *fs;
    return esp_codec_dev_open(ctx->play_inst.codec_dev, &fs_local);
}

static int open_record(codec_case_ctx_t *ctx, const esp_codec_dev_sample_info_t *fs)
{
    if (ctx == NULL || fs == NULL || ctx->record_inst.codec_dev == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    esp_codec_dev_sample_info_t fs_local = *fs;
    return esp_codec_dev_open(ctx->record_inst.codec_dev, &fs_local);
}

static int close_play(codec_case_ctx_t *ctx)
{
    if (ctx == NULL || ctx->play_inst.codec_dev == NULL) {
        return ESP_CODEC_DEV_OK;
    }
    return esp_codec_dev_close(ctx->play_inst.codec_dev);
}

static int close_record(codec_case_ctx_t *ctx)
{
    if (ctx == NULL || ctx->record_inst.codec_dev == NULL) {
        return ESP_CODEC_DEV_OK;
    }
    return esp_codec_dev_close(ctx->record_inst.codec_dev);
}

static int _get_bytes_per_second(const esp_codec_dev_sample_info_t *fs)
{
    int channel_mask = fs->channel_mask;
    if (channel_mask == 0) {
        for (int i = 0; i < fs->channel; i++) {
            channel_mask |= BIT(i);
        }
    }
    int channel_num = __builtin_popcount(channel_mask);
    return fs->sample_rate * channel_num * (fs->bits_per_sample >> 3);
}

static void mirror_task(void *arg)
{
    mirror_ctx_t *ctx = (mirror_ctx_t *)arg;
    uint8_t *mirror_buf = (uint8_t *)malloc(MIRROR_READ_SIZE);

    if (mirror_buf == NULL) {
        ctx->ret = ESP_CODEC_DEV_NO_MEM;
        xSemaphoreGive(ctx->done);
        vTaskDelete(NULL);
    }
    ctx->ret = ESP_CODEC_DEV_OK;
    while (!ctx->stop) {
        int bytes_read = 0;
        int ret = esp_codec_dev_mirror_read(ctx->record_dev, mirror_buf, MIRROR_READ_SIZE,
                                            MIRROR_TIMEOUT_MS, &bytes_read);
        if (ret == ESP_CODEC_DEV_OK) {
            ctx->total_read += bytes_read;
            int max_sample = 0;
            int min_sample = 0;
            codec_max_sample(mirror_buf, bytes_read, &max_sample, &min_sample);
            if (max_sample > min_sample) {
                ctx->saw_active_data = true;
            }
            ret = esp_codec_dev_write(ctx->play_dev, mirror_buf, bytes_read);
            TEST_ESP_OK(ret);
        } else if (ret == ESP_CODEC_DEV_TIMEOUT) {
            continue;
        } else if (ret == ESP_CODEC_DEV_WRONG_STATE && ctx->stop) {
            break;
        } else {
            ctx->ret = ret;
            break;
        }
    }
    free(mirror_buf);
    xSemaphoreGive(ctx->done);
    vTaskDelete(NULL);
}

static int verify_basic_io(codec_case_ctx_t *ctx,
                           const esp_codec_dev_sample_info_t *play_fs,
                           const esp_codec_dev_sample_info_t *record_fs)
{
    int ret;
    if (ctx == NULL || play_fs == NULL || record_fs == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int record_bytes = _get_bytes_per_second(record_fs) / 20;
    int total_play_bytes = 3 * _get_bytes_per_second(play_fs);
    int played_bytes = 0;
    uint8_t *recorded_all = (uint8_t *)calloc(1, total_play_bytes);
    uint8_t *record_buf = (uint8_t *)calloc(1, record_bytes);
    if (recorded_all == NULL || record_buf == NULL) {
        free(recorded_all);
        free(record_buf);
        return ESP_CODEC_DEV_NO_MEM;
    }
    ret = esp_codec_dev_mirror_cfg(ctx->record_inst.codec_dev, MIRROR_BUF_SIZE);
    TEST_ESP_OK(ret);
    mirror_ctx_t mirror_ctx = {
        .record_dev = ctx->record_inst.codec_dev,
        .play_dev = ctx->play_inst.codec_dev,
        .done = xSemaphoreCreateBinary(),
        .ret = ESP_CODEC_DEV_OK,
    };
    TEST_ASSERT_NOT_NULL(mirror_ctx.done);
    BaseType_t task_ret = xTaskCreate(mirror_task, "codec_mirror", MIRROR_TASK_STACK, &mirror_ctx,
                                      tskIDLE_PRIORITY + 1, NULL);
    TEST_ASSERT_EQUAL(pdPASS, task_ret);

    esp_codec_dev_sleep(100);
    int ret_code = ESP_CODEC_DEV_OK;
    while (played_bytes < total_play_bytes) {
        ret = esp_codec_dev_read(ctx->record_inst.codec_dev, record_buf, record_bytes);
        if (ret != ESP_CODEC_DEV_OK) {
            ret_code = ret;
            break;
        }
        // test_print_pcm_s16_head(record_buf, 4);
        memcpy(recorded_all + played_bytes, record_buf, record_bytes);
        played_bytes += record_bytes;
    }
    if (ret_code == ESP_CODEC_DEV_OK) {
        ret_code = test_analyze_recorded_pcm_s16(recorded_all, total_play_bytes, record_bytes);
    }
    mirror_ctx.stop = true;
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(mirror_ctx.done, pdMS_TO_TICKS(5000)));
    vSemaphoreDelete(mirror_ctx.done);
    TEST_ESP_OK(mirror_ctx.ret);
    TEST_ASSERT_GREATER_THAN(0, mirror_ctx.total_read);
    TEST_ASSERT_TRUE(mirror_ctx.saw_active_data);

    free(recorded_all);
    free(record_buf);
    return ret_code;
}

static int verify_play_only(codec_case_ctx_t *ctx, const esp_codec_dev_sample_info_t *play_fs)
{
    if (ctx == NULL || play_fs == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int total_play_bytes = 3 * _get_bytes_per_second(play_fs);
    int music_bytes = music_pcm_end - music_pcm_start;
    total_play_bytes = total_play_bytes < music_bytes ? total_play_bytes : music_bytes;
    return esp_codec_dev_write(ctx->play_inst.codec_dev, (void *)music_pcm_start, total_play_bytes);
}

static int verify_record_only(codec_case_ctx_t *ctx, const esp_codec_dev_sample_info_t *record_fs)
{
    int ret;
    if (ctx == NULL || record_fs == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int record_bytes = _get_bytes_per_second(record_fs) / 20;
    int total_record_bytes = 3 * _get_bytes_per_second(record_fs);
    int recorded_bytes = 0;
    uint8_t *recorded_all = (uint8_t *)calloc(1, total_record_bytes);
    if (recorded_all == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }
    esp_codec_dev_sleep(100);
    int ret_code = ESP_CODEC_DEV_OK;
    while (recorded_bytes < total_record_bytes) {
        ret = esp_codec_dev_read(ctx->record_inst.codec_dev, recorded_all + recorded_bytes, record_bytes);
        if (ret != ESP_CODEC_DEV_OK) {
            ret_code = ret;
            break;
        }
        recorded_bytes += record_bytes;
    }
    if (ret_code == ESP_CODEC_DEV_OK) {
        ret_code = test_analyze_recorded_pcm_s16(recorded_all, total_record_bytes, record_bytes);
    }
    free(recorded_all);
    return ret_code;
}

static void verify_record_label_layout(codec_case_ctx_t *ctx)
{
    char label[16] = {0};
    int ret = esp_codec_dev_get_data_layout_label(ctx->record_inst.codec_dev, label, sizeof(label));
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_OK, ret);
    TEST_ASSERT_EQUAL_STRING("FL,FR", label);
    ret = esp_codec_dev_get_data_layout_label(ctx->record_inst.codec_dev, label, 3);
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_INVALID_ARG, ret);
    ret = esp_codec_dev_set_data_layout_label(ctx->record_inst.codec_dev, "");
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_INVALID_ARG, ret);
    ret = esp_codec_dev_set_data_layout_label(ctx->record_inst.codec_dev, "FL,FL");
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_INVALID_ARG, ret);

    ret = esp_codec_dev_set_data_layout_label(ctx->record_inst.codec_dev, "FR,FL");
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_OK, ret);
    esp_codec_dev_channel_map_t order = {0};
    ret = esp_codec_dev_get_data_layout(ctx->record_inst.codec_dev, &order);
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_OK, ret);
    TEST_ASSERT_EQUAL_HEX32(ESP_CODEC_DEV_CHANNEL_MAP(2, 1, 0, 0, 0, 0, 0, 0), order.value);
}

static int verify_basic_io_with_32bit_capture_check(codec_case_ctx_t *ctx,
                                                    const esp_codec_dev_sample_info_t *play_fs,
                                                    const esp_codec_dev_sample_info_t *record_fs)
{
    int ret;
    if (ctx == NULL || play_fs == NULL || record_fs == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int io_bytes = _get_bytes_per_second(play_fs) / 20;
    int done_bytes = 0;
    uint8_t *io_buf = (uint8_t *)calloc(1, io_bytes);
    int verify_bytes = 2 * _get_bytes_per_second(record_fs);
    int total_bytes = 5 * _get_bytes_per_second(record_fs);
    int saved_bytes = 0;
    uint8_t *recorded_verify = (uint8_t *)calloc(1, verify_bytes);
    if (io_buf == NULL || recorded_verify == NULL) {
        free(io_buf);
        free(recorded_verify);
        ESP_LOGE(TAG, "verify_basic_io_with_32bit_capture_check: no memory");
        return ESP_CODEC_DEV_NO_MEM;
    }
    esp_codec_dev_sleep(100);
    int ret_code = ESP_CODEC_DEV_OK;
    while (done_bytes < total_bytes) {
        ret = esp_codec_dev_read(ctx->record_inst.codec_dev, io_buf, io_bytes);
        if (ret != ESP_CODEC_DEV_OK) {
            ret_code = ret;
            break;
        }
        if (saved_bytes < verify_bytes) {
            int copy_bytes = verify_bytes - saved_bytes;
            if (copy_bytes > io_bytes) {
                copy_bytes = io_bytes;
            }
            memcpy(recorded_verify + saved_bytes, io_buf, copy_bytes);
            saved_bytes += copy_bytes;
        }
        ret = esp_codec_dev_write(ctx->play_inst.codec_dev, io_buf, io_bytes);
        if (ret != ESP_CODEC_DEV_OK) {
            ret_code = ret;
            break;
        }
        done_bytes += io_bytes;
    }
    if (ret_code == ESP_CODEC_DEV_OK) {
        if (saved_bytes != verify_bytes) {
            ret_code = ESP_CODEC_DEV_INVALID_ARG;
        } else {
            ret_code = test_analyze_recorded_pcm_s32(recorded_verify, verify_bytes, io_bytes);
        }
    }
    free(io_buf);
    free(recorded_verify);
    return ret_code;
}

static void run_es8311_es7210_open_order_case(case_init_order_t init_order,
                                              case_new_order_t new_order,
                                              case_open_order_t open_order)
{
    codec_case_ctx_t ctx = {0};
    int ret = ESP_CODEC_DEV_OK;
    esp_codec_dev_sample_info_t play_fs = make_play_fs_2ch_16bit(init_order);
    esp_codec_dev_sample_info_t record_fs = make_record_fs_4ch_16bit(init_order);

    ret = case_setup_i2s(init_order);
    CHECK_CODEC_DEV_GOTO_OK(ret);

    if (new_order == CASE_NEW_PLAY_FIRST) {
        ret = init_play_inst(&ctx.play_inst);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = init_record_inst(&ctx.record_inst);
        CHECK_CODEC_DEV_GOTO_OK(ret);
    } else {
        ret = init_record_inst(&ctx.record_inst);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = init_play_inst(&ctx.play_inst);
        CHECK_CODEC_DEV_GOTO_OK(ret);
    }

    if (open_order == CASE_OPEN_PLAY_FIRST) {
        ret = open_play(&ctx, &play_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = open_record(&ctx, &record_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
    } else {
        ret = open_record(&ctx, &record_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = open_play(&ctx, &play_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
    }

    ret = verify_basic_io(&ctx, &play_fs, &record_fs);
    CHECK_CODEC_DEV_GOTO_OK(ret);
    verify_record_label_layout(&ctx);

cleanup:
    case_teardown(&ctx);
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_OK, ret);
}

static void run_es8311_es7210_shared_data_if_case(case_init_order_t init_order,
                                                  case_new_order_t new_order,
                                                  case_open_order_t open_order)
{
    codec_case_ctx_t ctx = {0};
    int ret = ESP_CODEC_DEV_OK;
    esp_codec_dev_sample_info_t play_fs = make_play_fs_2ch_16bit(init_order);
    esp_codec_dev_sample_info_t record_fs = make_record_fs_4ch_16bit(init_order);
    const audio_codec_data_if_t *shared_data_if = NULL;

    ret = case_setup_i2s(init_order);
    CHECK_CODEC_DEV_GOTO_OK(ret);
    shared_data_if = create_shared_data_if();
    if (shared_data_if == NULL) {
        ret = ESP_CODEC_DEV_NO_MEM;
        goto cleanup;
    }

    if (new_order == CASE_NEW_PLAY_FIRST) {
        ret = init_play_inst_with_data_if(&ctx.play_inst, shared_data_if);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = init_record_inst_with_data_if(&ctx.record_inst, shared_data_if);
        CHECK_CODEC_DEV_GOTO_OK(ret);
    } else {
        ret = init_record_inst_with_data_if(&ctx.record_inst, shared_data_if);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = init_play_inst_with_data_if(&ctx.play_inst, shared_data_if);
        CHECK_CODEC_DEV_GOTO_OK(ret);
    }

    if (open_order == CASE_OPEN_PLAY_FIRST) {
        ret = open_play(&ctx, &play_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = open_record(&ctx, &record_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
    } else {
        ret = open_record(&ctx, &record_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = open_play(&ctx, &play_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
    }

    ret = verify_basic_io(&ctx, &play_fs, &record_fs);
cleanup:
    case_teardown(&ctx);
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_OK, ret);
}

static void run_es8311_es7210_close_order_case(bool shared_data_if,
                                               case_init_order_t init_order,
                                               case_open_order_t open_order,
                                               case_close_order_t close_order)
{
    codec_case_ctx_t ctx = {0};
    int ret = ESP_CODEC_DEV_OK;
    esp_codec_dev_sample_info_t play_fs = make_play_fs_2ch_16bit(init_order);
    esp_codec_dev_sample_info_t record_fs = make_record_fs_4ch_16bit(init_order);
    const audio_codec_data_if_t *shared = NULL;

    ret = case_setup_i2s(init_order);
    CHECK_CODEC_DEV_GOTO_OK(ret);
    if (shared_data_if) {
        shared = create_shared_data_if();
        if (shared == NULL) {
            ret = ESP_CODEC_DEV_NO_MEM;
            goto cleanup;
        }
        ret = init_play_inst_with_data_if(&ctx.play_inst, shared);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = init_record_inst_with_data_if(&ctx.record_inst, shared);
        CHECK_CODEC_DEV_GOTO_OK(ret);
    } else {
        ret = init_play_inst(&ctx.play_inst);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = init_record_inst(&ctx.record_inst);
        CHECK_CODEC_DEV_GOTO_OK(ret);
    }

    if (open_order == CASE_OPEN_PLAY_FIRST) {
        ret = open_play(&ctx, &play_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = open_record(&ctx, &record_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
    } else {
        ret = open_record(&ctx, &record_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = open_play(&ctx, &play_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
    }

    ret = verify_basic_io(&ctx, &play_fs, &record_fs);
    CHECK_CODEC_DEV_GOTO_OK(ret);
    if (close_order == CASE_CLOSE_PLAY_FIRST) {
        ret = close_play(&ctx);
    } else {
        ret = close_record(&ctx);
    }
    CHECK_CODEC_DEV_GOTO_OK(ret);
    if (close_order == CASE_CLOSE_PLAY_FIRST) {
        ret = close_record(&ctx);
    } else {
        ret = close_play(&ctx);
    }

cleanup:
    case_teardown(&ctx);
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_OK, ret);
}

static void run_es8311_es7210_single_side_case(bool shared_data_if,
                                               bool playback_only,
                                               case_init_order_t init_order)
{
    codec_case_ctx_t ctx = {0};
    int ret = ESP_CODEC_DEV_OK;
    esp_codec_dev_sample_info_t play_fs = make_play_fs_2ch_16bit(init_order);
    esp_codec_dev_sample_info_t record_fs = make_record_fs_4ch_16bit(init_order);
    const audio_codec_data_if_t *shared = NULL;

    ret = case_setup_i2s(init_order);
    CHECK_CODEC_DEV_GOTO_OK(ret);
    if (shared_data_if) {
        shared = create_shared_data_if();
        if (shared == NULL) {
            ret = ESP_CODEC_DEV_NO_MEM;
            goto cleanup;
        }
    }

    if (playback_only) {
        if (shared_data_if) {
            ret = init_play_inst_with_data_if(&ctx.play_inst, shared);
        } else {
            ret = init_play_inst(&ctx.play_inst);
        }
        CHECK_CODEC_DEV_GOTO_OK(ret);
        play_fs.bits_per_sample = 16;
        play_fs.sample_rate = 16000;
        play_fs.channel = 2;
        play_fs.channel_mask = 0x01;
        ret = open_play(&ctx, &play_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = verify_play_only(&ctx, &play_fs);
    } else {
        if (shared_data_if) {
            ret = init_record_inst_with_data_if(&ctx.record_inst, shared);
        } else {
            ret = init_record_inst(&ctx.record_inst);
        }
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = open_record(&ctx, &record_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = verify_record_only(&ctx, &record_fs);
    }

cleanup:
    case_teardown(&ctx);
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_OK, ret);
}

static void run_es8311_es7210_assist_lifecycle_case(bool shared_data_if,
                                                    case_init_order_t init_order,
                                                    bool start_with_playback)
{
    codec_case_ctx_t ctx = {0};
    int ret = ESP_CODEC_DEV_OK;
    esp_codec_dev_sample_info_t play_fs = make_play_fs_2ch_16bit(init_order);
    esp_codec_dev_sample_info_t record_fs = make_record_fs_4ch_16bit(init_order);
    const audio_codec_data_if_t *shared = NULL;

    ret = case_setup_i2s(init_order);
    CHECK_CODEC_DEV_GOTO_OK(ret);
    if (shared_data_if) {
        shared = create_shared_data_if();
        if (shared == NULL) {
            ret = ESP_CODEC_DEV_NO_MEM;
            goto cleanup;
        }
        ret = init_play_inst_with_data_if(&ctx.play_inst, shared);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = init_record_inst_with_data_if(&ctx.record_inst, shared);
        CHECK_CODEC_DEV_GOTO_OK(ret);
    } else {
        ret = init_play_inst(&ctx.play_inst);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = init_record_inst(&ctx.record_inst);
        CHECK_CODEC_DEV_GOTO_OK(ret);
    }

    if (start_with_playback) {
        esp_codec_dev_sample_info_t play_only_fs = play_fs;
        play_only_fs.sample_rate = 16000;
        play_only_fs.channel = 2;
        play_only_fs.channel_mask = BIT(0);
        ret = open_play(&ctx, &play_only_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = verify_play_only(&ctx, &play_only_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = close_play(&ctx);
        CHECK_CODEC_DEV_GOTO_OK(ret);

        ret = open_record(&ctx, &record_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = verify_record_only(&ctx, &record_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = close_record(&ctx);
        CHECK_CODEC_DEV_GOTO_OK(ret);

        ret = open_play(&ctx, &play_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = open_record(&ctx, &record_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
    } else {
        ret = open_record(&ctx, &record_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = verify_record_only(&ctx, &record_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = close_record(&ctx);
        CHECK_CODEC_DEV_GOTO_OK(ret);

        esp_codec_dev_sample_info_t play_only_fs = play_fs;
        play_only_fs.sample_rate = 16000;
        play_only_fs.channel = 2;
        play_only_fs.channel_mask = BIT(0);
        ret = open_play(&ctx, &play_only_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = verify_play_only(&ctx, &play_only_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = close_play(&ctx);
        CHECK_CODEC_DEV_GOTO_OK(ret);

        ret = open_record(&ctx, &record_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
        ret = open_play(&ctx, &play_fs);
        CHECK_CODEC_DEV_GOTO_OK(ret);
    }

    ret = verify_basic_io(&ctx, &play_fs, &record_fs);
    CHECK_CODEC_DEV_GOTO_OK(ret);
    ret = close_play(&ctx);
    CHECK_CODEC_DEV_GOTO_OK(ret);
    ret = close_record(&ctx);

cleanup:
    if (ret != ESP_CODEC_DEV_OK && ret != ESP_CODEC_DEV_WRONG_STATE) {
        // ensure both directions are attempted to be closed before tear down
        close_play(&ctx);
        close_record(&ctx);
    }
    case_teardown(&ctx);
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_OK, ret);
}

static void run_es8311_es7210_equal_total_bits_case(case_init_order_t init_order,
                                                    case_open_order_t open_order)
{
    codec_case_ctx_t ctx = {0};
    int ret = ESP_CODEC_DEV_OK;
    esp_codec_dev_sample_info_t play_fs = make_fs_2ch_32bit(init_order);
    esp_codec_dev_sample_info_t record_fs = make_fs_2ch_32bit(init_order);

    ret = case_setup_i2s(init_order);
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_OK, ret);
    ret = init_play_inst(&ctx.play_inst);
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_OK, ret);
    ret = init_record_inst(&ctx.record_inst);
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_OK, ret);

    if (open_order == CASE_OPEN_PLAY_FIRST) {
        ret = open_play(&ctx, &play_fs);
        TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_OK, ret);
        ret = open_record(&ctx, &record_fs);
        TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_OK, ret);
    } else {
        ret = open_record(&ctx, &record_fs);
        TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_OK, ret);
        ret = open_play(&ctx, &play_fs);
        TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_OK, ret);
    }

    ret = verify_basic_io_with_32bit_capture_check(&ctx, &play_fs, &record_fs);
    case_teardown(&ctx);
    TEST_ASSERT_EQUAL_INT(ESP_CODEC_DEV_OK, ret);
}

#define DEFINE_ORDER_CASE(_name, _init_order, _new_order, _open_order)            \
    TEST_CASE(_name, "[korvo2_v3][i2s_order]")                                    \
    {                                                                             \
        run_es8311_es7210_open_order_case(_init_order, _new_order, _open_order);  \
    }

#define DEFINE_SHARED_ORDER_CASE(_name, _init_order, _new_order, _open_order)         \
    TEST_CASE(_name, "[korvo2_v3][i2s_order][shared_data_if]")                        \
    {                                                                                 \
        run_es8311_es7210_shared_data_if_case(_init_order, _new_order, _open_order);  \
    }

#define DEFINE_CLOSE_ORDER_CASE(_name, _shared, _init_order, _open_order, _close_order)       \
    TEST_CASE(_name, "[korvo2_v3][i2s_order][close_order]")                                   \
    {                                                                                         \
        run_es8311_es7210_close_order_case(_shared, _init_order, _open_order, _close_order);  \
    }

#define DEFINE_SINGLE_SIDE_CASE(_name, _shared, _playback_only, _init_order)       \
    TEST_CASE(_name, "[korvo2_v3][i2s_order][single_side]")                        \
    {                                                                              \
        run_es8311_es7210_single_side_case(_shared, _playback_only, _init_order);  \
    }

#define DEFINE_ASSIST_LIFECYCLE_CASE(_name, _shared, _init_order, _start_with_playback)       \
    TEST_CASE(_name, "[korvo2_v3][i2s_order][assist_lifecycle]")                              \
    {                                                                                         \
        run_es8311_es7210_assist_lifecycle_case(_shared, _init_order, _start_with_playback);  \
    }

#define DEFINE_EQUAL_TOTAL_BITS_CASE(_name, _init_order, _open_order)       \
    TEST_CASE(_name, "[korvo2_v3][i2s_order][equal_total_bits]")            \
    {                                                                       \
        run_es8311_es7210_equal_total_bits_case(_init_order, _open_order);  \
    }

DEFINE_ORDER_CASE("es8311+es7210 isolated data_if init-tx-first new-play-first open-play-first",
                  CASE_INIT_TX_FIRST, CASE_NEW_PLAY_FIRST, CASE_OPEN_PLAY_FIRST)
DEFINE_ORDER_CASE("es8311+es7210 isolated data_if init-tx-first new-play-first open-record-first",
                  CASE_INIT_TX_FIRST, CASE_NEW_PLAY_FIRST, CASE_OPEN_RECORD_FIRST)
DEFINE_ORDER_CASE("es8311+es7210 isolated data_if init-tx-first new-record-first open-play-first",
                  CASE_INIT_TX_FIRST, CASE_NEW_RECORD_FIRST, CASE_OPEN_PLAY_FIRST)
DEFINE_ORDER_CASE("es8311+es7210 isolated data_if init-tx-first new-record-first open-record-first",
                  CASE_INIT_TX_FIRST, CASE_NEW_RECORD_FIRST, CASE_OPEN_RECORD_FIRST)
DEFINE_ORDER_CASE("es8311+es7210 isolated data_if init-rx-first new-play-first open-play-first",
                  CASE_INIT_RX_FIRST, CASE_NEW_PLAY_FIRST, CASE_OPEN_PLAY_FIRST)
DEFINE_ORDER_CASE("es8311+es7210 isolated data_if init-rx-first new-play-first open-record-first",
                  CASE_INIT_RX_FIRST, CASE_NEW_PLAY_FIRST, CASE_OPEN_RECORD_FIRST)
DEFINE_ORDER_CASE("es8311+es7210 isolated data_if init-rx-first new-record-first open-play-first",
                  CASE_INIT_RX_FIRST, CASE_NEW_RECORD_FIRST, CASE_OPEN_PLAY_FIRST)
DEFINE_ORDER_CASE("es8311+es7210 isolated data_if init-rx-first new-record-first open-record-first",
                  CASE_INIT_RX_FIRST, CASE_NEW_RECORD_FIRST, CASE_OPEN_RECORD_FIRST)

DEFINE_SHARED_ORDER_CASE("es8311+es7210 shared data_if init-tx-first new-play-first open-play-first",
                         CASE_INIT_TX_FIRST, CASE_NEW_PLAY_FIRST, CASE_OPEN_PLAY_FIRST)
DEFINE_SHARED_ORDER_CASE("es8311+es7210 shared data_if init-tx-first new-play-first open-record-first",
                         CASE_INIT_TX_FIRST, CASE_NEW_PLAY_FIRST, CASE_OPEN_RECORD_FIRST)
DEFINE_SHARED_ORDER_CASE("es8311+es7210 shared data_if init-tx-first new-record-first open-play-first",
                         CASE_INIT_TX_FIRST, CASE_NEW_RECORD_FIRST, CASE_OPEN_PLAY_FIRST)
DEFINE_SHARED_ORDER_CASE("es8311+es7210 shared data_if init-tx-first new-record-first open-record-first",
                         CASE_INIT_TX_FIRST, CASE_NEW_RECORD_FIRST, CASE_OPEN_RECORD_FIRST)
DEFINE_SHARED_ORDER_CASE("es8311+es7210 shared data_if init-rx-first new-play-first open-play-first",
                         CASE_INIT_RX_FIRST, CASE_NEW_PLAY_FIRST, CASE_OPEN_PLAY_FIRST)
DEFINE_SHARED_ORDER_CASE("es8311+es7210 shared data_if init-rx-first new-play-first open-record-first",
                         CASE_INIT_RX_FIRST, CASE_NEW_PLAY_FIRST, CASE_OPEN_RECORD_FIRST)
DEFINE_SHARED_ORDER_CASE("es8311+es7210 shared data_if init-rx-first new-record-first open-play-first",
                         CASE_INIT_RX_FIRST, CASE_NEW_RECORD_FIRST, CASE_OPEN_PLAY_FIRST)
DEFINE_SHARED_ORDER_CASE("es8311+es7210 shared data_if init-rx-first new-record-first open-record-first",
                         CASE_INIT_RX_FIRST, CASE_NEW_RECORD_FIRST, CASE_OPEN_RECORD_FIRST)

DEFINE_CLOSE_ORDER_CASE("es8311+es7210 isolated data_if init-rx-first open-play-first close-play-first",
                        false, CASE_INIT_RX_FIRST, CASE_OPEN_PLAY_FIRST, CASE_CLOSE_PLAY_FIRST)
DEFINE_CLOSE_ORDER_CASE("es8311+es7210 isolated data_if init-rx-first open-play-first close-record-first",
                        false, CASE_INIT_RX_FIRST, CASE_OPEN_PLAY_FIRST, CASE_CLOSE_RECORD_FIRST)
DEFINE_CLOSE_ORDER_CASE("es8311+es7210 isolated data_if init-rx-first open-record-first close-play-first",
                        false, CASE_INIT_RX_FIRST, CASE_OPEN_RECORD_FIRST, CASE_CLOSE_PLAY_FIRST)
DEFINE_CLOSE_ORDER_CASE("es8311+es7210 isolated data_if init-rx-first open-record-first close-record-first",
                        false, CASE_INIT_RX_FIRST, CASE_OPEN_RECORD_FIRST, CASE_CLOSE_RECORD_FIRST)
DEFINE_CLOSE_ORDER_CASE("es8311+es7210 shared data_if init-rx-first open-play-first close-play-first",
                        true, CASE_INIT_RX_FIRST, CASE_OPEN_PLAY_FIRST, CASE_CLOSE_PLAY_FIRST)
DEFINE_CLOSE_ORDER_CASE("es8311+es7210 shared data_if init-rx-first open-play-first close-record-first",
                        true, CASE_INIT_RX_FIRST, CASE_OPEN_PLAY_FIRST, CASE_CLOSE_RECORD_FIRST)
DEFINE_CLOSE_ORDER_CASE("es8311+es7210 shared data_if init-rx-first open-record-first close-play-first",
                        true, CASE_INIT_RX_FIRST, CASE_OPEN_RECORD_FIRST, CASE_CLOSE_PLAY_FIRST)
DEFINE_CLOSE_ORDER_CASE("es8311+es7210 shared data_if init-rx-first open-record-first close-record-first",
                        true, CASE_INIT_RX_FIRST, CASE_OPEN_RECORD_FIRST, CASE_CLOSE_RECORD_FIRST)

DEFINE_SINGLE_SIDE_CASE("es8311 isolated data_if playback-only init-tx-first",
                        false, true, CASE_INIT_TX_FIRST)
DEFINE_SINGLE_SIDE_CASE("es8311 isolated data_if playback-only init-rx-first",
                        false, true, CASE_INIT_RX_FIRST)
DEFINE_SINGLE_SIDE_CASE("es7210 isolated data_if record-only init-tx-first",
                        false, false, CASE_INIT_TX_FIRST)
DEFINE_SINGLE_SIDE_CASE("es7210 isolated data_if record-only init-rx-first",
                        false, false, CASE_INIT_RX_FIRST)
DEFINE_SINGLE_SIDE_CASE("es8311 shared data_if playback-only init-tx-first",
                        true, true, CASE_INIT_TX_FIRST)
DEFINE_SINGLE_SIDE_CASE("es8311 shared data_if playback-only init-rx-first",
                        true, true, CASE_INIT_RX_FIRST)
DEFINE_SINGLE_SIDE_CASE("es7210 shared data_if record-only init-tx-first",
                        true, false, CASE_INIT_TX_FIRST)
DEFINE_SINGLE_SIDE_CASE("es7210 shared data_if record-only init-rx-first",
                        true, false, CASE_INIT_RX_FIRST)

DEFINE_ASSIST_LIFECYCLE_CASE("es8311+es7210 isolated data_if assist lifecycle init-rx-first play-close-record-close-duplex",
                             false, CASE_INIT_RX_FIRST, true)
DEFINE_ASSIST_LIFECYCLE_CASE("es8311+es7210 isolated data_if assist lifecycle init-tx-first record-close-play-close-duplex",
                             false, CASE_INIT_TX_FIRST, false)
DEFINE_ASSIST_LIFECYCLE_CASE("es8311+es7210 shared data_if assist lifecycle init-rx-first play-close-record-close-duplex",
                             true, CASE_INIT_RX_FIRST, true)
DEFINE_ASSIST_LIFECYCLE_CASE("es8311+es7210 shared data_if assist lifecycle init-tx-first record-close-play-close-duplex",
                             true, CASE_INIT_TX_FIRST, false)

DEFINE_EQUAL_TOTAL_BITS_CASE("es8311+es7210 isolated data_if equal-total-bits init-tx-first open-play-first",
                             CASE_INIT_TX_FIRST, CASE_OPEN_PLAY_FIRST)
DEFINE_EQUAL_TOTAL_BITS_CASE("es8311+es7210 isolated data_if equal-total-bits init-tx-first open-record-first",
                             CASE_INIT_TX_FIRST, CASE_OPEN_RECORD_FIRST)
DEFINE_EQUAL_TOTAL_BITS_CASE("es8311+es7210 isolated data_if equal-total-bits init-rx-first open-play-first",
                             CASE_INIT_RX_FIRST, CASE_OPEN_PLAY_FIRST)
DEFINE_EQUAL_TOTAL_BITS_CASE("es8311+es7210 isolated data_if equal-total-bits init-rx-first open-record-first",
                             CASE_INIT_RX_FIRST, CASE_OPEN_RECORD_FIRST)
#endif  /* CONFIG_IDF_TARGET_ESP32S3 && defined(CONFIG_CODEC_ES7210_SUPPORT) && defined(CONFIG_CODEC_ES8311_SUPPORT) */
