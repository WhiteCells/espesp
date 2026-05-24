# speaker_client

## 使用方式

先在电脑端启动音频推流服务：

```sh
cd server
python -m speaker_server input.wav --host 0.0.0.0 --port 8082 --path /audio --sample-rate 16000
```

ESP32 端选择模块：

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> speaker_client: Wi-Fi + WebSocket audio playback
```

需要配置：

```text
ESPESP Menu
  -> WiFi module
  -> Speaker module
  -> Speaker client module
```

`Speaker module` 里配置 I2S 功放的 BCLK、WS、DOUT 和采样率。`Speaker client module`
里把 URI 改成电脑局域网地址，例如：

```text
ws://192.168.1.23:8082/audio
```

不要在 ESP32 上使用 `127.0.0.1`，它指向 ESP32 自己。

## 协议

服务端先发送文本 JSON：

```json
{"type":"audio_start","format":"pcm_s16le","sample_rate_hz":16000,"channels":1,"sample_width_bits":16}
```

随后发送 WebSocket binary frame，内容是 signed 16-bit little-endian mono PCM。
结束时发送：

```json
{"type":"audio_end","frames":160000,"bytes":320000}
```

客户端只播放 `pcm_s16le`、单声道、16-bit、采样率等于
`CONFIG_ESPESP_SPK_SAMPLE_RATE_HZ` 的音频。格式不匹配会打印错误并丢弃二进制音频。

## 当前模块接口

### `esp_err_t speaker_client_run(void)`

连接 Wi-Fi，创建 I2S TX 通道，连接 `speaker_server` WebSocket，接收 PCM 流并写入 I2S。

## 注意事项

- 服务端目前支持 PCM WAV 输入；MP3/FLAC 需要先转成 WAV。
- WAV 采样率必须和 ESP32 `Speaker module` 采样率一致，默认都是 16000 Hz。
- 如果听到变速或失真，优先检查服务端日志里的输出格式和 ESP 串口日志里的 `audio_start`。
- WebSocket binary frame 可能被底层拆分，客户端会缓存奇数字节，避免 16-bit 样本错位。
