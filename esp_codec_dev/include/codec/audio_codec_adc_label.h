/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>

#include "esp_codec_dev_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Parse an ADC channel label list into a hardware microphone mask
 *
 *         `label` is a comma-separated list of logical channel names in
 *         codec_dev 2.0 channel order (LSB to MSB). Spaces around tokens
 *         are ignored. Duplicate names are allowed.
 *
 *         Only the following tokens are supported:
 *         - FC: Front Center
 *         - RE: Reference signal
 *         - FL / FR: Front Left / Right
 *         - SL / SR: Side Left / Right
 *         - BL / BR: Back Left / Right
 *         - NA: Not available / not enabled
 *
 *         RE and NA have special meaning: RE is the reference channel, NA
 *         leaves that physical channel unused (bit i is 0).
 *         Any other token is invalid, including lowercase names (`na`, `fl`)
 *         and near-NA spellings such as `N/A` or `NF`. Matching is exact and
 *         case-sensitive. A NULL or empty label selects all channels (`mic_mask`
 *         is 0xFFFF, `channel_num` is 0). Codecs apply the bits that correspond to
 *         their physical microphones. Empty tokens such as "FL,,RE" are invalid.
 *
 *         Examples:
 *         - "FL,FL,NA,FL"         bits 0, 1, 3;        channel_num 4
 *         - "RE,FL"               bits 0, 1;           channel_num 2
 *         - "FL,FR,RE,NA"         bits 0, 1, 2;        channel_num 4
 *         - "FL,FR,SL,SR,RE,NA"   bits 0, 1, 2, 3, 4;  channel_num 6
 *         - NULL or ""            0xFFFF;              channel_num 0
 *
 * @param[in]   label        ADC label list such as "FL,NA,RE"
 * @param[out]  mic_mask     Bit i is set when token i is not "NA"
 * @param[out]  channel_num  Token count including "NA"; 0 if label is NULL or empty.
 *                           May be NULL if the caller only needs mic_mask.
 *                           Set to 0 before parsing; remains 0 if the label is malformed
 *
 * @return
 *       - ESP_CODEC_DEV_OK           On success
 *       - ESP_CODEC_DEV_INVALID_ARG  mic_mask is NULL or label list is malformed
 */
int audio_codec_adc_label_parse(const char *label, uint16_t *mic_mask, uint8_t *channel_num);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
