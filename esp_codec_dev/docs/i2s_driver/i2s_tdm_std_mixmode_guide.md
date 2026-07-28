# ESP-IDF I2S 驱动 TDM 与 STD 模式混用指南

> 本文档说明在全双工模式下混用 TDM 和 I2S_STD 模式的可行性、配置要求及注意事项

---

## 1. 应用场景

| 通道 | 模式 | 配置 |
|------|------|------|
| TX | TDM | 4 通道，16bit，16KHz |
| RX | I2S_STD | 2 通道，16bit，16KHz |

---

## 2. 全双工使用方式

使用 `i2s_new_channel()` **同时创建 TX 和 RX**，两个通道会在同一 I2S 端口上组成全双工使用方式：

```c
i2s_chan_handle_t tx_handle, rx_handle;
i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

// 同时传入 tx 和 rx handle，组成同一端口的全双工通道
i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle);
```

**关键点**：全双工关系在通道创建时确定，与后续使用 TDM 还是 STD 初始化**无关**。在 ESP-IDF v6.0 及以上版本中，先初始化的通道保持 Master 角色，后初始化的通道作为 Slave 使用。

---

## 3. 混用配置一致性要求

全双工模式下，TX 和 RX 共享 **BCLK** 和 **WS** 信号，因此必须满足以下一致性要求：

### 3.1 必须一致的参数

| 参数 | 要求 | 原因 |
|------|------|------|
| `sample_rate_hz` | **必须相同** | WS 信号频率 = 采样率 |
| `mclk_multiple` | **必须相同** | MCLK 频率一致，BCLK 分频正确 |
| `clk_src` | **必须相同** | 时钟源一致 |
| `total_slot × slot_bit_width` | **必须相同** | BCLK 频率匹配 |
| `ws_pol` | **必须相同** | 左右声道对齐 |
| `bit_shift` | **必须相同** | 数据位对齐 |
| `ws_width` | **推荐相同** | 帧边界正确识别 |

### 3.2 可以不同的参数

| 参数 | 说明 |
|------|------|
| `data_bit_width` | 各通道独立处理有效数据位宽 |
| `slot_mask` | 各通道选择不同的 slot |

---

## 4. BCLK 匹配方案

### 4.1 BCLK 计算公式

```
BCLK = sample_rate × total_slot × slot_bit_width
```

### 4.2 配置匹配示例

| 模式 | total_slot | slot_bit_width | data_bit_width | BCLK 计算 |
|------|------------|----------------|----------------|-----------|
| TDM 4ch | 4 | 16bit | 16bit | 16000 × 4 × 16 = **1,024,000 Hz** |
| STD 2ch | 2 | **32bit** | 16bit | 16000 × 2 × 32 = **1,024,000 Hz** |

**技巧**：STD 使用 32bit slot 宽度，但 `data_bit_width` 仍为 16bit，硬件自动处理数据对齐。

### 4.3 帧结构对比

```
TDM 4ch, 16bit slot (每帧 64 BCLK):
+--------+--------+--------+--------+
| SLOT0  | SLOT1  | SLOT2  | SLOT3  |
| 16bit  | 16bit  | 16bit  | 16bit  |
+--------+--------+--------+--------+
|<------------- 64 bits ----------->|

STD 2ch, 32bit slot (每帧 64 BCLK):
+----------------+----------------+
|      LEFT      |     RIGHT      |
| 16bit + 16pad  | 16bit + 16pad  |
+----------------+----------------+
|<------------- 64 bits ----------->|
```

---

## 5. MCLK 配置要求

### 5.1 MCLK 计算

```
MCLK = sample_rate × mclk_multiple
```

示例（16KHz 采样率）：
| mclk_multiple | MCLK 频率 |
|---------------|-----------|
| 256 | 16000 × 256 = 4,096,000 Hz |
| 384 | 16000 × 384 = 6,144,000 Hz |

