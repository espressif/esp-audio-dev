| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C6 | ESP32-S2 | ESP32-S3 | ESP32-P4 |
| ----------------- | ----- | -------- | -------- | -------- | -------- | -------- | -------- |

# esp_codec_dev: Codec device test application

Unity tests for `esp_codec_dev` v2.0. ESP-IDF version follows the component [idf_component.yml](../../idf_component.yml) (currently `>=5.4.4`, with some 5.5.x versions excluded).

## Test groups

1. Mock codec tests (`main/mock/test_my_codec.c`, `main/mock/test_layout_mock.c`): API coverage without hardware.
2. Board tests: hardware playback, recording, duplex, and multi-codec cases per target.

Target-specific sources are selected in [main/CMakeLists.txt](main/CMakeLists.txt):

| Target | Board test sources |
| --- | --- |
| ESP32-S3 | `main/boards/esp32s3/test_board.c`, `test_board_i2s_open_order.c`, `test_board_single_channel.c`; `main/boards/es8389/test_board_es8389.c` |
| ESP32-S31 | `main/boards/es8389/test_board_es8389.c` |
| ESP32 | `main/boards/esp32/test_board_i2s_hw_1.c` (ESP32-LyraT-Mini) |
| ESP32-C3 | `main/boards/esp32c3/test_board_c3_lyra.c` (requires `CONFIG_CODEC_DATA_ADC_SUPPORT`) |
| ESP32-P4 | `main/boards/esp32p4/test_p4_ev_board.c`, `test_p4_ev_board_lp.c` |

## Board configuration

Default S3 board pins and peripherals are defined in [test_board.h](main/include/test_board.h) and [test_board_periph.c](main/common/test_board_periph.c). For another board, update those files and the board-specific test source under `main/boards/`.

## Build and run

```sh
cd esp_codec_dev/test_apps/codec_dev_test
idf.py set-target esp32s3
idf.py build flash monitor
```

Pytest entry: [pytest_esp_codec_dev_test.py](pytest_esp_codec_dev_test.py).

Component documentation: [../../README.md](../../README.md).
