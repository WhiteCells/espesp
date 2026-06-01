# micro_wake_word

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> micro_wake_word: local microWakeWord keyword detection
```

还需要确认：

```text
Component config
  -> ESP PSRAM
ESPESP Menu
  -> Microphone module
ESPESP Menu
  -> microWakeWord module
```

默认示例模型来自 `micro_wake_word_standalone` 的 `Hey Jarvis` TFLite 模型。
串口看到 `microWakeWord listening` 后，对麦克风说英文发音的 `Hey Jarvis`
（类似 `hey JAR-vis`）。

## 自定义模型

microWakeWord 使用量化后的 TFLite 模型。训练自己的模型后，把模型转成 C 数组头
文件，并替换 `hey_jarvis_model.h` 里的 `espesp_micro_wake_word_model` 数组内容即可。

需要同步调整：

- `CONFIG_ESPESP_MICRO_WAKE_WORD_LABEL`
- `CONFIG_ESPESP_MICRO_WAKE_WORD_PROBABILITY_CUTOFF`
- `CONFIG_ESPESP_MICRO_WAKE_WORD_SLIDING_WINDOW_SIZE`
- `CONFIG_ESPESP_MICRO_WAKE_WORD_TENSOR_ARENA_SIZE`

## 注意事项

- 输入固定为 16 kHz、16-bit、mono PCM。
- ESP32-S3 N16R8 可以跑；必须启用 PSRAM。
- 如果启动时报 `Failed to resize buffer`，说明 `TFLite tensor arena size`
  太小。当前默认值是 `40960`，自定义模型可继续加到 `49152` 或 `65536`。
- 如果日志里的 `avg_abs` 接近 0，先切换 I2S slot 或检查麦克风接线。
- 如果 `avg_abs/peak` 有变化但不触发，降低 probability cutoff 到 `0.90` 或
  `0.85`，并贴近麦克风用英文发音说 `Hey Jarvis`。
- 如果误触发多，提高 probability cutoff；如果漏检多，降低 cutoff 或调整音量。
