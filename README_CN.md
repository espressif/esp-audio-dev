# ESP Audio Device

- [English](./README.md)

`esp-audio-dev` 汇集乐鑫平台的音频设备组件，为应用和上层音频框架提供统一的硬件访问能力。仓库中的每个组件均可独立集成，并维护各自的源码、组件清单、文档和测试。

当前仓库主要提供 `esp_codec_dev`：它将音频 codec 芯片驱动、控制通路和 PCM 数据通路组合为统一的设备接口，可用于播放、录音、音量与增益控制等场景。

## 组件

| 组件 | 状态 | 功能 | 文档与测试 |
| --- | --- | --- | --- |
| [`esp_codec_dev`](./esp_codec_dev) | Beta | 为外部及片上音频通路提供统一的 codec device 抽象、芯片驱动和播放/录音接口 | [中文文档](./esp_codec_dev/README_CN.md) · [English](./esp_codec_dev/README.md) · [测试应用](./esp_codec_dev/test_apps) |

## 快速开始

### 通过 ESP Component Registry 添加

在 ESP-IDF 项目目录中运行：

```bash
idf.py add-dependency "espressif/esp_codec_dev"
```

当前 `esp_codec_dev` v2.0 Beta 需要 ESP-IDF 6.0 或更高版本。通过 ESP Component Registry 集成时，请选择与项目 ESP-IDF 版本兼容的组件版本；具体依赖和可选功能以 [`idf_component.yml`](./esp_codec_dev/idf_component.yml) 及组件文档为准。

### 从源码集成

将所需组件目录放入 ESP-IDF 项目的 `components/` 目录，然后按照该组件的 README 完成硬件接口创建和设备初始化。`esp_codec_dev` 的支持芯片、接线分层、配置示例及 API 用法请参阅：

- [`esp_codec_dev` 中文文档](./esp_codec_dev/README_CN.md)
- [`codec_dev_test` 测试应用](./esp_codec_dev/test_apps/codec_dev_test)
- [`codec_dev_uac_test` 测试应用](./esp_codec_dev/test_apps/codec_dev_uac_test)

## 仓库结构

```text
esp-audio-dev/
├── esp_codec_dev/   # Codec device 组件、驱动、文档与测试
└── tools/           # 仓库测试与 CI 工具
```

后续音频设备组件将以独立顶层目录加入；每个组件自行维护源码、元数据、文档和测试，仓库根目录用于组件发现与导航。

## 反馈与贡献

- 遇到问题或有功能建议，请提交 [GitHub Issue](https://github.com/espressif/esp-audio-dev/issues)。
- 欢迎贡献新的 codec 驱动、平台适配、测试及文档改进。
- 新组件应使用独立顶层目录，并同步更新根目录的中英文组件列表。
