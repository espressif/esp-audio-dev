/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

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
#define CHATBOT_I2C_SCL_SPEED_HZ        (400000)
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
/* Temporary: S31 needs a slower I2C clock for reliable codec access. */
#define CHATBOT_I2C_SCL_SPEED_HZ        (10000)
#endif  /* CONFIG_IDF_TARGET_ESP32S3 */

#define CHATBOT_PLAY_TASK_STACK   (configMINIMAL_STACK_SIZE * 4)
#define CHATBOT_PLAY_CHUNK_BYTES  (2048)
#define CHATBOT_TASK_JOIN_MS      (5000)

extern const uint8_t music_pcm_start[] asm("_binary_16k_mono_16bit_pcm_start");
extern const uint8_t music_pcm_end[] asm("_binary_16k_mono_16bit_pcm_end");

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

typedef enum {
    CHATBOT_OPEN_TX_FIRST = 0,
    CHATBOT_OPEN_RX_FIRST,
} chatbot_open_order_t;

typedef struct {
    uint8_t   tx_channel;
    uint16_t  tx_mask;
    uint8_t   rx_channel;
    uint16_t  rx_mask;
    uint8_t   tx_bits;
    uint8_t   rx_bits;
    uint8_t   total_slot;
    uint8_t   tx_total_slot;
    uint8_t   rx_total_slot;
    uint16_t  tx_slot_mask;
    uint16_t  rx_slot_mask;
    uint32_t  tx_layout;
    uint32_t  rx_layout;
} chatbot_case_spec_t;

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
} chatbot_case_ctx_t;

typedef struct {
    esp_codec_dev_bus_info_t     tx_bus;
    esp_codec_dev_bus_info_t     rx_bus;
    esp_codec_dev_channel_map_t  tx_layout;
    esp_codec_dev_channel_map_t  rx_layout;
} chatbot_case_result_t;

typedef struct {
    esp_codec_dev_handle_t  play_dev;
    uint8_t                 bits_per_sample;
    volatile bool           stop;
    int                     ret;
    SemaphoreHandle_t       done;
} chatbot_play_ctx_t;

static uint8_t chatbot_spec_bits(uint8_t bits)
{
    return bits != 0 ? bits : 16;
}

static uint8_t chatbot_spec_slots(uint8_t slots, uint8_t total_slot)
{
    return slots != 0 ? slots : total_slot;
}

