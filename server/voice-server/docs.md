# 当前服务接口

本文档描述当前 `voice-server` 对外暴露的接口。服务默认启动命令：

```bash
uv run voice-server --host 0.0.0.0 --port 8765
```

默认地址示例：

- HTTP: `http://127.0.0.1:8765`
- WebSocket: `ws://127.0.0.1:8765/ws`

## HTTP 接口

### GET /health

健康检查。

响应：

```json
{"status": "ok"}
```

## WebSocket 接口

### 连接地址

```text
ws://127.0.0.1:8765/ws
```

连接成功后，服务端会先发送 `ready` 事件：

```json
{
  "type": "ready",
  "protocol": {
    "control": "json text frames",
    "input_audio": "binary PCM frames or JSON audio base64",
    "output_audio": "binary audio frames"
  },
  "defaults": {
    "input_sample_rate": 48000,
    "tts_sample_rate": 48000,
    "tts_response_format": "pcm"
  }
}
```

`defaults` 来自 `.env` 配置，不是代码兜底默认值。

## 客户端发送事件

客户端可以发送 JSON 文本帧，也可以直接发送二进制音频帧。

### start

开始一轮 ASR -> TTS 会话，并声明输入音频格式。

```json
{
  "type": "start",
  "audio_format": {
    "encoding": "pcm_s16le",
    "sample_rate": 48000,
    "channels": 1,
    "container": "raw"
  },
  "segment_seconds": 3,
  "asr": {
    "language": "zh",
    "model": "Qwen/Qwen3-ASR-1.7B"
  },
  "tts": {
    "voice": "zh1_voice",
    "response_format": "pcm"
  }
}
```

字段说明：

- `audio_format.encoding`：当前只支持 `pcm_s16le`、`pcm16`、`s16le`。
- `audio_format.sample_rate`：输入音频采样率，例如 `48000`。
- `audio_format.channels`：输入音频声道数，例如 `1`。
- `audio_format.container`：推荐 `raw`；如果传 `wav`，服务端会在 `commit` 时解析 WAV。
- `segment_seconds`：自动切分 ASR 音频片段的秒数。
- `asr`：覆盖本轮 ASR 参数。
- `tts`：覆盖本轮 TTS 参数。

如果客户端没有发送 `start`，直接发送音频帧，服务端会用 `.env` 中的
`ASR_SAMPLE_RATE`、`ASR_CHANNELS`、`ASR_SEGMENT_SECONDS` 自动开始会话。

### 二进制音频帧

发送 raw PCM 音频数据。格式必须和 `start.audio_format` 一致。

音频流建议分块发送，每个 WebSocket binary frame 放一小段连续 PCM 数据。服务端不要求固定
chunk 大小，但客户端不要把很长的音频一次性塞进一个帧里，否则会增加延迟，也更容易触发
`MAX_AUDIO_BUFFER_BYTES` 限制。

推荐：

- 实时麦克风：每帧 `20ms` 到 `100ms` 音频。
- 文件验证：可以用更大的块，例如当前验证客户端默认 `32 KiB`。
- 每个 chunk 的字节数最好是单个采样帧大小的整数倍：`channels * 2` bytes。

以 `48000 Hz / mono / pcm_s16le` 为例：

```text
20ms  = 48000 * 0.02 * 1 * 2 = 1920 bytes
40ms  = 48000 * 0.04 * 1 * 2 = 3840 bytes
100ms = 48000 * 0.10 * 1 * 2 = 9600 bytes
32KiB = 32768 bytes，大约 341ms
```

`commit` 表示一段话结束并提交处理，不需要每个音频 chunk 都发送一次 `commit`。

`pcm_s16le` 表示 16-bit little-endian PCM，不代表固定 16 kHz。实际采样率由
`sample_rate` 决定。例如：

```text
48000 Hz * 16 bit * 1 channel = 768 kbps = 96000 bytes/s
```

当前服务端不会重采样，声明的采样率必须和实际音频字节一致。

### audio

用 JSON 文本帧发送 base64 音频。适合不方便发送二进制帧的客户端。

```json
{
  "type": "audio",
  "data": "BASE64_AUDIO_BYTES"
}
```

也可以使用 `audio` 字段：

```json
{
  "type": "audio",
  "audio": "BASE64_AUDIO_BYTES"
}
```

### commit

提交当前缓冲区音频，触发 ASR -> TTS。

```json
{"type": "commit"}
```

