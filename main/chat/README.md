# chat

`chat` 是面向 `server/vchat/` 的可打断流程对话客户端：

- I2S 麦克风持续采集，VADNet 只负责判断本地语音段起止。
- 从 silence 进入 speech 时发送 `audio_start`、预录 PCM 和后续 binary PCM；VADNet 回到 silence 后还会保留一小段对话级静音窗口，只有持续静音才发送 `audio_end`。
- 本地再次开口会清空 TTS 播放队列、重置扬声器 DMA，并发送 `cancel_response` 打断服务端回复。
- 服务端返回 `tts_start`、binary PCM、`tts_done` 后，ESP32 用 I2S 扬声器播放。
- 播放路径把实际写入扬声器的 PCM 喂给轻量 AEC；采集路径先做输入软限幅和高通。普通语音轮使用近端麦克风帧，播放期间的打断走 strong/weak 两级确认，并通过 AEC-clean 与 clean/raw 比例过滤扬声器漏音。确认期间的前缓存会先发给 ASR，打断后的短窗口继续上传 AEC-clean 音频，随后恢复原始近端麦克风。

## 代码结构

- `chat.c`：模块入口、主采集循环、VAD 状态机、麦克风软限幅/高通、预录缓冲和生命周期清理。
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
- `tts_start`：开始播放流，携带 `sample_rate`，ESP32 按该采样率配置扬声器。
- binary：TTS raw `pcm_s16le` mono。
- `tts_done`：在播放队列后排入结束标记，WebSocket 回调不等待整段音频播放完。
- `response_cancelled`、`warning`、`error`：停止本地播放或记录诊断日志。

`server/vchat/` 会把完整 LLM 回复一次性接入流式 TTS。ESP32 收到单路 raw PCM 流后连续播放，并在本地做短淡入/淡出与 PCM 对齐处理，降低爆音和分片抖动影响。

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
- `voice-chat/.env.example` 当前示例 `TTS_SAMPLE_RATE=48000`，ESP32 会从 `ready.tts_sample_rate` 自动切到服务端采样率。
- 如果第一字被吞，继续增大 `Pre-speech audio kept before VAD in ms`；默认已提高到 500 ms，以覆盖 VADNet 和打断确认窗口。
- 如果误打断多，增大 `Chat VADNet aggressiveness` 或 `Minimum speech duration in ms`。
- 如果一句话里稍停顿就被拆轮，先增大 `Additional end-of-turn silence hold in ms`，再考虑增大 `Minimum silence duration in ms`。
- 如果播放期间没说话也会偶发误打断，先增大 `Minimum AEC-clean to raw level percent for TTS barge-in`、`Weak playback barge-in confirmation in ms` 或 `Additional playback barge-in confirmation in ms`。
- 如果播放音量大导致 VAD 误触发，先降低 `TTS playback volume percent`，再调 AEC 参数。
- 如果说话时有电流声或破音，观察日志里的 `input_clipped`、`input_limited` 和 `input_gain_q15`；前两者增长时优先把 `Microphone sample right shift bits` 调到 14。
- ESP32 日志出现 `Connection reset by peer` 时，先确认 `server/vchat` 使用 `--host 0.0.0.0` 启动，且 ESP32 URI 不带 `/ws`。
- ESP32 日志出现 `playback queue full` 时，说明服务端 TTS 推流快于实时播放；当前实现会等待队列腾出空间，不直接丢 chunk。
