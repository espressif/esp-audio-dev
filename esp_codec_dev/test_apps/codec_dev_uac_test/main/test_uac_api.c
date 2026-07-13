/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "unity.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "usb/usb_host.h"

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_uac.h"

#define TEST_UAC_USB_POWER_GPIO            GPIO_NUM_48
#define TEST_UAC_SAMPLE_RATE               16000
#define TEST_UAC_BITS                      16
#define TEST_UAC_CHANNELS                  1
#define TEST_UAC_CHANNEL_MASK              0x01
#define TEST_UAC_CHUNK_BYTES               800
#define TEST_UAC_CHUNK_COUNT               30
#define TEST_UAC_PLAYBACK_SEC              5
#define TEST_UAC_OUT_VOLUME                30
#define TEST_UAC_BYTES_PER_SEC             (TEST_UAC_SAMPLE_RATE * (TEST_UAC_BITS / 8) * TEST_UAC_CHANNELS)
#define TEST_UAC_CHUNK_MS                  ((TEST_UAC_CHUNK_BYTES * 1000) / TEST_UAC_BYTES_PER_SEC)
#define TEST_UAC_PLAY_CHUNKS               ((TEST_UAC_BYTES_PER_SEC * TEST_UAC_PLAYBACK_SEC) / TEST_UAC_CHUNK_BYTES)
#define TEST_UAC_RECORD_SEC                10
#define TEST_UAC_RECORD_CHUNKS             ((TEST_UAC_BYTES_PER_SEC * TEST_UAC_RECORD_SEC) / TEST_UAC_CHUNK_BYTES)
#define TEST_UAC_LOOPBACK_SEC              10
#define TEST_UAC_LOOPBACK_CHUNKS           ((TEST_UAC_BYTES_PER_SEC * TEST_UAC_LOOPBACK_SEC) / TEST_UAC_CHUNK_BYTES)
#define TEST_UAC_LOOPBACK_OUT_VOLUME       10
#define TEST_UAC_LOOPBACK_MIC_VOLUME       10
#define TEST_UAC_LOOPBACK_MIC_GAIN_DB      ((TEST_UAC_LOOPBACK_MIC_VOLUME - 100) / 2.0f)
#define TEST_UAC_POWER_CYCLE_DELAY_MS      1000
#define TEST_UAC_DISCONNECT_RECORD_SEC     2
#define TEST_UAC_DISCONNECT_RECORD_CHUNKS  ((TEST_UAC_BYTES_PER_SEC * TEST_UAC_DISCONNECT_RECORD_SEC) / TEST_UAC_CHUNK_BYTES)
#define TEST_UAC_RECONNECT_READ_CHUNKS     20
#define TEST_UAC_MELODY_NOTE_MS            250
#define TEST_UAC_MELODY_AMP                6000
#define TEST_UAC_CAPS_MAX                  32

static const char *TAG = "TEST_UAC_API";

typedef struct {
    bool          running;
    TaskHandle_t  task;
} test_usb_host_ctx_t;

typedef struct {
    uint16_t  freq_hz;
    uint16_t  duration_ms;
} test_uac_note_t;

typedef struct {
    esp_codec_dev_handle_t  dev;
} test_uac_dev_ctx_t;

typedef struct {
    int  connected_count;
    int  disconnected_count;
} uac_event_stats_t;

static const char *test_uac_event_name(esp_codec_dev_uac_event_type_t event)
{
    switch (event) {
        case ESP_CODEC_DEV_UAC_EVENT_CONNECTED:
            return "CONNECTED";
        case ESP_CODEC_DEV_UAC_EVENT_DISCONNECTED:
            return "DISCONNECTED";
        default:
            return "UNKNOWN";
    }
}

static const char *test_uac_dev_type_str(esp_codec_dev_type_t dev_type)
{
    switch (dev_type) {
        case ESP_CODEC_DEV_TYPE_IN:
            return "IN";
        case ESP_CODEC_DEV_TYPE_OUT:
            return "OUT";
        case ESP_CODEC_DEV_TYPE_IN_OUT:
            return "IN_OUT";
        default:
            return "NONE";
    }
}

static void test_uac_event_cb(const esp_codec_dev_uac_event_info_t *info, void *ctx)
{
    uac_event_stats_t *stats = (uac_event_stats_t *)ctx;
    if (stats != NULL) {
        if (info->event == ESP_CODEC_DEV_UAC_EVENT_CONNECTED) {
            stats->connected_count++;
        } else if (info->event == ESP_CODEC_DEV_UAC_EVENT_DISCONNECTED) {
            stats->disconnected_count++;
        }
    }
    ESP_LOGI(TAG, "UAC event: %s, addr=%u, dev_type=%s",
             test_uac_event_name(info->event),
             info->addr,
             test_uac_dev_type_str(info->dev_type));

    uint8_t num = 0;
    if (esp_codec_dev_uac_get_num(&num) != ESP_CODEC_DEV_OK) {
        return;
    }
    ESP_LOGI(TAG, "UAC connected device count: %u", num);
    for (uint8_t i = 0; i < num; i++) {
        esp_codec_dev_uac_info_t dev_info = {0};
        if (esp_codec_dev_uac_get_info(i, &dev_info) == ESP_CODEC_DEV_OK) {
            ESP_LOGI(TAG, "  [%u] addr=%u, dev_type=%s",
                     i, dev_info.addr, test_uac_dev_type_str(dev_info.dev_type));
        }
    }
}

