# vadnet

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> vadnet: local neural network voice activity detection
```

还需要确认：

```text
ESPESP Menu
  -> Microphone module
ESPESP Menu
  -> VADNet module
ESP Speech Recognition
  -> Select voice activity detection
  -> voice activity detection (vadnet1 medium)
```

默认配置会选择 ESP-SR 自带的 `vadnet1_medium`，并把模型打进 `model` 分区。
执行：

```sh
idf.py build flash monitor
```

串口看到 `VADNet listening` 后，对着麦克风说话。模块会在语音状态开始和结束时打印
`voice activity started` / `voice activity ended`，并每秒打印一次当前统计。

## 代码结构

- `vadnet.c`：加载 ESP-SR 模型分区里的 VADNet，读取 I2S 麦克风并调用神经网络 VAD。
- `vadnet.h`：模块入口声明。

## 配置项

- `CONFIG_ESPESP_MIC_BCLK_GPIO`：I2S 麦克风 BCLK。
- `CONFIG_ESPESP_MIC_WS_GPIO`：I2S 麦克风 WS/LRCLK。
- `CONFIG_ESPESP_MIC_DIN_GPIO`：I2S 麦克风 DATA 输入。
- `CONFIG_SR_VADN_VADNET1_MEDIUM`：ESP-SR VADNet 模型选择，必须开启才能打包模型。
- `CONFIG_ESPESP_VADNET_MODEL`：VADNet 模型名或模型名片段，默认留空选择第一个 `vadnet` 模型。
- `CONFIG_ESPESP_VADNET_MODE_*`：VADNet 灵敏度，数字越高越保守。
- `CONFIG_ESPESP_VADNET_SET_THRESHOLD` / `CONFIG_ESPESP_VADNET_THRESHOLD`：可选手动检测阈值。
- `CONFIG_ESPESP_VADNET_MIN_SPEECH_MS`：连续多少毫秒语音后进入 speech 状态。
- `CONFIG_ESPESP_VADNET_MIN_SILENCE_MS`：连续多少毫秒非语音后回到 silence 状态。
- `CONFIG_ESPESP_VADNET_MIC_SLOT_LEFT` / `CONFIG_ESPESP_VADNET_MIC_SLOT_RIGHT`：麦克风所在 I2S slot。
- `CONFIG_ESPESP_VADNET_SAMPLE_SHIFT_BITS`：32-bit I2S 样本转 16-bit PCM 的右移位数。

## 注意事项

- VADNet 是 ESP-SR 的神经网络 VAD，通常比 WebRTC VAD 更抗噪，但资源占用更高。
- 本模块需要 `model` 分区；项目根目录的 `partitions_16mb_sr.csv` 已包含这个分区。
- 执行 `idf.py flash` 时会同时烧录模型分区。只烧 app 时，模型分区不会更新。
- 如果一直没有语音状态变化，先运行 `microphone` 或 `pcm_stream`，确认麦克风接线、slot 和音量。
- 如果启动时报找不到 VADNet 模型，确认 `CONFIG_SR_VADN_VADNET1_MEDIUM=y` 并重新 `idf.py build flash`。
