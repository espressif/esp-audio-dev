/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "soc/soc_caps.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#if SOC_LP_I2S_SUPPORTED && SOC_LP_VAD_SUPPORTED && defined(CONFIG_CODEC_ES8311_SUPPORT)

/**
 * @brief  LP_I2S RX + ES8311 ADC capture and LP VAD light-sleep wakeup exercise on P4 EV board.
 *
 *         Not registered in Unity by default. Uncomment TEST_CASE in test_p4_ev_board_lp.c when enabling CI.
 */
void test_p4_ev_board_lp_i2s_rx_es8311_record_vad(void);

#endif  /* SOC_LP_I2S_SUPPORTED && SOC_LP_VAD_SUPPORTED && defined(CONFIG_CODEC_ES8311_SUPPORT) */

#ifdef __cplusplus
}
#endif  /* __cplusplus */