static esp_codec_dev_uac_cfg_t test_uac_mgr_cfg(uint32_t connect_timeout_ms)
{
    return (esp_codec_dev_uac_cfg_t) {
        .connect_timeout_ms = connect_timeout_ms,
        .event_cb = test_uac_event_cb,
    };
}

static esp_err_t test_usb_power_enable(bool enable)
{
    return gpio_set_level(TEST_UAC_USB_POWER_GPIO, enable ? 0 : 1);
}

static void delayed_usb_power_on_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "USB power enabled during UAC wait");
    test_usb_power_enable(true);
    vTaskDelete(NULL);
}

static void usb_host_task(void *arg)
{
    test_usb_host_ctx_t *ctx = (test_usb_host_ctx_t *)arg;
    while (ctx->running) {
        uint32_t event_flags = 0;
        usb_host_lib_handle_events(pdMS_TO_TICKS(100), &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
    }
    vTaskDelete(NULL);
}

static esp_err_t test_usb_host_start(test_usb_host_ctx_t *ctx)
{
    gpio_config_t power_gpio_cfg = {
        .pin_bit_mask = BIT64(TEST_UAC_USB_POWER_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    esp_err_t ret = gpio_config(&power_gpio_cfg);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = test_usb_power_enable(false);
    if (ret != ESP_OK) {
        return ret;
    }
    ESP_LOGI(TAG, "USB power disabled on GPIO%d(active low)", TEST_UAC_USB_POWER_GPIO);

    usb_host_config_t host_cfg = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ret = usb_host_install(&host_cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }
    ctx->running = true;
    if (xTaskCreate(usb_host_task, "uac_usb_host", 4096, ctx, 20, &ctx->task) != pdPASS) {
        ctx->running = false;
        usb_host_uninstall();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void test_usb_host_stop(test_usb_host_ctx_t *ctx)
{
    ctx->running = false;
    vTaskDelay(pdMS_TO_TICKS(300));
    usb_host_uninstall();
}

static esp_codec_dev_sample_info_t test_uac_sample_info(void)
{
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = TEST_UAC_SAMPLE_RATE,
        .bits_per_sample = TEST_UAC_BITS,
        .channel = TEST_UAC_CHANNELS,
        .channel_mask = TEST_UAC_CHANNEL_MASK,
    };
    return fs;
}

static esp_codec_dev_handle_t test_uac_open_first_available(void)
{
    esp_codec_dev_sample_info_t fs = test_uac_sample_info();
    esp_codec_dev_type_t types[] = {ESP_CODEC_DEV_TYPE_IN, ESP_CODEC_DEV_TYPE_OUT};

    for (int i = 0; i < 2; i++) {
        esp_codec_dev_uac_select_t sel = {
            .dev_type = types[i],
        };
        esp_codec_dev_handle_t dev = esp_codec_dev_uac_new_dev(&sel);
        TEST_ASSERT_NOT_NULL(dev);
        esp_err_t ret = esp_codec_dev_open(dev, &fs);
        ESP_LOGI(TAG, "open %s -> %s",
                 types[i] == ESP_CODEC_DEV_TYPE_IN ? "RX" : "TX",
                 esp_err_to_name(ret));
        if (ret == ESP_CODEC_DEV_OK) {
            return dev;
        }
        TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_del_dev(dev));
    }
    return NULL;
}

static esp_err_t test_uac_dev_open_with_power(esp_codec_dev_type_t dev_type, test_uac_dev_ctx_t *ctx, bool delayed_power_on)
{
    esp_codec_dev_uac_cfg_t mgr_cfg = test_uac_mgr_cfg(5000);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_install(&mgr_cfg));

    esp_codec_dev_uac_select_t sel = {
        .dev_type = dev_type,
    };
    ctx->dev = esp_codec_dev_uac_new_dev(&sel);
    TEST_ASSERT_NOT_NULL(ctx->dev);

    esp_codec_dev_sample_info_t fs = test_uac_sample_info();
    test_usb_power_enable(false);
    vTaskDelay(pdMS_TO_TICKS(300));
    if (delayed_power_on) {
        xTaskCreate(delayed_usb_power_on_task, "uac_power_on", 2048, NULL, 19, NULL);
    }
    esp_err_t ret = esp_codec_dev_open(ctx->dev, &fs);
    ESP_LOGI(TAG, "open %s %lu Hz %u-bit %u ch mask 0x%x -> %s",
             dev_type == ESP_CODEC_DEV_TYPE_OUT ? "TX" :
                                                (dev_type == ESP_CODEC_DEV_TYPE_IN ? "RX" : "RX_TX"),
             fs.sample_rate, fs.bits_per_sample, fs.channel,
             fs.channel_mask, esp_err_to_name(ret));
    return ret;
}

static esp_err_t test_uac_dev_open(esp_codec_dev_type_t dev_type, test_uac_dev_ctx_t *ctx)
{
    return test_uac_dev_open_with_power(dev_type, ctx, true);
}

static void test_uac_dev_close(test_uac_dev_ctx_t *ctx)
{
    if (ctx->dev) {
        TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_del_dev(ctx->dev));
    }
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_uninstall());
    memset(ctx, 0, sizeof(*ctx));
}

