/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Columns per dump row (offsets 00..0F)
 */
#define CODEC_REG_DUMP_COLS  16

/**
 * @brief  Max register address supported by dump buffer (8-bit map)
 */
#define CODEC_REG_DUMP_MAX_ADDR  256

/**
 * @brief  Info-level log without timestamp / tag text
 *
 *         Still respects compile-time and runtime log level filtering.
 */
#define ESP_LOGI_RAW(tag, format, ...)  do {                               \
    if (LOG_LOCAL_LEVEL >= ESP_LOG_INFO) {                               \
        esp_log_write(ESP_LOG_INFO, tag, format "\n", ##__VA_ARGS__);  \
    }                                                                  \
} while (0)

/**
 * @brief  Context for buffered register dump
 */
typedef struct {
    const char *tag;                                       /*!< Log tag / title */
    uint8_t     val_digits;                                /*!< Value width: 2 -> %02x, 4 -> %04x */
    int         min_addr;                                  /*!< Lowest pushed address */
    int         max_addr;                                  /*!< Highest pushed address */
    uint8_t     valid[(CODEC_REG_DUMP_MAX_ADDR + 7) / 8];  /*!< Bitmap of valid entries */
    uint16_t    vals[CODEC_REG_DUMP_MAX_ADDR];             /*!< Register values */
} codec_reg_dump_ctx_t;

/**
 * @brief  Initialize register dump context
 */
static inline void codec_reg_dump_init(codec_reg_dump_ctx_t *ctx, const char *tag, uint8_t val_digits)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->tag = tag;
    ctx->val_digits = val_digits ? val_digits : 2;
    ctx->min_addr = CODEC_REG_DUMP_MAX_ADDR;
    ctx->max_addr = -1;
}

static inline bool codec_reg_dump_is_valid(const codec_reg_dump_ctx_t *ctx, int addr)
{
    if (addr < 0 || addr >= CODEC_REG_DUMP_MAX_ADDR) {
        return false;
    }
    return (ctx->valid[addr / 8] & (uint8_t)(1u << (addr % 8))) != 0;
}

/**
 * @brief  Push one register into dump buffer
 */
static inline void codec_reg_dump_push(codec_reg_dump_ctx_t *ctx, int addr, int val)
{
    if (ctx == NULL || addr < 0 || addr >= CODEC_REG_DUMP_MAX_ADDR) {
        return;
    }
    ctx->vals[addr] = (uint16_t)val;
    ctx->valid[addr / 8] |= (uint8_t)(1u << (addr % 8));
    if (addr < ctx->min_addr) {
        ctx->min_addr = addr;
    }
    if (addr > ctx->max_addr) {
        ctx->max_addr = addr;
    }
}

/**
 * @brief  Flush is a no-op; dump is printed in codec_reg_dump_end()
 */
static inline void codec_reg_dump_flush(codec_reg_dump_ctx_t *ctx)
{
    (void)ctx;
}

static inline bool codec_reg_dump_row_has_data(const codec_reg_dump_ctx_t *ctx, int row_base)
{
    for (int c = 0; c < CODEC_REG_DUMP_COLS; c++) {
        if (codec_reg_dump_is_valid(ctx, row_base + c)) {
            return true;
        }
    }
    return false;
}

static inline void codec_reg_dump_print_header(const codec_reg_dump_ctx_t *ctx)
{
    char line[128];
    int pos = snprintf(line, sizeof(line), "addr");
    for (int c = 0; c < CODEC_REG_DUMP_COLS; c++) {
        if (c == 8) {
            pos += snprintf(line + pos, sizeof(line) - (size_t)pos, " ");
        }
        if (ctx->val_digits >= 4) {
            pos += snprintf(line + pos, sizeof(line) - (size_t)pos, "   %02x", c);
        } else {
            pos += snprintf(line + pos, sizeof(line) - (size_t)pos, " %02x", c);
        }
        if (pos < 0 || (size_t)pos >= sizeof(line)) {
            break;
        }
    }
    ESP_LOGI_RAW(ctx->tag, "%s", line);
}

static inline void codec_reg_dump_print_row(const codec_reg_dump_ctx_t *ctx, int row_base)
{
    char line[128];
    int pos = snprintf(line, sizeof(line), "0x%02x", row_base);
    for (int c = 0; c < CODEC_REG_DUMP_COLS; c++) {
        if (c == 8) {
            pos += snprintf(line + pos, sizeof(line) - (size_t)pos, " ");
        }
        int addr = row_base + c;
        if (codec_reg_dump_is_valid(ctx, addr)) {
            if (ctx->val_digits >= 4) {
                pos += snprintf(line + pos, sizeof(line) - (size_t)pos, " %04x",
                                (unsigned)ctx->vals[addr]);
            } else {
                pos += snprintf(line + pos, sizeof(line) - (size_t)pos, " %02x",
                                (unsigned)ctx->vals[addr]);
            }
        } else if (ctx->val_digits >= 4) {
            pos += snprintf(line + pos, sizeof(line) - (size_t)pos, " ----");
        } else {
            pos += snprintf(line + pos, sizeof(line) - (size_t)pos, " --");
        }
        if (pos < 0 || (size_t)pos >= sizeof(line)) {
            break;
        }
    }
    ESP_LOGI_RAW(ctx->tag, "%s", line);
}

/**
 * @brief  Print register dump in hexdump style, then a trailing blank line
 *
 *         Example:
 *         ES7210:
 *         addr  00 01 02 03 04 05 06 07  08 09 0a 0b 0c 0d 0e 0f
 *         0x00  32 00 00 00 00 00 11 00  00 ff 00 00 00 00 00 00
 *         0x10  00 00 00 00 00 00 00 00  c3 00 0a 00 00 00 00 00
 */
static inline void codec_reg_dump_end(codec_reg_dump_ctx_t *ctx)
{
    if (ctx == NULL || ctx->max_addr < 0 || LOG_LOCAL_LEVEL < ESP_LOG_INFO) {
        return;
    }

    ESP_LOGI_RAW(ctx->tag, "%s:", ctx->tag ? ctx->tag : "REG");
    codec_reg_dump_print_header(ctx);

    const int row_start = ctx->min_addr & ~(CODEC_REG_DUMP_COLS - 1);
    const int row_end = ctx->max_addr & ~(CODEC_REG_DUMP_COLS - 1);
    for (int row = row_start; row <= row_end; row += CODEC_REG_DUMP_COLS) {
        if (codec_reg_dump_row_has_data(ctx, row)) {
            codec_reg_dump_print_row(ctx, row);
        }
    }
    esp_log_write(ESP_LOG_INFO, ctx->tag, "\n");
}

#ifdef __cplusplus
}
#endif  /* __cplusplus */