static void chatbot_case_teardown(chatbot_case_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    if (ctx->record_dev) {
        esp_codec_dev_close(ctx->record_dev);
        esp_codec_dev_delete(ctx->record_dev);
    }
    if (ctx->play_dev) {
        esp_codec_dev_close(ctx->play_dev);
        esp_codec_dev_delete(ctx->play_dev);
    }
    if (ctx->in_codec_if) {
        audio_codec_delete_codec_if(ctx->in_codec_if);
    }
    if (ctx->out_codec_if) {
        audio_codec_delete_codec_if(ctx->out_codec_if);
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
    memset(ctx, 0, sizeof(*ctx));
    ut_i2s_deinit(0);
    ut_i2c_deinit(0);
    ut_clr_i2s_mode();
}

static int chatbot_case_setup(chatbot_case_ctx_t *ctx)
{
    if (ctx == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    memset(ctx, 0, sizeof(*ctx));
    ut_set_i2s_mode(I2S_COMM_MODE_TDM, I2S_COMM_MODE_TDM);
    int ret = ut_i2c_init(0, (codec_i2c_pin_t *)&s_chatbot_i2c_pin);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    ret = ut_i2s_init_channel(0);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    ret = ut_i2s_init_tx_phase(0, (codec_i2s_pin_t *)&s_chatbot_i2s_pin, CHATBOT_I2S_CLK_SRC_TX_DEFAULT);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    audio_codec_i2s_cfg_t out_i2s_cfg = {
        .port = 0,
        .tx_handle = ut_i2s_get_tx_handle(0),
        .clk_src = CHATBOT_I2S_CLK_SRC_TX_DEFAULT,
    };
    ctx->out_data_if = audio_codec_new_i2s_data(&out_i2s_cfg);
    if (ctx->out_data_if == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }

    ret = ut_i2s_init_rx_phase(0, (codec_i2s_pin_t *)&s_chatbot_i2s_pin, CHATBOT_I2S_CLK_SRC_RX_DEFAULT);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    audio_codec_i2s_cfg_t in_i2s_cfg = {
        .port = 0,
        .rx_handle = ut_i2s_get_rx_handle(0),
        .clk_src = CHATBOT_I2S_CLK_SRC_RX_DEFAULT,
    };
    ctx->in_data_if = audio_codec_new_i2s_data(&in_i2s_cfg);
    if (ctx->in_data_if == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }

    audio_codec_i2c_cfg_t i2c_cfg = {
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = ut_i2c_get_bus_handle(),
        .clock_speed_hz = CHATBOT_I2C_SCL_SPEED_HZ,
    };
    ctx->out_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (ctx->out_ctrl_if == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }
    i2c_cfg.addr = ES7210_CODEC_DEFAULT_ADDR;
    ctx->in_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (ctx->in_ctrl_if == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }
    ctx->gpio_if = audio_codec_new_gpio();
    if (ctx->gpio_if == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = ctx->out_ctrl_if,
        .gpio_if = ctx->gpio_if,
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
    ctx->out_codec_if = es8311_codec_new(&es8311_cfg);
    if (ctx->out_codec_if == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }
    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = ctx->in_ctrl_if,
        .sys_cfg = {
            .is_master = false,
            .no_mclk = false,
        },
    };
    ctx->in_codec_if = es7210_codec_new(&es7210_cfg);
    if (ctx->in_codec_if == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = ctx->out_codec_if,
        .data_if = ctx->out_data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    ctx->play_dev = esp_codec_dev_new(&dev_cfg);
    if (ctx->play_dev == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }
    dev_cfg.codec_if = ctx->in_codec_if;
    dev_cfg.data_if = ctx->in_data_if;
    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
    ctx->record_dev = esp_codec_dev_new(&dev_cfg);
    if (ctx->record_dev == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }
    ret = esp_codec_dev_set_out_vol(ctx->play_dev, 80);
    if (ret == ESP_CODEC_DEV_OK) {
        ret = esp_codec_dev_set_in_gain(ctx->record_dev, TEST_CODEC_BOARD_IN_GAIN);
    }
    return ret;
}

static int chatbot_play_music(esp_codec_dev_handle_t play_dev)
{
    esp_codec_dev_sample_info_t play_fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = BIT(0),
        .mclk_multiple = 384,
    };
    int ret = esp_codec_dev_open(play_dev, &play_fs);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    printf("Play music start\n");
    ret = esp_codec_dev_write(play_dev, (uint8_t *)music_pcm_start, music_pcm_end - music_pcm_start);
    esp_codec_dev_sleep(200);
    int close_ret = esp_codec_dev_close(play_dev);
    return (ret != ESP_CODEC_DEV_OK) ? ret : close_ret;
}

/* The ES8311 ADC feeds the cascaded RX slots on this board, so it has to run even though the
   playback handle only opened the DAC direction. */
static int chatbot_enable_out_adc(chatbot_case_ctx_t *ctx)
{
#if !CONFIG_IDF_TARGET_ESP32S3
    if (ctx->out_codec_if->adc_if && ctx->out_codec_if->adc_if->ops.enable) {
        return ctx->out_codec_if->adc_if->ops.enable(ctx->out_codec_if, true);
    }
#endif  /* !CONFIG_IDF_TARGET_ESP32S3 */
    (void)ctx;
    return ESP_CODEC_DEV_OK;
}

static void chatbot_play_task(void *arg)
{
    chatbot_play_ctx_t *ctx = (chatbot_play_ctx_t *)arg;
    const uint8_t *cur = music_pcm_start;
    int32_t *expand = NULL;

    ctx->ret = ESP_CODEC_DEV_OK;
    if (ctx->bits_per_sample == 32) {
        expand = (int32_t *)malloc(CHATBOT_PLAY_CHUNK_BYTES * 2);
        if (expand == NULL) {
            ctx->ret = ESP_CODEC_DEV_NO_MEM;
            xSemaphoreGive(ctx->done);
            vTaskDelete(NULL);
            return;
        }
    }
    while (!ctx->stop) {
        int left = (int)(music_pcm_end - cur);
        if (left <= 0) {
            cur = music_pcm_start;
            continue;
        }
        int chunk = (left < CHATBOT_PLAY_CHUNK_BYTES) ? left : CHATBOT_PLAY_CHUNK_BYTES;
        chunk &= ~1;
        if (chunk <= 0) {
            cur = music_pcm_start;
            continue;
        }
        // A Unity assertion here would longjmp out of this task and leave the caller waiting on
        // ctx->done. Report through ctx and let the caller assert.
        int ret;
        if (ctx->bits_per_sample == 32) {
            const int16_t *src = (const int16_t *)cur;
            int samples = chunk / 2;
            for (int i = 0; i < samples; i++) {
                expand[i] = ((int32_t)src[i]) << 16;
            }
            ret = esp_codec_dev_write(ctx->play_dev, expand, samples * (int)sizeof(int32_t));
        } else {
            ret = esp_codec_dev_write(ctx->play_dev, (void *)cur, chunk);
        }
        if (ret != ESP_CODEC_DEV_OK) {
            ctx->ret = ret;
            break;
        }
        cur += chunk;
    }
    free(expand);
    xSemaphoreGive(ctx->done);
    vTaskDelete(NULL);
}

/* Keeps TX busy from a separate task while the caller validates capture, for frames whose active
   channel count differs between the two directions. */
static int chatbot_start_play_task(chatbot_play_ctx_t *play_ctx, esp_codec_dev_handle_t play_dev,
                                   uint8_t bits_per_sample)
{
    play_ctx->play_dev = play_dev;
    play_ctx->bits_per_sample = bits_per_sample;
    play_ctx->stop = false;
    play_ctx->ret = ESP_CODEC_DEV_OK;
    play_ctx->done = xSemaphoreCreateBinary();
    if (play_ctx->done == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }
    if (xTaskCreate(chatbot_play_task, "chatbot_play", CHATBOT_PLAY_TASK_STACK, play_ctx,
                    tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        vSemaphoreDelete(play_ctx->done);
        play_ctx->done = NULL;
        return ESP_CODEC_DEV_NO_MEM;
    }
    return ESP_CODEC_DEV_OK;
}

static int chatbot_stop_play_task(chatbot_play_ctx_t *play_ctx)
{
    play_ctx->stop = true;
    int ret = play_ctx->ret;
    if (xSemaphoreTake(play_ctx->done, pdMS_TO_TICKS(CHATBOT_TASK_JOIN_MS)) != pdTRUE) {
        ret = (ret == ESP_CODEC_DEV_OK) ? ESP_CODEC_DEV_TIMEOUT : ret;
    }
    vSemaphoreDelete(play_ctx->done);
    play_ctx->done = NULL;
    return ret;
}

/* Reads whole capture frames and requires each chunk to carry varying samples. When loopback_dev is
   set, every chunk is echoed back verbatim, which is only meaningful if both directions carry the
   same number of active channels. */
static int chatbot_pump_frames(esp_codec_dev_handle_t record_dev, esp_codec_dev_handle_t loopback_dev,
                               uint8_t *data, int record_bytes, int limit_size)
{
    int got_size = 0;
    int ret = ESP_CODEC_DEV_OK;
    while (got_size < limit_size) {
        ret = esp_codec_dev_read(record_dev, data, record_bytes);
        if (ret != ESP_CODEC_DEV_OK) {
            break;
        }
        test_print_pcm_s16_head(data, 6);
        if (loopback_dev != NULL) {
            ret = esp_codec_dev_write(loopback_dev, data, record_bytes);
            if (ret != ESP_CODEC_DEV_OK) {
                break;
            }
        }
        int max_sample = 0;
        int min_sample = 0;
        codec_max_sample(data, record_bytes, &max_sample, &min_sample);
        if (max_sample == min_sample) {
            ret = ESP_CODEC_DEV_READ_FAIL;
            break;
        }
        got_size += record_bytes;
    }
    return ret;
}

/* Runs both directions for a few seconds. When playback and capture agree on the active channel
   count the captured frames are echoed back from this loop; otherwise playback is driven by its own
   task and this loop only validates capture. */
static int chatbot_verify_dma(chatbot_case_ctx_t *ctx,
                              const esp_codec_dev_sample_info_t *play_fs,
                              const esp_codec_dev_sample_info_t *record_fs)
{
    int ret = chatbot_enable_out_adc(ctx);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    const int chunk_frames = 240 * 3;
    const int play_active_ch = __builtin_popcount(play_fs->channel_mask);
    const int record_active_ch = __builtin_popcount(record_fs->channel_mask);
    const int bytes_per_sample = record_fs->bits_per_sample >> 3;
    const bool loopback = (play_active_ch == record_active_ch);
    int record_bytes = chunk_frames * record_active_ch * bytes_per_sample;
    uint8_t *data = (uint8_t *)malloc(record_bytes);
    if (data == NULL) {
        return ESP_CODEC_DEV_NO_MEM;
    }
    chatbot_play_ctx_t play_ctx = {0};
    if (!loopback) {
        ret = chatbot_start_play_task(&play_ctx, ctx->play_dev, play_fs->bits_per_sample);
        if (ret != ESP_CODEC_DEV_OK) {
            free(data);
            return ret;
        }
    }
    int limit_size = 5 * record_fs->sample_rate * record_active_ch * bytes_per_sample;
    esp_codec_dev_sleep(200);
    ret = chatbot_pump_frames(ctx->record_dev, loopback ? ctx->play_dev : NULL, data, record_bytes,
                              limit_size);
    int play_ret = loopback ? ESP_CODEC_DEV_OK : chatbot_stop_play_task(&play_ctx);
    free(data);
    return (ret != ESP_CODEC_DEV_OK) ? ret : play_ret;
}

static int chatbot_run_order(chatbot_case_ctx_t *ctx, const chatbot_case_spec_t *spec,
                             chatbot_open_order_t open_order, chatbot_case_result_t *result)
{
    if (ctx == NULL || spec == NULL || result == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    memset(result, 0, sizeof(*result));
    esp_codec_dev_sample_info_t play_fs = {
        .sample_rate = 16000,
        .channel = spec->tx_channel,
        .bits_per_sample = chatbot_spec_bits(spec->tx_bits),
        .channel_mask = spec->tx_mask,
        .mclk_multiple = 384,
    };
    esp_codec_dev_sample_info_t record_fs = {
        .sample_rate = 16000,
        .channel = spec->rx_channel,
        .bits_per_sample = chatbot_spec_bits(spec->rx_bits),
        .channel_mask = spec->rx_mask,
        .mclk_multiple = 384,
    };
    int ret;
    if (open_order == CHATBOT_OPEN_TX_FIRST) {
        ret = esp_codec_dev_open(ctx->play_dev, &play_fs);
        if (ret == ESP_CODEC_DEV_OK) {
            ret = esp_codec_dev_open(ctx->record_dev, &record_fs);
        }
    } else {
        ret = esp_codec_dev_open(ctx->record_dev, &record_fs);
        if (ret == ESP_CODEC_DEV_OK) {
            ret = esp_codec_dev_open(ctx->play_dev, &play_fs);
        }
    }
    if (ret == ESP_CODEC_DEV_OK) {
        if (ctx->out_data_if->get_bus_info == NULL || ctx->in_data_if->get_bus_info == NULL) {
            ret = ESP_CODEC_DEV_NOT_SUPPORT;
        } else {
            ret = ctx->out_data_if->get_bus_info(ctx->out_data_if, ESP_CODEC_DEV_TYPE_OUT, &result->tx_bus);
        }
    }
    if (ret == ESP_CODEC_DEV_OK) {
        ret = ctx->in_data_if->get_bus_info(ctx->in_data_if, ESP_CODEC_DEV_TYPE_IN, &result->rx_bus);
    }
    if (ret == ESP_CODEC_DEV_OK) {
        ret = esp_codec_dev_get_data_layout(ctx->play_dev, &result->tx_layout);
    }
    if (ret == ESP_CODEC_DEV_OK) {
        ret = esp_codec_dev_get_data_layout(ctx->record_dev, &result->rx_layout);
    }
    if (ret == ESP_CODEC_DEV_OK) {
        ret = chatbot_verify_dma(ctx, &play_fs, &record_fs);
    }
    /* Both directions are closed so the next open order starts from a clean state,
       while the I2C/I2S hardware stays installed for the whole test case. */
    int close_ret = esp_codec_dev_close(ctx->record_dev);
    if (close_ret == ESP_CODEC_DEV_OK) {
        close_ret = esp_codec_dev_close(ctx->play_dev);
    }
    return (ret != ESP_CODEC_DEV_OK) ? ret : close_ret;
}

static void chatbot_verify_case(const chatbot_case_spec_t *spec)
{
    chatbot_case_ctx_t ctx = {0};
    chatbot_case_result_t results[2] = {0};
    int ret = chatbot_case_setup(&ctx);
    if (ret == ESP_CODEC_DEV_OK) {
        ret = chatbot_play_music(ctx.play_dev);
    }
    for (int order = CHATBOT_OPEN_TX_FIRST; ret == ESP_CODEC_DEV_OK && order <= CHATBOT_OPEN_RX_FIRST; order++) {
        ret = chatbot_run_order(&ctx, spec, (chatbot_open_order_t)order, &results[order]);
    }
    chatbot_case_teardown(&ctx);

    TEST_ESP_OK(ret);
    const uint8_t tx_bits = chatbot_spec_bits(spec->tx_bits);
    const uint8_t rx_bits = chatbot_spec_bits(spec->rx_bits);
    const uint8_t tx_slots = chatbot_spec_slots(spec->tx_total_slot, spec->total_slot);
    const uint8_t rx_slots = chatbot_spec_slots(spec->rx_total_slot, spec->total_slot);
    for (int order = CHATBOT_OPEN_TX_FIRST; order <= CHATBOT_OPEN_RX_FIRST; order++) {
        const chatbot_case_result_t *result = &results[order];
        TEST_ASSERT_EQUAL_UINT8(tx_slots, result->tx_bus.total_slot);
        TEST_ASSERT_EQUAL_UINT8(rx_slots, result->rx_bus.total_slot);
        TEST_ASSERT_EQUAL_UINT8(tx_bits, result->tx_bus.data_bit);
        TEST_ASSERT_EQUAL_UINT8(tx_bits, result->tx_bus.slot_bit);
        TEST_ASSERT_EQUAL_UINT8(rx_bits, result->rx_bus.data_bit);
        TEST_ASSERT_EQUAL_UINT8(rx_bits, result->rx_bus.slot_bit);
        TEST_ASSERT_EQUAL_UINT16((uint16_t)tx_slots * tx_bits, result->tx_bus.total_frame_bits);
        TEST_ASSERT_EQUAL_UINT16((uint16_t)rx_slots * rx_bits, result->rx_bus.total_frame_bits);
        TEST_ASSERT_EQUAL_UINT16(spec->tx_slot_mask, result->tx_bus.slot_mask);
        TEST_ASSERT_EQUAL_UINT16(spec->rx_slot_mask, result->rx_bus.slot_mask);
        TEST_ASSERT_EQUAL_HEX32(spec->tx_layout, result->tx_layout.value);
        TEST_ASSERT_EQUAL_HEX32(spec->rx_layout, result->rx_layout.value);
    }
}

TEST_CASE("Chatbot 4-slot RX mask 0x03", "[chatbot][duplex][total_slot]")
{
    const chatbot_case_spec_t spec = {
        .tx_channel = 4,
        .tx_mask = 0x03,
        .rx_channel = 4,
        .rx_mask = 0x03,
        .total_slot = 4,
        .tx_slot_mask = 0x05,
        .rx_slot_mask = 0x03,
        .tx_layout = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2),
        .rx_layout = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 3),
    };
    chatbot_verify_case(&spec);
}

TEST_CASE("Chatbot 4-slot RX mask 0x05", "[chatbot][duplex][total_slot]")
{
    const chatbot_case_spec_t spec = {
        .tx_channel = 2,
        .tx_mask = 0x03,
        .rx_channel = 4,
        .rx_mask = 0x05,
        .total_slot = 4,
        .tx_slot_mask = 0x05,
        .rx_slot_mask = 0x05,
        .tx_layout = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2),
        .rx_layout = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2),
    };
    chatbot_verify_case(&spec);
}