### 5.2 为什么 MCLK 必须一致？

1. **MCLK 输出唯一**：只能输出一个频率给外部 codec
2. **BCLK 分频依赖 MCLK**：
   ```
   bclk_div = mclk / bclk
   ```
3. **时钟树一致性**：确保 TX/RX 时钟同源同频

### 5.3 配置示例

```c
// TX (TDM)
.clk_cfg = {
    .sample_rate_hz = 16000,
    .clk_src = I2S_CLK_SRC_DEFAULT,          // 必须一致
    .mclk_multiple = I2S_MCLK_MULTIPLE_256,  // 必须一致
}

// RX (STD)
.clk_cfg = {
    .sample_rate_hz = 16000,
    .clk_src = I2S_CLK_SRC_DEFAULT,          // 必须一致
    .mclk_multiple = I2S_MCLK_MULTIPLE_256,  // 必须一致
}
```

---

## 6. 短帧与长帧兼容性

### 6.1 帧格式定义

| 帧格式 | ws_width | 典型应用 | WS 信号特点 |
|--------|----------|----------|-------------|
| 短帧 (Short Frame) | 1 | TDM 默认 | WS 仅在帧开始有 1 BCLK 脉冲 |
| 长帧 (Long Frame) | slot_bit_width | I2S_STD 默认 | WS 高低电平持续整个 slot |

### 6.2 时序对比

```
TDM 短帧 (ws_width = 1):
WS:   _|‾|_______|‾|_______|‾|_______|‾|_______
BCLK: _|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_
      ↑SLOT0   ↑SLOT1   ↑SLOT2   ↑SLOT3

I2S_STD 长帧 (ws_width = 32):
WS:   ________________________________|‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
BCLK: _|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_
      |<-------- LEFT (32bit) ------->|<-------- RIGHT (32bit) ------>|
```

### 6.3 混用兼容性结论

#### 场景 1：TDM 发送 + I2S_STD 接收

| TDM TX 帧格式 | I2S_STD RX 帧格式 | 兼容性 | 说明 |
|---------------|-------------------|--------|------|
| TDM 长帧 | I2S_STD 长帧 | 正常 | `ws_width` 一致 |
| TDM 短帧 | I2S_STD 长帧 | 异常 | RX 无法正确区分左右声道 |
| TDM 长帧 | I2S_STD 短帧 | 可能异常 | 不推荐 |
| TDM 短帧 | I2S_STD 短帧 | 非标准 | I2S_STD 通常不使用短帧 |

#### 场景 2：I2S_STD 发送 + TDM 接收

| I2S_STD TX 帧格式 | TDM RX 帧格式 | 兼容性 | 说明 |
|-------------------|---------------|--------|------|
| I2S_STD 长帧 | TDM 长帧 | 正常 | `ws_width` 一致 |
| I2S_STD 长帧 | TDM 短帧 | 可能异常 | RX 可能误判帧边界 |
| I2S_STD 短帧 | TDM 长帧 | 异常 | 不推荐 |
| I2S_STD 短帧 | TDM 短帧 | 非标准 | I2S_STD 通常不使用短帧 |

### 6.4 问题说明

**TDM 短帧 + I2S_STD 长帧混用时**：

```
TDM 产生的短帧 WS:
WS:   _|‾|_______________________________|‾|___
      ↑帧开始                             ↑下一帧
      (WS 大部分时间为 0)

I2S_STD 期望的长帧 WS:
WS:   ________________|‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾|___
      |<-- LEFT -->|<-- RIGHT -->|
      (WS=0 左声道, WS=1 右声道)
```

**问题**：I2S_STD 依赖 WS 电平区分左右声道，但 TDM 短帧时 WS 大部分为 0，导致 I2S_STD 认为一直在接收左声道。

### 6.5 推荐配置：统一使用长帧

**TDM 和 I2S_STD 混用时，双方都配置为长帧**：