static esp_err_t try_open_uac_dev(esp_codec_dev_type_t dev_type, esp_codec_dev_sample_info_t *fs)
{
    esp_codec_dev_uac_cfg_t mgr_cfg = test_uac_mgr_cfg(15000);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_install(&mgr_cfg));

    esp_codec_dev_uac_select_t sel = {
        .dev_type = dev_type,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_uac_new_dev(&sel);
    TEST_ASSERT_NOT_NULL(dev);

    esp_codec_dev_sample_info_t open_fs = *fs;
    test_usb_power_enable(false);
    vTaskDelay(pdMS_TO_TICKS(300));
    xTaskCreate(delayed_usb_power_on_task, "uac_power_on", 2048, NULL, 19, NULL);
    esp_err_t ret = esp_codec_dev_open(dev, &open_fs);
    ESP_LOGI(TAG, "try %s %lu Hz %u-bit %u ch mask 0x%x -> %s",
             dev_type == ESP_CODEC_DEV_TYPE_OUT ? "TX" : "RX",
             open_fs.sample_rate, open_fs.bits_per_sample, open_fs.channel,
             open_fs.channel_mask, esp_err_to_name(ret));
    if (ret == ESP_CODEC_DEV_OK) {
        esp_codec_dev_close(dev);
    }
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_del_dev(dev));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_uninstall());
    return ret;
}

static int16_t melody_sample(uint16_t freq_hz, int sample_offset)
{
    if (freq_hz == 0) {
        return 0;
    }
    int period = TEST_UAC_SAMPLE_RATE / freq_hz;
    if (period < 2) {
        period = 2;
    }
    int phase = sample_offset % period;
    int half = period / 2;
    int value = phase < half ? phase : period - phase;
    return (int16_t)(((value * TEST_UAC_MELODY_AMP * 2) / half) - TEST_UAC_MELODY_AMP);
}

static uint16_t melody_freq_at_sample(int sample_offset, int *note_sample_offset)
{
    static const test_uac_note_t melody[] = {
        {262, TEST_UAC_MELODY_NOTE_MS},
        {294, TEST_UAC_MELODY_NOTE_MS},
        {330, TEST_UAC_MELODY_NOTE_MS},
        {392, TEST_UAC_MELODY_NOTE_MS},
        {330, TEST_UAC_MELODY_NOTE_MS},
        {294, TEST_UAC_MELODY_NOTE_MS},
        {262, TEST_UAC_MELODY_NOTE_MS},
        {0, TEST_UAC_MELODY_NOTE_MS / 2},
        {392, TEST_UAC_MELODY_NOTE_MS},
        {440, TEST_UAC_MELODY_NOTE_MS},
        {392, TEST_UAC_MELODY_NOTE_MS},
        {330, TEST_UAC_MELODY_NOTE_MS},
        {294, TEST_UAC_MELODY_NOTE_MS},
        {262, TEST_UAC_MELODY_NOTE_MS * 2},
    };
    const int melody_len = sizeof(melody) / sizeof(melody[0]);
    int cycle_samples = 0;
    for (int i = 0; i < melody_len; i++) {
        cycle_samples += (TEST_UAC_SAMPLE_RATE * melody[i].duration_ms) / 1000;
    }

    int pos = sample_offset % cycle_samples;
    for (int i = 0; i < melody_len; i++) {
        int note_samples = (TEST_UAC_SAMPLE_RATE * melody[i].duration_ms) / 1000;
        if (pos < note_samples) {
            *note_sample_offset = pos;
            return melody[i].freq_hz;
        }
        pos -= note_samples;
    }
    *note_sample_offset = 0;
    return 0;
}

static void fill_test_melody(uint8_t *data, int size, int sample_offset)
{
    int16_t *samples = (int16_t *)data;
    int sample_count = size / sizeof(int16_t);
    for (int i = 0; i < sample_count; i++) {
        int note_sample_offset = 0;
        uint16_t freq_hz = melody_freq_at_sample(sample_offset + i, &note_sample_offset);
        samples[i] = melody_sample(freq_hz, note_sample_offset);
    }
}

