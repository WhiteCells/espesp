# 10 microphone: I2S 麦克风采样

## 学什么

- `i2s_new_channel()` 创建 I2S RX 通道。
- `i2s_channel_init_std_mode()` 配置标准 I2S 模式。
- `i2s_channel_read()` 从 DMA 缓冲读取音频帧。
- I2S 麦克风常见信号是 BCLK、WS/LRCLK、DATA。

## 怎么运行

```text
idf.py menuconfig
  -> Case2 ESP Learning
  -> Module selector
  -> 10 microphone
```

可调参数：

```text
Case2 ESP Learning
  -> Microphone module
```

## 看哪段代码

- `main/microphone/microphone.c`

## 接口介绍

- `microphone_run()`：读取 I2S 麦克风数据并打印平均幅度和峰值。
- 常用接口：`i2s_new_channel()`、`i2s_channel_enable()`、`i2s_channel_read()`。

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
