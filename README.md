# ESP Audio Device

- [中文](./README_CN.md)

`esp-audio-dev` brings together audio device components for Espressif platforms and provides applications and higher-level audio frameworks with consistent hardware access. Each component can be integrated independently and maintains its own source code, component manifest, documentation, and tests.

The repository currently centers on `esp_codec_dev`. It combines audio codec chip drivers, control paths, and PCM data paths behind a unified device API for playback, recording, volume control, gain control, and related use cases.

## Components

| Component | Status | Purpose | Documentation and tests |
| --- | --- | --- | --- |
| [`esp_codec_dev`](./esp_codec_dev) | Beta | Unified codec device abstraction, chip drivers, and playback/recording interfaces for external and on-chip audio paths | [English documentation](./esp_codec_dev/README.md) · [中文](./esp_codec_dev/README_CN.md) · [Test applications](./esp_codec_dev/test_apps) |

## Getting Started

### Add the Component from ESP Component Registry

Run the following command in your ESP-IDF project:

```bash
idf.py add-dependency "espressif/esp_codec_dev"
```

The current `esp_codec_dev` v2.0 Beta requires ESP-IDF 6.0 or later. When integrating through ESP Component Registry, select a component release compatible with your project's ESP-IDF version. See [`idf_component.yml`](./esp_codec_dev/idf_component.yml) and the component documentation for dependencies and optional features.

### Integrate from Source

Place the required component directory under your ESP-IDF project's `components/` directory, then follow its README to create the hardware interfaces and initialize the device. For supported chips, hardware architecture, configuration examples, and API usage, see:

- [`esp_codec_dev` documentation](./esp_codec_dev/README.md)
- [`codec_dev_test` test application](./esp_codec_dev/test_apps/codec_dev_test)
- [`codec_dev_uac_test` test application](./esp_codec_dev/test_apps/codec_dev_uac_test)

## Repository Layout

```text
esp-audio-dev/
├── esp_codec_dev/   # Codec device component, drivers, documentation, and tests
└── tools/           # Repository test and CI tools
```

Future audio device components will be added as independent top-level directories. Each component owns its source, metadata, documentation, and tests, while the repository root remains the entry point for discovery and navigation.

## Feedback and Contributions

- Report problems and feature requests through [GitHub Issues](https://github.com/espressif/esp-audio-dev/issues).
- Contributions of codec drivers, platform ports, tests, and documentation improvements are welcome.
- New components should use independent top-level directories and update both root component lists.