static bool has_nonzero_sample(const uint8_t *data, int size)
{
    for (int i = 0; i < size; i++) {
        if (data[i] != 0) {
            return true;
        }
    }
    return false;
}

static void print_record_samples(int chunk_idx, const uint8_t *data)
{
    const int16_t *samples = (const int16_t *)data;
    printf("index %04d: %6d, %6d, %6d, %6d\n",
           chunk_idx, samples[0], samples[1], samples[2], samples[3]);
}

static bool try_common_uac_formats(esp_codec_dev_type_t dev_type)
{
    esp_codec_dev_sample_info_t formats[] = {
        {
            .sample_rate = TEST_UAC_SAMPLE_RATE,
            .bits_per_sample = TEST_UAC_BITS,
            .channel = TEST_UAC_CHANNELS,
            .channel_mask = 0x01,
        },
    };

    for (int i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
        if (try_open_uac_dev(dev_type, &formats[i]) == ESP_CODEC_DEV_OK) {
            return true;
        }
    }
    return false;
}

static void log_uac_caps(const esp_codec_dev_capability_t *caps, int count)
{
    for (int i = 0; i < count; i++) {
        if (caps[i].mode == ESP_CODEC_DEV_CAPS_MODE_FIXED) {
            ESP_LOGI(TAG, "cap[%d] type=%d fixed: %lu Hz %u-bit %u ch",
                     i, caps[i].dev_type, caps[i].fixed.sample_rate,
                     caps[i].fixed.bits_per_sample, caps[i].fixed.channel);
        } else {
            ESP_LOGI(TAG, "cap[%d] type=%d flexible: max_ch=%u bits=%u rates=%u",
                     i, caps[i].dev_type, caps[i].flexible.max_channels,
                     caps[i].flexible.bits_num, caps[i].flexible.sample_rate_num);
        }
    }
}

static bool select_first_fixed_cap(const esp_codec_dev_capability_t *caps, int count,
                                   esp_codec_dev_sample_info_t *fs)
{
    for (int i = 0; i < count; i++) {
        if (caps[i].mode == ESP_CODEC_DEV_CAPS_MODE_FIXED &&
            caps[i].fixed.sample_rate == TEST_UAC_SAMPLE_RATE) {
            fs->sample_rate = caps[i].fixed.sample_rate;
            fs->bits_per_sample = caps[i].fixed.bits_per_sample;
            fs->channel = caps[i].fixed.channel;
            fs->channel_mask = (caps[i].fixed.channel < 16) ? ((1U << caps[i].fixed.channel) - 1U) : 0xffff;
            return true;
        }
    }
    return false;
}

static esp_err_t try_query_and_open_uac_dev(esp_codec_dev_type_t dev_type, bool delayed_power_on)
{
    test_uac_dev_ctx_t dev_ctx = {0};
    esp_codec_dev_uac_cfg_t mgr_cfg = test_uac_mgr_cfg(15000);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_install(&mgr_cfg));

    esp_codec_dev_uac_select_t sel = {
        .dev_type = dev_type,
    };
    dev_ctx.dev = esp_codec_dev_uac_new_dev(&sel);
    TEST_ASSERT_NOT_NULL(dev_ctx.dev);

    if (delayed_power_on) {
        test_usb_power_enable(false);
        vTaskDelay(pdMS_TO_TICKS(300));
        xTaskCreate(delayed_usb_power_on_task, "uac_power_on", 2048, NULL, 19, NULL);
    }

    esp_codec_dev_capability_t caps[TEST_UAC_CAPS_MAX];
    int count = sizeof(caps) / sizeof(caps[0]);
    esp_err_t ret = esp_codec_dev_get_caps(dev_ctx.dev, caps, &count);
    ESP_LOGI(TAG, "get_caps %s -> %s, count=%d",
             dev_type == ESP_CODEC_DEV_TYPE_OUT ? "TX" : "RX",
             esp_err_to_name(ret), count);
    if (ret == ESP_CODEC_DEV_OK) {
        log_uac_caps(caps, count);
        esp_codec_dev_sample_info_t fs = {0};
        TEST_ASSERT_TRUE(select_first_fixed_cap(caps, count, &fs));
        ret = esp_codec_dev_open(dev_ctx.dev, &fs);
        ESP_LOGI(TAG, "open from caps %s %lu Hz %u-bit %u ch mask 0x%x -> %s",
                 dev_type == ESP_CODEC_DEV_TYPE_OUT ? "TX" : "RX",
                 fs.sample_rate, fs.bits_per_sample, fs.channel,
                 fs.channel_mask, esp_err_to_name(ret));
        if (ret == ESP_CODEC_DEV_OK) {
            esp_codec_dev_close(dev_ctx.dev);
        }
    }

    test_uac_dev_close(&dev_ctx);
    return ret;
}

