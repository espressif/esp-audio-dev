/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include <stdbool.h>

#include "esp_idf_version.h"
#include "soc/soc_caps.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "hal/i2s_ll.h"
#if SOC_I2S_SUPPORTS_PDM_TX
#include "driver/i2s_pdm.h"
#endif  /* SOC_I2S_SUPPORTS_PDM_TX */
#include "driver/i2c_master.h"
#include "unity.h"
#include "driver/ledc.h"
#include "driver/gpio.h"

#include "test_board.h"
#include "test_board_periph.h"

#if defined(SOC_I2S_NUM)
#define I2S_MAX_KEEP  SOC_I2S_NUM
#elif defined(I2S_LL_INST_NUM)
#define I2S_MAX_KEEP  I2S_LL_INST_NUM
#endif  /* defined(SOC_I2S_NUM) */

typedef struct {
    i2s_chan_handle_t  tx_handle;
    i2s_chan_handle_t  rx_handle;
} i2s_keep_t;

static i2s_comm_mode_t s_i2s_in_mode = I2S_COMM_MODE_STD;
static i2s_comm_mode_t s_i2s_out_mode = I2S_COMM_MODE_STD;
static i2s_keep_t *s_i2s_keep[I2S_MAX_KEEP];
static bool rx_init_first = false;

static i2c_master_bus_handle_t s_i2c_bus_handle;

int ut_i2c_init(uint8_t port, codec_i2c_pin_t *i2c_pin)
{
    i2c_master_bus_config_t i2c_bus_config = {0};
    i2c_bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_bus_config.i2c_port = port;
    i2c_bus_config.scl_io_num = i2c_pin ? i2c_pin->scl : TEST_BOARD_I2C_SCL_PIN;
    i2c_bus_config.sda_io_num = i2c_pin ? i2c_pin->sda : TEST_BOARD_I2C_SDA_PIN;
    i2c_bus_config.glitch_ignore_cnt = 7;
    i2c_bus_config.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&i2c_bus_config, &s_i2c_bus_handle);
}

int ut_i2c_deinit(uint8_t port)
{
    (void)port;
    if (s_i2c_bus_handle) {
        i2c_del_master_bus(s_i2c_bus_handle);
    }
    s_i2c_bus_handle = NULL;
    return 0;
}

void ut_set_i2s_mode(i2s_comm_mode_t out_mode, i2s_comm_mode_t in_mode)
{
    s_i2s_in_mode = in_mode;
    s_i2s_out_mode = out_mode;
}

void ut_clr_i2s_mode(void)
{
    s_i2s_in_mode = I2S_COMM_MODE_STD;
    s_i2s_out_mode = I2S_COMM_MODE_STD;
}

void ut_i2s_set_rx_init_first(bool enable)
{
    rx_init_first = enable;
}

bool ut_i2s_is_rx_init_first(void)
{
    return rx_init_first;
}

