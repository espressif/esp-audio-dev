/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * Register definition template
 * ----------------------------
 * Replace the example register map below with the real chip registers from the
 * datasheet. The goal of keeping a separate *_reg.h file is:
 * - centralize register addresses and bit definitions
 * - make source code easier to read and review
 * - document which register group controls reset / clock / ADC / DAC / GPIO
 *
 * Recommended workflow for a new codec:
 * 1. Group registers by function block from the datasheet
 * 2. Use one prefix for all register names, e.g. MYCODEC_REG_xxx
 * 3. Add bit masks for fields that are written repeatedly
 * 4. Keep comments close to datasheet wording when possible
 */

/* CODEC_TEMPLATE register space */

/**
 * CHIP / RESET
 */
#define CODEC_TEMPLATE_REG_RESET     0x00  /* Soft reset / global reset */
#define CODEC_TEMPLATE_REG_CHIP_ID1  0x7E  /* Chip ID high byte */
#define CODEC_TEMPLATE_REG_CHIP_ID2  0x7F  /* Chip ID low byte */

/**
 * CLOCK / SYSTEM
 */
#define CODEC_TEMPLATE_REG_CLK_CTRL1     0x01  /* Clock source selection */
#define CODEC_TEMPLATE_REG_CLK_CTRL2     0x02  /* Divider / multiplier */
#define CODEC_TEMPLATE_REG_SYSTEM_POWER  0x03  /* Power-up / power-down */
#define CODEC_TEMPLATE_REG_SYSTEM_IFACE  0x04  /* Serial interface format */

/**
 * ADC PATH
 */
#define CODEC_TEMPLATE_REG_ADC_POWER   0x10  /* ADC analog/digital power */
#define CODEC_TEMPLATE_REG_ADC_VOLUME  0x11  /* ADC digital volume */
#define CODEC_TEMPLATE_REG_ADC_MUTE    0x12  /* ADC mute control */
#define CODEC_TEMPLATE_REG_ADC_GAIN    0x13  /* ADC analog gain or PGA */

/**
 * DAC PATH
 */
#define CODEC_TEMPLATE_REG_DAC_POWER   0x20  /* DAC analog/digital power */
#define CODEC_TEMPLATE_REG_DAC_VOLUME  0x21  /* DAC digital volume */
#define CODEC_TEMPLATE_REG_DAC_MUTE    0x22  /* DAC mute control */

/**
 * GPIO / TEST
 */
#define CODEC_TEMPLATE_REG_GPIO_CTRL  0x30  /* GPIO / function mux */

/**
 * Example bit definitions. Replace with real fields from the chip manual.
 */
#define CODEC_TEMPLATE_BIT_ADC_ENABLE  (1 << 0)
#define CODEC_TEMPLATE_BIT_DAC_ENABLE  (1 << 1)
#define CODEC_TEMPLATE_BIT_ADC_MUTE    (1 << 2)
#define CODEC_TEMPLATE_BIT_DAC_MUTE    (1 << 3)

#define CODEC_TEMPLATE_MAX_REGISTER  0x7F

#ifdef __cplusplus
}
#endif  /* __cplusplus */