static void test_uac_get_caps_rejects_invalid_args(void)
{
    esp_codec_dev_uac_cfg_t mgr_cfg = {0};
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_install(&mgr_cfg));

    esp_codec_dev_uac_select_t sel = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_uac_new_dev(&sel);
    TEST_ASSERT_NOT_NULL(dev);

    esp_codec_dev_capability_t caps[1];
    int count = 1;
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG,
                      esp_codec_dev_get_caps(NULL, caps, &count));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG,
                      esp_codec_dev_get_caps(dev, caps, NULL));
    count = -1;
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG,
                      esp_codec_dev_get_caps(dev, caps, &count));

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_del_dev(dev));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_uninstall());
}

static void test_uac_manager_creates_and_derives_device(void)
{
    esp_codec_dev_uac_cfg_t mgr_cfg = {0};
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_install(&mgr_cfg));

    esp_codec_dev_uac_select_t sel = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_uac_new_dev(&sel);
    TEST_ASSERT_NOT_NULL(dev);

    /* Multi-device build: deriving a second handle (e.g. another address) succeeds;
     * binding is resolved lazily at open time. */
    esp_codec_dev_uac_select_t sel2 = {
        .mode = ESP_CODEC_DEV_UAC_SELECT_BY_ADDR,
        .addr = 2,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    esp_codec_dev_handle_t dev2 = esp_codec_dev_uac_new_dev(&sel2);
    TEST_ASSERT_NOT_NULL(dev2);

    /* Invalid argument handling */
    TEST_ASSERT_NULL(esp_codec_dev_uac_new_dev(NULL));

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_del_dev(dev));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_del_dev(dev2));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_uninstall());
}

static void test_uac_get_num_and_info_without_connected_device(void)
{
    esp_codec_dev_uac_cfg_t mgr_cfg = {0};
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_install(&mgr_cfg));

    uint8_t num = 0;
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_get_num(&num));
    TEST_ASSERT_EQUAL(0, num);

    esp_codec_dev_uac_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_NOT_FOUND, esp_codec_dev_uac_get_info(0, &info));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, esp_codec_dev_uac_get_info(0, NULL));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_INVALID_ARG, esp_codec_dev_uac_get_num(NULL));

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_uninstall());
}

static void test_uac_install_accepts_event_callback(void)
{
    esp_codec_dev_uac_cfg_t mgr_cfg = {
        .event_cb = test_uac_event_cb,
    };
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_install(&mgr_cfg));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_uninstall());
}

static void test_uac_layout_query_not_supported(void)
{
    esp_codec_dev_uac_cfg_t mgr_cfg = {0};
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_install(&mgr_cfg));

    esp_codec_dev_uac_select_t sel = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_uac_new_dev(&sel);
    TEST_ASSERT_NOT_NULL(dev);

    esp_codec_dev_channel_map_t layout = {0};
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_NOT_SUPPORT,
                      esp_codec_dev_get_data_layout(dev, &layout));

    char label[16] = {0};
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_NOT_SUPPORT,
                      esp_codec_dev_get_data_layout_label(dev, label, sizeof(label)));

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_del_dev(dev));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_uninstall());
}

static void test_uac_hardware_observe_connect_disconnect_events(void)
{
    test_usb_host_ctx_t host_ctx = {0};
    uac_event_stats_t stats = {0};

    TEST_ESP_OK(test_usb_host_start(&host_ctx));

    esp_codec_dev_uac_cfg_t mgr_cfg = test_uac_mgr_cfg(15000);
    mgr_cfg.event_ctx = &stats;
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_install(&mgr_cfg));

    TEST_ESP_OK(test_usb_power_enable(true));
    ESP_LOGI(TAG, "USB power enabled, waiting for UAC enumeration");
    vTaskDelay(pdMS_TO_TICKS(1000));

    esp_codec_dev_handle_t dev = test_uac_open_first_available();
    TEST_ASSERT_NOT_NULL_MESSAGE(dev, "Connect a UAC device before running this test");

    ESP_LOGI(TAG, "Stream opened; connected events so far: %d", stats.connected_count);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, stats.connected_count,
                                     "Expected CONNECTED event after UAC enumeration");

    ESP_LOGI(TAG, "Cutting USB power to trigger disconnect (stream still open)");
    TEST_ESP_OK(test_usb_power_enable(false));
    vTaskDelay(pdMS_TO_TICKS(TEST_UAC_POWER_CYCLE_DELAY_MS));

    ESP_LOGI(TAG, "Disconnect events so far: %d", stats.disconnected_count);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, stats.disconnected_count,
                                     "Expected DISCONNECTED event after USB power off with open stream");

    esp_codec_dev_close(dev);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_del_dev(dev));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_uninstall());
    test_usb_host_stop(&host_ctx);
}

