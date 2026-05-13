# 10 microphone: I2S 麦克风采样

## 模块概览

- `i2s_new_channel()` 创建 I2S RX 通道。
- `i2s_channel_init_std_mode()` 配置标准 I2S 模式。
- `i2s_channel_read()` 从 DMA 缓冲读取音频帧。
- I2S 麦克风常见信号是 BCLK、WS/LRCLK、DATA。

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> 10 microphone
```

可调参数：

```text
ESPESP Menu
  -> Microphone module
```

## 源码位置

- `main/microphone/microphone.c`

## 当前模块接口参考

- `microphone_run()`：读取 I2S 麦克风数据并打印平均幅度和峰值。
- `microphone_create_channel()`：创建并配置 I2S RX 标准模式通道。

## 常用接口说明

- `i2s_chan_config_t`：配置 I2S 控制器角色、DMA 描述符数量和每个描述符帧数。
- `i2s_new_channel()`：创建 I2S RX/TX 通道，本模块只创建 RX。
- `i2s_std_config_t`：配置标准 I2S 的时钟、slot 和 GPIO。
- `i2s_channel_init_std_mode()`：把标准 I2S 配置应用到通道。
- `i2s_channel_enable()`：启用 I2S 通道开始收数。
- `i2s_channel_read()`：从 I2S DMA 缓冲读取音频帧。
- `i2s_channel_disable()`、`i2s_del_channel()`：停止并释放通道，长期项目清理资源时使用。

## 配置项

- `CONFIG_ESPESP_MIC_BCLK_GPIO`：I2S 麦克风 BCLK GPIO。
- `CONFIG_ESPESP_MIC_WS_GPIO`：I2S 麦克风 WS/LRCLK GPIO。
- `CONFIG_ESPESP_MIC_DIN_GPIO`：I2S 麦克风数据输入 GPIO。
- `CONFIG_ESPESP_MIC_SAMPLE_RATE_HZ`：采样率，单位 Hz。

## 接线

默认配置：

- BCLK GPIO4 -> 麦克风 SCK/BCLK
- WS GPIO5 -> 麦克风 WS/LRCLK
- DIN GPIO6 -> 麦克风 SD/DOUT
- 3V3 -> 麦克风 VCC
- GND -> 麦克风 GND

## 注意事项

- INMP441、SPH0645 等模块的左右声道选择脚会影响输出 slot。
- 读数长期为 0 时，先检查供电、GND、WS/BCLK 和 L/R 选择。
- 本模块只做幅度统计，不保存音频文件。
