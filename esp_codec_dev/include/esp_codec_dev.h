/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "audio_codec_if.h"
#include "audio_codec_data_if.h"
#include "esp_codec_dev_types.h"
#include "esp_codec_dev_vol.h"
#include "audio_codec_vol_if.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Codec device configuration
 */
typedef struct {
    esp_codec_dev_type_t         dev_type;  /*!< Codec device type */
    const audio_codec_if_t      *codec_if;  /*!< Codec interface */
    const audio_codec_data_if_t *data_if;   /*!< Codec data interface */
} esp_codec_dev_cfg_t;

/**
 * @brief  Codec device handle
 */
typedef void *esp_codec_dev_handle_t;

/**
 * @brief  Get `esp_codec_dev` version string
 *
 * @return
 *       - Version  information
 */
const char *esp_codec_dev_get_version(void);

/**
 * @brief  New codec device
 *
 * @param[in]  codec_dev_cfg  Codec device configuration
 *
 * @return
 *       - NULL    Fail to new codec device
 *       - Others  Codec device handle
 */
esp_codec_dev_handle_t esp_codec_dev_new(esp_codec_dev_cfg_t *codec_dev_cfg);

/**
 * @brief  Open codec device
 *
 * @param[in]  codec  Codec device handle
 * @param[in]  fs     Audio sample information
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Open success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Codec not support or driver not ready yet
 */
int esp_codec_dev_open(esp_codec_dev_handle_t codec, esp_codec_dev_sample_info_t *fs);

/**
 * @brief  Get codec device capabilities
 *
 * @param[in]      codec  Codec device handle
 * @param[out]     caps   Capability array to fill. NULL only queries required count
 * @param[in,out]  count  Input capability array capacity, output actual or required count
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Get capabilities success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Codec not support capability query
 *       - ESP_CODEC_DEV_NO_MEM       Capability array is too small, count updated to required count
 *       - ESP_CODEC_DEV_WRONG_STATE  Underlying system is not ready
 *       - ESP_CODEC_DEV_NOT_FOUND    Dynamic device not found
 *       - ESP_CODEC_DEV_DRV_ERR      Driver error
 */
int esp_codec_dev_get_caps(esp_codec_dev_handle_t codec, esp_codec_dev_capability_t *caps, int *count);

/**
 * @brief  Set codec memory data layout
 *
 * @note  If the requested layout cannot be applied by hardware settings, read/write will use
 *        software layout conversion. In that case, read/write lengths must be full-frame aligned.
 * @note  For capture (input), this sets the desired channel order in memory after selecting
 *        samples from the bus. For playback (output), it sets the logical order stored in memory;
 *        when it differs from the bus order, software reordering is used on read/write.
 *        A simple setup uses fs->channel at the codec maximum (for example 4) with all channels
 *        selected in the mask; the target order may be reached by mask adjustment or software
 *        conversion when hardware cannot apply it directly.
 *
 * @param[in]  codec  Codec device handle
 * @param[in]  map    Expected logical-channel to memory-slot mapping; caller retains ownership
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Set layout success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Codec not support
 *       - ESP_CODEC_DEV_WRONG_STATE  Codec device not opened yet
 */
int esp_codec_dev_set_data_layout(esp_codec_dev_handle_t codec, const esp_codec_dev_channel_map_t *map);

/**
 * @brief  Get current memory data layout
 *
 * @note  If the codec device is not opened yet, this API tries to resolve the layout from the
 *        current data interface format. It returns ESP_CODEC_DEV_NOT_SUPPORT when the codec or
 *        data interface does not provide order tables.
 *
 * @param[in]   codec  Codec device handle
 * @param[out]  map    Current effective logical-channel to memory-slot mapping
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Query success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Codec/data interface not support
 */
int esp_codec_dev_get_data_layout(esp_codec_dev_handle_t codec, esp_codec_dev_channel_map_t *map);

/**
 * @brief  Set codec memory data layout by ADC channel labels
 *
 * @note  This API uses the ADC channel labels configured at codec initialization
 *        (adc_cfg.label) to translate labels into a memory data layout. It is only supported
 *        for capture devices that provide ADC labels. It does not modify the codec ADC label
 *        configuration. Output-only devices return ESP_CODEC_DEV_NOT_SUPPORT.
 *
 * @param[in]  codec  Codec device handle
 * @param[in]  label  Expected channel labels in memory, such as "FL,RE,FR"
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Set layout success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments or label configuration
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Codec not support
 *       - ESP_CODEC_DEV_WRONG_STATE  Codec device not opened yet
 */
int esp_codec_dev_set_data_layout_label(esp_codec_dev_handle_t codec, const char *label);

/**
 * @brief  Get current memory data layout as ADC channel labels
 *
 * @note  This API uses the ADC channel labels configured at codec initialization
 *        (adc_cfg.label) to translate the current effective data layout into labels. It is
 *        only supported for capture devices that provide ADC labels. Output-only devices return
 *        ESP_CODEC_DEV_NOT_SUPPORT. If the codec device is not opened yet, this API can query
 *        the label only when the data interface can provide a current format and the codec/data
 *        interface can resolve an order.
 *
 * @param[in]   codec       Codec device handle
 * @param[out]  label       Output label buffer
 * @param[in]   label_size  Output label buffer size in bytes
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Query success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments or label configuration
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Codec/data interface not support
 */