static void test_uac_hardware_device_can_open_with_usb_host(void)
{
    test_usb_host_ctx_t host_ctx = {0};
    TEST_ESP_OK(test_usb_host_start(&host_ctx));
    ESP_LOGI(TAG, "USB Host started, keep the UAC device connected");
    vTaskDelay(pdMS_TO_TICKS(1000));

    bool opened = try_common_uac_formats(ESP_CODEC_DEV_TYPE_OUT);
    if (!opened) {
        opened = try_common_uac_formats(ESP_CODEC_DEV_TYPE_IN);
    }

    test_usb_host_stop(&host_ctx);
    TEST_ASSERT_TRUE_MESSAGE(opened, "No UAC RX/TX stream opened with common formats");
}

static void test_uac_hardware_get_caps_before_open(void)
{
    test_usb_host_ctx_t host_ctx = {0};
    TEST_ESP_OK(test_usb_host_start(&host_ctx));
    ESP_LOGI(TAG, "USB Host started, query UAC caps before open");

    esp_err_t ret = try_query_and_open_uac_dev(ESP_CODEC_DEV_TYPE_OUT, true);
    if (ret != ESP_CODEC_DEV_OK) {
        ret = try_query_and_open_uac_dev(ESP_CODEC_DEV_TYPE_IN, true);
    }

    test_usb_host_stop(&host_ctx);
    TEST_ESP_OK(ret);
}

static void test_uac_hardware_open_times_out_without_device(void)
{
    test_usb_host_ctx_t host_ctx = {0};
    test_uac_dev_ctx_t dev_ctx = {0};

    TEST_ESP_OK(test_usb_host_start(&host_ctx));
    esp_err_t ret = test_uac_dev_open_with_power(ESP_CODEC_DEV_TYPE_IN, &dev_ctx, false);
    TEST_ASSERT_NOT_EQUAL(ESP_CODEC_DEV_OK, ret);
    ESP_LOGI(TAG, "open without UAC device returned %s", esp_err_to_name(ret));

    test_uac_dev_close(&dev_ctx);
    test_usb_host_stop(&host_ctx);
}

static void test_uac_hardware_record_detects_usb_disconnect(void)
{
    test_usb_host_ctx_t host_ctx = {0};
    test_uac_dev_ctx_t dev_ctx = {0};
    uint8_t data[TEST_UAC_CHUNK_BYTES] = {0};

    TEST_ESP_OK(test_usb_host_start(&host_ctx));
    TEST_ESP_OK(test_uac_dev_open(ESP_CODEC_DEV_TYPE_IN, &dev_ctx));

    for (int i = 0; i < TEST_UAC_DISCONNECT_RECORD_CHUNKS; i++) {
        TEST_ESP_OK(esp_codec_dev_read(dev_ctx.dev, data, sizeof(data)));
    }
    ESP_LOGI(TAG, "recorded %d seconds before USB disconnect", TEST_UAC_DISCONNECT_RECORD_SEC);

    TEST_ESP_OK(test_usb_power_enable(false));
    vTaskDelay(pdMS_TO_TICKS(TEST_UAC_POWER_CYCLE_DELAY_MS));
    int ret = esp_codec_dev_read(dev_ctx.dev, data, sizeof(data));
    TEST_ASSERT_NOT_EQUAL(ESP_CODEC_DEV_OK, ret);
    ESP_LOGI(TAG, "record after USB disconnect returned %s", esp_err_to_name(ret));

    test_uac_dev_close(&dev_ctx);
    test_usb_host_stop(&host_ctx);
}

static void test_uac_hardware_reopens_after_usb_power_cycle(void)
{
    test_usb_host_ctx_t host_ctx = {0};
    test_uac_dev_ctx_t dev_ctx = {0};
    uint8_t data[TEST_UAC_CHUNK_BYTES] = {0};
    bool got_data = false;

    TEST_ESP_OK(test_usb_host_start(&host_ctx));
    TEST_ESP_OK(test_uac_dev_open(ESP_CODEC_DEV_TYPE_IN, &dev_ctx));
    vTaskDelay(pdMS_TO_TICKS(TEST_UAC_POWER_CYCLE_DELAY_MS));
    test_uac_dev_close(&dev_ctx);
    vTaskDelay(pdMS_TO_TICKS(1000));
    test_usb_host_stop(&host_ctx);
    TEST_ESP_OK(test_usb_power_enable(false));
    vTaskDelay(pdMS_TO_TICKS(300));
    TEST_ESP_OK(test_usb_power_enable(true));
    TEST_ESP_OK(test_usb_host_start(&host_ctx));

    TEST_ESP_OK(test_uac_dev_open(ESP_CODEC_DEV_TYPE_IN, &dev_ctx));
    for (int i = 0; i < TEST_UAC_RECONNECT_READ_CHUNKS; i++) {
        memset(data, 0, sizeof(data));
        TEST_ESP_OK(esp_codec_dev_read(dev_ctx.dev, data, sizeof(data)));
        got_data |= has_nonzero_sample(data, sizeof(data));
    }
    TEST_ASSERT_TRUE_MESSAGE(got_data, "No nonzero microphone data after USB power cycle reopen");
    ESP_LOGI(TAG, "reopened after USB power cycle and read microphone data");

    test_uac_dev_close(&dev_ctx);
    test_usb_host_stop(&host_ctx);
}

