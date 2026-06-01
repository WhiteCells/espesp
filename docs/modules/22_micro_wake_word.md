# 22 micro_wake_word: 本地 microWakeWord 唤醒词检测

## 模块概览

- ESP32-S3 从 I2S MEMS 麦克风读取 16 kHz 音频。
- 固件把 32-bit I2S slot 转成 16-bit mono PCM。
- microWakeWord 在本地运行量化 TFLite 模型，不依赖 Wi-Fi。
- 默认内置 `Hey Jarvis` 示例模型，方便验证整条链路。

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> micro_wake_word
```

确认 PSRAM：

```text
Component config
  -> ESP PSRAM
  -> Support for external, SPI-connected RAM
```

配置麦克风引脚：

```text
ESPESP Menu
  -> Microphone module
```

构建、烧录并监视：

```sh
idf.py build flash monitor
```

看到 `microWakeWord listening` 后，说出默认模型的英文唤醒词：

```text
Hey Jarvis
```

不要按中文“嘿贾维斯”逐字念，尽量用英文发音，类似 `hey JAR-vis`。

检测成功会打印：

```text
micro wake word detected: count=..., label=...
```

## 自定义唤醒词

microWakeWord 的自定义不是在固件里直接填文字，而是先训练一个兼容的量化 TFLite
模型，再把模型编译进固件。

常见替换步骤：

- 把训练好的 `.tflite` 转成 C 数组头文件。
- 替换 `main/micro_wake_word/hey_jarvis_model.h` 里的 `espesp_micro_wake_word_model` 数组。
- 在 `microWakeWord module` 里调整 label、cutoff、sliding window 和 tensor arena。

## 排错

- 启动时报 PSRAM 未启用：进入 `Component config -> ESP PSRAM` 打开 external SPI RAM。
- 启动时报 `Failed to resize buffer`：`TFLite tensor arena size` 太小，默认已提高到
  `40960`；自定义模型可以继续加到 `49152` 或 `65536`。
- 一直检测不到：先看 `avg_abs/peak` 是否有变化；没有变化就切换 I2S slot 或检查接线。
- `avg_abs/peak` 有变化但不触发：降低 `Detection probability cutoff`，例如 `0.90`、
  `0.85`；同时贴近麦克风，用英文发音说 `Hey Jarvis`。
- 音量太小：把 `Microphone sample right shift bits` 从 `10` 调到 `9`；如果 `peak`
  经常接近 `32767`，说明过大，调回 `11`。
- 误触发多：提高 `Detection probability cutoff`，例如从 0.97 调到 0.98。
- 自定义模型启动失败：增大 `TFLite tensor arena size`，并确认模型输入输出符合 microWakeWord streaming 模型格式。
