# Voice Server

- `voice_server.server`：FastAPI WebSocket 服务端。
- `voice_server.client`：命令行验证客户端。
- `voice_server.audio`、`voice_server.protocol`、`voice_server.upstream`：共享音频、协议和上游模型适配代码。

数据流：

- client -> server：JSON 控制帧 + 二进制输入音频帧。
- server -> client：JSON 事件 + 二进制 VoxCPM2 音频帧。
- 上游 ASR：Qwen-ASR 的 OpenAI 兼容转写接口。
- 上游 TTS：VoxCPM2 的 OpenAI 兼容流式语音接口。

默认 TTS 走已验证的 HTTP 流式接口：`POST /v1/audio/speech`，参数包含
`stream=true`、`response_format=pcm`、`stream_format=audio`。`TTS_TRANSPORT=ws` 保留为
后续适配点。

## 安装

```bash
uv sync
```

## 启动服务端

```bash
cp .env.example .env
uv run voice-server --host 0.0.0.0 --port 8765
```

健康检查：

```bash
curl http://127.0.0.1:8765/health
```

## 验证客户端

只测 TTS：

```bash
uv run voice-client \
  --url ws://127.0.0.1:8765/ws \
  --text "你好，这是一次端到端语音合成测试。" \
  --output out/tts.wav
```

用 16-bit PCM/WAV 文件测试 ASR -> TTS：

```bash
uv run voice-client \
  --url ws://127.0.0.1:8765/ws \
  --audio out/tts.wav \
  --output out/asr-tts.wav
```

客户端会分块发送音频。WAV 输入会先解码成 PCM 帧再发送；加上 `--real-time` 可以按输入采样率模拟实时推流：

```bash
uv run voice-client \
  --url ws://127.0.0.1:8765/ws \
  --audio out/tts.wav \
  --real-time \
  --segment-seconds 3 \
  --output out/asr-tts.wav
```

从标准输入读取实时 raw PCM：

```bash
ffmpeg -f alsa -i default -ac 1 -ar 16000 -f s16le - \
  | uv run voice-client \
      --url ws://127.0.0.1:8765/ws \
      --stdin-pcm \
      --input-sample-rate 16000 \
      --input-channels 1 \
      --segment-seconds 3 \
      --output out/live-asr-tts.wav
```

## 本地 WebSocket 协议

开始会话：

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
  "asr": {"language": "zh"},
  "tts": {"voice": "zh1_voice", "response_format": "pcm"}
}
```

之后发送二进制 PCM 音频帧。结束当前语音段：

```json
{"type": "commit"}
```

停止会话：

```json
{"type": "stop"}
```

服务端 JSON 事件：

- `ready`
- `started`
- `transcript`
- `tts_start`
- `tts_end`
- `committed`
- `stopped`
- `error`

TTS 音频通过二进制 WebSocket 帧返回。默认 `pcm` 格式下，客户端会给 24 kHz PCM payload
补一层 WAV 头再写入文件。

## 配置

见 [.env.example](.env.example)。外部服务地址、模型、音色和音频参数需要显式配置；
代码里不为这些部署相关配置提供兜底默认值。

常用变量：

- `ASR_HTTP_BASE_URL`、`ASR_WS_URL`、`ASR_API_KEY`、`ASR_MODEL`
- `ASR_SAMPLE_RATE`、`ASR_CHANNELS`、`ASR_SEGMENT_SECONDS`
- `TTS_HTTP_BASE_URL`、`TTS_WS_URL`、`TTS_TRANSPORT`
- `TTS_MODEL`、`TTS_VOICE`、`TTS_RESPONSE_FORMAT`、`TTS_SAMPLE_RATE`

## 代码结构

```text
src/voice_server/
  server/
    app.py      # FastAPI app、WebSocket 会话处理
    cli.py      # voice-server 命令入口
  client/
    cli.py      # voice-client 命令入口和验证逻辑
  audio.py      # PCM/WAV 工具
  config.py     # 环境变量配置
  protocol.py   # WebSocket 事件模型
  upstream.py   # ASR/TTS 上游接口适配
```

## 说明

ASR WebSocket URL 可以配置，但 `https://asr.acabc.de/docs` 的公开 OpenAPI schema 没有给出
WebSocket 事件格式。当前默认使用 OpenAI 兼容转写 HTTP 接口，并把 WebSocket 适配方法隔离在
`voice_server.upstream.QwenAsrClient`，方便拿到准确事件协议后替换。
