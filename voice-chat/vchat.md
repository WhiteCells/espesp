# vchat 客户端通信步骤

`vchat` 是一个 WebSocket 语音对话服务。客户端通过同一条 WebSocket 连接发送控制消息和用户语音音频，服务端返回识别文本、LLM 增量文本以及 TTS 音频。

## 1. 启动 vchat

在仓库根目录先准备依赖和配置：

```bash
uv sync
cp .env.example .env
```

启动服务：

```bash
uv run vchat
```

默认监听地址来自 `.env`：

```text
VCHAT_HOST=127.0.0.1
VCHAT_PORT=8765
```

客户端默认连接：

```text
ws://127.0.0.1:8765
```

也可以显式指定：

```bash
uv run vchat --host 0.0.0.0 --port 8765
```

## 2. 建立 WebSocket 连接

连接建立后，服务端会先发送一条 JSON 文本消息：

```json
{
  "type": "ready",
  "asr_sample_rate": 16000,
  "tts_sample_rate": 48000,
  "channels": 1,
  "sample_width": 2
}
```

客户端应使用这条消息确认音频格式：

- 发送给 ASR 的用户音频：单声道、16-bit PCM、小端序，采样率通常是 `asr_sample_rate`。
- 接收并播放的 TTS 音频：单声道、16-bit PCM、小端序，采样率通常是 `tts_sample_rate`。
- `sample_width=2` 表示每个采样点 2 字节。

## 3. 开始一轮用户语音

检测到用户开始说话后，先发送 `audio_start` 控制消息：

```json
{
  "type": "audio_start",
  "sample_rate": 16000,
  "channels": 1,
  "sample_width": 2
}
```

说明：

- `sample_rate` 最好与服务端 `ready.asr_sample_rate` 一致。
- 如果采样率不一致，服务端会返回 `warning`，但仍会继续处理。
- 一轮语音开始后，不能再次发送 `audio_start`，必须先发送 `audio_end` 结束当前轮。

## 4. 发送用户音频

`audio_start` 后，客户端直接发送二进制 WebSocket 消息作为音频块。

音频要求：

- 格式：raw PCM，不要 WAV 头。
- 编码：signed 16-bit little-endian PCM。
- 声道：mono。
- 采样率：通常为 `16000 Hz`。
- 分块：建议每块 20-40 ms；现有 `vclient` 使用 32 ms，也就是 16 kHz 下每块 512 个采样点、1024 字节。

发送顺序示例：

```text
JSON:  {"type":"audio_start","sample_rate":16000,"channels":1,"sample_width":2}
BYTES: pcm_chunk_1
BYTES: pcm_chunk_2
BYTES: pcm_chunk_3
...
JSON:  {"type":"audio_end"}
```

如果在没有 `audio_start` 的情况下发送二进制音频，服务端会返回 `error`。

## 5. 结束一轮用户语音

用户停止说话后，发送：

```json
{
  "type": "audio_end"
}
```

服务端会通知 ASR 结束输入，等待最终识别结果。如果 ASR 超时，服务端会返回 `asr_timeout`，并尽量使用最后一次识别文本继续后续流程。

## 6. 处理服务端事件

服务端返回两类消息：

- 文本消息：JSON 事件。
- 二进制消息：TTS 音频 PCM 数据，客户端应送入播放器。

常见 JSON 事件如下：

| 事件 | 含义 |
| --- | --- |
| `ready` | 连接就绪，告知 ASR/TTS 音频参数。 |
| `turn_started` | 服务端已创建新一轮语音，包含 `turn_id`。 |
| `asr_partial` | ASR 中间结果，包含 `turn_id`、`text`。 |
| `asr_final` | ASR 最终结果，包含 `turn_id`、`text`。 |
| `asr_timeout` | 等待 ASR 最终结果超时，包含 `text`、`timeout`。 |
| `llm_start` | LLM 开始生成回复。 |
| `llm_delta` | LLM 增量文本，包含 `text`。 |
| `tts_start` | TTS 开始输出音频，包含 `sample_rate`、`response_format` 和短文本预览。 |
| `tts_done` | TTS 音频输出结束，包含 `audio_bytes`、`elapsed`。 |
| `llm_done` | LLM 文本回复结束，包含完整 `text`。 |
| `turn_done` | 当前轮结束，包含 `turn_id` 和用户文本 `text`。 |
| `response_cancelled` | 当前回复被取消。 |
| `warning` | 非致命警告。 |
| `error` | 错误消息。 |
| `pong` | 对 `ping` 的回应。 |

