# voice_callback

`voice_callback` 是一个本地全双工语音回放模块。它用于把麦克风采到的近端声音播放到扬声器，同时避免扬声器声音被麦克风再次采到后形成回音闭环：

```text
I2S microphone -> input limiter -> pcm_s16le -> high-pass/DC blocker
  -> echo/noise guard -> optional noise floor attenuation -> latest-frame queue -> volume/limit
  -> I2S speaker
```

它不依赖 Wi-Fi，不连接服务端。模块启动后会同时运行两个 FreeRTOS 任务：

- `vc_capture`：持续读取 I2S 麦克风，转换为 16-bit 单声道 PCM，执行回声/噪声门控。
- `vc_playback`：持续写 I2S 功放。通过门控的帧会播放；疑似扬声器回灌或低能噪声的帧会写静音，避免再次放大。

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> voice_callback: full-duplex microphone monitor with echo guard
```

需要配置：

```text
ESPESP Menu
  -> Microphone module
  -> Speaker module
  -> Voice callback module
```

## 当前模块接口

### `esp_err_t voice_callback_run(void)`

创建 I2S RX/TX 通道、帧队列，并启动采集和播放任务。函数成功后立即返回，两个任务会长期运行。

## 防回灌行为

- 麦克风 RX 和扬声器 TX 同时运行，麦克风路径不会因为扬声器播放而暂停。
- 播放队列只保留很短的最新帧缓冲，旧帧会丢弃，避免本地监听延迟堆积成回声。
- 麦克风 32-bit 转 16-bit 前有线性输入限幅，避免先削顶再降音量造成不可恢复的破音。
- 麦克风 PCM 先经过可选高通/DC blocker，削掉直流偏移和低频轰鸣。
- 语音通过门控后会进入保持窗口，窗口内使用较低释放门限和保持增益，减少轻声和句中停顿被切断。
- 保持窗口后还有低增益尾音窗口，门控开关会在一帧内平滑淡入淡出，避免整帧硬切。
- 播放队列短暂空洞时会主动写入静音帧，避免 I2S TX 断流；日志里的 `underflow` 会记录这种情况。
- 扬声器刚播放后的保护窗口内，麦克风帧需要强于衰减后的扬声器参考门限才会继续播放。
- 通过门控的帧可选做整帧小信号衰减；默认关闭，优先保证人声不失真。
- 疑似扬声器回灌或低能噪声的帧会被静音或平滑淡出，但 I2S 播放任务仍持续写入。
- `Enable acoustic echo cancellation (AEC)` 是可选实验项，默认关闭；当前业务默认依赖低延迟队列和强门控来保证不回音。

## 调参顺序

1. 先分别运行 `microphone` 和 `speaker`，确认硬件链路正常。
2. 播放容易啸叫时，先降低 `Playback volume percent`，例如 10 到 15。
3. 声音破音或失真时，先看日志里的 `mic_peak`、`input_gain_q15`、`input_limited` 和 `limited`；`input_limited` 增长说明麦克风输入过大，优先把 `Microphone sample right shift bits` 提到 13。
4. 仍失真时，把 `Playback noise suppression floor amplitude` 保持为 0，并把 `Microphone high-pass alpha Q15` 调回 31800 或临时关闭高通。
5. 有明显底噪时，先确认 `Enable microphone high-pass filter` 开启；再把 `Voice gate release minimum average amplitude` 提到 220 或 250。
6. 讲话时仍有沙沙底噪时，再把 `Playback noise suppression floor amplitude` 从 0 逐步提到 80 或 120。
7. 低频轰鸣明显时，把 `Microphone high-pass alpha Q15` 从 31800 降到 31200 或 31000；声音变薄或发闷破碎则调回更大。
8. 播放断续时，先看日志里的 `underflow`、`muted`、`hold`、`tail` 和 `gate_gain_q15`；`underflow` 增长说明采集帧供不上，`muted` 很快增长说明门控过严。
9. 门控过严时，先降低 `Residual noise gate average amplitude`；再增大 `Voice gate hold time in ms`、`Voice gate hold gain percent`、`Voice gate tail time in ms` 或 `Voice gate tail gain percent`。
10. 仍能听到明显扬声器回灌时，增大 `Residual echo gate percent` 或 `Speaker active gate window in ms`。
11. 麦克风声音太小或破音时，调 `Microphone sample right shift bits`。
12. 只有在 CPU 余量确认足够时再启用 AEC；启用后如出现 WDT、声音抽动或失真，先关闭 AEC。

## 边界

- 软件门控不能替代结构设计。麦克风和扬声器仍应尽量拉开距离，并避免把扬声器正对麦克风。
- 这是本地监听模块。由于麦克风内容会被实时播放，开很大音量时仍可能产生物理声学反馈；默认参数优先保证不回音、不啸叫。
- 本模块没有自动增益、双讲检测或高阶非线性回声抑制。
