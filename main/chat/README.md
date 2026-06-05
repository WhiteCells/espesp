# chat

`chat` 是面向 `server/vchat/` 的可打断流程对话客户端：

- I2S 麦克风持续采集，VADNet 只负责判断本地语音段起止。
- 从 silence 进入 speech 时发送 `audio_start`、预录 PCM 和后续 binary PCM，回到 silence 时发送 `audio_end`。
- 本地再次开口会清空 TTS 播放队列、重置扬声器 DMA，并发送 `cancel_response` 打断服务端回复。
- 服务端返回 `tts_start`、binary PCM、`tts_done` 后，ESP32 用 I2S 扬声器播放。
- 播放路径把实际写入扬声器的 PCM 喂给轻量 AEC，采集路径在 VAD 和上送前做回声抵消。

## 代码结构

- `chat.c`：模块入口、主采集循环、VAD 状态机、预录缓冲和生命周期清理。
- `chat_audio.c`：VADNet 模型加载、I2S RX/TX 创建、采样转换和扬声器采样率切换。
- `chat_playback.c`：TTS 播放队列、I2S 写入、音量/限幅、PCM 分片对齐、AEC reference feed、打断清队列。
- `chat_protocol.c`：WebSocket 连接、轻量控制 JSON 解析、上行音频事件和下行 TTS 事件处理。
- `chat_context.h`：共享运行状态。

## vchat 协议

上行到 `server/vchat/main.py`：

- `{"type":"audio_start","turn_id":...,"sample_rate":...,"channels":1,"encoding":"pcm_s16le","container":"raw"}`
- binary：`pcm_s16le`、mono、raw PCM，采样率来自 VADNet 模型。
- `{"type":"audio_end","turn_id":...}`
- `{"type":"cancel_response","reason":"local_vad_barge_in"}`

下行：

- `ready`：读取 `asr_sample_rate` 用于日志校验，读取 `tts_sample_rate` 作为默认播放采样率。
- `turn_started`、`asr_partial`、`asr_final`、`llm_delta`、`llm_done`、`turn_done`：记录日志。
- `tts_start`：开始或续接播放流；当前 `vchat` 不发送 `sample_rate`，ESP32 使用 `ready.tts_sample_rate`。
- binary：TTS raw `pcm_s16le` mono。
- `tts_done`：在播放队列后排入结束标记，WebSocket 回调不等待整段音频播放完。
- `response_cancelled`、`warning`、`error`：停止本地播放或记录诊断日志。

`server/vchat/` 可能把一次 LLM 回复拆成多个 `tts_start -> binary -> tts_done` 小段。客户端会在采样率不变时续接播放队列，避免分段 TTS 的上一段尾音被下一段开头截断。

## 启动与配置

电脑端：

```sh
cd server/vchat
python main.py --host 0.0.0.0 --port 8765
```

ESP32 端：

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> chat: VADNet barge-in voice chat client
```

继续配置：

- `WiFi module`：SSID 和密码。
- `Microphone module`：I2S 麦克风 BCLK、WS、DIN。
- `Speaker module`：I2S 扬声器 BCLK、WS、DOUT。
- `Chat module`：`vchat WebSocket URI`、麦克风 slot、采样右移、默认扬声器采样率、VADNet、AEC、队列和超时。

`Chat module -> vchat WebSocket URI` 填电脑局域网 IP，例如 `ws://192.168.1.23:8765`，不要填 `127.0.0.1`，也不要带 `/ws`。

## 注意事项

- `server/vchat/.env` 当前默认 `VCHAT_HOST=127.0.0.1`，给 ESP32 使用时要改成 `0.0.0.0`，或启动时显式传 `--host 0.0.0.0`。
- VADNet 的采样率由模型决定，通常是 16000 Hz；`server/vchat` 的 `ASR_SAMPLE_RATE` 要与它一致。
- `server/vchat/.env` 当前示例 `TTS_SAMPLE_RATE=48000`，ESP32 会从 `ready.tts_sample_rate` 自动切到 48 kHz；播放链路压力大时可改为 24000 Hz。
- 如果第一字被吞，增大 `Pre-speech audio kept before VAD in ms`。
- 如果误打断多，增大 `Chat VADNet aggressiveness` 或 `Minimum speech duration in ms`。
- 如果句尾被切掉，增大 `Minimum silence duration in ms`。
- 如果播放音量大导致 VAD 误触发，先降低 `TTS playback volume percent`，再调 AEC 参数。
- ESP32 日志出现 `Connection reset by peer` 时，先确认 `server/vchat` 使用 `--host 0.0.0.0` 启动，且 ESP32 URI 不带 `/ws`。
- ESP32 日志出现 `playback queue full` 时，说明服务端 TTS 推流快于实时播放；当前实现会等待队列腾出空间，不直接丢 chunk。
