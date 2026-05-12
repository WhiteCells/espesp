# microphone

## 模块接口

### `esp_err_t microphone_run(void)`

创建 I2S RX 通道，读取 I2S MEMS 麦克风数据，打印每帧样本数、平均绝对幅度和峰值。

参数：无。BCLK、WS、DIN 和采样率来自 Kconfig。

返回值：

- 正常情况下不会返回。
- I2S 初始化、使能或读取失败时返回对应 `esp_err_t`。

## 内部接口

### `static esp_err_t microphone_create_channel(i2s_chan_handle_t *rx_channel)`

创建并初始化 I2S RX 通道。

参数：

- `rx_channel`：输出参数，成功后写入 I2S RX 通道句柄。

返回值：

- `ESP_OK`：通道创建并初始化成功。
- 其他错误：I2S 通道创建或标准模式初始化失败。

## 使用的 ESP-IDF 接口和结构体

### `i2s_chan_config_t`

I2S 通道基础配置。本模块通过 `I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER)` 生成。

关键字段：

- `id`：I2S 控制器编号。`I2S_NUM_AUTO` 表示自动选择可用控制器。
- `role`：I2S 角色。本模块使用 `I2S_ROLE_MASTER`，由 ESP 输出 BCLK/WS。
- `dma_desc_num`：DMA 描述符数量，默认宏里已给出常用值。
- `dma_frame_num`：每个 DMA buffer 的 frame 数，影响中断频率和延迟。

### `i2s_new_channel(const i2s_chan_config_t *chan_cfg, i2s_chan_handle_t *ret_tx_handle, i2s_chan_handle_t *ret_rx_handle)`

创建 I2S 通道。

参数：

- `chan_cfg`：通道配置。
- `ret_tx_handle`：输出 TX 句柄；本模块不发送，传 `NULL`。
- `ret_rx_handle`：输出 RX 句柄；本模块传 `rx_channel`。

### `i2s_std_config_t`

标准 I2S 模式配置，包含 clock、slot 和 GPIO 三部分。

本模块字段：

- `clk_cfg`：由 `I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_CASE2_MIC_SAMPLE_RATE_HZ)` 生成。
- `slot_cfg`：由 `I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO)` 生成。
- `gpio_cfg`：BCLK、WS、DIN 引脚配置。

### `i2s_std_clk_config_t`

I2S clock 配置。

关键字段：

- `sample_rate_hz`：采样率，本模块来自 `CONFIG_CASE2_MIC_SAMPLE_RATE_HZ`。
- `clk_src`：时钟源，默认宏使用 `I2S_CLK_SRC_DEFAULT`。
- `mclk_multiple`：MCLK 与采样率倍数，默认宏通常使用 256。
- `bclk_div`：BCLK 分频，默认宏提供常用值。

### `i2s_std_slot_config_t`

I2S slot 配置。

本模块设置：

- `data_bit_width = I2S_DATA_BIT_WIDTH_32BIT`：每个样本按 32-bit 读入。
- `slot_mode = I2S_SLOT_MODE_MONO`：单声道。
- `slot_mask`：默认宏选择合适的 slot。
- `bit_shift`：Philips I2S 通常需要 1-bit shift，默认宏已处理。

说明：很多 I2S MEMS 麦克风输出 24-bit 有效数据，常放在 32-bit slot 中读取，所以代码里对样本右移 8 bit 后统计幅度。

### `i2s_std_gpio_config_t`

I2S GPIO 配置。

本模块字段：

- `mclk = I2S_GPIO_UNUSED`：不输出 MCLK。
- `bclk = CONFIG_CASE2_MIC_BCLK_GPIO`：bit clock。
- `ws = CONFIG_CASE2_MIC_WS_GPIO`：word select，也叫 LRCLK。
- `dout = I2S_GPIO_UNUSED`：不发送数据。
- `din = CONFIG_CASE2_MIC_DIN_GPIO`：麦克风数据输入。
- `invert_flags`：时钟反相配置，本模块全部为 `false`。

### `i2s_channel_read(handle, dest, size, bytes_read, timeout_ms)`

读取 I2S RX 数据。

参数：

- `handle`：RX 通道句柄。
- `dest`：接收缓冲区，本模块是 `int32_t samples[256]`。
- `size`：缓冲区字节数，本模块是 `sizeof(samples)`。
- `bytes_read`：实际读取字节数输出。
- `timeout_ms`：超时，单位 ms，本模块为 `1000`。

## 可配置项

- `CONFIG_CASE2_MIC_BCLK_GPIO`：麦克风 BCLK。
- `CONFIG_CASE2_MIC_WS_GPIO`：麦克风 WS/LRCLK。
- `CONFIG_CASE2_MIC_DIN_GPIO`：麦克风 DATA 输入。
- `CONFIG_CASE2_MIC_SAMPLE_RATE_HZ`：采样率。

## 注意事项

- INMP441、SPH0645 等模块的 L/R 选择脚会影响数据所在声道。
- 如果长期读到 0，优先检查供电、GND、BCLK、WS、DATA 和 L/R。
- 本模块只做幅度统计，不做 WAV 编码、降噪或语音识别。
