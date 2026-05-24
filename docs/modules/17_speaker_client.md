# 17 speaker_client: WebSocket 音频播放客户端

## 模块概览

- `speaker_server` 在电脑端读取本地 PCM WAV 文件。
- 服务端先发送 `audio_start` JSON，再连续发送 binary PCM chunk，最后发送 `audio_end` JSON。
- ESP32 `speaker_client` 通过 `esp_websocket_client` 接收音频流。
- 客户端把 `pcm_s16le` 单声道 16-bit PCM 写入 I2S TX 数字功放。

## 使用方式

电脑端：

```sh
cd server
python -m speaker_server input.wav --host 0.0.0.0 --port 8082 --path /audio --sample-rate 16000
```

ESP32：

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> speaker_client: Wi-Fi + WebSocket audio playback
```

配置项：

```text
ESPESP Menu
  -> WiFi module
  -> Speaker module
  -> Speaker client module
```

## 源码位置

- `main/speaker_client/speaker_client.c`
- `main/speaker_client/speaker_client.h`
- `main/speaker_client/README.md`
- `server/speaker_server/server.py`

## 协议格式

开始帧是文本 JSON：

```json
{"type":"audio_start","format":"pcm_s16le","sample_rate_hz":16000,"channels":1,"sample_width_bits":16,"chunk_bytes":1024}
```

音频帧是 WebSocket binary frame，payload 为 signed 16-bit little-endian mono PCM。

结束帧是文本 JSON：

```json
{"type":"audio_end","frames":160000,"bytes":320000}
```

## 注意事项

- 服务端不做重采样；WAV 采样率必须和 ESP32 speaker 采样率一致。
- 服务端可把多声道 PCM WAV 下混成单声道，并转换常见 PCM 位宽到 16-bit。
- 客户端会拒绝采样率、通道数或位宽不匹配的音频，避免错速和失真。
- WebSocket 分片可能拆开 16-bit 样本，客户端会缓存单个尾字节再与下一片拼接。