```c
// TDM 配置（无论 TX 或 RX）
.slot_cfg = {
    .total_slot = 4,
    .slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT,
    .ws_width = 32,   // 长帧：帧长/2 = 64/2 = 32
    // ...
}

// I2S_STD 配置（无论 TX 或 RX）
.slot_cfg = {
    .slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT,
    .ws_width = 32,   // 长帧：与 TDM 一致
    // ...
}
```

### 6.6 兼容性总结

| 配置方案 | TDM | I2S_STD | 结果 |
|----------|-----|---------|------|
| 推荐 | `ws_width = 32` | `ws_width = 32` | 完全兼容 |
| 避免 | `ws_width = 1` | `ws_width = 32` | 声道错乱 |
| 避免 | `ws_width = 32` | `ws_width = 1` | 帧同步异常 |

---

## 7. WS 信号其他配置

### 7.1 ws_pol（极性）

**必须一致**，否则左右声道反转：

```
ws_pol = false（默认）:
WS ________|‾‾‾‾‾‾‾‾|________
           ↑ 右声道开始

ws_pol = true:
WS ‾‾‾‾‾‾‾‾|________|‾‾‾‾‾‾‾‾
           ↑ 右声道开始
```

### 7.2 bit_shift

**必须一致**，否则数据错位 1 bit：

```
bit_shift = true (Philips I2S 标准):
WS   _____|‾‾‾‾‾‾‾‾‾
DATA -----X D15 | D14 | D13 ...
          ↑ 数据延迟 1 BCLK

bit_shift = false (MSB-Justified):
WS   _____|‾‾‾‾‾‾‾‾‾
DATA -----| D15 | D14 | D13 ...
          ↑ 数据立即开始
```

---

## 8. 完整配置示例

### 8.1 TDM TX + STD RX 混用

```c
#include "driver/i2s_tdm.h"
#include "driver/i2s_std.h"

static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;

esp_err_t init_i2s_mixed_mode(void)
{
    // ========================================
    // 1. 同时创建 TX 和 RX（自动触发全双工）
    // ========================================
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle));
    
    // ========================================
    // 2. TX 配置：TDM 4 通道
    // ========================================
    // BCLK = 16000 × 4 × 16 = 1,024,000 Hz
    i2s_tdm_config_t tx_tdm_cfg = {
        .clk_cfg = {
            .sample_rate_hz = 16000,          // 必须与 RX 一致
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3,
            .total_slot = 4,
            .ws_width = 32,                   // 帧长/2，兼容 STD
            .ws_pol = false,                  // 必须与 RX 一致
            .bit_shift = true,                // 必须与 RX 一致
        },
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,
            .bclk = GPIO_NUM_4,
            .ws = GPIO_NUM_5,
            .dout = GPIO_NUM_18,
            .din = GPIO_NUM_NC,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    // ========================================
    // 3. RX 配置：STD 2 通道，32bit slot
    // ========================================
    // BCLK = 16000 × 2 × 32 = 1,024,000 Hz（与 TX 匹配）
    i2s_std_config_t rx_std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = 16000,          // 必须与 TX 一致
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,   // 实际数据 16bit
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT,   // 关键：32bit slot
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .ws_width = 32,                   // 必须与 TX 一致
            .ws_pol = false,                  // 必须与 TX 一致
            .bit_shift = true,                // 必须与 TX 一致
        },
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,
            .bclk = GPIO_NUM_4,               // 与 TX 共享
            .ws = GPIO_NUM_5,                 // 与 TX 共享
            .dout = GPIO_NUM_NC,
            .din = GPIO_NUM_19,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    // ========================================
    // 4. 初始化通道（TX 先初始化保持 Master）
    // ========================================
    ESP_ERROR_CHECK(i2s_channel_init_tdm_mode(tx_handle, &tx_tdm_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &rx_std_cfg));
    
    // ========================================
    // 5. 启用通道
    // ========================================
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
    
    return ESP_OK;
}
```

