# 模块使用参考

## 切换模块

```sh
idf.py menuconfig
```

进入：

```text
ESPESP Menu
  -> Module selector
```

选中一个模块后保存退出，再执行：

```sh
idf.py build flash monitor
```

## 常用 menuconfig 参数

LED：

```text
ESPESP Menu
  -> LED module
```

Wi-Fi：

```text
ESPESP Menu
  -> WiFi
```

HTTP、WebSocket 和 MQTT：

```text
ESPESP Menu
  -> LAN service
  -> HTTP client module
  -> HTTP server module
  -> HTTPS server module
  -> WebSocket server module
  -> WebSocket client module
  -> MQTT client module
  -> Speaker client module
  -> Voice client module
```

ADC：

```text
ESPESP Menu
  -> ADC reader module
```

UART：

```text
ESPESP Menu
  -> UART echo module
```

I2C：

```text
ESPESP Menu
  -> I2C scan module
```

音频和显示：

```text
ESPESP Menu
  -> Microphone module
  -> PCM stream module
  -> Speaker module
  -> Speaker client module
  -> Voice callback module
  -> Voice client module
  -> Display module
```

## 日志参考

所有模块启动时都会打印 banner，包含：

- 当前模块名称。
- 配套文档路径。
- 如何切换模块。

`app_main` 保留 `Hello world!` 输出，方便沿用基础 smoke test。

## 常见问题

编译时出现 `__FILE` 或 `_READ_WRITE_RETURN_TYPE` 冲突：

- 先执行 `. /home/cells/esp/v5.5.2/esp-idf/export.sh`。
- 再执行 `idf.py fullclean`。
- 最后重新执行 `idf.py build`。
- 这个问题通常来自旧 `build/` 缓存里的 libc 编译参数和当前 `sdkconfig` 不一致。

Wi-Fi 报 SSID 为空：

- 进入 `ESPESP Menu -> WiFi` 设置 SSID 和密码。

LED 不闪：

- 检查开发板 LED 引脚，不同板子可能不是 GPIO2。
- 有些板载 LED 是低电平点亮，看到日志翻转但灯相反是正常的。

ADC 值不变：

- 检查选择的是 ADC channel，不是 GPIO number。
- ESP32-S3 的 ADC1 channel 2 通常对应 GPIO3。
- 输入电压不要超过芯片允许范围。

I2C 扫不到设备：

- 确认 SDA/SCL 没接反。
- 确认模块供电和 GND 共地。
- I2C 需要上拉电阻，内部上拉只适合低速和短线场景。

UART 没回显：

- TX/RX 交叉连接。
- 波特率一致。
- UART0 常用于日志，外设连接建议 UART1。

I2S 麦克风或扬声器没有数据：

- 确认 BCLK、WS/LRCLK、DIN/DOUT 引脚和模块方向。
- 确认供电电压符合模块要求，并且 GND 共地。
- 麦克风的左右声道选择脚可能影响数据输出。

speaker_client 无声或失真：

- 先确认 `speaker` 正弦波模块能正常发声。
- 服务端 WAV 采样率必须等于 `CONFIG_ESPESP_SPK_SAMPLE_RATE_HZ`，默认 16000 Hz。
- ESP32 端 URI 要填电脑局域网 IP，例如 `ws://192.168.1.23:8082/audio`，不要填 `127.0.0.1`。
- 串口日志里的 `audio_start` 应显示 `format=pcm_s16le channels=1 bits=16`。

voice_client 连不上或无声：

- 先启动 `server/voice-server`，并确认电脑和 ESP32 在同一个局域网。
- ESP32 URI 要填 `ws://电脑局域网IP:8765/ws`，不要填 `127.0.0.1`。
- `Voice client module` 的输入采样率和 `server/voice-server/.env` 里的采样率最好一致。
- 服务端 TTS 要返回 `pcm`，串口日志应出现 `tts_start format=pcm sample_rate=...`。
- TTS 播放炸麦时，把 `Voice client module -> TTS playback volume percent` 降到 50 或 40；
  观察 `tts_end` 日志里的 `peak_in`、`peak_out` 和 `limited`。
