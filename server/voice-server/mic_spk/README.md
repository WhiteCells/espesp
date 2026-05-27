# Mic Speaker

实现 python ws 客户端，获取本地设备麦克风，将麦克风的音频pcm推送到服务端，从服务端接收音频数据进行播放。

## 运行

先启动 voice-server：

```bash
uv run voice-server --host 0.0.0.0 --port 8765
```

本客户端依赖 `sounddevice` 访问本地麦克风和扬声器。可以临时安装运行：

```bash
uv run python mic_spk/client.py
```

默认连接 `ws://127.0.0.1:8765/ws`，以 `48000 Hz / mono / pcm_s16le` 从默认麦克风采集音频，
并播放服务端返回的 `pcm` TTS 音频。

## 常用命令

查看本机音频设备：

```bash
uv run python mic_spk/client.py --list-devices
```

指定服务地址、输入输出设备和采样率：

```bash
uv run --with sounddevice python mic_spk/client.py \
  --url ws://127.0.0.1:8765/ws \
  --input-device 0 \
  --output-device 1 \
  --input-sample-rate 48000 \
  --input-channels 1 \
  --output-channels 1 \
  --segment-seconds 3
```

运行时终端控制：

- 直接按 `Enter`：发送 `commit`，提交当前语音段。
- 输入 `p` 后按 `Enter`：发送 `ping`。
- 输入 `q` 后按 `Enter`：发送 `stop` 并退出。

按 `Ctrl+C` 也会提交当前缓冲并停止会话。

## 协议行为

客户端连接后会发送：

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
    "language": "zh"
  },
  "tts": {
    "response_format": "pcm"
  }
}
```

之后把麦克风数据按 `--chunk-ms` 切成 WebSocket binary frames 发给服务端。
收到服务端 `tts_start` 后，客户端会按事件里的 `sample_rate` 打开扬声器并播放后续二进制音频帧。
实时播放只支持 `--tts-format pcm`，其他编码格式会打印收到的字节数但不会解码播放。
