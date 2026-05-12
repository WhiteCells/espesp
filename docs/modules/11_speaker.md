# 11 speaker: I2S 扬声器输出

## 学什么

- `i2s_new_channel()` 创建 I2S TX 通道。
- `i2s_channel_init_std_mode()` 配置 16-bit 单声道输出。
- `i2s_channel_write()` 持续写入音频样本。
- ESP32 通常通过 I2S 数字功放驱动扬声器。

## 怎么运行

```text
idf.py menuconfig
  -> Case2 ESP Learning
  -> Module selector
  -> 11 speaker
```

可调参数：

```text
Case2 ESP Learning
  -> Speaker module
```

## 看哪段代码

- `main/speaker/speaker.c`

## 接口介绍

- `speaker_run()`：持续输出正弦波样本。
- 常用接口：`i2s_new_channel()`、`i2s_channel_enable()`、`i2s_channel_write()`。

## 接线

默认配置：

- BCLK GPIO10 -> 功放 BCLK
- WS GPIO11 -> 功放 LRC/WS
- DOUT GPIO12 -> 功放 DIN
- 3V3/5V -> 功放 VCC，按模块规格选择
- GND -> 功放 GND

## 注意事项

- GPIO 不能直接驱动大功率喇叭，需要 I2S 功放模块。
- 初次测试建议使用较低音量和合适功率的扬声器。
- 接线错误通常表现为无声，不一定有软件错误日志。
