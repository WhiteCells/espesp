# 23 vad: 本地语音活动检测

## 模块概览

- ESP32-S3 从 I2S MEMS 麦克风读取 16 kHz 音频。
- 固件把 32-bit I2S slot 转成 16-bit mono PCM。
- ESP-SR WebRTC VAD 在本地判断当前帧是否有人声，不依赖 Wi-Fi。
- 串口日志输出语音开始、语音结束和每秒统计。

VAD 只判断“像不像人在说话”，不识别说话内容，也不等同于唤醒词检测。

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> vad
```

配置麦克风引脚：

```text
ESPESP Menu
  -> Microphone module
```

配置 VAD 参数：

```text
ESPESP Menu
  -> VAD module
```

构建、烧录并监视：

```sh
idf.py build flash monitor
```

看到 `VAD listening` 后，对着麦克风说话。检测到语音段会打印：

```text
voice activity started: segment=...
voice activity ended: duration_ms=...
```

## 参数调试

- `VAD aggressiveness`：数字越高越保守。环境噪声误触发多时调高，轻声漏检时调低。
- `VAD frame length`：ESP-SR VAD 支持 10、20、30 ms。默认 20 ms 是延迟和稳定性的折中。
- `Minimum speech duration`：进入 speech 状态前需要持续多少毫秒语音。调大可减少短噪声误触发。
- `Minimum silence duration`：回到 silence 状态前需要持续多少毫秒静音。调大可避免句尾过早结束。
- `VAD microphone I2S slot`：选择麦克风输出所在的 left/right slot。
- `Microphone sample right shift bits`：32-bit I2S 样本转 16-bit PCM 的右移位数。数值越小越响，数值越大越安静。

## 源码位置

- `main/vad/vad.c`
- `main/vad/vad.h`

## 当前模块接口参考

- `vad_run()`：初始化 ESP-SR VAD、创建 I2S RX，持续读取麦克风并打印语音活动状态。
- `vad_create_rx_channel()`：创建并配置 16 kHz I2S RX 标准模式通道。
- `vad_convert_sample()`：把 32-bit I2S 样本缩放并限幅成 16-bit PCM。

## 常用接口说明

- `vad_create_with_param()`：创建 VAD 实例，并设置模式、采样率、帧长、最短语音和最短静音时长。
- `vad_process_with_trigger()`：输入一帧 16-bit PCM，返回经过触发器平滑后的 `VAD_SPEECH` 或 `VAD_SILENCE`。
- `vad_destroy()`：释放 VAD 实例。
- `i2s_channel_read()`：从 I2S DMA 缓冲读取麦克风样本。

## 排错

- 一直是 `silence`：先运行 `microphone` 或 `pcm_stream`，确认麦克风有声音且 I2S slot 选对。
- `avg_abs/peak` 长期接近 0：检查供电、GND、BCLK、WS/LRCLK、DATA 和麦克风 L/R 选择脚。
- 误触发多：把 `VAD aggressiveness` 调到 3 或 4，或增大 `Minimum speech duration`。
- 轻声漏检：把 `VAD aggressiveness` 调低到 1 或 0，或把右移位数从 12 调到 11。
- 句尾被切掉：增大 `Minimum silence duration`，例如从 500 ms 调到 800 ms。
