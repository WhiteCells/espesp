# pcm_stream

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> pcm_stream: stream microphone PCM to computer
```

配置麦克风引脚：

```text
ESPESP Menu
  -> Microphone module
```

选择传输方式：

```text
ESPESP Menu
  -> PCM stream module
```

然后执行：

```sh
idf.py build flash monitor
```

## 电脑端录音

UART：

```sh
cd server
pip install -e .
python -m pcm_recorder uart /dev/ttyUSB0 out.wav --baud 921600 --seconds 10
```

UDP：

```sh
cd server
python -m pcm_recorder udp out.wav --bind 0.0.0.0 --port 8765 --seconds 10
```

## 当前模块已有接口

### `esp_err_t pcm_stream_run(void)`

创建 I2S RX 通道，读取 I2S MEMS 麦克风 32-bit slot 数据，转换成 16-bit
little-endian mono PCM，并通过 UART 或 Wi-Fi UDP 发送到电脑。

返回值：

- 正常情况下不会返回。
- I2S、UART、Wi-Fi 或 UDP 初始化失败时返回对应 `esp_err_t`。

## 数据格式

每个包由 28 字节 little-endian header 和 PCM payload 组成：

- `magic`：`PCM1`
- `version`：当前为 `1`
- `header_size`：当前为 `28`
- `sequence`：递增包序号
- `sample_rate_hz`：采样率，来自 `CONFIG_ESPESP_MIC_SAMPLE_RATE_HZ`
- `channels`：当前为 `1`
- `sample_width_bits`：当前为 `16`
- `frame_samples`：payload 里的采样点数
- `payload_bytes`：payload 字节数

电脑端脚本按包头信息写 WAV 文件。UDP 丢包时会按序号补静音，UART 会自动重新寻找
`PCM1` 同步字。

## 可配置项

- `CONFIG_ESPESP_MIC_BCLK_GPIO`：I2S 麦克风 BCLK。
- `CONFIG_ESPESP_MIC_WS_GPIO`：I2S 麦克风 WS/LRCLK。
- `CONFIG_ESPESP_MIC_DIN_GPIO`：I2S 麦克风 DATA 输入。
- `CONFIG_ESPESP_MIC_SAMPLE_RATE_HZ`：采样率。
- `CONFIG_ESPESP_PCM_STREAM_TRANSPORT_UART`：使用 UART 传输。
- `CONFIG_ESPESP_PCM_STREAM_TRANSPORT_UDP`：使用 Wi-Fi UDP 传输。
- `CONFIG_ESPESP_PCM_UART_*`：UART 端口、波特率和引脚。
- `CONFIG_ESPESP_PCM_UDP_HOST`：UDP recorder IPv4 地址，默认 `255.255.255.255` 广播。
- `CONFIG_ESPESP_PCM_UDP_PORT`：电脑端 UDP recorder 端口。
- `CONFIG_ESPESP_PCM_FRAME_SAMPLES`：每个 UDP/UART 包里的 PCM 采样数，默认 128。
- `CONFIG_ESPESP_PCM_INPUT_SLOT_LEFT` / `CONFIG_ESPESP_PCM_INPUT_SLOT_RIGHT`：选择麦克风所在的 I2S slot，只会发送其中一个 slot 作为 mono PCM。
- `CONFIG_ESPESP_PCM_SAMPLE_SHIFT_BITS`：32-bit I2S 样本转 16-bit PCM 时右移的位数，默认 12；声音太小可调低，仍有破音可调高。

## 注意事项

- 避免使用 UART0 传 PCM，UART0 通常同时承载 ESP-IDF monitor 日志。
- 16 kHz mono 16-bit PCM 约 256 kbit/s，UART 建议 921600 baud 或更高。
- 如果录制 10 秒但 WAV 播放约 20 秒，说明左右两个 I2S slot 被一起写入了单声道流；本模块默认只发送 left slot，如果没有声音请改成 right slot。
- 如果 WAV 完全没声音或音量很低，把 `PCM sample right shift bits` 从 12 调到 11 或 10；如果仍然严重失真，把它调到 13 或 14。
- UDP 如果出现 `errno=12`，通常是本地 UDP/lwIP 缓冲压力，优先减小 `PCM samples per packet`。
- UDP 模式先用默认广播验证链路；如果广播也收不到，通常是电脑端 Python 运行在容器/虚拟网络里，或 Wi-Fi/AP/防火墙拦截。
- UDP 模式要先启动电脑端 recorder，再启动 ESP 模块更容易确认链路。
- 本模块只把音频流送到电脑，不在 ESP flash 上保存音频文件。
