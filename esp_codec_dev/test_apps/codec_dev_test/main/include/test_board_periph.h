/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_idf_version.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "soc/soc_caps.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef struct {
    int16_t  scl;
    int16_t  sda;
} codec_i2c_pin_t;

typedef struct {
    int16_t  mclk;
    int16_t  bclk;
    int16_t  ws;
    int16_t  dout;
    int16_t  din;
} codec_i2s_pin_t;

/** Default playback volume (%) for board codec tests. */
#define TEST_CODEC_BOARD_OUT_VOL  (60)
/** Default record gain (dB) for board codec tests. */
#define TEST_CODEC_BOARD_IN_GAIN  (25.0f)

int ut_i2c_init(uint8_t port, codec_i2c_pin_t *i2c_pin);
int ut_i2c_deinit(uint8_t port);

void ut_set_i2s_mode(i2s_comm_mode_t out_mode, i2s_comm_mode_t in_mode);
void ut_clr_i2s_mode(void);
void ut_i2s_set_rx_init_first(bool enable);
bool ut_i2s_is_rx_init_first(void);
int ut_i2s_init_channel(uint8_t port);
int ut_i2s_init_single_channel(uint8_t port, i2s_dir_t dir, i2s_role_t role);
int ut_i2s_init(uint8_t port, codec_i2s_pin_t *i2s_pin, i2s_clock_src_t clk_src);
/** Phase 1: allocate I2S channel pair, init and enable TX only (RX not initialized yet). */
int ut_i2s_init_tx_phase(uint8_t port, codec_i2s_pin_t *i2s_pin, i2s_clock_src_t clk_src);
/** Phase 2: init and enable RX (after ut_i2s_init_tx_phase). Use the same i2s_pin and clk_src as the TX phase for full-duplex on one port. */
int ut_i2s_init_rx_phase(uint8_t port, codec_i2s_pin_t *i2s_pin, i2s_clock_src_t clk_src);
int ut_i2s_deinit(uint8_t port);

i2s_chan_handle_t ut_i2s_get_tx_handle(uint8_t port);
i2s_chan_handle_t ut_i2s_get_rx_handle(uint8_t port);

#if SOC_I2S_SUPPORTS_PDM_TX
int ut_i2s_init_pdm_out(uint8_t port);
#endif  /* SOC_I2S_SUPPORTS_PDM_TX */

i2c_master_bus_handle_t ut_i2c_get_bus_handle(void);

int ut_ledc_output_pwm(uint32_t freq_hz, int gpio_num);
int ut_ledc_deinit(int gpio_num);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
