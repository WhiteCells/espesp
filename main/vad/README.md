# vad

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> vad: local voice activity detection
```

还需要确认：

```text
ESPESP Menu
  -> Microphone module
ESPESP Menu
  -> VAD module
```

执行：

```sh
idf.py build flash monitor
```

串口看到 `VAD listening` 后，对着麦克风说话。模块会在语音状态开始和结束时打印
`voice activity started` / `voice activity ended`，并每秒打印一次当前统计。

## 代码结构

- `vad.c`：创建 I2S RX 通道，把 32-bit I2S 样本转成 16-bit PCM，并调用 ESP-SR VAD。
- `vad.h`：模块入口声明。

## 配置项

- `CONFIG_ESPESP_MIC_BCLK_GPIO`：I2S 麦克风 BCLK。
- `CONFIG_ESPESP_MIC_WS_GPIO`：I2S 麦克风 WS/LRCLK。
- `CONFIG_ESPESP_MIC_DIN_GPIO`：I2S 麦克风 DATA 输入。
- `CONFIG_ESPESP_VAD_MODE_*`：VAD 灵敏度，数字越高越保守，误触发更少但可能漏掉轻声。
- `CONFIG_ESPESP_VAD_FRAME_MS`：单帧处理时长，支持 10、20、30 ms。
- `CONFIG_ESPESP_VAD_MIN_SPEECH_MS`：连续多少毫秒语音后进入 speech 状态。
- `CONFIG_ESPESP_VAD_MIN_SILENCE_MS`：连续多少毫秒静音后回到 silence 状态。
- `CONFIG_ESPESP_VAD_MIC_SLOT_LEFT` / `CONFIG_ESPESP_VAD_MIC_SLOT_RIGHT`：麦克风所在 I2S slot。
- `CONFIG_ESPESP_VAD_SAMPLE_SHIFT_BITS`：32-bit I2S 样本转 16-bit PCM 的右移位数。

## 注意事项

- ESP-SR WebRTC VAD 支持 8 kHz、16 kHz、32 kHz；本模块固定用 16 kHz，便于和其它语音模块对齐。
- VAD 不识别说话内容，只判断当前音频像不像人声。
- 如果一直是 silence，先运行 `microphone` 或 `pcm_stream`，确认麦克风接线、slot 和音量。
- 如果误触发多，把 VAD mode 提高到 3 或 4，或增大 `Minimum speech duration`。
- 如果句尾被截断，把 `Minimum silence duration` 调大一些。
