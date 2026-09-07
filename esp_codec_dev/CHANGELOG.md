# Changelog

## v2.0.0-beta4

### Breaking Change

- Defined direction-specific `channel_mask` semantics for mapping-aware TDM: input masks select physical slots, while output masks select codec logical channels.
- Mapping-aware STD requests wider than two channels now require a full application channel mask; unsupported partial masks return `ESP_CODEC_DEV_NOT_SUPPORT` instead of being silently widened.
- Removed `audio_codec_data_if_t.get_order` and `get_channel_mask`. Slot mask to channel map conversion is done inside the component (`codec_dev_order_from_mask` / `codec_dev_order_to_mask`); custom data interfaces no longer implement these hooks.

### Feature

- Added `audio_codec_adc_label_parse()` to convert `adc_cfg.label` into a hardware MIC mask (LSB→MSB; bit i is set unless token i is `NA`) and an output `channel_num` (token count including `NA`). Allowed tokens are `FC`, `RE`, `FL`, `FR`, `SL`, `SR`, `BL`, `BR`, and `NA` (exact, case-sensitive). Duplicates are allowed; empty tokens such as `"FL,,RE"` and unknown tokens such as `"na"` or `"N/A"` are invalid; `NULL` or `""` yields mask `0xFFFF` and `channel_num` 0. Codecs can use the label to select which microphone channels are enabled.
- Added `audio_codec_ctrl_ref_acquire()` / `audio_codec_ctrl_ref_release()` so IN/OUT instances can share one control interface. Acquire returning `1` means first user (open hardware); release returning `0` means last user (close).
- Added VAD hardware processing APIs for configuration, event callbacks, status polling, and filtered-audio FIFO reads. `esp_audio_hw_vad_get_frame_info()` reports the sample rate, sample width, channel count and exact frame size of the filtered-audio output, which is a different data format from the main ADC capture, so a caller can size its buffer before reading. A frame read is all-or-nothing and reports zero bytes on every failure.
- Added common codec interrupt pin configuration through `audio_hw_int_cfg_t` and `audio_codec_cfg_t`.
- Added codec-aware full-duplex I2S/TDM frame-width coordination: mapping-aware TDM sides expand `total_slot` from dense `get_order_list`/`order_info[]` rows, with atomic TX/RX reconfiguration, rollback, and actual data-layout reporting.
- Unified codec frame geometry as dense permutations of 1..N in `order_info[]` (`channels` is frame slot count); physical converter count comes from `get_caps`.
- Allowed `AUDIO_CODEC_REGISTER()` chip cfg size to equal `sizeof(audio_codec_cfg_t)`. When sizes match, `audio_codec_new()` uses `build_chip_cfg` if set, otherwise it passes the buffer to `create` as-is.

### Bug Fixed

- Fixed occasional ES8311 ADC recording errors after DAC was disabled, by not writing `ES8311_SYSTEM_REG0E` on DAC stop.

## v2.0.0-beta3

### Breaking Change

- Replaced the hardware audio processing handle APIs (`audio_hw_*_new()` / `audio_hw_*_delete()`) with `esp_audio_hw_*()` functions that operate directly on `esp_codec_dev_handle_t`.
- Renamed hardware audio processing headers, types, and macros to the `esp_audio_hw_` prefix, and moved driver vtables to `interface/esp_audio_hw_proc_if.h`.

### Feature

- Added link-time external codec registration through `AUDIO_CODEC_REGISTER()`.
- Allowed omitting `build_chip_cfg` so `audio_codec_new()` can take chip-specific cfg only.
- Enforced at compile time that chip cfg size is non-zero and differs from
  `sizeof(audio_codec_cfg_t)`, so `audio_codec_new()` cannot confuse shared and
  chip-specific configuration.
- Logged a warning when a link-time registered codec is shadowed by a built-in driver.

## v2.0.0-beta2

### Bug Fixed

- Fixed I2S TDM mono setup: the 1-channel to 2-slot promotion is done on an internal copy so the caller's `esp_codec_dev_sample_info_t` is not modified, and `ws_width` follows the actual slot count.
- Fixed slot bit width exceeding the 32-bit hardware limit after duplex peer negotiation; unsupported widths now return `ESP_CODEC_DEV_NOT_SUPPORT` instead of being silently truncated.
- Fixed the `espressif/usb_host_uac` dependency rule, which was gated on `$CONFIG{CODEC_UAC_SUPPORT}` and could fail to resolve the component.
- Fixed ES8311 initialization on boards where the first I2C write returns an error, by ignoring the result of the dummy write to `ES8311_GPIO_REG44`.

