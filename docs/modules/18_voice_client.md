# 18 voice_client: 实时语音对话客户端

## 模块概览

- ESP32 通过 I2S 麦克风采集音频。
- 客户端把麦克风数据转换成 `pcm_s16le` 单声道 raw PCM。
- WebSocket 连接到 `server/voice-server` 的 `/ws` 接口。
- 客户端先发送 `start` JSON，再持续发送 binary PCM chunk。
- 服务端返回 `transcript`、`tts_start`、二进制 TTS PCM、`tts_end` 等事件。
- ESP32 将返回的 PCM 音频写入 I2S 扬声器播放。

## 使用方式

电脑端：

```sh
cd server/voice-server
uv run voice-server --host 0.0.0.0 --port 8765
```

ESP32：

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> voice_client: microphone -> voice-server -> speaker
```

配置项：

```text
ESPESP Menu
  -> WiFi module
  -> Microphone module
  -> Speaker module
  -> Voice client module
```

`Voice client module -> voice-server WebSocket URI` 填电脑的局域网 IP，例如：

```text
ws://192.168.1.23:8765/ws
```

## 源码位置

- `main/voice_client/voice_client.c`
- `main/voice_client/voice_client.h`
- `main/voice_client/README.md`
- `server/voice-server/src/voice_server/server/app.py`

## 音频格式

上行音频：

- encoding：`pcm_s16le`
- container：`raw`
- channels：`1`
- sample_rate：来自 `CONFIG_ESPESP_VOICE_CLIENT_INPUT_SAMPLE_RATE_HZ`
- chunk：默认 40 ms，每帧约 `sample_rate * 0.04 * 2` 字节

下行音频：

- `tts_start.response_format` 必须是 `pcm`
- `tts_start.sample_rate` 用于配置扬声器 I2S 时钟
- binary frame payload 是 signed 16-bit little-endian mono PCM
- 写入 I2S 前会做 TTS 播放音量衰减和软限幅，避免满幅 PCM 直接打爆小功放/小喇叭。

## 注意事项

- ESP32 端不能用 `127.0.0.1` 访问电脑服务，要使用电脑局域网 IP。
- 当前客户端不做重采样，也不解码压缩音频；服务端应返回 PCM。
- 默认开启 `Pause upstream microphone while TTS is playing`，让设备按半双工方式运行。
- 服务端 `.env` 中的 `ASR_SAMPLE_RATE`、`TTS_SAMPLE_RATE` 和 ESP 端配置最好保持一致。
- 如果 TTS 播放炸麦，先把 `Voice client module -> TTS playback volume percent` 降到 50 或 40；
  串口日志里的 `peak_in` 接近 32768 或 `limited` 不为 0 表示音频峰值余量不足。