int ut_i2s_init(uint8_t port, codec_i2s_pin_t *i2s_pin, i2s_clock_src_t clk_src)
{
    if (port >= I2S_MAX_KEEP) {
        return -1;
    }
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(port, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(48000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(16, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = i2s_pin ? i2s_pin->mclk : TEST_BOARD_I2S_MCK_PIN,
            .bclk = i2s_pin ? i2s_pin->bclk : TEST_BOARD_I2S_BCK_PIN,
            .ws = i2s_pin ? i2s_pin->ws : TEST_BOARD_I2S_DATA_WS_PIN,
            .dout = i2s_pin ? i2s_pin->dout : TEST_BOARD_I2S_DATA_OUT_PIN,
            .din = i2s_pin ? i2s_pin->din : TEST_BOARD_I2S_DATA_IN_PIN,
        },
    };
    std_cfg.clk_cfg.clk_src = clk_src;
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;
    if (s_i2s_keep[port] == NULL) {
        s_i2s_keep[port] = (i2s_keep_t *)calloc(1, sizeof(i2s_keep_t));
        if (s_i2s_keep[port] == NULL) {
            return -1;
        }
    }
#if SOC_I2S_SUPPORTS_TDM
    i2s_tdm_slot_mask_t slot_mask = I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3;
    i2s_tdm_config_t tdm_cfg = {
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(16, I2S_SLOT_MODE_STEREO, slot_mask),
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(16000),
        .gpio_cfg = {
            .mclk = i2s_pin ? i2s_pin->mclk : TEST_BOARD_I2S_MCK_PIN,
            .bclk = i2s_pin ? i2s_pin->bclk : TEST_BOARD_I2S_BCK_PIN,
            .ws = i2s_pin ? i2s_pin->ws : TEST_BOARD_I2S_DATA_WS_PIN,
            .dout = i2s_pin ? i2s_pin->dout : TEST_BOARD_I2S_DATA_OUT_PIN,
            .din = i2s_pin ? i2s_pin->din : TEST_BOARD_I2S_DATA_IN_PIN,
        },
    };
    tdm_cfg.slot_cfg.total_slot = 4;
    tdm_cfg.clk_cfg.clk_src = clk_src;
#endif  /* SOC_I2S_SUPPORTS_TDM */

    int ret = i2s_new_channel(&chan_cfg,
                              s_i2s_out_mode == I2S_COMM_MODE_NONE ? NULL : &s_i2s_keep[port]->tx_handle,
                              s_i2s_in_mode == I2S_COMM_MODE_NONE ? NULL : &s_i2s_keep[port]->rx_handle);
    TEST_ESP_OK(ret);
    if (rx_init_first == false) {
        if (s_i2s_out_mode != I2S_COMM_MODE_NONE) {
            if (s_i2s_keep[port]->tx_handle == NULL) {
                return -1;
            }
            if (s_i2s_out_mode == I2S_COMM_MODE_STD) {
                ret = i2s_channel_init_std_mode(s_i2s_keep[port]->tx_handle, &std_cfg);
            }
#if SOC_I2S_SUPPORTS_TDM
            else if (s_i2s_out_mode == I2S_COMM_MODE_TDM) {
                ret = i2s_channel_init_tdm_mode(s_i2s_keep[port]->tx_handle, &tdm_cfg);
            }
#endif  /* SOC_I2S_SUPPORTS_TDM */
            TEST_ESP_OK(ret);
        }
    }

    if (s_i2s_in_mode != I2S_COMM_MODE_NONE) {
        if (s_i2s_keep[port]->rx_handle == NULL) {
            return -1;
        }
        if (s_i2s_in_mode == I2S_COMM_MODE_STD) {
            ret = i2s_channel_init_std_mode(s_i2s_keep[port]->rx_handle, &std_cfg);
        }
#if SOC_I2S_SUPPORTS_TDM
        else if (s_i2s_in_mode == I2S_COMM_MODE_TDM) {
            ret = i2s_channel_init_tdm_mode(s_i2s_keep[port]->rx_handle, &tdm_cfg);
        }
#endif  /* SOC_I2S_SUPPORTS_TDM */
    }

    if (rx_init_first) {
        if (s_i2s_out_mode != I2S_COMM_MODE_NONE) {
            if (s_i2s_keep[port]->tx_handle == NULL) {
                return -1;
            }
            if (s_i2s_out_mode == I2S_COMM_MODE_STD) {
                ret = i2s_channel_init_std_mode(s_i2s_keep[port]->tx_handle, &std_cfg);
            }
#if SOC_I2S_SUPPORTS_TDM
            else if (s_i2s_out_mode == I2S_COMM_MODE_TDM) {
                ret = i2s_channel_init_tdm_mode(s_i2s_keep[port]->tx_handle, &tdm_cfg);
            }
#endif  /* SOC_I2S_SUPPORTS_TDM */
            TEST_ESP_OK(ret);
        }
    }

    if (s_i2s_keep[port]->tx_handle) {
        i2s_channel_enable(s_i2s_keep[port]->tx_handle);
    }
    if (s_i2s_keep[port]->rx_handle) {
        i2s_channel_enable(s_i2s_keep[port]->rx_handle);
    }
    return ret;
}

#if SOC_I2S_SUPPORTS_TDM
static i2s_tdm_config_t s_saved_tdm_cfg[I2S_MAX_KEEP];
#endif  /* SOC_I2S_SUPPORTS_TDM */
static i2s_std_config_t s_saved_std_cfg[I2S_MAX_KEEP];

static void ut_i2s_save_slot_cfg(uint8_t port, codec_i2s_pin_t *i2s_pin, i2s_clock_src_t clk_src)
{
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(48000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(16, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = i2s_pin ? i2s_pin->mclk : TEST_BOARD_I2S_MCK_PIN,
            .bclk = i2s_pin ? i2s_pin->bclk : TEST_BOARD_I2S_BCK_PIN,
            .ws = i2s_pin ? i2s_pin->ws : TEST_BOARD_I2S_DATA_WS_PIN,
            .dout = i2s_pin ? i2s_pin->dout : TEST_BOARD_I2S_DATA_OUT_PIN,
            .din = i2s_pin ? i2s_pin->din : TEST_BOARD_I2S_DATA_IN_PIN,
        },
    };
    std_cfg.clk_cfg.clk_src = clk_src;
    s_saved_std_cfg[port] = std_cfg;
#if SOC_I2S_SUPPORTS_TDM
    i2s_tdm_slot_mask_t slot_mask = I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3;
    i2s_tdm_config_t tdm_cfg = {
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(16, I2S_SLOT_MODE_STEREO, slot_mask),
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(48000),
        .gpio_cfg = {
            .mclk = i2s_pin ? i2s_pin->mclk : TEST_BOARD_I2S_MCK_PIN,
            .bclk = i2s_pin ? i2s_pin->bclk : TEST_BOARD_I2S_BCK_PIN,
            .ws = i2s_pin ? i2s_pin->ws : TEST_BOARD_I2S_DATA_WS_PIN,
            .dout = i2s_pin ? i2s_pin->dout : TEST_BOARD_I2S_DATA_OUT_PIN,
            .din = i2s_pin ? i2s_pin->din : TEST_BOARD_I2S_DATA_IN_PIN,
        },
    };
    tdm_cfg.slot_cfg.total_slot = 4;
    tdm_cfg.clk_cfg.clk_src = clk_src;
    s_saved_tdm_cfg[port] = tdm_cfg;
#endif  /* SOC_I2S_SUPPORTS_TDM */
}

int ut_i2s_init_channel(uint8_t port)
{
    if (port >= I2S_MAX_KEEP) {
        return -1;
    }
    if (s_i2s_keep[port] == NULL) {
        s_i2s_keep[port] = (i2s_keep_t *)calloc(1, sizeof(i2s_keep_t));
        if (s_i2s_keep[port] == NULL) {
            return -1;
        }
    }
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(port, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    int ret = i2s_new_channel(&chan_cfg,
                              s_i2s_out_mode == I2S_COMM_MODE_NONE ? NULL : &s_i2s_keep[port]->tx_handle,
                              s_i2s_in_mode == I2S_COMM_MODE_NONE ? NULL : &s_i2s_keep[port]->rx_handle);
    TEST_ESP_OK(ret);
    return ret;
}

int ut_i2s_init_single_channel(uint8_t port, i2s_dir_t dir, i2s_role_t role)
{
    if (port >= I2S_MAX_KEEP) {
        return -1;
    }
    if (s_i2s_keep[port] == NULL) {
        s_i2s_keep[port] = (i2s_keep_t *)calloc(1, sizeof(i2s_keep_t));
        if (s_i2s_keep[port] == NULL) {
            return -1;
        }
    }
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(port, role);
    chan_cfg.auto_clear = true;
    int ret = i2s_new_channel(&chan_cfg,
                              dir == I2S_DIR_TX ? &s_i2s_keep[port]->tx_handle : NULL,
                              dir == I2S_DIR_RX ? &s_i2s_keep[port]->rx_handle : NULL);
    TEST_ESP_OK(ret);
    return ret;
}

int ut_i2s_init_tx_phase(uint8_t port, codec_i2s_pin_t *i2s_pin, i2s_clock_src_t clk_src)
{
    if (port >= I2S_MAX_KEEP) {
        return -1;
    }
    if (s_i2s_keep[port] == NULL || s_i2s_keep[port]->tx_handle == NULL) {
        return -1;
    }

    i2s_chan_info_t info = {0};
    i2s_channel_get_info(s_i2s_keep[port]->tx_handle, &info);
    if (info.mode != I2S_COMM_MODE_NONE) {
        return 0;
    }

    int ret = 0;
    ut_i2s_save_slot_cfg(port, i2s_pin, clk_src);

    if (s_i2s_out_mode == I2S_COMM_MODE_STD) {
        ret = i2s_channel_init_std_mode(s_i2s_keep[port]->tx_handle, &s_saved_std_cfg[port]);
    }
#if SOC_I2S_SUPPORTS_TDM
    else if (s_i2s_out_mode == I2S_COMM_MODE_TDM) {
        ret = i2s_channel_init_tdm_mode(s_i2s_keep[port]->tx_handle, &s_saved_tdm_cfg[port]);
    }
#endif  /* SOC_I2S_SUPPORTS_TDM */
    else {
        ret = ESP_ERR_NOT_SUPPORTED;
    }
    TEST_ESP_OK(ret);

    ret = i2s_channel_enable(s_i2s_keep[port]->tx_handle);
    TEST_ESP_OK(ret);
    return ret;
}

int ut_i2s_init_rx_phase(uint8_t port, codec_i2s_pin_t *i2s_pin, i2s_clock_src_t clk_src)
{
    if (port >= I2S_MAX_KEEP) {
        return -1;
    }
    if (s_i2s_keep[port] == NULL || s_i2s_keep[port]->rx_handle == NULL) {
        return -1;
    }

    int ret = 0;
    ut_i2s_save_slot_cfg(port, i2s_pin, clk_src);

    if (s_i2s_in_mode == I2S_COMM_MODE_STD) {
        ret = i2s_channel_init_std_mode(s_i2s_keep[port]->rx_handle, &s_saved_std_cfg[port]);
    }
#if SOC_I2S_SUPPORTS_TDM
    else if (s_i2s_in_mode == I2S_COMM_MODE_TDM) {
        ret = i2s_channel_init_tdm_mode(s_i2s_keep[port]->rx_handle, &s_saved_tdm_cfg[port]);
    }
#endif  /* SOC_I2S_SUPPORTS_TDM */
    else {
        ret = ESP_ERR_NOT_SUPPORTED;
    }
    TEST_ESP_OK(ret);

    ret = i2s_channel_enable(s_i2s_keep[port]->rx_handle);
    TEST_ESP_OK(ret);

#if ESP_IDF_VERSION <= ESP_IDF_VERSION_VAL(5, 5, 1)
    i2s_chan_info_t info = {0};
    i2s_channel_get_info(s_i2s_keep[port]->rx_handle, &info);
    if (info.pair_chan != NULL) {
        ut_i2s_init_tx_phase(port, i2s_pin, clk_src);
    }
#endif  /* ESP_IDF_VERSION <= ESP_IDF_VERSION_VAL(5, 5, 1) */
    return ret;
}

int ut_i2s_deinit(uint8_t port)
{
    if (port >= I2S_MAX_KEEP) {
        return -1;
    }
    if (s_i2s_keep[port] == NULL) {
        return 0;
    }
    if (s_i2s_keep[port]->tx_handle) {
        i2s_channel_disable(s_i2s_keep[port]->tx_handle);
        i2s_del_channel(s_i2s_keep[port]->tx_handle);
        s_i2s_keep[port]->tx_handle = NULL;
    }
    if (s_i2s_keep[port]->rx_handle) {
        i2s_channel_disable(s_i2s_keep[port]->rx_handle);
        i2s_del_channel(s_i2s_keep[port]->rx_handle);
        s_i2s_keep[port]->rx_handle = NULL;
    }
    free(s_i2s_keep[port]);
    s_i2s_keep[port] = NULL;
    return 0;
}

i2s_chan_handle_t ut_i2s_get_tx_handle(uint8_t port)
{
    if (port >= I2S_MAX_KEEP || s_i2s_keep[port] == NULL) {
        return NULL;
    }
    return s_i2s_keep[port]->tx_handle;
}

i2s_chan_handle_t ut_i2s_get_rx_handle(uint8_t port)
{
    if (port >= I2S_MAX_KEEP || s_i2s_keep[port] == NULL) {
        return NULL;
    }
    return s_i2s_keep[port]->rx_handle;
}

i2c_master_bus_handle_t ut_i2c_get_bus_handle(void)
{
    return s_i2c_bus_handle;
}

#if SOC_I2S_SUPPORTS_PDM_TX
int ut_i2s_init_pdm_out(uint8_t port)
{
    if (port >= I2S_MAX_KEEP) {
        return -1;
    }
    if (s_i2s_keep[port] == NULL) {
        s_i2s_keep[port] = (i2s_keep_t *)calloc(1, sizeof(i2s_keep_t));
        if (s_i2s_keep[port] == NULL) {
            return -1;
        }
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(port, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    int ret = i2s_new_channel(&chan_cfg, &s_i2s_keep[port]->tx_handle, NULL);
    TEST_ESP_OK(ret);

    i2s_pdm_tx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_TX_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(16, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = GPIO_NUM_NC,
            .dout = 3,
#if SOC_I2S_PDM_MAX_TX_LINES > 1
            .dout2 = GPIO_NUM_NC,
#endif  /* SOC_I2S_PDM_MAX_TX_LINES > 1 */
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };
    pdm_cfg.clk_cfg.up_sample_fp = 960;
    pdm_cfg.clk_cfg.up_sample_fs = 441;
    pdm_cfg.clk_cfg.bclk_div = 8;
    pdm_cfg.slot_cfg.sd_scale = I2S_PDM_SIG_SCALING_MUL_4;
    pdm_cfg.slot_cfg.hp_scale = I2S_PDM_SIG_SCALING_MUL_4;
    pdm_cfg.slot_cfg.lp_scale = I2S_PDM_SIG_SCALING_MUL_4;
    pdm_cfg.slot_cfg.sinc_scale = I2S_PDM_SIG_SCALING_MUL_4;
#if SOC_I2S_HW_VERSION_2
    pdm_cfg.slot_cfg.line_mode = I2S_PDM_TX_ONE_LINE_CODEC;
#endif  /* SOC_I2S_HW_VERSION_2 */
#if SOC_I2S_HW_VERSION_1
    pdm_cfg.slot_cfg.slot_mask = I2S_PDM_SLOT_LEFT;
#endif  /* SOC_I2S_HW_VERSION_1 */

    ret = i2s_channel_init_pdm_tx_mode(s_i2s_keep[port]->tx_handle, &pdm_cfg);
    TEST_ESP_OK(ret);
    ret = i2s_channel_enable(s_i2s_keep[port]->tx_handle);
    TEST_ESP_OK(ret);
    return 0;
}
#endif  /* SOC_I2S_SUPPORTS_PDM_TX */

int ut_ledc_output_pwm(uint32_t freq_hz, int gpio_num)
{
    if (freq_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    int duty_resolution = LEDC_TIMER_2_BIT;
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = duty_resolution,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = (uint32_t)freq_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    uint32_t duty = (1U << duty_resolution) / 2U;  // 50% duty
    ledc_channel_config_t chan_cfg = {
        .gpio_num = gpio_num,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = duty,
        .hpoint = 0,
        .flags.output_invert = 0,
        .sleep_mode = LEDC_SLEEP_MODE_KEEP_ALIVE,
    };
    ret = ledc_channel_config(&chan_cfg);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    if (ret != ESP_OK) {
        return ret;
    }
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

int ut_ledc_deinit(int gpio_num)
{
    esp_err_t ret = ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    if (ret != ESP_OK) {
        return ret;
    }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    ledc_channel_config_t chan_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .deconfigure = true,
    };
    ret = ledc_channel_config(&chan_cfg);
    if (ret != ESP_OK) {
        return ret;
    }
#endif  /* ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0) */

    ret = ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
    if (ret != ESP_OK) {
        return ret;
    }

    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .deconfigure = true,
    };
    ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    if (gpio_num >= 0) {
        return gpio_reset_pin(gpio_num);
    }
    return ESP_OK;
}
