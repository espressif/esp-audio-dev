# `esp_codec_dev` API 与 `codec_cfg` 变更说明

本文档说明从旧版 `esp_codec_dev`（以 v1.6.x / ADF 组件树为代表）迁移到 v2.0 时需要关注的 API、配置结构和使用方式变化，聚焦两类内容：

- 对外可见 API/头文件变更
- `codec_cfg` 定义与使用语义变更

说明：

- 本文只覆盖公开头文件、公开构造参数以及会影响接入方式的行为变化
- 文中的“旧版”指 v2.0 之前的 `esp_codec_dev` 接口模型（典型为组件版本 1.6.x），“v2.0”指当前发布文档对应的接口模型

---

## 1. 总体结论

本轮改动不是单点增补，而是一次接口模型升级，核心变化有五个：

1. `esp_codec_dev` 新增了“data layout / data order”能力，允许查询和设置内存中的通道排列。
2. codec 驱动接口从“单个大接口”拆成了 `hw_base + adc/dac 子操作`，为输入/输出分离控制、order 查询和后续硬件音频处理扩展铺路。
3. `codec_cfg` 已统一迁到“基础子配置组合”模型；各 codec 按硬件能力选用 `sys/adc/dac/pa/reset` 等子集。部分 codec 尚未完成充分测试与验证。
4. 若干配置项从“构造时静态决定”改成“打开流时由 `fs` 决定”，最典型的是 `mclk_div` 和 `ES7210` 的 MIC 选择。
5. I2C 控制路径强制使用 `i2c_master` + `bus_handle`；若干公开 API 发生重命名；工作模式枚举 `ESP_CODEC_DEV_WORK_MODE_*` 已删除。

---

## 2. API 变更

### 2.1 `esp_codec_dev.h` 新增 data layout 相关 API

相对旧版，v2.0 **新增** 4 个公开 layout 接口：

- `esp_codec_dev_set_data_layout()`
- `esp_codec_dev_get_data_layout()`
- `esp_codec_dev_set_data_layout_label()`
- `esp_codec_dev_get_data_layout_label()`

以及能力查询接口：

- `esp_codec_dev_get_caps()`

对应能力：

- 允许上层显式声明期望的内存通道顺序
- 允许查询当前配置下的 layout（open 前从 data interface 查询当前格式与模式，open 后含 `set_data_layout` 生效结果）
- 允许按 ADC label 设置和查询通道顺序
- 允许在 open 前查询设备支持的采样格式组合

`channel/channel_mask/i2s_mode` 到 layout 的反查属于内部实现细节，由 `esp_codec_dev.c` 内部 helper 使用。

**详细术语、三层架构（L1/L2/L3）与布局 API**见 [`data_layout/data_layout_logic.md`](./data_layout/data_layout_logic.md)，板级 Codec/DMA 顺序见 [`data_layout/memory_data_layout.md`](./data_layout/memory_data_layout.md)。

迁移影响：

- 旧版只支持“按 codec + data_if 固定顺序工作”
- v2.0 开始把“总线槽位顺序”和“内存布局顺序”作为显式能力暴露
- 如果旧代码默认假设 `read/write` 的通道排列恒定不变，需要重新确认和 `set_data_layout()` 的关系
- 若启用了软件 layout 转换，`read/write` 的 `len` 须按整帧（通道数 × 每采样字节数）对齐

### 2.2 公开 API 重命名与删除

| 旧版 | v2.0 | 说明 |
| --- | --- | --- |
| `esp_codec_set_disable_when_closed()` | `esp_codec_dev_set_disable_when_closed()` | 函数名补齐 `dev` 前缀 |
| `esp_codec_dev_col_calc_hw_gain()` | `esp_codec_dev_vol_calc_hw_gain()` | 修正拼写（`col` → `vol`） |
| `ESP_CODEC_DEV_WORK_MODE_*` / `esp_codec_dec_work_mode_t` | 已删除 | 方向改由 `esp_codec_dev_cfg_t.dev_type` 与运行时 enable/open 决定 |
| `include/esp_codec_adc.h` | `include/impl/esp_codec_adc_data.h` | 头文件改名；通过 `esp_codec_dev_defaults.h` 间接包含时为 `esp_codec_adc_data.h` |
| `include/esp_codec_dev_os.h` | `include/impl/esp_codec_dev_os.h` | 移至 impl，默认不作为应用侧公开路径 |

