# codec_dev_uac_test

This test application validates USB Audio Class support in `esp_codec_dev`.

Mock tests run without a USB audio device. Hardware tests require an ESP32-S2, ESP32-S3, or ESP32-P4 board with USB Host wiring, adequate device power, and a UAC 1.0 compatible USB audio device.

The application owns the USB Host lifecycle. Hardware tests call `usb_host_install()`, run a USB Host library event task that calls `usb_host_lib_handle_events()`, and call `usb_host_uninstall()` during cleanup.

Enable `CONFIG_CODEC_UAC_SUPPORT` in menuconfig before building.

## Build

ESP-IDF version follows the component [idf_component.yml](../../idf_component.yml) (currently `>=5.4.4`, with some 5.5.x versions excluded).

```sh
export IDF_PATH=/path/to/esp-idf
. $IDF_PATH/export.sh
cd esp_codec_dev/test_apps/codec_dev_uac_test
idf.py set-target esp32s3
idf.py build flash monitor
```

Pytest entry: [pytest_codec_dev_uac.py](pytest_codec_dev_uac.py).

Component documentation: [../../README.md](../../README.md).