static void test_uac_hardware_record_reads_microphone_data(void)
{
    test_usb_host_ctx_t host_ctx = {0};
    test_uac_dev_ctx_t dev_ctx = {0};
    uint8_t data[TEST_UAC_CHUNK_BYTES] = {0};
    bool got_data = false;

    TEST_ESP_OK(test_usb_host_start(&host_ctx));
    TEST_ESP_OK(test_uac_dev_open(ESP_CODEC_DEV_TYPE_IN, &dev_ctx));
    TEST_ESP_OK(esp_codec_dev_set_in_gain(dev_ctx.dev, TEST_UAC_LOOPBACK_MIC_GAIN_DB));

    for (int i = 0; i < TEST_UAC_RECORD_CHUNKS; i++) {
        memset(data, 0, sizeof(data));
        TEST_ESP_OK(esp_codec_dev_read(dev_ctx.dev, data, sizeof(data)));
        print_record_samples(i, data);
        got_data |= has_nonzero_sample(data, sizeof(data));
    }

    ESP_LOGI(TAG, "record read %d chunks, %d seconds, nonzero:%d",
             TEST_UAC_RECORD_CHUNKS, TEST_UAC_RECORD_SEC, got_data);
    test_uac_dev_close(&dev_ctx);
    test_usb_host_stop(&host_ctx);
}

static void test_uac_hardware_playback_writes_speaker_data(void)
{
    test_usb_host_ctx_t host_ctx = {0};
    test_uac_dev_ctx_t dev_ctx = {0};
    uint8_t data[TEST_UAC_CHUNK_BYTES] = {0};

    TEST_ESP_OK(test_usb_host_start(&host_ctx));
    TEST_ESP_OK(test_uac_dev_open(ESP_CODEC_DEV_TYPE_OUT, &dev_ctx));
    TEST_ESP_OK(esp_codec_dev_set_out_vol(dev_ctx.dev, TEST_UAC_OUT_VOLUME));

    for (int i = 0; i < TEST_UAC_PLAY_CHUNKS; i++) {
        fill_test_melody(data, sizeof(data), i * (sizeof(data) / sizeof(int16_t)));
        TEST_ESP_OK(esp_codec_dev_write(dev_ctx.dev, data, sizeof(data)));
        vTaskDelay(pdMS_TO_TICKS(TEST_UAC_CHUNK_MS));
    }
    vTaskDelay(pdMS_TO_TICKS(300));

    ESP_LOGI(TAG, "playback wrote %d chunks, %d seconds at %d%% volume",
             TEST_UAC_PLAY_CHUNKS, TEST_UAC_PLAYBACK_SEC, TEST_UAC_OUT_VOLUME);
    test_uac_dev_close(&dev_ctx);
    test_usb_host_stop(&host_ctx);
}

static void test_uac_hardware_loopback_reads_mic_and_writes_speaker(void)
{
    test_usb_host_ctx_t host_ctx = {0};
    test_uac_dev_ctx_t dev_ctx = {0};
    uint8_t data[TEST_UAC_CHUNK_BYTES] = {0};

    TEST_ESP_OK(test_usb_host_start(&host_ctx));
    TEST_ESP_OK(test_uac_dev_open(ESP_CODEC_DEV_TYPE_IN_OUT, &dev_ctx));
    TEST_ESP_OK(esp_codec_dev_set_in_gain(dev_ctx.dev, TEST_UAC_LOOPBACK_MIC_GAIN_DB));
    TEST_ESP_OK(esp_codec_dev_set_out_vol(dev_ctx.dev, TEST_UAC_LOOPBACK_OUT_VOLUME));

    for (int i = 0; i < TEST_UAC_LOOPBACK_CHUNKS; i++) {
        TEST_ESP_OK(esp_codec_dev_read(dev_ctx.dev, data, sizeof(data)));
        TEST_ESP_OK(esp_codec_dev_write(dev_ctx.dev, data, sizeof(data)));
    }
    vTaskDelay(pdMS_TO_TICKS(300));

    ESP_LOGI(TAG, "loopback transferred %d chunks, %d seconds, mic %d%%, speaker %d%%",
             TEST_UAC_LOOPBACK_CHUNKS, TEST_UAC_LOOPBACK_SEC,
             TEST_UAC_LOOPBACK_MIC_VOLUME, TEST_UAC_LOOPBACK_OUT_VOLUME);
    test_uac_dev_close(&dev_ctx);
    test_usb_host_stop(&host_ctx);
}