### 2.3 `esp_codec_dev_types.h` 扩展了 layout/channel map 类型

相对旧版（1.6.x），v2.0 **新增** 以下类型：

- `esp_codec_dev_i2s_mode_t`
- `esp_codec_dev_channel_map_t`（内存位置 → slot/通道 ID）
- `esp_codec_dev_device_map_info_t`
- `esp_codec_dev_data_map_info_t`
- `esp_codec_dev_caps_mode_t` / `esp_codec_dev_capability_t`

请使用 `ESP_CODEC_DEV_CHANNEL_MAP(...)` 构造 channel map，并通过 `.value` 读写打包值。

其他说明：

- 旧版 `esp_codec_dev_sample_info_t` 主要描述 `bits/channel/channel_mask/sample_rate/mclk_multiple`
- v2.0 仍让 `esp_codec_dev_sample_info_t` 只描述流格式；I2S mode 由 data interface 从底层 I2S channel 查询
- channel map 计算依赖 `esp_codec_dev_i2s_mode_t`，但该 mode 属于 `audio_codec_data_if_t.get_mode()` 提供的内部总线状态，不需要上层在 `fs` 中传入

### 2.4 `audio_hw_base.h` 为新增公开基础接口

新增公开基础接口 `interface/audio_hw_base.h`，主要引入：

- `audio_hw_base_t`
- `audio_hw_base_t.get_order_list`
- `audio_hw_base_t.get_adc_label`
- `audio_hw_base_t.get_caps`

`get_order_list` 返回 `esp_codec_dev_device_map_info_t **`（`mode` / `channels` / `map`）。

这意味着：

- “通道映射”不再是 `esp_codec_dev.c` 内部私有概念，而是已经进入公开接口层
- codec 驱动现在可以通过 `get_order_list` 主动声明其支持的 `channel + mode -> map` 组合

### 2.5 `audio_codec_if.h` 接口模型重构

旧版 `audio_codec_if_t` 是单个扁平结构，直接提供：

- `open/is_open/enable/set_fs`
- `mute/set_vol`
- `set_mic_gain/set_mic_channel_gain/mute_mic`
- `set_reg/get_reg/dump_reg/close`

v2.0 `audio_codec_if_t` 改为组合式结构：

- `hw_base`
- `ctrl_if`
- `adc_if`
- `dac_if`
- `hw_proc`（可选，硬件音频处理工厂入口）

`audio_codec_if.h` 仅声明运行时接口与 `audio_codec_delete_codec_if()`。`audio_codec_new()` 与工厂配置 `audio_codec_cfg_t` 位于 `esp_codec_dev_defaults.h`。共享硬件子配置 `audio_hw_*_cfg_t` 位于 `interface/audio_codec_hw_cfg.h`。ADC/DAC 使能、静音、音量等 helper 属于组件内部封装，应用通常通过 `esp_codec_dev_*` API 间接使用。

迁移影响：

- 对 codec 驱动作者来说，这属于接口断层式变化，旧驱动不能直接复用旧版 `audio_codec_if_t` 填表方式
- 对 `esp_codec_dev` 上层使用者来说，若只通过 `esp_codec_dev_*` API 操作，影响相对较小；若直接操作底层 codec interface，则需要按新模型适配
- 若代码仅 `#include "audio_codec_if.h"` 后使用 `audio_codec_cfg_t` / `audio_codec_new()`，需改为包含 `esp_codec_dev_defaults.h`；仅使用 `audio_hw_*_cfg_t` 时包含 `audio_codec_hw_cfg.h`（芯片头会间接包含）

### 2.6 `audio_codec_data_if.h` 新增 order/mode/fmt 查询钩子

相对旧版，新增回调：

- `get_mode`
- `get_fmt`
- `get_order`
- `get_channel_mask`

目的：

- 让 data interface 报告输入/输出 I2S mode
- 让 data interface 报告当前已打开的流格式（`get_fmt`）
- 让 data interface 根据 `channel` / `channel_mask` 计算内存位置→slot map，并支持从 map 反解 channel mask

这是 `esp_codec_dev_set_data_layout()`、`esp_codec_dev_get_data_layout()` 以及 label 相关接口能工作的关键前提之一。

