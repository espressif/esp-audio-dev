# esp_codec_dev 功能概览

本文档概述 `esp_codec_dev` 的主要能力与适用范围，覆盖公开 API、平台适配层、硬件 codec 驱动、硬件音频处理框架、数据布局和设备能力查询等功能。

## 1. 概述与定位

`esp_codec_dev` 是 ADF 中面向音频 codec 设备的统一驱动组件。应用通过 `esp_codec_dev_*` 完成播放、录音与控制；组件将硬件拆成三类可替换接口，便于同一套上层 API 对接不同芯片与数据通路：

- `audio_codec_ctrl_if_t`：控制通路（I2C、SPI），负责寄存器读写与设备识别。
- `audio_codec_data_if_t`：数据通路（I2S、片上 ADC、USB UAC），负责 PCM 收发与 bus 侧 channel order。
- `audio_codec_if_t`：芯片能力抽象；目前由 `hw_base`、`adc`、`dac` 与可选 `hw_proc` 组合实现。

应用侧典型调用顺序为：创建 `ctrl_if` / `data_if` / `codec_if` → `esp_codec_dev_new()` → `esp_codec_dev_open()` → `read` / `write` 及音量等控制 → `close` / `delete`。USB UAC 场景改由 UAC 管理器派生句柄，见第 8 节。

v2.0 各 codec 的配置与驱动迁移进度不一致，完整限制见第 13 节；选型时可对照第 10 节矩阵。

## 2. 顶层设备 API

顶层 API 定义在 `include/esp_codec_dev.h`，绑定一个 `codec_if` 与一个 `data_if`，在 `open` 时统一下发采样格式并启用 ADC/DAC 方向。

| 功能 | API | 行为说明 |
| --- | --- | --- |
| 获取版本 | `esp_codec_dev_get_version()` | 返回组件版本字符串。 |
| 创建设备 | `esp_codec_dev_new()` | 绑定 `dev_type`、`codec_if`、`data_if`，返回 `esp_codec_dev_handle_t`。 |
| 打开设备 | `esp_codec_dev_open()` | 校验并规范化 `esp_codec_dev_sample_info_t`，配置 data_if 与 codec，按方向启用 ADC/DAC。 |
| 读取录音数据 | `esp_codec_dev_read()` | 从 data_if 读取；若内存 layout 与硬件不一致，在组件内部做软件重排。 |
| 写入播放数据 | `esp_codec_dev_write()` | 必要时先做软件音量处理，再通过 data_if 写出。 |
| 关闭设备 | `esp_codec_dev_close()` | 禁用 data_if，并按策略禁用 ADC/DAC。 |
| 删除设备 | `esp_codec_dev_delete()` | 释放 `esp_codec_dev` 实例资源。 |
| 关闭策略 | `esp_codec_dev_set_disable_when_closed()` | 控制 `close` 时是否禁用底层 ADC/DAC，默认禁用。 |

`esp_codec_dev_open()` 使用 `esp_codec_dev_sample_info_t` 描述流格式，主要字段如下：

- `bits_per_sample`：采样位宽，支持 **16 / 24 / 32**。
- `channel`：通道数；为 0 时默认 **2**。
- `channel_mask`：启用的通道 mask；为 0 时使用与 `channel` 匹配的默认 mask。
- `sample_rate`：采样率，范围 **8000～192000 Hz**。
- `mclk_multiple`：MCLK 与采样率倍率；为 0 时默认 **256**；24 bit 且非 3 的倍数时会改为 **384**。

I2S mode（STD、TDM、PDM 等）由 data interface 从底层 I2S channel 查询，不要在 `esp_codec_dev_sample_info_t` 中传入。参数校验与报错说明见 [FAQ.md](FAQ.md) 第 1 节。

## 3. 音量、静音与增益

输出侧优先使用 codec 硬件音量与 mute；硬件不支持时回退到软件音量或静音电平。输入侧通过 ADC 增益与 mute 接口控制。用户音量范围为 **0～100**（100 对应 0 dB，0 为静音）；可通过 `esp_codec_dev_set_vol_curve()` 自定义映射。

### 输出音量

