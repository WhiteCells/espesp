# 27 chat: VADNet 可打断流程对话客户端

## 模块概览

- ESP32 通过 I2S 麦克风采集音频。
- 本地 VADNet 只判断语音段起止；检测到 speech 后才把 PCM 推给 `server/vchat/`。
- 检测到新的本地 speech 时，客户端会清空扬声器播放队列，并向服务端发送 `cancel_response`，用于打断正在播放或生成的回答。
- 服务端回推 raw TTS PCM 后，客户端通过 I2S 扬声器播放。
- 播放 PCM 同时作为 AEC reference，麦克风路径在 VAD 和上送前执行 AEC。

## 使用方式

电脑端启动 `vchat`：

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

```text
ESPESP Menu
  -> WiFi module
  -> Microphone module
  -> Speaker module
  -> Chat module
```

`Chat module -> vchat WebSocket URI` 填电脑局域网 IP，例如：

```text
ws://192.168.1.23:8765
```

URI 不带 `/ws`。`server/vchat/.env` 当前默认 `VCHAT_HOST=127.0.0.1`，给 ESP32 使用时要改成 `0.0.0.0`，或启动时显式传 `--host 0.0.0.0`。

## 音频参数

采集参数来自 `Chat module` 和 VADNet 模型：

- encoding：`pcm_s16le`
- container：`raw`
- channels：`1`
- sample_rate：VADNet 模型采样率，通常 16000 Hz。
- slot：`Chat microphone I2S slot`
- 32-bit I2S 到 16-bit PCM：`Microphone sample right shift bits`

播放参数：

- 默认 sample_rate：`Default speaker sample rate`，默认 24000 Hz。
- `ready.tts_sample_rate` 或 `tts_start.sample_rate` 会覆盖默认播放采样率。
- 当前 `server/vchat/main.py` 的 `tts_start` 不带 `sample_rate`，实际播放采样率来自连接后的 `ready.tts_sample_rate`。
- 音量和限幅由 `TTS playback volume percent`、`TTS soft limit percent` 控制。
- `server/vchat/.env` 当前示例 `TTS_SAMPLE_RATE=48000`，ESP32 会自动切到 48 kHz；如果播放链路压力大，可把服务端改为 24000 Hz。

## 协议流程

```text
speech start
  -> clear local playback queue
  -> cancel_response
  -> audio_start
  -> pre-speech PCM
  -> speech PCM frames

speech end
  -> final PCM frame
  -> audio_end

server response
  <- tts_start
  <- binary pcm_s16le chunks
  <- tts_done
```

上行事件：

- `audio_start`：携带 `turn_id`、`sample_rate`、`channels=1`、`encoding=pcm_s16le`、`container=raw`。
- binary frame：raw mono `pcm_s16le`。
- `audio_end`：结束本轮 ASR 输入。
- `cancel_response`：请求 `vchat` 取消正在生成或发送的回复。

下行事件：

- `ready`：携带 `asr_sample_rate`、`tts_sample_rate`、`channels`、`sample_width`。
- `turn_started`、`asr_partial`、`asr_final`、`llm_delta`、`llm_done`、`turn_done`：主要用于日志观察。
- `tts_start`：声明后续 binary frame 是 TTS PCM。
- binary frame：raw mono `pcm_s16le`。
- `tts_done`：把播放结束标记排入队列，播放 task 在前面的 PCM 真正写入 I2S 后再结束本地播放状态。
- `response_cancelled`、`warning`、`error`：停止本地播放或输出诊断日志。

`server/vchat/` 会按 LLM 输出文本片段多次调用 TTS，所以一次回答可能包含多个 `tts_start -> binary -> tts_done` 小段。客户端在采样率不变时续接播放队列，避免下一段开头打断上一段尾音。

## 打断行为

本地 VADNet 从 silence 进入 speech 时，`chat` 会同时做三件事：

- 清空尚未播放的 TTS 队列。
- 重置 I2S TX，尽量丢掉 DMA 中的尾音。
- 发送 `cancel_response`，让 `server/vchat` 取消当前回复任务。

这套策略让“用户正在听回答时开口说话”优先进入新一轮上行语音，而不是等旧 TTS 播放结束。

## AEC

`chat` 复用 `voice_client` 的轻量 NLMS AEC：

- TTS 播放 task 会把实际写入扬声器的 PCM 送入 AEC reference buffer。
- 麦克风采样转换成 `pcm_s16le` 后先过 AEC，再进入 VADNet 和上行发送。
- 播放结束或打断后，AEC 保留一段尾音窗口，用来覆盖扬声器余振回到麦克风的时间。

## 调参建议

- 一直 silence：先跑 `microphone` 或 `pcm_stream`，确认麦克风有声音且 slot 选对；再把采样右移从 12 调到 11。
- 误打断多：提高 `Chat VADNet aggressiveness`，或增大 `Minimum speech duration in ms`。
- 句尾被切掉：增大 `Minimum silence duration in ms`。
- 第一字被吞：增大 `Pre-speech audio kept before VAD in ms`。
- 播放炸麦：降低 `TTS playback volume percent`，观察日志里的 `peak_in`、`peak_out`、`limited`。
- 播放时 VAD 被扬声器触发：先降低 TTS 音量，再增大 AEC filter length 或降低 step size。
- 日志出现 `playback queue full` 时，说明服务端 TTS 推流快于 I2S 实时播放；当前客户端会施加 WebSocket 背压等待播放消费，持续出现时可把 `TTS_SAMPLE_RATE` 降到 24000 Hz。
- 日志出现 `Connection reset by peer` 时，先确认 `server/vchat` 使用 `--host 0.0.0.0` 启动，且 ESP32 URI 不带 `/ws`。

## 源码位置

- `main/chat/chat.c`
- `main/chat/chat_audio.c`
- `main/chat/chat_playback.c`
- `main/chat/chat_protocol.c`
- `main/chat/README.md`
- `server/vchat/main.py`