### 2.7 `audio_codec_ctrl_if.h`：`get_info` 字段变更

旧版（1.6.x）已具备：

- `audio_codec_ctrl_type_t`
- `audio_codec_ctrl_info_t`
- `audio_codec_ctrl_if_t.get_info`

相对旧版，v2.0 的关键变化是 `audio_codec_ctrl_info_t.i2c` 字段：

| 旧版 | v2.0 |
| --- | --- |
| `uint8_t port` | `void *bus_handle`（I2C master bus handle） |

当前直接受益的驱动是 `ES8311`：

- 实现通过 `get_info` 获取 I2C 地址 / bus handle 或 SPI CS 信息
- 然后做 reference count，允许多个实例复用同一物理 codec 控制设备而不重复关电/重开

迁移影响：

- 自定义 `ctrl_if` 若仍上报 `port` 语义，需改为上报 `bus_handle`
- 如果自定义 `ctrl_if` 没有实现 `get_info`，多数场景仍可工作
- 但对于依赖设备复用识别的逻辑，`get_info` 缺失会削弱新机制的效果

### 2.8 I2C / 平台配置 breaking 变更

`audio_codec_i2c_cfg_t`：

| 字段 | 旧版 | v2.0 |
| --- | --- | --- |
| `port` | 有 | **已删除** |
| `bus_handle` | 可选（IDF ≥ 5.3） | **必填** |
| `clock_speed_hz` | `int` | `uint32_t` |

其他平台变化：

- `CONFIG_CODEC_I2C_BACKWARD_COMPATIBLE` 已删除；不再支持 legacy `driver/i2c.h` 路径，始终使用 `i2c_master`
- `audio_codec_gpio_if_t` 新增可选 `add_isr` / `remove_isr`
- 依赖要求见组件 `idf_component.yml`（当前为 IDF `>=5.4.4`，并排除部分 5.5.x）；请以该文件为准

### 2.9 新增硬件音频处理相关公开头文件

v2.0 新增以下公开接口头：

- `include/hw_proc/audio_hw_alc.h`（含 `audio_alc_cfg_t`）
- `include/hw_proc/audio_hw_drc.h`（含 `audio_drc_cfg_t`）
- `include/hw_proc/audio_hw_eq.h`（含 `audio_eq_cfg_t`）
- `include/hw_proc/audio_hw_line.h`
- `include/hw_proc/audio_hw_mute.h`（含 `auto_mute_cfg_t`、`soft_mute_cfg_t`）
- `interface/audio_codec_hw_cfg.h`（共享 `audio_hw_*_cfg_t` 子配置）

同时新增统一硬件音频处理构造入口，例如：

- `audio_hw_alc_new()`
- `audio_hw_mute_new()`
- `audio_hw_drc_new()`
- `audio_hw_eq_new()`
- `audio_hw_line_new()`

可选能力：启用 `CONFIG_CODEC_UAC_SUPPORT` 后可通过 `include/impl/esp_codec_dev_uac.h` 使用 USB UAC 设备接口。

这部分属于“新增能力”，不是对旧 API 的兼容替换，但它会影响 v2.0 的推荐使用方式：硬件音频处理开始被纳入统一框架，应用不再直接依赖各驱动私有 factory 接口。

### 2.10 `esp_codec_adc_data.h` 的路径与编译门槛

头文件：

- 旧版：`include/esp_codec_adc.h`
- v2.0：`include/impl/esp_codec_adc_data.h`（defaults 中 `#include "esp_codec_adc_data.h"`）

旧版条件：

- `ESP_IDF_VERSION >= 5.0.0 && SOC_ADC_SUPPORTED`

v2.0 条件：

- `SOC_ADC_SUPPORTED`
- `CONFIG_CODEC_DATA_ADC_SUPPORT`

迁移影响：

- 对外工厂 API 名称没变（仍是 `audio_codec_new_adc_data()` 一类入口）
- 需更新 include 路径/文件名
- 头文件可见性不再显式卡在 IDF 5.0 及以上；实际编译仍需启用 `CONFIG_CODEC_DATA_ADC_SUPPORT`

---

## 3. `codec_cfg` 变更

### 3.1 整体趋势

v2.0 开始把 codec 配置收敛为几类公共子配置（定义在 `interface/audio_codec_hw_cfg.h`）：