| 功能 | API | 行为说明 |
| --- | --- | --- |
| 设置输出音量 | `esp_codec_dev_set_out_vol()` | 以 0-100 作为用户音量；若 codec 支持硬件音量，映射为 dB 后写寄存器；否则使用软件音量。 |
| 获取输出音量 | `esp_codec_dev_get_out_vol()` | 返回当前保存的用户音量值。 |
| 设置音量曲线 | `esp_codec_dev_set_vol_curve()` | 自定义音量到 dB 的映射曲线。 |
| 设置音量处理器 | `esp_codec_dev_set_vol_handler()` | 注入自定义 `audio_codec_vol_if_t`；未提供时使用内置软件音量。 |

默认音量曲线由组件内部维护。软件音量主要处理 16-bit PCM，并支持渐变以避免突变；启用软件音量时 `esp_codec_dev_write()` 可能原地修改 buffer。

### 输出静音

| 功能 | API | 行为说明 |
| --- | --- | --- |
| 设置输出静音 | `esp_codec_dev_set_out_mute()` | 优先走 DAC 硬件 mute；不支持时使用软件音量降到静音电平。 |
| 获取输出静音状态 | `esp_codec_dev_get_out_mute()` | 返回当前输出静音标志。 |

### 输入增益与输入静音

| 功能 | API | 行为说明 |
| --- | --- | --- |
| 设置输入总增益 | `esp_codec_dev_set_in_gain()` | 对 ADC 输入通道设置统一增益，单位为 dB。 |
| 按通道设置输入增益 | `esp_codec_dev_set_in_channel_gain()` | 使用 `channel_mask` 对指定输入通道设置增益。 |
| 获取输入增益 | `esp_codec_dev_get_in_gain()` | 返回保存的输入增益值。 |
| 设置输入静音 | `esp_codec_dev_set_in_mute()` | 调用 ADC mute 能力。 |
| 获取输入静音 | `esp_codec_dev_get_in_mute()` | 返回当前输入静音标志。 |

## 4. 数据通道布局（data layout）

v2.0 引入 data layout，用于声明**用户内存中 PCM 各通道的排列顺序**。当硬件 bus order 与期望顺序不一致时，组件在 `read` / `write` 路径上按帧做软件重排（不对外暴露转换 API）。相关类型与 API 见 `include/esp_codec_dev_types.h`、`include/esp_codec_dev.h`。

| 功能 | API | 行为说明 |
| --- | --- | --- |
| 设置内存通道映射 | `esp_codec_dev_set_data_layout()` | 传入 `esp_codec_dev_channel_map_t`；优先通过硬件 channel mask 达成，否则启用软件重排。 |
| 用 label 设置通道映射 | `esp_codec_dev_set_data_layout_label()` | 例如 `"FL,RE,FR"`，按 codec 保存的 ADC label 映射到 channel map。 |
| 查询当前通道映射 | `esp_codec_dev_get_data_layout()` | 返回当前生效的内存 channel map。 |
| 用 label 查询映射 | `esp_codec_dev_get_data_layout_label()` | 将当前 map 反解为 ADC label 字符串。 |
| 构造 channel map | `ESP_CODEC_DEV_CHANNEL_MAP(...)` | 将逻辑声道到物理时隙的映射打包为 `map.value`。 |

layout 由两层 map 信息合成：

- `audio_codec_data_if_t.get_order` / `get_channel_mask`：数据接口根据 `channel` 与 `channel_mask` 计算 bus 侧 map。
- `audio_hw_base_t.get_order_list`：codec 驱动声明芯片侧在不同通道数、I2S 模式下的 map。

`esp_codec_dev` 组合上述结果得到最终内存顺序。规则与示例见 [data_layout/data_layout_logic.md](data_layout/data_layout_logic.md)、[data_layout/memory_data_layout.md](data_layout/memory_data_layout.md)。

## 5. 设备能力查询

`esp_codec_dev_get_caps()` 用于在打开流之前查询设备支持的采样格式组合，返回值写入 `esp_codec_dev_capability_t` 数组。

| 模式 | 含义 |
| --- | --- |
| `ESP_CODEC_DEV_CAPS_MODE_FIXED` | 固定的 `channel + bits_per_sample + sample_rate` 元组。 |
| `ESP_CODEC_DEV_CAPS_MODE_FLEXIBLE` | 指定位宽、采样率列表；`max_channels` 为该方向 I2S 最大 slot 数。 |

当前仅 **ES8311**（codec 侧）与 **UAC**（USB alt setting）实现 `get_caps`；其他 codec 调用返回 `ESP_CODEC_DEV_NOT_SUPPORT`。详见第 13 节与第 10 节矩阵。

