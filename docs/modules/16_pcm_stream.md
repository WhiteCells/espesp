# 16 pcm_stream: PCM 音频流到电脑

## 模块概览

- ESP32 从 I2S MEMS 麦克风读取 32-bit slot 数据。
- 固件把样本转换成 16-bit mono PCM。
- PCM 通过 UART 或 Wi-Fi UDP 发送到电脑。
- Python recorder 收包后写成 `.wav`，避免在 ESP flash 上频繁写入。

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> 16 pcm_stream
```

配置项：

```text
ESPESP Menu
  -> Microphone module
ESPESP Menu
  -> PCM stream module
```

UART 模式建议使用 UART1 外接 USB-UART 转接板，默认：

- ESP TX GPIO17 -> USB-UART RX
- ESP GND -> USB-UART GND
- 波特率 921600

电脑端录音：

```sh
cd server
pip install -e .
python -m pcm_recorder uart /dev/ttyUSB0 out.wav --baud 921600 --seconds 10
```

UDP 模式需要先配置 Wi-Fi。`UDP recorder IPv4 address` 默认是
`255.255.255.255` 广播地址，适合先验证链路；电脑端监听
`0.0.0.0:8765` 即可。如果确认电脑 IP 后想改成单播，再把它改成
Python 启动时显示的本机 IPv4。

```text
ESPESP Menu
  -> WiFi module
ESPESP Menu
  -> PCM stream module
  -> UDP recorder IPv4 address
```

电脑端先监听：

```sh
cd server
python -m pcm_recorder udp out.wav --bind 0.0.0.0 --port 8765 --seconds 10
```

然后启动 ESP 模块。

## 源码位置

- `main/pcm_stream/pcm_stream.c`
- `server/pcm_recorder/recorder.py`

## 当前模块接口参考

- `pcm_stream_run()`：初始化 I2S，读取麦克风，打包 PCM，发送到 UART 或 UDP。
- `pcm_stream_create_channel()`：创建 I2S RX 标准模式通道。
- `pcm_stream_send_frame()`：把一帧 PCM 样本打成 `PCM1` 包。

## 包格式

包头为 little-endian：

```text
uint32 magic             // "PCM1"
uint16 version           // 1
uint16 header_size       // 28
uint32 sequence
uint32 sample_rate_hz
uint16 channels          // 1
uint16 sample_width_bits // 16
uint32 frame_samples
uint32 payload_bytes
```

payload 是 signed 16-bit little-endian PCM。

## 配置项

- `CONFIG_ESPESP_MIC_BCLK_GPIO`：I2S 麦克风 BCLK GPIO。
- `CONFIG_ESPESP_MIC_WS_GPIO`：I2S 麦克风 WS/LRCLK GPIO。
- `CONFIG_ESPESP_MIC_DIN_GPIO`：I2S 麦克风数据输入 GPIO。
- `CONFIG_ESPESP_MIC_SAMPLE_RATE_HZ`：采样率。
- `CONFIG_ESPESP_PCM_STREAM_TRANSPORT_UART`：UART 传输。
- `CONFIG_ESPESP_PCM_STREAM_TRANSPORT_UDP`：Wi-Fi UDP 传输。
- `CONFIG_ESPESP_PCM_UART_BAUD_RATE`：UART 波特率。
- `CONFIG_ESPESP_PCM_UDP_HOST`：UDP recorder IPv4 地址，默认 `255.255.255.255` 广播。
- `CONFIG_ESPESP_PCM_UDP_PORT`：电脑端 UDP 端口。
- `CONFIG_ESPESP_PCM_FRAME_SAMPLES`：每个包里的 PCM 采样数，默认 128。
- `CONFIG_ESPESP_PCM_INPUT_SLOT_LEFT` / `CONFIG_ESPESP_PCM_INPUT_SLOT_RIGHT`：选择麦克风所在的 I2S slot，只会发送其中一个 slot 作为 mono PCM。
- `CONFIG_ESPESP_PCM_SAMPLE_SHIFT_BITS`：32-bit I2S 样本转 16-bit PCM 时右移的位数，默认 12；声音太小可调低，仍有破音可调高。

## 注意事项

- 不要用 ESP 自己的 `127.0.0.1` 当 UDP host；先用默认广播，或填电脑的局域网 IP。
- UART0 通常被日志占用，音频流建议使用 UART1 或 UART2。
- 如果 WAV 听起来速度不对，确认 ESP 采样率和 Python 输出里的 sample_rate 一致。
- 如果录制 10 秒但 WAV 播放约 20 秒，说明左右两个 I2S slot 被一起写入了单声道流；本模块默认只发送 left slot，如果没有声音请改成 right slot。
- 如果 WAV 完全没声音或音量很低，把 `PCM sample right shift bits` 从 12 调到 11 或 10；如果仍然严重失真，把它调到 13 或 14。
- 如果 UART 录音断续，优先提高波特率或降低采样率。
- 如果 UDP 日志里出现 `errno=12`，优先降低 `PCM samples per packet`，并确认电脑 IP 正确。