TEST_CASE("Chatbot 6-slot duplex coordination", "[chatbot][duplex][total_slot]")
{
    /* A 6-slot frame numbers its channels 1,3,5,2,4,6. Mask 0x03 keeps the first two slots
       (ES7210 CH1 and CH3) while the bus still runs at six slots. */
    const chatbot_case_spec_t spec = {
        .tx_channel = 2,
        .tx_mask = 0x03,
        .rx_channel = 6,
        .rx_mask = 0x03,
        .total_slot = 6,
        .tx_slot_mask = 0x09,
        .rx_slot_mask = 0x03,
        .tx_layout = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 2),
        .rx_layout = ESP_CODEC_DEV_CHANNEL_MAP_2CH(1, 3),
    };
    chatbot_verify_case(&spec);
}

TEST_CASE("Chatbot 6-slot RX mask 0x3F", "[chatbot][duplex][total_slot]")
{
    /* A 6-slot frame can carry ES7210's four ADCs plus two cascaded ES8311 slots. Whether those
       extra slots contain valid audio is a board wiring concern; the device still presents six
       channels when the full slot mask is requested. */
    const chatbot_case_spec_t spec = {
        .tx_channel = 2,
        .tx_mask = 0x01,
        .rx_channel = 6,
        .rx_mask = 0x3F,
        .total_slot = 6,
        .tx_slot_mask = 0x01,
        .rx_slot_mask = 0x3F,
        .tx_layout = ESP_CODEC_DEV_CHANNEL_MAP_1CH(1),
        .rx_layout = ESP_CODEC_DEV_CHANNEL_MAP_6CH(1, 3, 5, 2, 4, 6),
    };
    chatbot_verify_case(&spec);
}