## 6. 寄存器访问与调试

寄存器 API 经 `codec_if->hw_base` 转发至各 codec 驱动，用于 bring-up 与现场调试；是否支持取决于驱动是否实现对应 ops。

| 功能 | API | 适用范围 |
| --- | --- | --- |
| 读寄存器 | `esp_codec_dev_read_reg()` | 支持 `get_reg` 的 codec。 |
| 写寄存器 | `esp_codec_dev_write_reg()` | 支持 `set_reg` 的 codec。 |
| dump 寄存器 | `esp_codec_dev_dump_reg()` | 支持 `dump_reg` 的 codec。 |

控制通路通常由 `audio_codec_new_i2c_ctrl()` 或 `audio_codec_new_spi_ctrl()` 提供；寄存器宽度、地址格式与 dump 范围因芯片而异。

## 7. 硬件音频处理框架

v2.0 将 ALC、DRC、EQ、Line、Mute 等芯片侧音频处理模块抽象为统一句柄：应用先对目标 `codec_if` 调用 `audio_hw_*_new()` 创建处理句柄，再调用各模块的配置与 enable API。公共头文件位于 `include/hw_proc/`。

| Effect | 创建 API | 主要操作 |
| --- | --- | --- |
| ALC | `audio_hw_alc_new()` | `audio_hw_alc_set_gain()`、`audio_hw_alc_set_channel_mask()`、`audio_hw_alc_init()`、`audio_hw_alc_set_noise_gate()` |
| DRC | `audio_hw_drc_new()` | `audio_hw_drc_set_offset_gain()`、`audio_hw_drc_init()`、`audio_hw_drc_enable()` |
| EQ | `audio_hw_eq_new()` | `audio_hw_eq_set_cfg()`、`audio_hw_eq_set_band_para()`、`audio_hw_eq_enable()`、`audio_hw_eq_dump_info()` |
| Line | `audio_hw_line_new()` | `audio_hw_line_enable_in()`、`audio_hw_line_enable_out()` |
| Mute | `audio_hw_mute_new()` | `audio_hw_auto_mute_set_cfg()`、`audio_hw_auto_mute_enable()`、`audio_hw_soft_mute_set_cfg()`、`audio_hw_soft_mute_enable()` |

| Codec | 已实现硬件音频处理 |
| --- | --- |
| `ES8311` | ALC、DRC、EQ、Mute |
| `ES7210` | ALC、Mute |
| `ES8388` | ALC、Line |
| 其他 codec | 当前未提供硬件音频处理实现 |

`audio_hw_*_delete()` 仅释放句柄；芯片内部状态恢复由驱动负责。删除 `audio_codec_if_t` 前须先删除其下所有处理句柄。

## 8. 数据通路实现

数据通路决定 PCM 如何进出芯片或 USB 设备，并与 control 通路独立组合。

### I2S data interface

`audio_codec_new_i2s_data()` 对接 ESP-IDF I2S driver，提供读写、格式配置与 layout 查询。

- 支持 STD、TDM、PDM TX、PDM RX 模式。
- 支持 TX/RX 全双工；同一 I2S port 上可多 `data_if` 实例协同。
- peer 已使用更宽 slot 时，可重配 slot bit width 以对齐。
- 实现 `get_mode`、`get_fmt`、`get_order`、`get_channel_mask`，参与 data layout 推导。
- 共享端口场景维护 per-port 上下文、互斥与 peer enable 状态。

### 内部 ADC data interface

`audio_codec_new_adc_data()` 面向无外部 ADC codec 的录音场景，仅支持输入。

- ADC continuous mode；pattern 或 single unit 配置通道。
- `set_fmt` 按 `channel` 与 `channel_mask` 选择 pattern；`read` 输出中心化 PCM（16/32 bit）。
- 不支持 write、I2S mode 与 order 查询。

### USB UAC（多设备管理器）

UAC 通过 `esp_codec_dev_uac.h` 管理器暴露，依赖 `CONFIG_CODEC_UAC_SUPPORT` 与 `espressif/usb_host_uac`。应用安装 USB Host 后，由管理器派生标准 `esp_codec_dev` 句柄，无需手动拼装 `codec_if` / `data_if`：