## v2.0.0-beta1

### Feature

- Added data layout API: `esp_codec_dev_set/get_data_layout()` and label-based layout helpers for explicit PCM channel order in memory.
- Refactored codec drivers toward `hw_base + adc/dac` split configuration (`audio_hw_sys_cfg_t`, `audio_hw_adc_cfg_t`, `audio_hw_dac_cfg_t`, `audio_hw_pa_cfg_t`).
- Added hardware audio processing framework (ALC, DRC, EQ, line in/out, auto/soft mute) under `include/hw_proc/`.
- Added device capability query API: `esp_codec_dev_get_caps()`.
- Added USB UAC manager (`esp_codec_dev_uac.h`) and UAC data interface when `CONFIG_CODEC_UAC_SUPPORT` is enabled.
- Added internal PCM layout conversion and `codec_dev_data_cvt` support in the read/write path.
- Improved multi-instance I2S coordination, slot bit width compatibility, and shared-port peer handling on the I2S data interface.

### Breaking Change

- Raised the minimum supported ESP-IDF version to v5.4.4 (with unsupported 5.5.x versions excluded; see `idf_component.yml`). Dropped the legacy I2C driver path: the `CODEC_I2C_BACKWARD_COMPATIBLE` option is gone and the I2C control interface now always uses the `i2c_master` driver.
- Restructured chip configuration headers: flat fields such as `codec_mode`, top-level `pa_pin`, and `use_mclk` on ES8311 are replaced by grouped sub-configs; see [docs/api_migration_guide.md](docs/api_migration_guide.md).
- Stream parameters including `mclk_multiple` and ES7210 MIC selection are applied at `esp_codec_dev_open()` through `esp_codec_dev_sample_info_t` instead of static codec construction fields.

## v1.6.2

### Feature

- Added I2C clock setting for codec devices through `clock_speed_hz`.

## v1.6.1

### Bug Fixed

- Fixed the ES8374 sample format configuration result handling to propagate errors and avoid an `unused-but-set-variable` build failure.

## v1.6.0

### Feature

- Added `esp_codec_dev_mirror_cfg()` and `esp_codec_dev_mirror_read()` for bypass reading during `esp_codec_dev_read()`.
- Added reference counting so multiple `esp_codec_dev` instances can share one physical ES8311/ES8388/ES8389 codec; hardware open/enable runs for the first user and close/disable for the last.
- Added `get_info` to the codec control interface to expose I2C address/port and SPI CS pin for identifying the same physical device.

### Bug Fixed

- Fixed reused codec instance missing configuration normalization (`mclk_div` default, and `use_mclk`/`no_dac_ref` on ES8389), which produced an invalid clock coefficient and all-zero ADC data.
- Fixed `get_coeff` failure handling so an unsupported MCLK/sample-rate combination reports an error instead of using an out-of-range coefficient index.
- Fixed PA control to skip when the codec has no output capability or no PA pin, and to set up the PA pin for shared output instances created on the reuse path.

## v1.5.11

### Bug Fixed

- Fixed ADC codec build error on IDF v6.0 and master

## v1.5.10

### Bug Fixed

- Fixed ES8389 disable and enable issues: use bias standby via `es8389_stop()` instead of full suspend when disabling the codec.
- Fixed ES8156 Playback/Record entries in README and README_CN.

## v1.5.9

### Bug Fixed

- Fixed build failure on ESP-IDF <= 5.4 caused by missing `bclk_div` field in `clk_cfg`.

## v1.5.8

### Bug Fixed

- Fixed open failures when separate ADC and DAC `data_if` instances on the same I2S port use the same audio parameters.
- Fixed I2S clock source reconfiguration using the wrong `data_if` instance when separate RX and TX `data_if` instances share the same I2S port.

## v1.5.7

### Feature

- Added support for resolving TX I2S handle from RX pair channel when only RX handle is provided in full duplex mode.
- Added slot bit width reconfiguration when peer codec on the same I2S port already runs with wider slots.
- Added channel gain setting for ES8389

### Bug Fixed