TEST_CASE("Chatbot mixed 32-bit TX 16-bit RX", "[chatbot][duplex][total_slot]")
{
    /* ES8311 TDM order_info has 2/4/6/8 slots, not 3. TX 2ch 32-bit (mask 0x01) and RX 6ch
       16-bit (mask 0x3F) share a 128-bit frame: TX 4×32 and RX 8×16. On the 8-slot map
       (1,3,5,7,2,4,6,8) the six original IDs land on slots 1,2,3,5,6,7, so rx_slot_mask is
       0x77 rather than 0x3F. */
    const chatbot_case_spec_t spec = {
        .tx_channel = 2,
        .tx_mask = 0x01,
        .rx_channel = 6,
        .rx_mask = 0x3F,
        .tx_bits = 32,
        .rx_bits = 16,
        .tx_total_slot = 4,
        .rx_total_slot = 8,
        .tx_slot_mask = 0x01,
        .rx_slot_mask = 0x77,
        .tx_layout = ESP_CODEC_DEV_CHANNEL_MAP_1CH(1),
        .rx_layout = ESP_CODEC_DEV_CHANNEL_MAP_6CH(1, 3, 5, 2, 4, 6),
    };
    chatbot_verify_case(&spec);
}

#endif  /* defined(CONFIG_CODEC_ES8311_SUPPORT) && defined(CONFIG_CODEC_ES7210_SUPPORT) && SOC_I2S_SUPPORTS_TDM */