```c
static void uac_event_cb(const esp_codec_dev_uac_event_info_t *info, void *ctx)
{
    if (info->event == ESP_CODEC_DEV_UAC_EVENT_CONNECTED) {
        // info->addr, info->dev_type (IN / OUT / IN_OUT)
    }
}

esp_codec_dev_uac_install(&(esp_codec_dev_uac_cfg_t){
    .connect_timeout_ms = 15000,
    .event_cb = uac_event_cb,
});
esp_codec_dev_uac_select_t sel = { .mode = ESP_CODEC_DEV_UAC_SELECT_FIRST, .dev_type = ESP_CODEC_DEV_TYPE_OUT };
esp_codec_dev_handle_t spk = esp_codec_dev_uac_new_dev(&sel);
// 之后使用标准 esp_codec_dev_* 接口
esp_codec_dev_uac_del_dev(spk);
esp_codec_dev_uac_uninstall();
```

要点：

- 多设备：按 USB `addr` 管理，上限 `CONFIG_CODEC_UAC_MAX_DEVICES`。
- 选择策略：`ESP_CODEC_DEV_UAC_SELECT_FIRST` / `BY_ADDR`。
- 连接/断开：在 `esp_codec_dev_uac_install()` 时注册 `event_cb`，接收 `ESP_CODEC_DEV_UAC_EVENT_CONNECTED` / `ESP_CODEC_DEV_UAC_EVENT_DISCONNECTED`（按 USB 地址聚合 IN/OUT）。回调在 UAC 驱动任务中执行，勿阻塞。
- 轮询：仍可用 `esp_codec_dev_uac_get_num()` / `esp_codec_dev_uac_get_info()` 获取快照。
- 断开限制：当前版本对「已枚举但从未 open 的设备」可能无法收到断开回调，需依赖轮询或后续 USB Host `DEV_REMOVED` 支持。
- 派生句柄由用户 `esp_codec_dev_uac_del_dev()` 释放；`uninstall()` 前须释放所有派生句柄。
- 设备断连后已 open 的流上 `read` / `write` 返回 `ESP_CODEC_DEV_WRONG_STATE`；断连会清除已解析的 binding，再次 open 会重新解析。

## 9. 控制与 GPIO 通路

控制与 GPIO 接口由 codec 驱动在初始化时使用，应用通常通过工厂函数创建后传入 `codec_cfg`。

### I2C 控制接口

`audio_codec_new_i2c_ctrl()` 基于 `i2c_master`，提供 `open` / `read_reg` / `write_reg` / `get_info` / `close`。调用方须先 `i2c_new_master_bus()` 并传入 `bus_handle`。`get_info` 用于识别同一物理 I2C 设备。

### SPI 控制接口

`audio_codec_new_spi_ctrl()` 提供 SPI 初始化、寄存器读写与 `get_info`（port、CS 等）。

### GPIO 接口

`audio_codec_new_gpio()` 提供配置、set/get 与 ISR 注册，供 PA、reset、IRQ 等板级引脚使用。

### 多实例引用计数

`ES8311`、`ES8389` 等通过 `codec_ref_helper` 对同一 I2C 控制设备做引用计数：相同物理设备只 open 一次，最后一个逻辑实例 close 时才真正关闭硬件。

## 10. 支持的 codec 矩阵

以下矩阵用于选型与确认 v2.0 配置模型；与 README 芯片列表互补，列出版本差异与能力细项。各芯片厂商在线文档见 [codec_datasheets.md](codec_datasheets.md)。

**硬件实测**列：`Y` 表示已在本组件 `test_apps` 中完成实机验证；`N` 表示驱动仍可编译使用，但 v2.0 尚未完成硬件实测。数据通路实测状态见本节末尾。

### 播放/录音与配置模型

