# 19 voice_callback: 全双工本地语音回放

## 模块概览

- ESP32 同时打开 I2S 麦克风 RX 和 I2S 扬声器 TX。
- 麦克风采样转换成 `pcm_s16le` 单声道 PCM。
- 采集任务和播放任务通过预分配帧队列连接。
- 默认启用轻量 AEC，再用残余回声门控兜底，减少扬声器声音再次进入麦克风形成闭环。
- 播放队列只保留最新短缓冲，优先降低监听延迟和回授风险。
- 不依赖 Wi-Fi，适合验证本地全双工音频链路。

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> voice_callback: full-duplex microphone monitor with echo guard
```

配置项：

```text
ESPESP Menu
  -> Microphone module
  -> Speaker module
  -> Voice callback module
```

## 源码位置

- `main/voice_callback/voice_callback.c`
- `main/voice_callback/voice_callback.h`
- `main/voice_callback/voice_callback_aec.c`
- `main/voice_callback/voice_callback_aec.h`
- `main/voice_callback/README.md`

## 数据流

```text
I2S microphone
  -> int32 sample -> input limiter -> pcm_s16le
  -> high-pass/DC blocker
  -> fixed-point NLMS AEC
  -> residual echo/noise gate
  -> latest-frame queue
  -> playback volume + soft limit
  -> AEC reference feed
  -> I2S speaker