- 默认启用 AEC，播放时麦克风仍保持上送；如果仍被扬声器干扰，优先调低 TTS 音量并检查 AEC 参数。

voice_callback 啸叫或扬声器声音进麦克风：

- 先降低 `Voice callback module -> Playback volume percent`，例如调到 10 到 15。
- 默认不要启用 `Enable acoustic echo cancellation (AEC)`；当前本地 callback 先靠低延迟队列和门控阻断回声闭环。
- 麦克风不要正对扬声器，二者尽量拉开距离，并确认共地。
- 仍有明显回灌时，增大 `Residual echo gate percent` 或 `Speaker active gate window in ms`。
- 近端声音也被压住时，降低 `Residual noise gate average amplitude` 或 `Residual echo gate percent`。

voice_callback 底噪明显：

- 先确认 `Enable microphone high-pass filter` 已开启。
- 低频轰鸣明显时，把 `Microphone high-pass alpha Q15` 从 31800 降到 31200 或 31000；声音变薄或发闷破碎则调回更大。
- 讲话时有沙沙底噪时，把 `Playback noise suppression floor amplitude` 从 0 逐步提到 80 或 120。
- 没讲话时底噪会被保持窗口放出来时，把 `Voice gate release minimum average amplitude` 提到 220 或 250。

voice_callback 声音失真：

- 先看日志里的 `mic_peak`、`input_gain_q15`、`input_limited` 和 `limited`。
- `input_limited` 持续增长说明麦克风输入过大，优先把 `Microphone sample right shift bits` 提到 13。
- `limited` 持续增长说明播放端被削顶，降低 `Playback volume percent` 或 `Playback soft limit percent`。
- 保持 `Playback noise suppression floor amplitude=0`；仍失真时把高通系数调回 31800 或临时关闭高通。

voice_callback 播放断续：

- 先看日志里的 `underflow`、`muted`、`passed`、`hold`、`tail` 和 `gate_gain_q15`。
- `underflow` 增长说明播放任务等不到采集帧，先确认 I2S 读写没有 timeout。
- `muted` 很快增长且 `hold/tail` 很少时，降低 `Residual noise gate average amplitude`。
- 字尾被切掉时，增大 `Voice gate tail time in ms` 或 `Voice gate tail gain percent`。
- 句中停顿被切掉时，增大 `Voice gate hold time in ms` 或 `Voice gate hold gain percent`。

PCM stream 录不到 WAV：

- UART 模式建议使用 UART1/UART2，不要和 monitor 共用 UART0。
- 电脑端 `--baud` 要和 `ESPESP_PCM_UART_BAUD_RATE` 一致。
- UDP 模式先启动 `python -m pcm_recorder udp out.wav`，再启动 ESP 模块。
- UDP host 要填电脑的局域网 IPv4 地址，不要填 `127.0.0.1`。

电脑访问 ESP32 LAN server 失败：

- 先看串口日志里的 `got ip`，电脑端要访问这个局域网 IP。
- 电脑和 ESP32 必须在同一个 Wi-Fi 或可互通的局域网。
- 不要用 `127.0.0.1`，那只表示电脑或设备自己。
- 有些路由器开启了客户端隔离，设备之间会互相访问不到。

HTTP/HTTPS 返回 401：

- 进入 `ESPESP Menu -> LAN service` 设置 Bearer token。
- 电脑端测试时带上 `--token <token>`。

HTTPS 启动失败：

- HTTPS server 不把私钥写进源码，需要先把 PEM 格式的证书和私钥写入 NVS。
- 默认 namespace 是 `https_srv`，key 是 `servercert` 和 `prvtkey`。