- Fixed 2ch TDM record wrongly for default right alignment comparing with STD mode.
- Enhanced compatible check for multiple instance of codec_dev use same I2S port.
  - Peer lookup prefers another data_if on the same port with only RX or only TX; if none, it matches the same data_if that has both RX and TX.
- Fixed incorrect ES8389 microphone gain table

## v1.5.6

### Bug Fixed

- Fixed es8311 pop issue when init again without close
- Fixed the esp_adc dependency issue

## v1.5.5

### Feature

- Added internal ADC microphone data interface support through `audio_codec_new_adc_data`.
- Added dummy codec support for PA-only speaker designs, allowing `esp_codec_dev_open/close` to control PA automatically.

## v1.5.4

### Feature

- Added support for I2S clock source configuration, primarily for low-power applications

### Bug Fixed

- Fixed header dependency issue for compatibility with ESP-IDF v6.0

## v1.5.3

### Bug Fixed

- Fixed the ES8389 no-sound playback issue.

## v1.5.2

### Bug Fixed

- Fixed race condition when duplex I2S used by multiple data interface and run open in parallel.

## v1.5.1

### Bug Fixed

- Fixed the register configuration for es8311 when playing 8kHz audio without using MCLK.
- Fixed race condition when record or playback disable and enable during another running.
- Fixed test_app build failed on esp-idf v6.x.
- Added support for duplex I2S TX and RX use multiple codecs (data_if use same port with one channel only).
- Added `esp_codec_dev_dump_reg` to dump codec registers.

## v1.5.0

### Refactor

- Clean up the component dependency, no longer depend on the `driver` component

## v1.4.0

### Bug Fixed

- Fixed name error in some macro definitions for ES7210.
- Fixed link error for `vol_range` redefinition.

## v1.3.6

### Feature

- Added support for CJC8910 codec.

### Bug Fixed

- Added missing ES8389 include in `esp_codec_dev_defaults.h`.

## v1.3.5

### Feature

- Added support for ES8389 codec，see detail datasheet [ES8389](http://www.everest-semi.com/pdf/ES8389%20PB.pdf)
- Added read/write register APIs
- Disabled `CONFIG_CODEC_I2C_BACKWARD_COMPATIBLE` by default.
  For ESP-IDF v5.x users who wish to continue using the legacy `i2c_driver_xx` APIs, please enable this option manually.

### Bug Fixes

- Fixed I2S PDM play noise issue

## v1.3.4

### Bug Fixes

- Enhance ES8311 I2C noise immunity

## v1.3.3

### Bug Fixes

- Fix codec without mute API, set mute wrong behavior

## v1.3.2

### Bug Fixes

- Fix `ES8374` volume register set error
- Fix `ES8388` microphone volume gain set incorrect

## v1.3.1

### Features

- Fix condition `CODEC_I2C_BACKWARD_COMPATIBLE` reversed
- Fix `CODEC_I2C_BACKWARD_COMPATIBLE` set to false build error

## v1.3.0

### Features

- Add configuration `CODEC_I2C_BACKWARD_COMPATIBLE` in Kconfig to allow use of the old I2C driver.
  Default is set to `y` for backward compatibility. To use the new I2C driver, set it to `n` instead.
- Fix send I2C address low byte firstly

## v1.2.0

### Features

- Add IDF v5.3 support
  Using new i2c driver `esp_driver_i2c`, add `bus_handle` configuration for `audio_codec_i2c_cfg_t`.
  User need create the `bus_handle` using API `i2c_new_master_bus` instead of `i2c_driver_install`.
- Change test code to standalone application, user can directly build it under folder [codec_dev_test](test_apps/codec_dev_test)

### Bug Fixes

- Fix I2S work in PDM mode record or play mono channel audio wrongly

## v1.1.0

### Features

- Add driver for AW88298, see detail datasheet [AW88298](https://datasheetspdf.com/download_new.php?id=1513778)

### Bug Fixes

- Fix ES8311 playback fade in for long time

## v1.0.3

### Bug Fixes

- Fix I2S TX and RX work in share mode, need enable TX before RX

## v1.0.2

### Features

- Add I2S TDM support
- Add API to `esp_codec_dev_set_in_channel_gain` to set input channel gain dependently

## v1.0.1

### Bug Fixes

- Fix ES8388 volume register set incorrectly

## v1.0.0

- Initial version of `esp_codec_dev`
