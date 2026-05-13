# speaker

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> speaker: play I2S sine tone
```

可调参数：

```text
ESPESP Menu
  -> Speaker module
```

然后执行：

```sh
idf.py build flash monitor
```

## 当前模块已有接口

### `esp_err_t speaker_run(void)`

创建 I2S TX 通道，持续生成 16-bit 单声道正弦波并写入 I2S 数字功放。

参数：无。BCLK、WS、DOUT、采样率和音调频率来自 Kconfig。

返回值：

- 正常情况下不会返回。
- I2S 初始化、使能或写入失败时返回对应 `esp_err_t`。

## 内部接口

### `static esp_err_t speaker_create_channel(i2s_chan_handle_t *tx_channel)`

创建并初始化 I2S TX 通道。

参数：

- `tx_channel`：输出参数，成功后写入 I2S TX 通道句柄。

返回值：

- `ESP_OK`：创建成功。
- 其他错误：I2S 通道创建或初始化失败。

### `static void fill_sine_tone(int16_t *buffer, size_t sample_count, uint32_t *phase)`

填充一段正弦波 PCM 样本。

参数：

- `buffer`：输出样本缓冲区，类型为 `int16_t`。
- `sample_count`：要生成的样本个数。
- `phase`：相位计数器指针，函数会更新它以保证连续波形。

## 常用接口说明

### `i2s_chan_config_t`

由 `I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER)` 生成。

关键字段：

- `id`：I2S 控制器，自动选择。
- `role`：主机模式，由 ESP 输出 BCLK/WS。
- `dma_desc_num`、`dma_frame_num`：DMA 缓冲相关参数，影响延迟和稳定性。

### `i2s_new_channel(chan_cfg, ret_tx_handle, ret_rx_handle)`

创建 I2S TX 通道。

参数：

- `chan_cfg`：通道基础配置。
- `ret_tx_handle`：输出 TX 句柄，本模块传 `tx_channel`。
- `ret_rx_handle`：输出 RX 句柄，本模块不接收，传 `NULL`。

### `i2s_std_config_t`

标准 I2S 配置。

本模块字段：

- `clk_cfg`：采样率配置，来自 `CONFIG_ESPESP_SPK_SAMPLE_RATE_HZ`。
- `slot_cfg`：16-bit 单声道 Philips I2S。
- `gpio_cfg`：BCLK、WS、DOUT 引脚。

### `i2s_std_slot_config_t`

slot 配置由 `I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)` 生成。

关键含义：

- `data_bit_width = I2S_DATA_BIT_WIDTH_16BIT`：每个 PCM 样本 16 bit。
- `slot_mode = I2S_SLOT_MODE_MONO`：单声道输出。
- `slot_mask`：默认宏选择输出 slot。
- `ws_width`、`bit_shift`：Philips I2S 时序参数，默认宏处理。

### `i2s_std_gpio_config_t`

字段说明：

- `mclk = I2S_GPIO_UNUSED`：多数 MAX98357A 类模块不需要 MCLK。
- `bclk = CONFIG_ESPESP_SPK_BCLK_GPIO`：bit clock。
- `ws = CONFIG_ESPESP_SPK_WS_GPIO`：word select/LRCLK。
- `dout = CONFIG_ESPESP_SPK_DOUT_GPIO`：音频数据输出到功放 DIN。
- `din = I2S_GPIO_UNUSED`：不接收数据。
- `invert_flags`：本模块不反相。

### `i2s_channel_write(handle, src, size, bytes_written, timeout_ms)`

写入 I2S TX 数据。

参数：

- `handle`：TX 通道句柄。
- `src`：PCM 缓冲区，本模块是 `int16_t samples[256]`。
- `size`：写入字节数。
- `bytes_written`：实际写入字节数输出。
- `timeout_ms`：写入超时，本模块为 `1000`。

## 可配置项

- `CONFIG_ESPESP_SPK_BCLK_GPIO`：BCLK 引脚。
- `CONFIG_ESPESP_SPK_WS_GPIO`：WS/LRCLK 引脚。
- `CONFIG_ESPESP_SPK_DOUT_GPIO`：DATA 输出引脚。
- `CONFIG_ESPESP_SPK_SAMPLE_RATE_HZ`：采样率。
- `CONFIG_ESPESP_SPK_TONE_HZ`：正弦波频率。

## 注意事项

- ESP32 GPIO 不能直接驱动喇叭，需要 I2S 功放模块。
- 初次测试建议降低音量，避免突然大声。
- 无声时重点检查 BCLK、WS、DOUT、功放使能脚、供电和 GND。
