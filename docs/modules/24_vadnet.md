# 24 vadnet: 本地神经网络语音活动检测

## 模块概览

- ESP32-S3 从 I2S MEMS 麦克风读取音频。
- 固件把 32-bit I2S slot 转成 16-bit mono PCM。
- ESP-SR VADNet 在本地用神经网络模型判断当前帧是否有人声，不依赖 Wi-Fi。
- 串口日志输出语音开始、语音结束和每秒统计。

`vadnet` 和 `vad` 的区别：

- `vad` 使用 ESP-SR 的 WebRTC VAD，轻量，不依赖模型分区。
- `vadnet` 使用 ESP-SR 的 VADNet 模型，通常更抗噪，但需要模型分区和更多资源。

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> vadnet
```

确认 ESP-SR VADNet 模型：

```text
ESP Speech Recognition
  -> Select voice activity detection
  -> voice activity detection (vadnet1 medium)
```

配置麦克风引脚：

```text
ESPESP Menu
  -> Microphone module
```

配置 VADNet 参数：

```text
ESPESP Menu
  -> VADNet module
```

构建、烧录并监视：

```sh
idf.py build flash monitor
```

看到 `VADNet listening` 后，对着麦克风说话。检测到语音段会打印：

```text
voice activity started: segment=...
voice activity ended: duration_ms=...
```

## 参数调试

- `VADNet model name or substring`：选择模型分区里的 VADNet 模型。默认留空时选择第一个 `vadnet` 模型。
- `VADNet aggressiveness`：数字越高越保守。环境噪声误触发多时调高，轻声漏检时调低。
- `Set VADNet detection threshold manually`：默认关闭，使用模型自带阈值；需要精细调试时再开启。
- `VADNet detection threshold`：阈值越高越保守，误触发更少但可能漏检。
- `Minimum speech duration`：进入 speech 状态前需要持续多少毫秒语音。调大可减少短噪声误触发。
- `Minimum silence duration`：回到 silence 状态前需要持续多少毫秒非语音。调大可避免句尾过早结束。
- `VADNet microphone I2S slot`：选择麦克风输出所在的 left/right slot。
- `Microphone sample right shift bits`：32-bit I2S 样本转 16-bit PCM 的右移位数。数值越小越响，数值越大越安静。

## 源码位置

- `main/vadnet/vadnet.c`
- `main/vadnet/vadnet.h`

## 当前模块接口参考

- `vadnet_run()`：初始化 ESP-SR 模型分区、加载 VADNet、创建 I2S RX，持续读取麦克风并打印语音活动状态。
- `vadnet_select_model()`：从 `model` 分区里选择 `vadnet` 前缀模型。
- `vadnet_convert_sample()`：把 32-bit I2S 样本缩放并限幅成 16-bit PCM。

## 常用接口说明

- `esp_srmodel_init("model")`：挂载并读取 ESP-SR 模型分区。
- `esp_vadn_handle_from_name()`：按模型名获取 VADNet 接口。
- `esp_vadn_iface_t.create()`：创建 VADNet 实例，并设置模式、通道数、最短语音和最短静音时长。
- `esp_vadn_iface_t.detect()`：输入一帧 16-bit PCM，返回 `VAD_SPEECH` 或 `VAD_SILENCE`。
- `esp_vadn_iface_t.destroy()`：释放 VADNet 实例。

## 排错

- 启动时报 `no VADNet model found`：确认 `ESP Speech Recognition -> Select voice activity detection -> vadnet1 medium` 已启用，并执行 `idf.py flash`。
- 启动时报 `no speech models found`：确认使用 `partitions_16mb_sr.csv`，并且烧录了模型分区。
- 只烧 app 后仍找不到模型：执行完整 `idf.py flash`，不要只烧 `app`。
- 一直是 `silence`：先运行 `microphone` 或 `pcm_stream`，确认麦克风有声音且 I2S slot 选对。
- 误触发多：提高 `VADNet aggressiveness`，或增大 `Minimum speech duration`。
- 句尾被切掉：增大 `Minimum silence duration`，例如从 800 ms 调到 1200 ms。