服务端会返回：

```json
{"type": "committed", "queued": 1}
```

### text

只做 TTS，不走 ASR。

```json
{
  "type": "text",
  "text": "你好，这是一次语音合成测试。",
  "tts": {
    "voice": "zh1_voice",
    "response_format": "pcm"
  }
}
```

### ping

心跳。

```json
{"type": "ping"}
```

服务端返回：

```json
{"type": "pong"}
```

### stop

结束会话。服务端会先处理完已提交的音频，再发送 `stopped` 并关闭连接。

```json
{"type": "stop"}
```

## 服务端返回事件

### ready

连接建立后立即返回，说明协议和默认配置。

### started

服务端确认会话已开始。

```json
{
  "type": "started",
  "audio_format": {
    "encoding": "pcm_s16le",
    "sample_rate": 48000,
    "channels": 1,
    "container": "raw"
  },
  "segment_seconds": 3.0
}
```

### committed

音频已提交到处理队列。

```json
{"type": "committed", "queued": 1}
```

### transcript

ASR 识别结果。

```json
{
  "type": "transcript",
  "text": "识别出的文本",
  "duration_seconds": 3.0,
  "final": true
}
```

### tts_start

TTS 开始输出。

```json
{
  "type": "tts_start",
  "text": "要合成的文本",
  "response_format": "pcm",
  "sample_rate": 48000,
  "voice": "zh1_voice"
}
```

### 二进制 TTS 音频帧

`tts_start` 之后，服务端会发送一个或多个二进制音频帧。

- `response_format=pcm`：返回 raw PCM，采样率见 `tts_start.sample_rate`。
- `response_format=wav|flac|mp3|aac|opus`：返回对应编码的音频字节。

### tts_end

TTS 输出结束。

```json
{
  "type": "tts_end",
  "bytes": 96000
}
```

### stopped

会话结束。

```json
{"type": "stopped"}
```

### error

错误事件。

```json
{
  "type": "error",
  "code": "invalid_audio",
  "message": "Audio data is not valid base64."
}
```

常见 `code`：

- `invalid_json`
- `invalid_audio`
- `invalid_start`
- `invalid_text_event`
- `unsupported_audio_format`
- `audio_buffer_overflow`
- `invalid_wav`
- `unknown_event`
- `asr_error`
- `tts_error`

## 典型流程

### ASR -> TTS

1. 建立 WebSocket 连接。
2. 接收 `ready`。
3. 发送 `start`。
4. 持续发送分块后的二进制 PCM 音频帧。
5. 发送 `commit`。
6. 接收 `transcript`。
7. 接收 `tts_start`。
8. 接收二进制 TTS 音频帧。
9. 接收 `tts_end`。
10. 发送 `stop`。

### TTS-only

1. 建立 WebSocket 连接。
2. 接收 `ready`。
3. 发送 `text`。
4. 接收 `tts_start`。
5. 接收二进制 TTS 音频帧。
6. 接收 `tts_end`。
7. 发送 `stop`。

## 关键配置

以下配置需要显式写入 `.env` 或环境变量：

```env
ASR_HTTP_BASE_URL=http://sg.acabc.de:60004
ASR_WS_URL=ws://sg.acabc.de:60004/v1/realtime
ASR_MODEL=Qwen/Qwen3-ASR-1.7B
ASR_LANGUAGE=zh
ASR_RESPONSE_FORMAT=json
ASR_SAMPLE_RATE=48000
ASR_CHANNELS=1
ASR_SEGMENT_SECONDS=3

TTS_HTTP_BASE_URL=http://sg.acabc.de:60003
TTS_WS_URL=ws://sg.acabc.de:60003/v1/realtime
TTS_TRANSPORT=http
TTS_MODEL=openbmb/VoxCPM2
TTS_VOICE=zh1_voice
TTS_LANGUAGE=Chinese
TTS_RESPONSE_FORMAT=pcm
TTS_SAMPLE_RATE=48000
```

可选配置：

- `ASR_API_KEY`
- `TTS_API_KEY`
- `ASR_STREAM`
- `TTS_STREAM_CHUNK_SIZE`
- `TTS_SPEED`
- `TTS_TASK_TYPE`
- `TTS_REF_TEXT`
- `TTS_REF_AUDIO`
- `REQUEST_TIMEOUT_SECONDS`
- `CONNECT_TIMEOUT_SECONDS`
- `MAX_AUDIO_BUFFER_BYTES`