- `audio_hw_sys_cfg_t`
- `audio_hw_adc_cfg_t`
- `audio_hw_pa_cfg_t`
- `audio_hw_dac_cfg_t`
- `audio_hw_reset_cfg_t`

工厂入口使用的聚合配置 `audio_codec_cfg_t` 与 `audio_codec_new()` 位于 `esp_codec_dev_defaults.h`。

这样做的直接结果是：

- 公共字段不再在每个 codec 的 `codec_cfg` 里重复平铺
- 一部分“构造期硬编码选项”转移到 `esp_codec_dev_open()` 的 `fs` 参数决定
- 各 codec 的配置结构迁移已完成；差异主要体现在“按芯片能力选用哪些子配置”，以及部分驱动的测试覆盖程度

### 3.2 `ES8311` 的 `codec_cfg` 已切到新模型

### 旧版 `es8311_codec_cfg_t`

旧版主要字段：

- `ctrl_if`
- `gpio_if`
- `codec_mode`
- `pa_pin`
- `pa_reverted`
- `master_mode`
- `use_mclk`
- `digital_mic`
- `invert_mclk`
- `invert_sclk`
- `hw_gain`
- `no_dac_ref`
- `mclk_div`

### v2.0 `es8311_codec_cfg_t`

v2.0 字段：

- `ctrl_if`
- `gpio_if`
- `sys_cfg`
- `adc_cfg`
- `dac_cfg`
- `pa_cfg`

### 字段映射关系

| 旧字段 | 新字段 | 说明 |
| --- | --- | --- |
| `master_mode` | `sys_cfg.is_master` | 语义基本等价 |
| `use_mclk` | `sys_cfg.no_mclk = !use_mclk` | 命名从正向改成反向 |
| `digital_mic` | `adc_cfg.digital_mic` | 语义等价 |
| `pa_pin` | `pa_cfg.pa_pin` | 迁入 PA 子配置 |
| `pa_reverted` | `pa_cfg.pa_active_low` | 语义等价，但命名更清晰 |
| `hw_gain` | `pa_cfg.hw_gain` | 迁入 PA 子配置 |
| `no_dac_ref` | `dac_cfg.ref_enable = !no_dac_ref` | 从“禁用 DAC reference”的布尔值改为 DAC 配置结构中的启用标志 |
| `mclk_div` | `esp_codec_dev_sample_info_t.mclk_multiple` | 从 codec 构造参数迁到 open 时的流参数 |

### 被移除且当前无直接替代的字段

- `codec_mode`
- `invert_mclk`
- `invert_sclk`

需要注意：

- `codec_mode` 移除后，不再通过 `es8311_codec_cfg_t` 指定 ADC/DAC/BOTH，v2.0 模型更偏向让 `esp_codec_dev_cfg_t.dev_type` 和运行时 enable/open 流程决定使用方向
- `invert_mclk` / `invert_sclk` 在 v2.0 `es8311.c` 中没有等价配置入口，旧代码若依赖这两个反相开关，需要单独评估是否还能通过寄存器直写补偿

### 语义变化

1. `mclk_div` 的决定时机变化  
旧版由 `codec_cfg.mclk_div` 决定；v2.0 改为 `open()` 时从 `fs->mclk_multiple` 读取，未配置时走默认值。

2. `dac_cfg` 从单一布尔语义升级为结构化配置
v2.0 使用 `audio_hw_dac_cfg_t` 描述 DAC reference / loopback 相关设置，包含 `ref_enable`、`ref_dac_ch` 和 `real_adc_data_ch`。`ES8311` 当前主要使用 `ref_enable` 区分是否启用内部参考路径。

3. 支持同一物理 codec 的多实例复用  
v2.0 `ES8311` 借助 `ctrl_if->get_info()` 做 reference count，同地址设备不会重复做硬件 open/close。

### 3.3 `ES7210` 的 `codec_cfg` 已切到新模型

### 旧版 `es7210_codec_cfg_t`

旧版主要字段：

- `ctrl_if`
- `master_mode`
- `mic_selected`
- `mclk_src`
- `mclk_div`

### v2.0 `es7210_codec_cfg_t`

v2.0 字段：

- `ctrl_if`
- `sys_cfg`
- `adc_cfg`

### 字段映射关系