| Codec | 类型 | 播放 | 录音 | 配置模型 | 硬件实测 | 说明 |
| --- | --- | --- | --- | --- | :---: | --- |
| `ES8311` | ADC + DAC codec | Y | Y | 完整新模型 | Y | `sys_cfg`、`adc_cfg`、`dac_cfg`、`pa_cfg`；caps、order、label、多实例引用计数。 |
| `ES7210` | ADC | N | Y | 新模型 | Y | `sys_cfg`、`adc_cfg`；order、label、ALC、Mute。 |
| `ES7243` | ADC | N | Y | 部分新模型 | Y | 主要 `adc_cfg`；ADC label。 |
| `ES7243E` | ADC | N | Y | 部分新模型 | Y | 主要 `adc_cfg`；order、label。 |
| `ES8156` | DAC | Y | N | 部分新模型 | N | 主要 `pa_cfg`；DAC 与 PA 控制。 |
| `ES8374` | ADC + DAC + Line | Y | Y | 旧模型 | N | 旧式 flat `codec_cfg` 与接口填表。 |
| `ES8388` | ADC + DAC codec | Y | Y | 部分新模型 | Y | `sys_cfg`、`pa_cfg`；ALC、Line、order。 |
| `ES8389` | ADC + DAC codec | Y | Y | 完整新模型 | Y | `sys_cfg`、`adc_cfg`、`dac_cfg`、`pa_cfg`；order、label、多实例引用计数。 |
| `AW88298` | Smart amplifier | Y | N | 部分新模型 | N | `pa_cfg`、`reset_cfg`；作为 DAC path。 |
| `TAS5805M` | Class-D amplifier | Y | N | 部分新模型 | N | `sys_cfg`、`pa_cfg`、`reset_cfg`。 |
| `ZL38063` | DSP / voice processor | Y | N | 部分新模型 | N | `pa_cfg`、`reset_cfg`；含固件与 Twolf API。 |
| `CJC8910` | ADC + DAC codec | Y | Y | 部分新模型 | N | 主要 `pa_cfg`；ADC 侧能力有限。 |
| `dummy` | 虚拟 codec / PA stub | Y | N | 部分新模型 | N | PA-only 扬声器设计。 |
| `UAC` | USB Audio Class | Y | Y | UAC 专用模型 | Y | 能力由 USB alt setting 枚举。 |
| `template_codec` | 驱动模板 | Y | Y | 完整新模型示例 | N | 新增 codec 驱动参考。 |

### 能力矩阵

| Codec | ADC ops | DAC ops | PA 控制 | HW proc | `get_caps` | `get_order_list` | ADC label | 多实例复用 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `ES8311` | Y | Y | Y | ALC/DRC/EQ/Mute | Y | Y | Y | Y |
| `ES7210` | Y | N | N | ALC/Mute | N | Y | Y | N |
| `ES7243` | Y | N | N | N | N | N | Y | N |
| `ES7243E` | Y | N | N | N | N | Y | Y | N |
| `ES8156` | N | Y | Y | N | N | N | N | N |
| `ES8374` | 旧式 | 旧式 | 旧式 | N | N | N | N | N |
| `ES8388` | Y | Y | Y | ALC/Line | N | Y | N | N |
| `ES8389` | Y | Y | Y | N | N | Y | Y | Y |
| `AW88298` | N | Y | 芯片内/复位 | N | N | N | N | N |
| `TAS5805M` | N | Y | Y | N | N | N | N | N |
| `ZL38063` | N | Y | Y | N | N | N | N | N |
| `CJC8910` | 仅增益 | Y | 部分 | N | N | N | N | N |
| `dummy` | N | Y | Y | N | N | N | N | N |
| `UAC` | Y | Y | N | N | Y | 不支持 | N | N |
| `template_codec` | Y | Y | Y | N | N | Y | N | N |

### 数据通路实测状态

| 数据通路 | 说明 | 硬件实测 |
| --- | --- | :---: |
| I2S STD / TDM | `audio_codec_new_i2s_data()`，随已实测 codec 板级用例覆盖 | Y |
| I2S PDM TX | PDM 扬声器输出（如 ESP32-C3-Lyra） | Y |
| I2S PDM RX | PDM 麦克风输入 | N |
| 片上 ADC | `audio_codec_new_adc_data()` | Y |
| USB UAC | `esp_codec_dev_uac_*` 管理器 | Y |

## 11. 读写路径上的 layout 与位宽转换

当内存 layout 或位宽与硬件不一致时，`esp_codec_dev_read()` / `esp_codec_dev_write()` 在内部按帧做通道重排、位宽转换或缺失通道补零；应用无需也不应直接调用内部转换例程。通道顺序可用十六进制 digit 表示，例如 `0x1324` 表示 ch1、ch3、ch2、ch4。规则见 [data_layout/data_layout_logic.md](data_layout/data_layout_logic.md)。

## 12. 配置模型与默认接口工厂

v2.0 将各 codec 公共配置收敛到分组子结构，便于与 `audio_codec_new()` 及芯片专用 `*_codec_new()` 共用同一套字段语义。`audio_hw_*_cfg_t` 定义在 `interface/audio_codec_hw_cfg.h`；工厂袋 `audio_codec_cfg_t` 与 `audio_codec_new()` 定义在 `esp_codec_dev_defaults.h`；`audio_codec_if.h` 仅保留运行时接口与 `audio_codec_delete_codec_if()`。

