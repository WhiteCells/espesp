# 11 speaker: I2S 扬声器输出

## 模块概览

- `i2s_new_channel()` 创建 I2S TX 通道。
- `i2s_channel_init_std_mode()` 配置 16-bit 单声道输出。
- `i2s_channel_write()` 持续写入音频样本。
- ESP32 通常通过 I2S 数字功放驱动扬声器。

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> 11 speaker
```

可调参数：

```text
ESPESP Menu
  -> Speaker module
```

## 源码位置

- `main/speaker/speaker.c`

## 当前模块接口参考

- `speaker_run()`：持续输出正弦波样本。
- `speaker_create_channel()`：创建并配置 I2S TX 标准模式通道。
- `fill_sine_tone()`：填充一段连续正弦波 PCM 样本。

## 常用接口说明

- `i2s_chan_config_t`：配置 I2S 控制器角色、DMA 描述符数量和每个描述符帧数。
- `i2s_new_channel()`：创建 I2S TX/RX 通道，本模块只创建 TX。
- `i2s_std_config_t`：配置标准 I2S 的时钟、slot 和 GPIO。
- `i2s_channel_init_std_mode()`：把标准 I2S 配置应用到通道。
- `i2s_channel_enable()`：启用 I2S 通道开始输出。
- `i2s_channel_write()`：向 I2S DMA 缓冲写 PCM 样本。
- `sinf()`：生成正弦波样本，真实音频播放通常改成从文件或网络流读取 PCM。

## 配置项

- `CONFIG_ESPESP_SPK_BCLK_GPIO`：I2S 功放 BCLK GPIO。
- `CONFIG_ESPESP_SPK_WS_GPIO`：I2S 功放 WS/LRCLK GPIO。
- `CONFIG_ESPESP_SPK_DOUT_GPIO`：I2S 音频数据输出 GPIO。
- `CONFIG_ESPESP_SPK_SAMPLE_RATE_HZ`：输出采样率，单位 Hz。
- `CONFIG_ESPESP_SPK_TONE_HZ`：测试正弦波频率，单位 Hz。

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