| 旧字段 | 新字段 | 说明 |
| --- | --- | --- |
| `master_mode` | `sys_cfg.is_master` | 语义基本等价 |
| `mclk_div` | `esp_codec_dev_sample_info_t.mclk_multiple` | 从构造参数迁到流参数 |
| `mic_selected` | `esp_codec_dev_sample_info_t.channel_mask` | 从静态 MIC 选择改为流打开时选择通道 |
| `mclk_src` | 无 | v2.0 实现固定使用 PAD 作为内部 MCLK 来源 |

### 语义变化

1. `ES7210` v2.0 实现固定走 TDM 路径  
打开时直接写 `ES7210_SDP_INTERFACE2_REG12 = 0x02`，并输出 `Enable TDM mode` 日志，不再根据选中 MIC 数量动态切换 STD/TDM。

2. MIC 选择从构造时静态配置改成 `fs->channel_mask`  
v2.0 实现会在 `set_fs()` 中读取 `channel_mask`，并在后续 start/enable 路径应用到硬件：
   - `channel_mask == 0` 时默认选 MIC1 + MIC2
   - `channel == 4` 时强制使用 `0x0F`

3. `mclk_div` 迁移到 `fs->mclk_multiple`  
旧版是 codec 固定配置；v2.0 按每次 open/set_fs 的流参数决定。

4. 新增了 `adc_cfg`，但 v2.0 实现目前主要验证 `adc_cfg.label`，尚未测试 `adc_cfg.digital_mic`。

### 3.4 其他 codec 的 `codec_cfg` 与验证状态

各 codec 的 `codec_cfg` 均已迁到公共子配置模型。因芯片能力不同，实际携带的子配置子集不同，例如：

- `ES8389`、`ES8374`：`sys_cfg`、`adc_cfg`、`dac_cfg`、`pa_cfg`
- `ES8388`：`sys_cfg`、`pa_cfg`
- `ES7243`、`ES7243E`：`adc_cfg`
- `ES8156`、`CJC8910`：`pa_cfg`
- `TAS5805M`、`AW88298`、`ZL38063`：`pa_cfg`、`reset_cfg` 等

这不代表配置迁移未完成，而是按硬件能力选用对应子配置。相对充分验证较多集中在 `ES8311`、`ES7210` 等常用组合；其余 codec 配置接口已就绪，但部分场景仍需进一步测试确认。

额外注意：

- `CJC8910` 旧版的 `codec_mode`、`invert_lr`、`invert_sclk` 在 v2.0 中已移除，当前无公开等价配置入口
- 各 codec 旧版平铺的 `pa_pin` / `pa_reverted` / `hw_gain` / `reset_pin` 均已迁到 `pa_cfg` / `reset_cfg`

---

## 4. 迁移建议

### 4.1 上层只使用 `esp_codec_dev_*` API 的项目

建议重点检查：

- 是否需要显式使用 `esp_codec_dev_set_data_layout()`
- `mclk_multiple` 是否替代了原先写在 `codec_cfg` 里的 `mclk_div`
- 是否调用了已重命名的 `esp_codec_set_disable_when_closed` / `esp_codec_dev_col_calc_hw_gain`
- I2C 是否已改为传入 `bus_handle`（不再使用 `port`）
- 自定义 data interface 是否实现了 `get_mode()` / `get_fmt()` / `get_order()`，以便 layout/order 能从底层总线获取实际状态

### 4.2 直接实例化 codec 驱动的项目

建议分两类迁移：

1. 若使用 `ES8311`

- 把 `master_mode/use_mclk/digital_mic/no_dac_ref/pa_* / hw_gain` 迁到 `sys_cfg/adc_cfg/dac_cfg/pa_cfg`
- 删除对 `codec_mode/invert_mclk/invert_sclk/mclk_div` 的旧写法
- 把时钟倍率放到 `esp_codec_dev_open()` 传入的 `fs->mclk_multiple`

2. 若使用 `ES7210`

- 把 `master_mode` 迁到 `sys_cfg.is_master`
- 删除 `mic_selected/mclk_src/mclk_div`
- 在 `esp_codec_dev_open()` 的 `fs` 中通过 `channel_mask` 和 `mclk_multiple` 控制实际采集通道与时钟倍率