| 结构 | 作用 |
| --- | --- |
| `audio_hw_sys_cfg_t` | codec 主从模式、是否不使用 MCLK。 |
| `audio_hw_adc_cfg_t` | ADC / microphone 配置，含 digital mic 与 ADC channel label。 |
| `audio_hw_dac_cfg_t` | DAC reference / loopback 相关配置。 |
| `audio_hw_pa_cfg_t` | PA GPIO、有效电平、硬件增益。 |
| `audio_hw_reset_cfg_t` | reset GPIO 与有效电平。 |
| `audio_codec_cfg_t` | 组合配置：`ctrl_if`、`gpio_if` 与上述子配置。 |

| API | 功能 |
| --- | --- |
| `audio_codec_new()` | 按名称创建 codec interface（覆盖部分 Kconfig 启用的驱动；UAC 走 UAC 管理器）。配置须为 `audio_codec_cfg_t` 且 `cfg_size == sizeof(audio_codec_cfg_t)`。 |
| `audio_codec_new_i2c_ctrl()` | 创建 I2C 控制接口。 |
| `audio_codec_new_spi_ctrl()` | 创建 SPI 控制接口。 |
| `audio_codec_new_i2s_data()` | 创建 I2S 数据接口。 |
| `audio_codec_new_adc_data()` | 创建内部 ADC 数据接口。 |
| `esp_codec_dev_uac_install()` / `esp_codec_dev_uac_new_dev()` | 安装 UAC 管理器并派生句柄。 |
| `audio_codec_new_gpio()` | 创建 GPIO 操作接口。 |

部分 codec 仍主要通过 `es8311_codec_new()` 等专用构造函数创建；迁移与字段对照见 [api_migration_guide.md](api_migration_guide.md)。

## 13. 当前支持范围与限制

1. 各 codec 的配置迁移进度不同，部分驱动仍保留旧式 `codec_cfg`。
2. `ES8374` 仍使用旧式 flat `audio_codec_if_t` 填表。
3. `ES8311` 的 `dac_cfg` 已结构化，部分 DAC reference 模式仅区分启用与禁用。
4. `ES7210` 的 `adc_cfg.digital_mic` 字段当前驱动未消费。
5. `get_caps` 主要由 ES8311 与 UAC 实现；其他 codec 返回 `ESP_CODEC_DEV_NOT_SUPPORT`。
6. data layout 依赖 codec 实现 `get_order_list`，以及 data_if 实现 `get_order` / `get_channel_mask`；label API 另外依赖 codec 实现 `get_adc_label`。
7. 硬件音频处理实际覆盖集中在 ES8311、ES7210、ES8388。
8. `audio_codec_new()` 工厂未覆盖全部 codec。
9. v2.0 硬件实测已覆盖 ES7210、ES7243、ES7243E、ES8311、ES8388、ES8389，以及片上 ADC、USB UAC、I2S PDM TX；其余 codec 与 PDM RX 有待实机验证（见第 10 节）。

更详细的 API 与配置迁移说明见 [api_migration_guide.md](api_migration_guide.md)。

## 相关文档

- [api_migration_guide.md](api_migration_guide.md)：API 与 `codec_cfg` 迁移说明。
- [codec_datasheets.md](codec_datasheets.md)：各芯片厂商在线文档入口。
- [FAQ.md](FAQ.md)：常见问题与排查建议。
- [data_layout/data_layout_logic.md](data_layout/data_layout_logic.md)：data layout 设置逻辑。
- [data_layout/memory_data_layout.md](data_layout/memory_data_layout.md)：内存通道布局说明。
- [data_layout/adc_label.md](data_layout/adc_label.md)：硬件原理图 label 与内存 label 的关系。
- [i2s_driver/esp32_i2s_driver.md](i2s_driver/esp32_i2s_driver.md)：ESP32 I2S 基础行为说明。
- [i2s_driver/i2s_compatibility_guide.md](i2s_driver/i2s_compatibility_guide.md)：I2S v6.0 行为与旧版本差异。
- [i2s_driver/i2s_tdm_std_mixmode_guide.md](i2s_driver/i2s_tdm_std_mixmode_guide.md)：I2S TDM/STD 混用说明。