---

## 9. 数据读写

### 9.1 TX 写入（TDM 4 通道）

```c
void tx_task(void *arg)
{
    // 每帧 4 个 16bit 采样
    int16_t tx_buffer[4 * 128];  // 128 帧
    size_t bytes_written;
    
    while (1) {
        // 填充 4 通道数据：[CH0][CH1][CH2][CH3][CH0][CH1]...
        for (int frame = 0; frame < 128; frame++) {
            tx_buffer[frame * 4 + 0] = /* CH0 data */;
            tx_buffer[frame * 4 + 1] = /* CH1 data */;
            tx_buffer[frame * 4 + 2] = /* CH2 data */;
            tx_buffer[frame * 4 + 3] = /* CH3 data */;
        }
        
        i2s_channel_write(tx_handle, tx_buffer, sizeof(tx_buffer), 
                          &bytes_written, portMAX_DELAY);
    }
}
```

### 9.2 RX 读取（STD 2 通道）

```c
void rx_task(void *arg)
{
    // 虽然 slot 是 32bit，但 data_bit_width=16bit
    // DMA 自动提取有效 16bit 数据
    int16_t rx_buffer[2 * 128];  // 128 帧，每帧 L+R
    size_t bytes_read;
    
    while (1) {
        esp_err_t ret = i2s_channel_read(rx_handle, rx_buffer, sizeof(rx_buffer), 
                                          &bytes_read, portMAX_DELAY);
        if (ret == ESP_OK) {
            // 数据格式：[L0][R0][L1][R1]...
            for (int i = 0; i < bytes_read / sizeof(int16_t); i += 2) {
                int16_t left = rx_buffer[i];
                int16_t right = rx_buffer[i + 1];
                // 处理音频数据...
            }
        }
    }
}
```

---

## 10. 配置一致性检查清单

| 检查项 | TX (TDM) | RX (STD) | 状态 |
|--------|----------|----------|------|
| `sample_rate_hz` | 16000 | 16000 | 一致 |
| `clk_src` | DEFAULT | DEFAULT | 一致 |
| `mclk_multiple` | 256 | 256 | 一致 |
| BCLK 系数 | 4 × 16 = 64 | 2 × 32 = 64 | 一致 |
| `ws_width` | 32 | 32 | 一致 |
| `ws_pol` | false | false | 一致 |
| `bit_shift` | true | true | 一致 |
| `data_bit_width` | 16bit | 16bit | （可不同）|

---

## 11. 常见问题

### Q1: 为什么可以混用 TDM 和 STD？

TX 和 RX 在 `i2s_new_channel()` 时已经归属同一 I2S 端口，后续可分别使用 TDM 或 STD 初始化。只要 BCLK 和 WS 配置一致，硬件就能正常工作。

### Q2: STD 32bit slot 接收 16bit 数据如何处理？

硬件根据 `data_bit_width` 自动提取有效数据。DMA 读取时返回的是连续的 16bit 采样，无需手动处理 padding。

### Q3: 初始化顺序有影响吗？

有。**先初始化的通道保持 Master 角色**，后初始化的通道作为 Slave 使用。

### Q4: 可以反过来 STD TX + TDM RX 吗？

可以，只需确保 BCLK 和 WS 配置一致即可。

---

## 12. 总结

### 混用配置四要素

1. **同时创建通道**：自动触发全双工
2. **时钟配置一致**：`sample_rate_hz`、`clk_src`、`mclk_multiple` 相同
3. **BCLK 必须匹配**：调整 `slot_bit_width` 实现
4. **WS 配置一致**：`ws_pol`、`bit_shift`、`ws_width` 相同

### 推荐配置

```
TDM 4ch × 16bit slot = STD 2ch × 32bit slot
         ↓                      ↓
    BCLK = 64 × Fs         BCLK = 64 × Fs
```