int esp_codec_dev_get_data_layout_label(esp_codec_dev_handle_t codec, char *label, int label_size);

/**
 * @brief  Read data from codec
 *
 * @note  If software data layout conversion is enabled by esp_codec_dev_set_data_layout()
 *        or esp_codec_dev_set_data_layout_label(), len must be a multiple of one
 *        destination frame: channel count in the configured memory layout times bytes per sample.
 *
 * @param[in]   codec  Codec device handle
 * @param[out]  data   Data to be read
 * @param[in]   len    Data length to be read
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Read success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Codec not support
 *       - ESP_CODEC_DEV_WRONG_STATE  Driver not open yet
 */
int esp_codec_dev_read(esp_codec_dev_handle_t codec, void *data, int len);

/**
 * @brief  Configure input mirror for codec input data
 *
 * @note  Input mirror copies data returned by esp_codec_dev_read() into an internal
 *        ring buffer. It is single consumer only, not ISR-safe, and older data may
 *        be overwritten if it is not read in time.
 * @note  If input mirror has already been configured, this API returns ESP_CODEC_DEV_OK
 *        without resizing or recreating the buffer. esp_codec_dev_close() automatically
 *        frees mirror resources.
 *
 * @param[in]  codec  Codec device handle
 * @param[in]  size   Input mirror ring buffer capacity in bytes
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Configure input mirror success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Codec not support input mode
 *       - ESP_CODEC_DEV_NO_MEM       Not enough memory for input mirror
 */
int esp_codec_dev_mirror_cfg(esp_codec_dev_handle_t codec, int size);

/**
 * @brief  Read data from input mirror
 *
 * @note  This API blocks until the requested size is filled or the timeout expires.
 *        It is single consumer only.
 * @note  Do not call esp_codec_dev_close() or esp_codec_dev_delete() while this API is
 *        blocking. Stop the mirror read task before closing or deleting the codec device.
 *
 * @param[in]   codec       Codec device handle
 * @param[out]  buffer      Data buffer to fill
 * @param[in]   size        Data length to read in bytes
 * @param[in]   timeout_ms  Timeout in milliseconds; 0 for no wait, negative to wait forever
 * @param[out]  bytes_read  Actual bytes copied to buffer
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Read requested data success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Codec not support input mode
 *       - ESP_CODEC_DEV_TIMEOUT      Timeout before requested data is filled
 *       - ESP_CODEC_DEV_WRONG_STATE  Input mirror is not enabled
 */
int esp_codec_dev_mirror_read(esp_codec_dev_handle_t codec,
                              uint8_t *buffer,
                              int size,
                              int timeout_ms,
                              int *bytes_read);

/**
 * @brief  Write data to codec
 *
 * @note  When software volume is enabled, this API changes input data level directly without
 *        copy. Ensure that input data is writable.
 * @note  If software data layout conversion is enabled by esp_codec_dev_set_data_layout()
 *        or esp_codec_dev_set_data_layout_label(), len must be a multiple of one source frame:
 *        channel count in the configured memory layout times bytes per sample.
 *
 * @param[in]  codec  Codec device handle
 * @param[in]  data   Data to be wrote
 * @param[in]  len    Data length to be wrote
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Write success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Codec not support
 *       - ESP_CODEC_DEV_WRONG_STATE  Driver not open yet
 */
int esp_codec_dev_write(esp_codec_dev_handle_t codec, void *data, int len);

/**
 * @brief  Set codec hardware gain
 *
 * @param[in]  codec   Codec device handle
 * @param[in]  volume  Volume setting
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Set output volume success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Codec not support output mode
 *       - ESP_CODEC_DEV_WRONG_STATE  Driver not open yet
 */
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t codec, int volume);

/**
 * @brief  Get codec output volume
 *
 * @param[in]   codec   Codec device handle
 * @param[out]  volume  Volume to get
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Get volume success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Codec not support output mode
 *       - ESP_CODEC_DEV_WRONG_STATE  Driver not open yet
 */
int esp_codec_dev_get_out_vol(esp_codec_dev_handle_t codec, int *volume);

/**
 * @brief  Set codec software volume handler
 *
 * @note  Not needed when the codec supports hardware volume adjustment. If not provided, an
 *        internal software volume process handler is used instead.
 *
 * @param[in]  codec        Codec device handle
 * @param[in]  vol_handler  Software volume process interface
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Set volume handler success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Codec not support output mode
 *       - ESP_CODEC_DEV_WRONG_STATE  Driver not open yet
 */
int esp_codec_dev_set_vol_handler(esp_codec_dev_handle_t codec, const audio_codec_vol_if_t *vol_handler);

