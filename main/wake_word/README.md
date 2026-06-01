# wake_word

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> wake_word: local WakeNet keyword detection
```

需要配置：

```text
ESPESP Menu
  -> Microphone module
ESPESP Menu
  -> Wake word module
```

默认参数适合 ESP32-S3 N16R8：16 MB flash、8 MB PSRAM、I2S MEMS 麦克风。
执行：

```sh
idf.py build flash monitor
```

串口看到 `WakeNet listening` 后，对着麦克风说模型对应的唤醒词。检测到后会打印
`wake word detected`。

## 自定义指定唤醒词

WakeNet 不能在固件运行时把任意文字直接变成唤醒词；它只能运行已经训练好并烧进
`model` 分区的 WakeNet 模型。

本示例支持两种指定方式：

- `WakeNet model name or substring`：直接填模型名或模型名的一段，例如
  `wn9_hilexin`。
- `Wake keyword hint`：按关键词提示匹配模型名；当模型文件名包含这个提示时会选中。

如果要用自己的唤醒词，需要先按 ESP-SR/WakeNet 流程训练模型，把模型加入 ESP-SR
模型选择并烧到 `model` 分区，然后在上面的配置项里指定它的模型名。

## 代码结构

- `wake_word.c`：初始化 ESP-SR AFE/WakeNet，创建 I2S RX，持续读取麦克风并喂给 AFE。
- `wake_word.h`：模块入口声明。

## 配置项

- `CONFIG_ESPESP_MIC_BCLK_GPIO`：I2S 麦克风 BCLK。
- `CONFIG_ESPESP_MIC_WS_GPIO`：I2S 麦克风 WS/LRCLK。
- `CONFIG_ESPESP_MIC_DIN_GPIO`：I2S 麦克风 DATA 输入。
- `CONFIG_ESPESP_WAKE_WORD_MODEL`：WakeNet 模型名或模型名片段。
- `CONFIG_ESPESP_WAKE_WORD_KEYWORD_HINT`：模型名关键词提示。
- `CONFIG_ESPESP_WAKE_WORD_MIC_SLOT_LEFT` / `CONFIG_ESPESP_WAKE_WORD_MIC_SLOT_RIGHT`：麦克风所在 I2S slot。
- `CONFIG_ESPESP_WAKE_WORD_SAMPLE_SHIFT_BITS`：32-bit I2S 样本转 16-bit PCM 的右移位数。
- `CONFIG_ESPESP_WAKE_WORD_STATUS_EVERY_CHUNKS`：每隔多少个 AFE chunk 打印一次监听状态。

## 注意事项

- WakeNet 输入固定为 16 kHz、16-bit、mono，本模块会用 16 kHz 初始化 I2S。
- ESP-SR 模型需要 `model` 分区；项目根目录的 `partitions_16mb_sr.csv` 已包含这个分区。
- N16R8 可以跑这个示例；如果换成无 PSRAM 或小 flash 的板子，需要重新评估模型和分区大小。
- 如果一直没有检测，先运行 `microphone` 或 `pcm_stream`，确认麦克风有声音且 slot 选对。
- 如果日志里的 `avg_abs` 长期接近 0，检查接线、供电、GND、L/R 选择脚，或切换 I2S slot。
