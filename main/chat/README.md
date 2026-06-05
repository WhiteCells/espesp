# chat

`chat` 是面向 `server/vchat/` 的流程对话客户端：

- I2S 麦克风持续采集。
- VADNet 检测到语音段后才发送 `audio_start`、binary PCM、`audio_end`。
- 本地说话时立即清空 TTS 播放队列，并发送 `cancel_response`，形成打断效果。
- 服务端返回 `tts_start`、binary PCM、`tts_done` 后，ESP32 用 I2S 扬声器播放。
- 播放路径把实际写入扬声器的 PCM 喂给轻量 AEC，采集路径在 VAD 和上送前做回声抵消。

## 代码结构

- `chat.c`：模块入口、主采集循环、VAD 状态机、预录缓冲和生命周期清理。
- `chat_audio.c`：VADNet 模型加载、I2S RX/TX 创建、采样转换和扬声器采样率切换。
- `chat_playback.c`：TTS 播放队列、I2S 写入、音量/限幅、AEC reference feed、打断清队列。
- `chat_protocol.c`：WebSocket 连接、控制 JSON、上行音频事件和下行 TTS 事件处理。
- `chat_context.h`：共享运行状态。

## 主要流程

```text
I2S microphone
  -> int32 to pcm_s16le
  -> AEC process
  -> VADNet detect
  -> speech: audio_start + prebuffer + binary PCM
  -> silence: audio_end

WebSocket binary TTS
  -> playback queue
  -> volume/limiter
  -> AEC reference feed
  -> I2S speaker
```

## 配置项

入口：

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> chat: VADNet barge-in voice chat client
```

需要配置：

- `WiFi module`：SSID 和密码。
- `Microphone module`：I2S 麦克风 BCLK、WS、DIN。
- `Speaker module`：I2S 扬声器 BCLK、WS、DOUT。
- `Chat module`：`vchat WebSocket URI`、麦克风 slot、采样右移、默认扬声器采样率、VADNet、AEC、队列和超时。

`Chat module -> Default speaker sample rate` 默认 24000 Hz，对齐 `server/vchat` 的默认 TTS 采样率。

## 协议

上行：

- `{"type":"audio_start",...}`
- binary：`pcm_s16le`、mono、raw PCM，采样率来自 VADNet 模型。
- `{"type":"audio_end"}`
- `{"type":"cancel_response","reason":"local_vad_barge_in"}` 用于打断服务端正在生成或发送的回复。

下行：

- `ready`：读取 `tts_sample_rate` 作为默认播放采样率。
- `tts_start`：开始新的播放流；如果包含 `sample_rate`，会重新配置 I2S TX。
- binary：TTS `pcm_s16le` mono。
- `tts_done`：结束播放流。

## 注意事项

- ESP32 端 URI 要填电脑局域网 IP，例如 `ws://192.168.1.23:8765`，不要填 `127.0.0.1`。
- 本模块依赖 ESP-SR VADNet 和 `model` 分区；默认配置应选择 `vadnet1 medium`。
- VADNet 的采样率由模型决定，通常是 16000 Hz；`server/vchat` 的 `ASR_SAMPLE_RATE` 要与它一致。
- 如果第一字被吞，增大 `Pre-speech audio kept before VAD in ms`。
- 如果误打断多，增大 `Chat VADNet aggressiveness` 或 `Minimum speech duration in ms`。
- 如果句尾被切掉，增大 `Minimum silence duration in ms`。
- 如果播放音量大导致 VAD 误触发，先降低 `TTS playback volume percent`，再调 AEC 参数。
- ESP32 日志出现 `Connection reset by peer` 时，先确认 `server/vchat` 使用 `--host 0.0.0.0` 启动，且 ESP32 URI 不带 `/ws`。
- ESP32 日志出现 `playback queue full` 时，说明服务端 TTS 推流快于实时播放；当前实现会等待队列腾出空间，不再直接丢 chunk。
