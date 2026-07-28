/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"

#include "audio_codec_ctrl_if.h"
#include "esp_codec_dev_defaults.h"

static const char *TAG = "I2C_IF";

typedef struct {
    audio_codec_ctrl_if_t    base;
    bool                     is_open;
    uint8_t                  addr;
    i2c_master_bus_handle_t  bus_handle;
    i2c_master_dev_handle_t  dev_handle;
} i2c_ctrl_t;

#define DEFAULT_I2C_CLOCK          (100000)
#define DEFAULT_I2C_TRANS_TIMEOUT  (100)

static int _i2c_ctrl_open(const audio_codec_ctrl_if_t *ctrl, void *cfg, int cfg_size)
{
    audio_codec_i2c_cfg_t *i2c_cfg = (audio_codec_i2c_cfg_t *)cfg;
    ESP_RETURN_ON_FALSE(ctrl && cfg && cfg_size == sizeof(audio_codec_i2c_cfg_t),
                        ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle or configuration");
    ESP_RETURN_ON_FALSE(i2c_cfg->bus_handle, ESP_CODEC_DEV_INVALID_ARG, TAG, "I2C bus handle is NULL");

    i2c_ctrl_t *i2c_ctrl = (i2c_ctrl_t *)ctrl;
    i2c_ctrl->bus_handle = i2c_cfg->bus_handle;
    i2c_ctrl->addr = i2c_cfg->addr;
    uint32_t clock_speed_hz = i2c_cfg->clock_speed_hz ? i2c_cfg->clock_speed_hz : DEFAULT_I2C_CLOCK;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = (i2c_cfg->addr >> 1),
        .scl_speed_hz = clock_speed_hz,
    };
    int ret = i2c_master_bus_add_device(i2c_cfg->bus_handle, &dev_cfg, &i2c_ctrl->dev_handle);
    if (ret == ESP_OK) {
        i2c_ctrl->is_open = true;
    }
    return (ret == ESP_OK) ? 0 : ESP_CODEC_DEV_DRV_ERR;
}

static bool _i2c_ctrl_is_open(const audio_codec_ctrl_if_t *ctrl)
{
    if (ctrl) {
        i2c_ctrl_t *i2c_ctrl = (i2c_ctrl_t *)ctrl;
        return i2c_ctrl->is_open;
    }
    return false;
}

static int _i2c_master_read_reg(i2c_ctrl_t *i2c_ctrl, int addr, int addr_len, void *data, int data_len)
{
    uint8_t addr_data[2] = {0};
    if (addr_len > 1) {
        addr_data[0] = addr >> 8;
        addr_data[1] = addr & 0xff;
    } else {
        addr_data[0] = addr & 0xff;
    }
    int ret = i2c_master_transmit_receive(i2c_ctrl->dev_handle, addr_data, addr_len, data, data_len, DEFAULT_I2C_TRANS_TIMEOUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fail to read from dev %x", i2c_ctrl->addr);
    }
    return ret ? ESP_CODEC_DEV_READ_FAIL : ESP_CODEC_DEV_OK;
}

static int _i2c_master_write_reg(i2c_ctrl_t *i2c_ctrl, int addr, int addr_len, void *data, int data_len)
{
    esp_err_t ret = ESP_CODEC_DEV_NOT_SUPPORT;
    int len = addr_len + data_len;
    if (len <= 4) {
        // Not support write huge data
        uint8_t write_data[4] = {0};
        int i = 0;
        if (addr_len > 1) {
            write_data[i++] = addr >> 8;
            write_data[i++] = addr & 0xff;
        } else {
            write_data[i++] = addr & 0xff;
        }
        uint8_t *w = (uint8_t *)data;
        while (i < len) {
            write_data[i++] = *(w++);
        }
        ret = i2c_master_transmit(i2c_ctrl->dev_handle, write_data, len, DEFAULT_I2C_TRANS_TIMEOUT);
    }
    if (ret != 0) {
        ESP_LOGE(TAG, "Fail to write to dev %x", i2c_ctrl->addr);
    }
    return ret ? ESP_CODEC_DEV_WRITE_FAIL : ESP_CODEC_DEV_OK;
}

static int _i2c_ctrl_read_reg(const audio_codec_ctrl_if_t *ctrl, int addr, int addr_len, void *data, int data_len)
{
    i2c_ctrl_t *i2c_ctrl = (i2c_ctrl_t *)ctrl;
    ESP_RETURN_ON_FALSE(i2c_ctrl && data, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle or data");
    ESP_RETURN_ON_FALSE(i2c_ctrl->is_open, ESP_CODEC_DEV_WRONG_STATE, TAG, "I2C control not open");
    return _i2c_master_read_reg(i2c_ctrl, addr, addr_len, data, data_len);
}

static int _i2c_ctrl_write_reg(const audio_codec_ctrl_if_t *ctrl, int addr, int addr_len, void *data, int data_len)
{
    i2c_ctrl_t *i2c_ctrl = (i2c_ctrl_t *)ctrl;
    ESP_RETURN_ON_FALSE(ctrl && data, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle or data");
    ESP_RETURN_ON_FALSE(i2c_ctrl->is_open, ESP_CODEC_DEV_WRONG_STATE, TAG, "I2C control not open");
    return _i2c_master_write_reg(i2c_ctrl, addr, addr_len, data, data_len);
}

static int _i2c_ctrl_get_info(const audio_codec_ctrl_if_t *ctrl, audio_codec_ctrl_info_t *info)
{
    ESP_RETURN_ON_FALSE(ctrl && info, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle or info");
    i2c_ctrl_t *i2c_ctrl = (i2c_ctrl_t *)ctrl;
    info->type = AUDIO_CODEC_CTRL_I2C;
    info->i2c.addr = i2c_ctrl->addr;
    info->i2c.bus_handle = i2c_ctrl->bus_handle;
    return ESP_CODEC_DEV_OK;
}

static int _i2c_ctrl_close(const audio_codec_ctrl_if_t *ctrl)
{
    ESP_RETURN_ON_FALSE(ctrl, ESP_CODEC_DEV_INVALID_ARG, TAG, "Invalid handle");
    i2c_ctrl_t *i2c_ctrl = (i2c_ctrl_t *)ctrl;
    if (i2c_ctrl->dev_handle) {
        i2c_master_bus_rm_device(i2c_ctrl->dev_handle);
    }
    i2c_ctrl->is_open = false;
    return 0;
}

const audio_codec_ctrl_if_t *audio_codec_new_i2c_ctrl(audio_codec_i2c_cfg_t *i2c_cfg)
{
    if (i2c_cfg == NULL) {
        ESP_LOGE(TAG, "Bad configuration");
        return NULL;
    }
    i2c_ctrl_t *ctrl = calloc(1, sizeof(i2c_ctrl_t));
    if (ctrl == NULL) {
        ESP_LOGE(TAG, "No memory for instance");
        return NULL;
    }
    ctrl->base.open = _i2c_ctrl_open;
    ctrl->base.is_open = _i2c_ctrl_is_open;
    ctrl->base.read_reg = _i2c_ctrl_read_reg;
    ctrl->base.write_reg = _i2c_ctrl_write_reg;
    ctrl->base.get_info = _i2c_ctrl_get_info;
    ctrl->base.close = _i2c_ctrl_close;
    int ret = _i2c_ctrl_open(&ctrl->base, i2c_cfg, sizeof(audio_codec_i2c_cfg_t));
    if (ret != 0) {
        ESP_LOGE(TAG, "Fail to open I2C driver, ret=%d", ret);
        free(ctrl);
        return NULL;
    }
    return &ctrl->base;
}
