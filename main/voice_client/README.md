# voice_client

## 使用方式

先在电脑端启动 `voice-server`：

```sh
cd server/voice-server
uv run voice-server --host 0.0.0.0 --port 8765
```

ESP32 端选择模块：

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> voice_client: microphone -> voice-server -> speaker
```

需要配置：

```text
ESPESP Menu
  -> WiFi module
  -> Microphone module
  -> Speaker module
  -> Voice client module
```

`Voice client module` 里的 URI 要改成电脑局域网地址，例如：

```text
ws://192.168.1.23:8765/ws
```

## 协议

连接成功后，客户端发送 `start` 文本 JSON：

```json
{
  "type": "start",
  "audio_format": {
    "encoding": "pcm_s16le",
    "sample_rate": 48000,
    "channels": 1,
    "container": "raw"
  },
  "segment_seconds": 1.5,
  "asr": {"language": "zh"},
  "tts": {"voice": "zh1_voice", "response_format": "pcm"}
}
```

之后持续发送 WebSocket binary frame，payload 是 signed 16-bit little-endian mono PCM。
服务端返回 `tts_start` 后，后续 binary frame 会被写入 I2S 扬声器；收到 `tts_end`
后结束本轮播放。

## 代码结构

`voice_client` 对外只暴露 `voice_client_run()`，内部按职责拆成以下文件：

- `voice_client.c`：模块入口和生命周期编排。负责连接 Wi-Fi、创建资源、启动 WebSocket、运行麦克风采集主循环、统一清理资源。
- `voice_client_audio.c`：I2S 和 PCM 音频处理。负责麦克风/扬声器 I2S 通道创建、麦克风 32-bit 样本转 `pcm_s16le`、TTS PCM 音量衰减、软限幅、分片对齐和写入扬声器。
- `voice_client_protocol.c`：voice-server 文本协议。负责构造 `start` JSON、解析服务端控制事件、处理 `ready`、`started`、`transcript`、`tts_start`、`tts_end`、`error` 等事件。
- `voice_client_transport.c`：WebSocket 传输层。负责 URI/header 检查、发送 `start` 和麦克风 binary frame、处理 WebSocket 连接/断开/数据/错误事件。
- `voice_client.h`：公共接口，目前只有 `esp_err_t voice_client_run(void)`。
- `voice_client_context.h`：内部共享状态、连接事件 bit 和日志 tag。
- `voice_client_audio.h`：音频模块内部接口和音频相关常量。
- `voice_client_protocol.h`：协议模块内部接口和协议相关常量。
- `voice_client_transport.h`：传输模块内部接口和 WebSocket 相关常量。

### 数据流

```text
I2S microphone
  -> voice_client_audio.c: int32 sample -> pcm_s16le
  -> voice_client_transport.c: WebSocket binary frame
  -> voice-server
  -> voice_client_transport.c: WebSocket text/binary event
  -> voice_client_protocol.c: control JSON state changes
  -> voice_client_audio.c: TTS pcm_s16le -> volume/limit -> I2S speaker
```

### 内部状态

`voice_client_context_t` 放在 `voice_client_context.h`，包含：

- WebSocket 连接同步：`event_group`、`start_pending`、`session_started`。
- I2S 句柄和播放状态：`rx_channel`、`tx_channel`、`tx_enabled`、`playback_streaming`、`playback_pcm`。
- 分片处理状态：`binary_payload_active`、`has_pending_byte`、`pending_byte`。
- 统计信息：麦克风发送字节/分片、TTS 接收/写入字节、峰值、限幅次数等。

## 当前模块接口

### `esp_err_t voice_client_run(void)`

连接 Wi-Fi，创建 I2S 麦克风 RX 和 I2S 扬声器 TX 通道，连接 `voice-server`
WebSocket，把麦克风 PCM 推送到服务端，并播放服务端返回的 PCM TTS 音频。

## 注意事项

- `server/voice-server/.env` 示例目前使用 48 kHz ASR/TTS，模块默认也按 48 kHz
  声明和采集输入。
- 服务端返回 `tts_start.sample_rate` 后，客户端会把扬声器 I2S 时钟切到对应采样率。
- TTS 必须是 `response_format=pcm` 才能直接播放；MP3、AAC、OPUS 等压缩格式不会解码。
- TTS PCM 写入 I2S 前会先做数字音量衰减和软限幅，默认 `TTS playback volume percent`
  为 60、`TTS soft limit percent` 为 90。若播放仍有炸麦、破音或功放削顶，把 TTS 音量降到 50
  或 40 再试。
- 每轮 TTS 结束会打印 `peak_in`、`peak_out` 和 `limited`；`peak_in` 接近 32768 或
  `limited` 不为 0 时，说明下发音频余量很小。
- 默认启用半双工：播放 TTS 时会暂停继续上送麦克风音频，避免扬声器声音立刻被 ASR 再次识别。
- 如果语音太小或破音，调整 `Voice client module -> Microphone sample right shift bits`。
- 如果完全无声，先分别运行 `microphone`、`pcm_stream` 和 `speaker` 模块确认硬件链路。

## 修改建议

- 改 WebSocket 重连、鉴权、frame 发送策略时，优先改 `voice_client_transport.c`。
- 改 `start` payload 或新增服务端控制事件时，优先改 `voice_client_protocol.c`。
- 改采样、播放、音量、限幅、PCM 对齐和 I2S 行为时，优先改 `voice_client_audio.c`。
- 新增公共能力前先确认是否真的需要暴露到 `voice_client.h`；模块内部函数应放在对应的 `voice_client_audio.h`、`voice_client_protocol.h` 或 `voice_client_transport.h`。