/**
 * @brief  Set codec volume curve
 *
 * @note  When no volume curve is provided, the default internal curve is 1 for -49.5 dB and
 *        100 for 0 dB. Call this API to customize the volume curve.
 *
 * @param[in]  codec  Codec device handle
 * @param[in]  curve  Volume curve setting
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Set curve success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Codec not support output mode
 *       - ESP_CODEC_DEV_WRONG_STATE  Driver not open yet
 *       - ESP_CODEC_DEV_NO_MEM       Not enough memory to hold volume curve
 */
int esp_codec_dev_set_vol_curve(esp_codec_dev_handle_t codec, esp_codec_dev_vol_curve_t *curve);

/**
 * @brief  Set codec output mute
 *
 * @param[in]  codec  Codec device handle
 * @param[in]  mute   Whether mute output or not
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Set output mute success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Codec not support output mode
 *       - ESP_CODEC_DEV_WRONG_STATE  Driver not open yet
 */
int esp_codec_dev_set_out_mute(esp_codec_dev_handle_t codec, bool mute);

/**
 * @brief  Get codec output mute setting
 *
 * @param[in]   codec  Codec device handle
 * @param[out]  muted  Mute status to get
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Get output mute success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Codec not support output mode
 *       - ESP_CODEC_DEV_WRONG_STATE  Driver not open yet
 */
int esp_codec_dev_get_out_mute(esp_codec_dev_handle_t codec, bool *muted);

/**
 * @brief  Set codec input gain
 *
 * @param[in]  codec     Codec device handle
 * @param[in]  db_value  Input gain setting
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Set input gain success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Codec not support input mode
 *       - ESP_CODEC_DEV_WRONG_STATE  Driver not open yet
 */
int esp_codec_dev_set_in_gain(esp_codec_dev_handle_t codec, float db_value);

/**
 * @brief  Set codec input gain by channel
 *
 * @param[in]  codec         Codec device handle
 * @param[in]  channel_mask  Mask for channel to be set
 * @param[in]  db_value      Input gain setting
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Set input gain success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Codec not support input mode
 *       - ESP_CODEC_DEV_WRONG_STATE  Driver not open yet
 */
int esp_codec_dev_set_in_channel_gain(esp_codec_dev_handle_t codec, uint16_t channel_mask, float db_value);

/**
 * @brief  Get codec input gain
 *
 * @param[in]   codec     Codec device handle
 * @param[out]  db_value  Input gain to get
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Get input gain success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Codec not support input mode
 *       - ESP_CODEC_DEV_WRONG_STATE  Driver not open yet
 */
int esp_codec_dev_get_in_gain(esp_codec_dev_handle_t codec, float *db_value);

/**
 * @brief  Set codec input mute
 *
 * @param[in]  codec  Codec device handle
 * @param[in]  mute   Whether mute codec input or not
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Set input mute success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Codec not support input mode
 *       - ESP_CODEC_DEV_WRONG_STATE  Driver not open yet
 */
int esp_codec_dev_set_in_mute(esp_codec_dev_handle_t codec, bool mute);

/**
 * @brief  Get codec input mute
 *
 * @param[in]   codec  Codec device handle
 * @param[out]  muted  Mute value to get
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Get input mute success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Codec not support input mode
 *       - ESP_CODEC_DEV_WRONG_STATE  Driver not open yet
 */
int esp_codec_dev_get_in_mute(esp_codec_dev_handle_t codec, bool *muted);

/**
 * @brief  Whether disable codec when closed
 *
 * @param[in]  codec    Codec device handle
 * @param[in]  disable  Disable when closed (default is true)
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Setting success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 */
int esp_codec_dev_set_disable_when_closed(esp_codec_dev_handle_t codec, bool disable);

/**
 * @brief  Read register value from codec
 *
 * @param[in]   codec  Codec device handle
 * @param[in]   reg    Register address to be read
 * @param[out]  val    Value to be read
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Read success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 */
int esp_codec_dev_read_reg(esp_codec_dev_handle_t codec, int reg, int *val);

/**
 * @brief  Write register value to codec
 *
 * @param[in]  codec  Codec device handle
 * @param[in]  reg    Register address to be wrote
 * @param[in]  val    Value to be wrote
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Write success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 */
int esp_codec_dev_write_reg(esp_codec_dev_handle_t codec, int reg, int val);

/**
 * @brief  Dump registers from codec device
 *
 * @param[in]  codec  Codec device handle
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Dump success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 *       - ESP_CODEC_DEV_NOT_SUPPORT  Not supported
 */
int esp_codec_dev_dump_reg(esp_codec_dev_handle_t codec);

/**
 * @brief  Close codec device
 *
 * @param[in]  codec  Codec device handle
 *
 * @return
 *       - ESP_CODEC_DEV_OK           Close success
 *       - ESP_CODEC_DEV_INVALID_ARG  Invalid arguments
 */
int esp_codec_dev_close(esp_codec_dev_handle_t codec);

/**
 * @brief  Delete the specified codec device instance
 *
 * @param[in]  codec  Codec device handle
 */
void esp_codec_dev_delete(esp_codec_dev_handle_t codec);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
