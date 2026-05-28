# voice_client

## 使用方式

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
- 默认启用半双工：播放 TTS 时会暂停继续上送麦克风音频，避免扬声器声音立刻被 ASR 再次识别。
- 如果语音太小或破音，调整 `Voice client module -> Microphone sample right shift bits`。
- 如果完全无声，先分别运行 `microphone`、`pcm_stream` 和 `speaker` 模块确认硬件链路。
