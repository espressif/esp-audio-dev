# ADC label 映射说明

本文说明原理图上的 ADC 通道 `label` 如何映射到逻辑声道，以及在不同采集模式下，最终在内存中的顺序如何变化。

更完整的规则说明见 [`data_layout_logic.md`](./data_layout_logic.md) 和 [`memory_data_layout.md`](./memory_data_layout.md)。

## 1. 术语

| 名称 | 含义 |
|------|------|
| 原理图 label | 板级原理图或板级文档中对某一路 ADC 通道的语义命名，例如 `FL`、`FR`、`RE`。 |
| 逻辑声道 | 文档中统一使用的 `ch1`、`ch2`、`ch3`... 编号。 |
| 内存顺序 | `esp_codec_dev_read()` 后 buffer 中实际出现的声道排列。 |
| `NA` | Not Available / Not Enabled，表示该位置未启用或未接入有效通道。 |

原理图 label 只描述板级语义，不直接决定采集后 buffer 的顺序。最终顺序由 `codec` 输出顺序、`I2S` 选中的 slot，以及 DMA / 位宽打包方式共同决定。

## 2. label 定义

| Label | 含义 |
|------|------|
| `FC` | Front Center |
| `RE` | Reference |
| `FL` / `FR` | Front Left / Front Right |
| `SL` / `SR` | Side Left / Side Right |
| `BL` / `BR` | Back Left / Back Right |
| `NA` | Not Available / Not Enabled |

## 3. 映射规则

1. 原理图上的 label 先定义“这一通道代表什么”。
2. `codec` 芯片的输出顺序决定逻辑声道的时间顺序。
3. `I2S` 的 `channel`、`channel_mask` 和模式决定哪些 slot 会被采集进内存。
4. 如果是特殊位宽或特殊 DMA 组织方式，最终 buffer 顺序还会再经过一层内存适配。

因此，同一张原理图在不同采集配置下，内存里的 label 顺序可能不同，但板级语义不变。

## 4. 示例：esp32_s3_korvo2_v3

### 4.1 原理图与板级 label

该板上启用的通道为 `MIC1`、`MIC2`、`MIC3`，其中 `MIC3` 采集 DAC 输出信号。

板级 label 约定为：

`FL,FR,RE,NA`

含义分别是：

- `FL`：前左
- `FR`：前右
- `RE`：参考通道
- `NA`：未启用通道

### 4.2 TDM 4 通道采集

当 `ES7210` 使用 TDM 模式采集 4 通道数据时，codec 在总线上的实际顺序为：

`MIC1 -> MIC3 -> MIC2 -> MIC4`

对应到板级 label，内存中的顺序为：

`FL,RE,FR,NA`

这说明：

- 板级语义仍然是 `FL/FR/RE/NA`
- 但采集后的 buffer 顺序已经由总线和 codec 输出顺序决定

### 4.3 32bit 2ch 读取 16bit 4ch

ESP32 第一代 I2S 硬件（ESP32 和 ESP32S2）不具备 TDM 模式，因此当 STD 模式下使用 `32bit 2ch` 承载 `16bit 4ch` 数据时，内存中的典型顺序为：

`MIC3 -> MIC1 -> MIC4 -> MIC2`

对应到板级 label，内存中的顺序为：

`RE,FL,NA,FR`

这类场景下，label 的板级定义不变，但 buffer 顺序会因为 I2S / DMA 的组织方式而变化。其他目标芯片或 TDM 4ch×16bit 配置应以实际 I2S 模式和 [memory_data_layout.md](./memory_data_layout.md) 中的 L1/L2/L3 分析为准。

## 5. 使用建议

- label 应与原理图和板级文档保持一致，不要为了适配 buffer 顺序随意改名。
- 如果某一路没有实际接入，建议使用 `NA`。
- 需要判断最终 buffer 顺序时，请结合 [`data_layout_logic.md`](./data_layout_logic.md) 和 [`memory_data_layout.md`](./memory_data_layout.md) 一起阅读。
