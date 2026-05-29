# voice_client

## 使用方式

AEC 设计和实现细节见 [AEC.md](./AEC.md)。

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

控制事件顺序上，客户端按“文本事件驱动状态、binary 仅承载 PCM”的方式工作：

- `ready`、`started`、`transcript`、`committed` 只做日志和状态确认。
- 只有收到 `tts_start` 且 `response_format=pcm` 后，后续 binary/continuation frame 才会被当作可播放的 TTS PCM。
- 收到 `tts_end`、连接断开或服务端 `error` 后，会立即结束当前播放会话并清空分片对齐状态，避免旧会话残留字节污染下一轮。

## 代码结构

`voice_client` 对外只暴露 `voice_client_run()`，内部按职责拆成以下文件：

- `voice_client.c`：模块入口和生命周期编排。负责连接 Wi-Fi、创建资源、启动 WebSocket、运行麦克风采集主循环、统一清理资源。
- `voice_client_aec.c`：声学回声消除（AEC）。基于 NLMS 自适应滤波器，在麦克风采集后、发送前去除扬声器回声，实现全双工通话。TTS 播放时从音频路径获取参考信号，持续更新滤波器系数。
- `voice_client_audio.c`：I2S 和 PCM 音频处理。负责麦克风/扬声器 I2S 通道创建、麦克风 32-bit 样本转 `pcm_s16le`、TTS PCM 音量衰减、软限幅、分片对齐和写入扬声器。TTS PCM 写入 I2S 前会同时喂入 AEC 作为参考信号。
- `voice_client_protocol.c`：voice-server 文本协议。负责构造 `start` JSON、解析服务端控制事件、处理 `ready`、`started`、`transcript`、`tts_start`、`tts_end`、`error` 等事件。`tts_start` 和 `tts_end` 会触发 AEC 状态切换。
- `voice_client_transport.c`：WebSocket 传输层。负责 URI/header 检查、发送 `start` 和麦克风 binary frame、处理 WebSocket 连接/断开/数据/错误事件。断连时重置 AEC 状态。
- `voice_client.h`：公共接口，目前只有 `esp_err_t voice_client_run(void)`。
- `voice_client_aec.h`：AEC 模块接口。
- `voice_client_context.h`：内部共享状态、连接事件 bit 和日志 tag。
- `voice_client_audio.h`：音频模块内部接口和音频相关常量。
- `voice_client_protocol.h`：协议模块内部接口和协议相关常量。
- `voice_client_transport.h`：传输模块内部接口和 WebSocket 相关常量。

### 数据流

```text
I2S microphone
  -> voice_client_audio.c: int32 sample -> pcm_s16le
  -> voice_client_aec.c: AEC 去除回声（TTS 播放期间生效）
  -> voice_client_transport.c: WebSocket binary frame
  -> voice-server
  -> voice_client_transport.c: WebSocket text/binary event
  -> voice_client_protocol.c: control JSON state changes
  -> voice_client_audio.c: TTS pcm_s16le -> volume/limit -> AEC 参考信号 -> I2S speaker
```

### 内部状态

`voice_client_context_t` 放在 `voice_client_context.h`，包含：

- WebSocket 连接同步：`event_group`、`start_pending`、`session_started`。
- I2S 句柄和播放状态：`rx_channel`、`tx_channel`、`tx_enabled`、`playback_streaming`、`playback_pcm`。
- TTS 会话状态：`awaiting_tts_end` 用于标记当前是否处于一个已开始但尚未结束的 TTS 会话。
- AEC 实例：`aec`（`voice_client_aec_t *`），TTS 播放期间活跃，负责从麦克风信号中消除回声。
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
- 默认启用 AEC（声学回声消除）：播放 TTS 时麦克风保持采集，通过 NLMS 自适应滤波器实时去除扬声器回声，实现全双工通话。可在 menuconfig 中关闭 AEC 或调整以下参数：
  - `AEC filter length`：滤波器抽头数，默认 256。值越大能消除更长延迟的回声，但 CPU 和内存开销也更大。
  - `AEC step size`：收敛速度，默认 128（= 0.5）。值越大收敛越快但可能不稳定；值越小越稳定但收敛慢。
  - `AEC max delay`：默认 200ms。当前实现里它主要决定参考历史缓冲区大小和 `tts_end` 后的尾音处理窗口，不会自动搜索“最佳延迟点”。
- 如果 AEC 效果不佳（仍有回声），优先增大 `AEC filter length`；`AEC max delay` 主要影响历史缓存和尾音窗口。如果出现收敛不稳定（语音失真），降低 `AEC step size`。
- 当前 AEC 参考路径只做“按采样率比复制参考样本”的轻量对齐，不做高质量重采样；在麦克风/扬声器采样率差距较大时，回声消除效果会明显受限。
- 如果语音太小或破音，调整 `Voice client module -> Microphone sample right shift bits`。
- 如果完全无声，先分别运行 `microphone`、`pcm_stream` 和 `speaker` 模块确认硬件链路。

## 已确认的实现边界

- 控制 JSON 解析目前是轻量字符串扫描，不是完整 JSON 解析器；适合当前 `voice-server` 固定事件格式，但不适合复杂嵌套或同名键歧义很多的协议扩展。
- TTS 只支持 `pcm_s16le` 单声道 raw PCM 直通播放；收到 WAV/MP3/FLAC/Ogg 等带容器或压缩格式的字节流时，客户端会拒绝播放并打日志。
- WebSocket 文本控制消息要求单帧完整到达；若未来服务端发送分片文本帧，需要先扩展 `voice_client_transport.c` 的重组逻辑。

## 修改建议

- 改 WebSocket 重连、鉴权、frame 发送策略时，优先改 `voice_client_transport.c`。
- 改 `start` payload 或新增服务端控制事件时，优先改 `voice_client_protocol.c`。
- 改采样、播放、音量、限幅、PCM 对齐和 I2S 行为时，优先改 `voice_client_audio.c`。
- 改 AEC 滤波器算法、参考缓冲、收敛参数时，改 `voice_client_aec.c`；AEC 与音频路径的集成点在 `voice_client_audio.c`（喂参考信号）、`voice_client_protocol.c`（播放状态切换）和 `voice_client.c`（主循环处理麦克风）。
- 新增公共能力前先确认是否真的需要暴露到 `voice_client.h`；模块内部函数应放在对应的 `voice_client_aec.h`、`voice_client_audio.h`、`voice_client_protocol.h` 或 `voice_client_transport.h`。