```

## 全双工与防干扰

这个模块不是停止采集的半双工策略。扬声器播放时，麦克风任务仍然持续采集，扬声器任务也持续写 I2S。为了避免麦克风把扬声器声音直接再次采进去，模块默认使用 AEC、低延迟队列和带迟滞的残余门控：

- 播放队列只保留很短的最新帧缓冲，旧帧会丢弃，避免队列积压造成延迟回声。
- 麦克风 32-bit 转 16-bit 前有线性输入限幅，避免先削顶再降音量造成不可恢复的破音。
- 麦克风 PCM 先经过可选高通/DC blocker，削掉直流偏移和低频轰鸣。
- AEC 使用实际写给扬声器的 PCM 作为参考；参考不足时直接透传，不会再把麦克风帧清零。
- AEC 更新带参考能量检查和简单双讲保护，降低近端人声被滤波器学掉的概率；实现使用定点运算，降低采集任务 CPU 占用。
- 采集任务会周期性让出一个 FreeRTOS tick，避免连续满负载处理时饿死 CPU1 idle task 触发 task WDT。
- AEC 后的残余门控只处理低能底噪和扬声器刚播放后的残余回灌。
- 语音通过门控后会进入保持窗口，窗口内使用较低释放门限和保持增益，减少轻声和句中停顿被切断；增益在一帧内平滑变化，避免硬切。
- 播放队列短暂空洞时会主动写入静音帧，避免 I2S TX 断流；日志里的 `underflow` 会记录这种情况。
- 扬声器刚播放后的保护窗口内，AEC-cleaned 麦克风帧需要强于衰减后的扬声器参考门限才会继续播放。
- 低于噪声门限，或疑似扬声器回灌的帧，会被静音或平滑淡出。

默认播放音量较低，并有软限幅，目的是减少啸叫和削顶。

## 主要配置项

- `CONFIG_ESPESP_VOICE_CALLBACK_SAMPLE_RATE_HZ`：麦克风和扬声器共用采样率。
- `CONFIG_ESPESP_VOICE_CALLBACK_FRAME_SAMPLES`：每帧样本数，默认 160，16 kHz 下约 10 ms。
- `CONFIG_ESPESP_VOICE_CALLBACK_QUEUE_LENGTH`：预分配帧数量；播放路径只保留短的最新帧缓冲。
- `CONFIG_ESPESP_VOICE_CALLBACK_MIC_SAMPLE_SHIFT_BITS`：麦克风 32-bit 样本转 16-bit PCM 的右移位数。
- `CONFIG_ESPESP_VOICE_CALLBACK_MIC_INPUT_LIMIT_PERCENT`：麦克风输入限幅余量，默认 85，避免转换阶段削顶。
- `CONFIG_ESPESP_VOICE_CALLBACK_PLAYBACK_VOLUME_PERCENT`：播放数字音量。
- `CONFIG_ESPESP_VOICE_CALLBACK_PLAYBACK_LIMIT_PERCENT`：播放软限幅。
- `CONFIG_ESPESP_VOICE_CALLBACK_AEC_ENABLED`：是否启用 AEC，默认开启。
- `CONFIG_ESPESP_VOICE_CALLBACK_AEC_FILTER_LEN`：AEC 滤波器长度，默认 128 taps。
- `CONFIG_ESPESP_VOICE_CALLBACK_AEC_STEP_SIZE_X256`：AEC 收敛步长，默认 32/256。
- `CONFIG_ESPESP_VOICE_CALLBACK_AEC_REFERENCE_DELAY_MS`：AEC 参考延迟，默认 30 ms。
- `CONFIG_ESPESP_VOICE_CALLBACK_NOISE_GATE_AVG_ABS`：平均幅度低于该门限时静音。
- `CONFIG_ESPESP_VOICE_CALLBACK_ECHO_GATE_PERCENT`：扬声器刚播放后残余回声门限比例。
- `CONFIG_ESPESP_VOICE_CALLBACK_SPEAKER_ACTIVE_TIMEOUT_MS`：扬声器播放后的严格门控窗口。
- `CONFIG_ESPESP_VOICE_CALLBACK_GATE_HOLD_MS`：语音通过门控后的保持时间。
- `CONFIG_ESPESP_VOICE_CALLBACK_GATE_RELEASE_PERCENT`：保持窗口内的释放门限比例。
- `CONFIG_ESPESP_VOICE_CALLBACK_GATE_RELEASE_MIN_AVG_ABS`：保持窗口内仍需达到的最低平均幅度，避免底噪续开门控。
- `CONFIG_ESPESP_VOICE_CALLBACK_GATE_HOLD_GAIN_PERCENT`：保持窗口内弱语音帧的播放增益，默认 70。
- `CONFIG_ESPESP_VOICE_CALLBACK_HIGHPASS_FILTER_ENABLED`：是否启用麦克风高通/DC blocker。
- `CONFIG_ESPESP_VOICE_CALLBACK_HIGHPASS_ALPHA_Q15`：高通滤波系数，默认 31200；数值越小，低频抑制越强。

## 接线

麦克风使用 `Microphone module` 的引脚：

- BCLK -> `CONFIG_ESPESP_MIC_BCLK_GPIO`
- WS/LRCLK -> `CONFIG_ESPESP_MIC_WS_GPIO`
- DATA/DOUT -> `CONFIG_ESPESP_MIC_DIN_GPIO`

扬声器功放使用 `Speaker module` 的引脚：

- BCLK -> `CONFIG_ESPESP_SPK_BCLK_GPIO`
- WS/LRCLK -> `CONFIG_ESPESP_SPK_WS_GPIO`
- DIN -> `CONFIG_ESPESP_SPK_DOUT_GPIO`

麦克风、功放和 ESP32 必须共地。

## 调试建议

- 先运行 `microphone` 看幅度日志，再运行 `speaker` 听正弦波，确认硬件正常。
- 听到尖锐啸叫或回音噪声时，先降低 `Playback volume percent`，例如 10 到 15。
- 声音破音或失真时，先看日志里的 `mic_peak`、`input_gain_q15`、`input_limited` 和 `limited`；`input_limited` 增长说明麦克风输入过大，优先把 `Microphone sample right shift bits` 提到 13。
- 仍失真时，把 `Microphone high-pass alpha Q15` 调回 31800 或临时关闭高通。
- 有明显底噪时，先确认 `Enable microphone high-pass filter` 开启；当前默认高通为 31200，可继续降到 31000。
- 低频轰鸣明显时，把 `Microphone high-pass alpha Q15` 从 31200 降到 31000；声音变薄或发闷破碎则调回更大。
- 播放断续时，先看日志里的 `underflow`、`muted`、`hold` 和 `gate_gain_q15`；`underflow` 增长说明采集帧供不上，`muted` 很快增长说明门控过严。
- 门控过严时，先降低 `Residual noise gate average amplitude`；再增大 `Voice gate hold time in ms` 或 `Voice gate hold gain percent`。
- 仍然有扬声器回灌时，增大 `Residual echo gate percent` 或 `Speaker active gate window in ms`。
- 麦克风声音过小，降低 `Microphone sample right shift bits`；声音破音则提高它。
- 启用 AEC 后如仍出现 WDT、声音抽动或失真，先降低 `AEC filter length` 到 64；仍不稳定再降低 `AEC step size` 或临时关闭 AEC 排查。

## 注意事项

- 软件门控不能完全替代声学结构设计。麦克风不要正对扬声器，扬声器音量也不要过大。
- 默认参数优先保证不回音、不啸叫；如果需要更像监听扩声，可以在现场确认稳定后逐步降低门控、提高音量。
- 本模块没有自动增益或高阶非线性回声抑制。
- 如果只想单独验证录音或播放，优先使用 `microphone`、`pcm_stream` 和 `speaker`。
