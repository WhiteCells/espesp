# 21 wake_word: 本地 WakeNet 唤醒词检测

## 模块概览

- ESP32-S3 从 I2S MEMS 麦克风读取 16 kHz 音频。
- 固件把 32-bit I2S slot 转成 16-bit mono PCM。
- ESP-SR AFE + WakeNet 在本地检测唤醒词，不依赖 Wi-Fi。
- 串口日志输出可用模型、当前选中的模型和检测事件。

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> wake_word
```

配置麦克风引脚：

```text
ESPESP Menu
  -> Microphone module
```

配置唤醒词模块：

```text
ESPESP Menu
  -> Wake word module
```

构建、烧录并监视：

```sh
idf.py build flash monitor
```

看到 `WakeNet listening` 后，说出所选模型对应的唤醒词。检测成功会打印：

```text
wake word detected: count=..., model=...
```

## 自定义指定唤醒词

这里的“指定”是选择已经存在的 WakeNet 模型，而不是运行时输入任意中文短语。
WakeNet 模型需要提前训练并打包进 ESP-SR 的模型集合，再烧进 `model` 分区。

常用配置：

- `WakeNet model name or substring`：优先按模型名或片段匹配。
- `Wake keyword hint`：当模型名包含这个提示时匹配。

两个配置都留空时，示例会选择 `model` 分区里找到的第一个 WakeNet 模型。

## 分区和硬件

本项目新增 `partitions_16mb_sr.csv`：

- `factory`：应用固件。
- `model`：ESP-SR 模型分区。
- `nvs`、`phy_init`：系统数据分区。

默认配置面向 ESP32-S3 N16R8：

- 16 MB flash。
- 8 MB PSRAM。
- I2S MEMS 麦克风。

## 排错

- 构建时找不到 ESP-SR 头文件：确认 `main/idf_component.yml` 里的 `espressif/esp-sr` 已下载成功。
- 启动时报 `no speech models found`：确认使用自定义分区表，并执行的是 `idf.py flash`，不是只烧 app。
- 启动时报 `SR_RINGBUF ... Memory exhausted`：通常是当前 `sdkconfig` 没开 PSRAM。进入 `Component config -> ESP PSRAM`，启用 external SPI RAM，并选择 Octal、80 MHz、malloc 可分配。
- 一直检测不到：先跑 `microphone` 或 `pcm_stream`，确认麦克风接线和 slot。
- 音量太小：把 `Wake word module -> Microphone sample right shift bits` 从 12 调到 11 或 10。
- 频繁误触发：远离扬声器和噪声源，并把右移位数调大到 13 或 14。