static void test_uac_hardware_loopback_separate_in_out_devs(void)
{
    test_usb_host_ctx_t host_ctx = {0};
    esp_codec_dev_handle_t in_dev = NULL;
    esp_codec_dev_handle_t out_dev = NULL;
    uint8_t data[TEST_UAC_CHUNK_BYTES] = {0};

    TEST_ESP_OK(test_usb_host_start(&host_ctx));

    esp_codec_dev_uac_cfg_t mgr_cfg = test_uac_mgr_cfg(5000);
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_install(&mgr_cfg));

    esp_codec_dev_uac_select_t in_sel = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    esp_codec_dev_uac_select_t out_sel = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    in_dev = esp_codec_dev_uac_new_dev(&in_sel);
    out_dev = esp_codec_dev_uac_new_dev(&out_sel);
    TEST_ASSERT_NOT_NULL(in_dev);
    TEST_ASSERT_NOT_NULL(out_dev);

    esp_codec_dev_sample_info_t fs = test_uac_sample_info();
    test_usb_power_enable(false);
    vTaskDelay(pdMS_TO_TICKS(300));
    xTaskCreate(delayed_usb_power_on_task, "uac_power_on", 2048, NULL, 19, NULL);

    TEST_ESP_OK(esp_codec_dev_open(in_dev, &fs));
    TEST_ESP_OK(esp_codec_dev_open(out_dev, &fs));
    TEST_ESP_OK(esp_codec_dev_set_in_gain(in_dev, TEST_UAC_LOOPBACK_MIC_GAIN_DB));
    TEST_ESP_OK(esp_codec_dev_set_out_vol(out_dev, TEST_UAC_LOOPBACK_OUT_VOLUME));

    for (int i = 0; i < TEST_UAC_LOOPBACK_CHUNKS; i++) {
        TEST_ESP_OK(esp_codec_dev_read(in_dev, data, sizeof(data)));
        TEST_ESP_OK(esp_codec_dev_write(out_dev, data, sizeof(data)));
    }
    vTaskDelay(pdMS_TO_TICKS(300));

    ESP_LOGI(TAG, "separate IN/OUT loopback transferred %d chunks, %d seconds, mic %d%%, speaker %d%%",
             TEST_UAC_LOOPBACK_CHUNKS, TEST_UAC_LOOPBACK_SEC,
             TEST_UAC_LOOPBACK_MIC_VOLUME, TEST_UAC_LOOPBACK_OUT_VOLUME);

    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_del_dev(in_dev));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_del_dev(out_dev));
    TEST_ASSERT_EQUAL(ESP_CODEC_DEV_OK, esp_codec_dev_uac_uninstall());
    test_usb_host_stop(&host_ctx);
}

TEST_CASE("uac get caps rejects invalid args", "[mock][uac][api]")
{
    test_uac_get_caps_rejects_invalid_args();
}

TEST_CASE("uac manager creates and derives device", "[mock][uac][api]")
{
    test_uac_manager_creates_and_derives_device();
}

TEST_CASE("uac get num and info without connected device", "[mock][uac][api]")
{
    test_uac_get_num_and_info_without_connected_device();
}

TEST_CASE("uac install accepts event callback", "[mock][uac][event]")
{
    test_uac_install_accepts_event_callback();
}

TEST_CASE("uac layout query is not supported", "[mock][uac][layout]")
{
    test_uac_layout_query_not_supported();
}

TEST_CASE("uac hardware observe connect disconnect events", "[korvo_2l][uac][hardware][event][manual]")
{
    test_uac_hardware_observe_connect_disconnect_events();
}

TEST_CASE("uac hardware device can open with usb host", "[korvo_2l][uac][hardware][manual]")
{
    test_uac_hardware_device_can_open_with_usb_host();
}

TEST_CASE("uac hardware get caps before open", "[korvo_2l][uac][hardware][caps][manual]")
{
    test_uac_hardware_get_caps_before_open();
}

TEST_CASE("uac hardware open times out without device", "[korvo_2l][uac][hardware][disconnect][manual]")
{
    test_uac_hardware_open_times_out_without_device();
}

TEST_CASE("uac hardware record detects usb disconnect", "[korvo_2l][uac][hardware][disconnect][manual]")
{
    test_uac_hardware_record_detects_usb_disconnect();
}

TEST_CASE("uac hardware reopens after usb power cycle", "[korvo_2l][uac][hardware][disconnect][manual]")
{
    test_uac_hardware_reopens_after_usb_power_cycle();
}

TEST_CASE("uac hardware record reads microphone data", "[korvo_2l][uac][hardware][record][manual]")
{
    test_uac_hardware_record_reads_microphone_data();
}

TEST_CASE("uac hardware playback writes speaker data", "[korvo_2l][uac][hardware][playback][manual]")
{
    test_uac_hardware_playback_writes_speaker_data();
}

TEST_CASE("uac hardware loopback reads microphone and writes speaker", "[korvo_2l][uac][hardware][loopback][manual]")
{
    test_uac_hardware_loopback_reads_mic_and_writes_speaker();
}

TEST_CASE("uac hardware loopback with separate IN and OUT devices", "[korvo_2l][uac][hardware][loopback][manual]")
{
    test_uac_hardware_loopback_separate_in_out_devs();
}