3. 若使用 `CJC8910` 或其他已迁到子配置的驱动

- 将旧版平铺的 `pa_*` / `hw_gain` / `reset_*` 迁到对应子配置
- 删除 `codec_mode` 以及已无替代的 `invert_*` 字段

### 4.3 若自定义 codec/data/control interface

建议补齐以下接口，避免被当前框架能力绕过：

- `audio_codec_ctrl_if_t.get_info`（且 I2C 信息使用 `bus_handle`）
- `audio_codec_data_if_t.get_mode`
- `audio_codec_data_if_t.get_fmt`
- `audio_codec_data_if_t.get_order`
- `audio_codec_data_if_t.get_channel_mask`
- `audio_hw_base_t.get_order_list`

如果这些接口不实现：

- data layout/order 能力会受限
- 多实例复用识别能力会受限
- 部分基于新接口的自动推导逻辑会退化

---

## 5. 简化迁移示例

### 5.1 `ES8311` 配置迁移

旧写法：

```c
es8311_codec_cfg_t cfg = {
    .ctrl_if = ctrl_if,
    .gpio_if = gpio_if,
    .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
    .pa_pin = GPIO_NUM_10,
    .pa_reverted = false,
    .master_mode = false,
    .use_mclk = true,
    .digital_mic = false,
    .hw_gain = gain,
    .no_dac_ref = true,
    .mclk_div = 256,
};
```

v2.0 写法：

```c
es8311_codec_cfg_t cfg = {
    .ctrl_if = ctrl_if,
    .gpio_if = gpio_if,
    .sys_cfg = {
        .is_master = false,
        .no_mclk = false,
    },
    .adc_cfg = {
        .digital_mic = false,
    },
    .dac_cfg = {
        .ref_enable = false,
    },
    .pa_cfg = {
        .pa_pin = GPIO_NUM_10,
        .pa_active_low = false,
        .hw_gain = gain,
    },
};

esp_codec_dev_sample_info_t fs = {
    .bits_per_sample = 16,
    .channel = 2,
    .sample_rate = 16000,
    .mclk_multiple = 256,
};
```

### 5.2 `ES7210` 配置迁移

旧写法：

```c
es7210_codec_cfg_t cfg = {
    .ctrl_if = ctrl_if,
    .master_mode = false,
    .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2,
    .mclk_src = ES7210_MCLK_FROM_PAD,
    .mclk_div = 256,
};
```

v2.0 写法：

```c
es7210_codec_cfg_t cfg = {
    .ctrl_if = ctrl_if,
    .sys_cfg = {
        .is_master = false,
        .no_mclk = false,
    },
    .adc_cfg = {
        .digital_mic = false,
    },
};

esp_codec_dev_sample_info_t fs = {
    .bits_per_sample = 16,
    .channel = 2,
    .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) |
                    ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1),
    .sample_rate = 16000,
    .mclk_multiple = 256,
};
```

### 5.3 I2C 控制接口迁移

旧写法：

```c
audio_codec_i2c_cfg_t i2c_cfg = {
    .port = I2C_NUM_0,
    .addr = ES8311_CODEC_DEFAULT_ADDR,
};
```

v2.0 写法：

```c
audio_codec_i2c_cfg_t i2c_cfg = {
    .bus_handle = i2c_bus_handle,  /* from i2c_new_master_bus() */
    .addr = ES8311_CODEC_DEFAULT_ADDR,
};
```

---

## 6. 已知限制

1. `ES8311` 的 `dac_cfg` 已结构化，但 v2.0 主要使用 `ref_enable` 区分是否启用内部参考路径。
2. `ES8311` 移除了 `invert_mclk/invert_sclk` 的公开配置入口，旧项目若依赖这些能力，需要额外确认。
3. `CJC8910` 移除了 `invert_lr/invert_sclk/codec_mode` 的公开配置入口。
4. `ES7210` 不再通过 `mic_selected` 决定模式，v2.0 实现固定启用 TDM；旧项目若依赖“少 MIC 时自动走非 TDM”逻辑，需要重新验证。
5. `ES7210` v2.0 未消费 `adc_cfg.digital_mic`，结构体升级不代表功能已经完全接通。
6. 各 codec 的配置结构迁移已完成；部分 codec 的功能回归与板级验证覆盖仍有限，接入前建议按目标芯片补测。