TTS 音频块没有额外封装，收到二进制消息即可按 `ready.tts_sample_rate` 或 `tts_start.sample_rate` 播放。
服务端会先完成一轮 LLM 文本生成，再把完整回复一次性接入流式 TTS；不会再按标点或长度拆成多次 TTS 请求。

## 7. 打断正在播放的回复

如果客户端支持用户插话，可以在检测到用户开始新一轮语音时发送：

```json
{
  "type": "cancel_response",
  "reason": "barge_in"
}
```

服务端会取消正在生成或播放的上一轮回复，并返回：

```json
{
  "type": "response_cancelled",
  "turn_id": 1
}
```

客户端收到后应清空本地尚未播放的旧 TTS 缓冲，避免继续播报已取消的音频。随后按正常流程发送新一轮 `audio_start`、音频块和 `audio_end`。

## 8. 心跳

客户端可以发送：

```json
{
  "type": "ping"
}
```

服务端返回：

```json
{
  "type": "pong"
}
```

## 9. 最小 Python 客户端骨架

下面示例演示协议流程。`pcm_chunks()` 需要替换成实际的麦克风采集或 PCM 文件读取逻辑。

```python
import asyncio
import json

import websockets


def dumps(payload: dict) -> str:
    return json.dumps(payload, ensure_ascii=False, separators=(",", ":"))


async def pcm_chunks():
    # TODO: 这里替换为真实音频输入。
    # 每次 yield 一段 raw PCM16 little-endian mono 音频，例如 32 ms / 1024 bytes。
    if False:
        yield b""


async def receive_loop(ws):
    async for message in ws:
        if isinstance(message, bytes):
            # TODO: 把 TTS PCM16 音频写入播放器。
            print(f"[tts audio] {len(message)} bytes")
            continue

        payload = json.loads(message)
        message_type = payload.get("type")

        if message_type == "ready":
            print("[ready]", payload)
        elif message_type == "asr_partial":
            print("[asr partial]", payload.get("text", ""))
        elif message_type == "asr_final":
            print("[you]", payload.get("text", ""))
        elif message_type == "llm_delta":
            print(payload.get("text", ""), end="", flush=True)
        elif message_type == "llm_done":
            print()
        elif message_type == "error":
            print("[error]", payload.get("message", ""))
        else:
            print(f"[{message_type}]", payload)


async def send_one_turn(ws, sample_rate: int = 16000):
    await ws.send(
        dumps(
            {
                "type": "audio_start",
                "sample_rate": sample_rate,
                "channels": 1,
                "sample_width": 2,
            }
        )
    )

    async for chunk in pcm_chunks():
        await ws.send(chunk)

    await ws.send(dumps({"type": "audio_end"}))


async def main():
    uri = "ws://127.0.0.1:8765"
    async with websockets.connect(uri, max_size=None) as ws:
        receiver = asyncio.create_task(receive_loop(ws))
        await send_one_turn(ws)
        await receiver


if __name__ == "__main__":
    asyncio.run(main())
```

## 10. 客户端实现清单

实现一个完整客户端时，建议按这个顺序做：

1. 建立 WebSocket 连接，读取 `ready`，保存 ASR/TTS 音频参数。
2. 采集麦克风音频，并转换为 raw PCM16 little-endian mono。
3. 用 VAD 或按键控制说话边界。
4. 开始说话时发送 `audio_start`。
5. 说话过程中持续发送二进制 PCM 音频块。
6. 结束说话时发送 `audio_end`。
7. 后台持续接收 JSON 事件，展示 ASR 和 LLM 文本。
8. 后台持续接收二进制 TTS 音频，按 `tts_sample_rate` 播放。
9. 支持插话时，先发送 `cancel_response`，再开启新一轮语音。
10. 遇到 `warning`、`error`、`asr_timeout` 时记录日志并更新 UI 状态。

仓库里的 `vclient/` 已经实现了本地麦克风、VAD、插话打断和 TTS 播放，可以作为完整客户端参考。
