/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"

#include "audio_codec_ctrl_if.h"
#include "esp_codec_dev_defaults.h"

static const char *TAG = "SPI_IF";

typedef struct {
    audio_codec_ctrl_if_t  base;
    bool                   is_open;
    uint8_t                port;
    int16_t                cs_pin;
    spi_device_handle_t    spi_handle;
} spi_ctrl_t;

static int _spi_ctrl_open(const audio_codec_ctrl_if_t *ctrl, void *cfg, int cfg_size)
{
    ESP_RETURN_ON_FALSE(ctrl && cfg && cfg_size == sizeof(audio_codec_spi_cfg_t),
                        ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle or configuration");
    spi_ctrl_t *spi_ctrl = (spi_ctrl_t *)ctrl;
    audio_codec_spi_cfg_t *spi_cfg = (audio_codec_spi_cfg_t *)cfg;
    uint32_t speed = spi_cfg->clock_speed ? spi_cfg->clock_speed : 1000000;
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = speed,  // Clock out at 10 MHz
        .mode = 0,                // SPI mode 0
        .queue_size = 6,          // queue 6 transactions at a time
    };
    dev_cfg.spics_io_num = spi_cfg->cs_pin;
    spi_host_device_t host_id = (spi_host_device_t)spi_cfg->spi_port;
    int ret = spi_bus_add_device(host_id, &dev_cfg, &spi_ctrl->spi_handle);
    if (ret == 0) {
        gpio_set_pull_mode(spi_cfg->cs_pin, GPIO_FLOATING);
        spi_ctrl->cs_pin = spi_cfg->cs_pin;
        spi_ctrl->is_open = true;
    }
    return ret == 0 ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}

static bool _spi_ctrl_is_open(const audio_codec_ctrl_if_t *ctrl)
{
    if (ctrl) {
        spi_ctrl_t *spi_ctrl = (spi_ctrl_t *)ctrl;
        return spi_ctrl->is_open;
    }
    return false;
}

static int _spi_ctrl_read_reg(const audio_codec_ctrl_if_t *ctrl, int addr, int addr_len, void *data, int data_len)
{
    spi_ctrl_t *spi_ctrl = (spi_ctrl_t *)ctrl;
    ESP_RETURN_ON_FALSE(spi_ctrl && data, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle or data");
    ESP_RETURN_ON_FALSE(spi_ctrl->is_open, ESP_CODEC_DEV_WRONG_STATE, TAG, "SPI control not open");

    esp_err_t ret = ESP_OK;
    if (addr_len) {
        uint16_t *v = (uint16_t *)&addr;
        while (addr_len >= 2) {
            spi_transaction_t t = {0};
            t.length = 2 * 8;
            t.tx_buffer = v;
            ret = spi_device_transmit(spi_ctrl->spi_handle, &t);
            v++;
            addr_len -= 2;
        }
    }
    if (data_len) {
        spi_transaction_t t = {0};
        t.length = data_len * 8;
        t.rxlength = data_len * 8;
        t.rx_buffer = data;
        ret = spi_device_transmit(spi_ctrl->spi_handle, &t);
    }
    return ret == ESP_OK ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}

static int _spi_ctrl_write_reg(const audio_codec_ctrl_if_t *ctrl, int addr, int addr_len, void *data, int data_len)
{
    spi_ctrl_t *spi_ctrl = (spi_ctrl_t *)ctrl;
    ESP_RETURN_ON_FALSE(spi_ctrl && data, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle or data");
    ESP_RETURN_ON_FALSE(spi_ctrl->is_open, ESP_CODEC_DEV_WRONG_STATE, TAG, "SPI control not open");

    esp_err_t ret = ESP_OK;
    if (addr_len) {
        uint16_t *v = (uint16_t *)&addr;
        while (addr_len >= 2) {
            spi_transaction_t t = {0};
            t.length = 2 * 8;
            t.tx_buffer = v;
            ret = spi_device_transmit(spi_ctrl->spi_handle, &t);
            v++;
            addr_len -= 2;
        }
    }
    if (data_len) {
        spi_transaction_t t = {0};
        t.length = data_len * 8;
        t.tx_buffer = data;
        ret = spi_device_transmit(spi_ctrl->spi_handle, &t);
    }
    return ret == ESP_OK ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}

static int _spi_ctrl_get_info(const audio_codec_ctrl_if_t *ctrl, audio_codec_ctrl_info_t *info)
{
    ESP_RETURN_ON_FALSE(ctrl && info, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle or info");
    spi_ctrl_t *spi_ctrl = (spi_ctrl_t *)ctrl;
    info->type = AUDIO_CODEC_CTRL_SPI;
    info->spi.cs_pin = spi_ctrl->cs_pin;
    return ESP_CODEC_DEV_OK;
}

static int _spi_ctrl_close(const audio_codec_ctrl_if_t *ctrl)
{
    ESP_RETURN_ON_FALSE(ctrl, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    spi_ctrl_t *spi_ctrl = (spi_ctrl_t *)ctrl;
    int ret = 0;
    if (spi_ctrl->spi_handle) {
        ret = spi_bus_remove_device(spi_ctrl->spi_handle);
    }
    spi_ctrl->is_open = false;
    return ret == ESP_OK ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}

const audio_codec_ctrl_if_t *audio_codec_new_spi_ctrl(audio_codec_spi_cfg_t *spi_cfg)
{
    if (spi_cfg == NULL) {
        ESP_LOGE(TAG, "Bad configuration");
        return NULL;
    }
    spi_ctrl_t *ctrl = calloc(1, sizeof(spi_ctrl_t));
    if (ctrl == NULL) {
        ESP_LOGE(TAG, "No memory for instance");
        return NULL;
    }
    ctrl->base.open = _spi_ctrl_open;
    ctrl->base.is_open = _spi_ctrl_is_open;
    ctrl->base.read_reg = _spi_ctrl_read_reg;
    ctrl->base.write_reg = _spi_ctrl_write_reg;
    ctrl->base.get_info = _spi_ctrl_get_info;
    ctrl->base.close = _spi_ctrl_close;
    int ret = _spi_ctrl_open(&ctrl->base, spi_cfg, sizeof(audio_codec_spi_cfg_t));
    if (ret != 0) {
        ESP_LOGE(TAG, "Fail to open SPI driver");
        free(ctrl);
        return NULL;
    }
    return &ctrl->base;
}
