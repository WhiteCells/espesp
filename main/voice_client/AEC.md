# voice_client AEC 说明

这份文档专门解释 `voice_client` 里的 AEC（Acoustic Echo Cancellation，声学回声消除）逻辑。

目标不是讲一套抽象的 AEC 教科书，而是把“当前这份代码到底做了什么、没做什么、为什么这样接、哪里是边界”讲清楚，方便后面继续维护。

相关代码：

- [voice_client.c](./voice_client.c)
- [voice_client_audio.c](./voice_client_audio.c)
- [voice_client_protocol.c](./voice_client_protocol.c)
- [voice_client_transport.c](./voice_client_transport.c)
- [voice_client_aec.c](./voice_client_aec.c)
- [voice_client_aec.h](./voice_client_aec.h)

## 1. 为什么这里需要 AEC

`voice_client` 是一个全双工语音链路：

1. ESP32 麦克风持续采集本地说话声音。
2. 音频发给 `voice-server` 做 ASR / LLM / TTS。
3. 服务端回来的 TTS 又从本地扬声器播放出来。

问题在于：扬声器放出来的 TTS 会再次被麦克风收进去。

如果不处理，就会出现这些现象：

- 服务端刚说完一句话，麦克风又把这句话重新送回去。
- ASR 会把设备自己播出来的声音也当成用户输入。
- 对话容易自激，出现“机器跟自己说话”的循环。

AEC 的任务就是：

- 把“扬声器参考信号”喂给自适应滤波器；
- 估计这段声音通过“功放 -> 喇叭 -> 空气 -> 麦克风”后会变成什么样；
- 再从麦克风采样里减掉这部分估计值。

这里的 AEC 只处理“回声抵消”这件事，不负责：

- 噪声抑制（NS）
- 自动增益（AGC）
- 双讲检测（DTD）
- 非线性残余回声抑制

## 2. 当前实现的整体位置

AEC 在 `voice_client` 中不是一个独立任务，而是嵌在已有的播放和采集路径里。

### 下行（TTS 播放）路径

```text
WebSocket binary TTS PCM
  -> TTS 数字音量/软限幅
  -> AEC reference feed
  -> I2S speaker
```

对应实现：

- `voice_client_write_audio_chunk()` 负责处理服务端 binary TTS 分片。
- `voice_client_write_processed_pcm_i2s()` 负责逐块处理 PCM。
- `voice_client_aec_feed_reference()` 把“实际要送去扬声器的 PCM”喂给 AEC。

### 上行（麦克风采集）路径

```text
I2S microphone int32
  -> int16 pcm_s16le
  -> AEC process
  -> WebSocket binary upstream
```

对应实现：

- `voice_client_run()` 主循环里先做 `voice_client_convert_sample()`。
- 然后调用 `voice_client_aec_process()`。
- 最后再 `voice_client_send_audio_frame()` 发给服务端。

这意味着：

- AEC 看到的参考信号，来自“将要播放到扬声器”的 TTS PCM。
- AEC 处理的目标信号，来自“已经采到的麦克风 PCM”。

## 3. 生命周期和状态切换

### 创建

在 `voice_client_run()` 启动时，如果打开了 `CONFIG_ESPESP_VOICE_CLIENT_AEC_ENABLED`，会调用：

```c
voice_client_aec_create(
    filter_len,
    step_size_x256,
    mic_sample_rate,
    spk_sample_rate,
    max_delay_ms);
```

创建阶段会分配：

- `filter_w`：NLMS 自适应 FIR 滤波器系数
- `ref_buf`：扬声器参考环形缓冲区

### TTS 开始

收到服务端 `tts_start` 后：

1. 先根据 `tts_start.sample_rate` 调整扬声器 I2S 时钟。
2. 再调用 `voice_client_aec_set_speaker_rate()` 更新 AEC 内部采样率比。
3. 然后调用 `voice_client_aec_playback_start()`。

`playback_start()` 会做这些事：

- `playback_active = true`
- `active = true`
- 清空参考缓冲读写状态：`ref_write_pos`、`ref_count`、`ref_phase`
- 清空尾音计数：`tail_remaining = 0`

注意：

- 它**不会清空 `filter_w`**。
- 也就是说，滤波器权重会跨多轮 TTS 会话保留，属于“warm start”。

这样做的好处是，如果硬件位置和声学路径没变，下一轮通常能更快收敛。
代价是：如果扬声器/麦克风位置突然变化，旧权重会先带来一个短暂的重新适应过程。

### TTS 进行中

只要 `playback_active = true`：

- 服务端回来的 TTS PCM 会持续喂进参考缓冲区；
- 麦克风主循环每一帧都会继续执行 AEC；
- 上行麦克风不会因为播放 TTS 而暂停。

### TTS 结束

收到 `tts_end`、连接断开或服务端错误时，会调用 `voice_client_aec_playback_end()`：

- `playback_active = false`
- `tail_remaining = ref_size`

这里不是立刻完全停掉 AEC，而是进入一个“尾音处理窗口”。

这样做是因为：

- `tts_end` 到达时，扬声器的机械余振、箱体/桌面反射、空气传播中的尾部能量，可能还在继续进入麦克风；
- 如果立刻关掉 AEC，最后这小段回声很容易漏过去。

### 尾音结束

在 `voice_client_aec_process()` 里：

- 如果已经不在播放中，而且 `tail_remaining == 0`，则 `active = false`；
- 之后麦克风数据直接透传，不再做 AEC。

## 4. `voice_client_aec_t` 里的字段是什么意思

当前核心结构在 `voice_client_aec.c`：

| 字段 | 作用 |
| --- | --- |
| `filter_w` | 自适应 FIR 滤波器系数，长度是 `filter_len` |
| `ref_buf` | 扬声器参考环形缓冲区，保存参考历史 |
| `filter_len` | 滤波器抽头数，也是实际建模的主要时间跨度 |
| `ref_size` | 参考缓冲总长度 |
| `ref_write_pos` | 参考缓冲写指针 |
| `ref_count` | 当前缓冲里已有多少有效参考样本 |
| `step_size_x256` | NLMS 步长，放大 256 倍存储 |
| `mic_sample_rate` | 麦克风采样率 |
| `spk_sample_rate` | 扬声器采样率 |
| `mic_per_spk` | “每 1 个扬声器样本，折算成多少个麦克风域样本” |
| `ref_phase` | 参考采样率折算时的相位累积器 |
| `tail_remaining` | 还要继续保留多少个麦克风域样本的尾音处理窗口 |
| `playback_active` | 当前是否仍在收到新的播放参考 |
| `active` | AEC 是否总体处于启用状态（包括尾音阶段） |
| `total_processed` | 累计处理过多少个麦克风样本 |
| `total_fed` | 累计喂入过多少个扬声器参考样本 |

## 5. 参考信号是怎么进入 AEC 的

### 5.1 为什么喂“处理后的 TTS PCM”

`voice_client_audio.c` 里，TTS PCM 在写扬声器之前会先做：

- 数字音量缩放
- 软限幅

然后才会：

1. 把这批处理后的样本放进 `ref_samples`
2. 调用 `voice_client_aec_feed_reference()`
3. 写入 I2S 扬声器

这样做的原因是：

- AEC 想要的不是“服务端原始下发字节”，而是“真正进入扬声器电声链路的数字信号”；
- 既然音量和限幅会改变波形，那就应该把处理后的版本作为参考。

### 5.2 采样率不同时怎么处理

当前实现没有用正式的重采样器，而是用了一个很轻量的“相位累积”办法，把扬声器参考折算到麦克风采样域。

核心变量：

- `mic_per_spk = mic_sample_rate / spk_sample_rate`
- `ref_phase`

每来一个扬声器样本：

1. `ref_phase += mic_per_spk`
2. 只要 `ref_phase >= 1.0`，就把当前这个扬声器样本写入一次 `ref_buf`
3. 每写一次，`ref_phase -= 1.0`

这相当于一个很粗的零阶保持方案：

- 如果 `mic_sample_rate > spk_sample_rate`，同一个扬声器样本可能会被复制多次。
- 如果 `mic_sample_rate < spk_sample_rate`，有些扬声器样本会被跳过。

它的优点是简单、占用低。

它的缺点也很明显：

- 不是高质量重采样；
- 频谱会失真；
- 采样率差异越大，AEC 的参考对齐越粗糙。

所以实践上最好让：

- 麦克风采样率
- 服务端 TTS 采样率
- 扬声器播放采样率

尽量接近，甚至直接一致。

## 6. 麦克风侧到底怎么“消回声”

这份实现是一个时域 NLMS（Normalized LMS）自适应 FIR 滤波器。

### 6.1 输入和输出

对每个麦克风样本：

- `d[n]`：当前麦克风样本（用户说话 + 房间噪声 + 扬声器回声）
- `x[n]`：当前时刻对应的扬声器参考向量
- `y_hat[n]`：滤波器估计出来的“回声”
- `e[n] = d[n] - y_hat[n]`：抵消后的结果

最终发给服务端的是 `e[n]`。

### 6.2 当前代码里的计算流程

在 `voice_client_aec_process()` 里，每个麦克风样本都做：

1. 取出当前参考窗口 `x`
2. 计算估计回声

```text
y_hat = sum(w[k] * x[k])
```

3. 计算误差

```text
e = d - y_hat
```

4. 计算参考能量

```text
power = epsilon + sum(x[k]^2)
```

5. 得到归一化步长

```text
mu_eff = mu / power
```

6. 更新滤波器系数

```text
w[k] = w[k] + mu_eff * e * x[k]
```

7. 把 `e` 截断回 `int16_t` 作为输出样本

`AEC_EPSILON` 的作用是防止 `power` 太小时除以 0。

### 6.3 现在这一版是怎么按时间推进参考窗的

这里有一个实现细节很重要：

- AEC 处理的是“整帧麦克风数据”；
- 但参考缓冲是一个连续的时间环。

当前实现会把“这一帧最老的麦克风样本”对齐到“较老的参考位置”，
再随着 `n` 增长，让后面的麦克风样本逐步对齐到更新的参考位置。

也就是说：

- 同一帧里的每个麦克风样本，并不是都拿同一段参考窗；
- 参考位置会随着样本索引向前推进。

这比“整帧反复用同一个最新参考窗”更接近真实时间关系，也更符合 FIR 参考向量的直觉。

## 7. `filter_len`、`max_delay_ms`、`frame_ms` 各自到底影响什么

这三个参数很容易混。

### 7.1 `AEC filter length`

这是最关键的 AEC 参数。

它决定：

- FIR 滤波器有多少个抽头；
- 一次能建模多长的回声路径。

例如：

- 16 kHz 下，256 taps 约等于 16 ms
- 48 kHz 下，256 taps 约等于 5.3 ms

所以如果你的系统是 48 kHz，而真实声学延迟有十几毫秒甚至几十毫秒，`256 taps` 很可能偏短。

### 7.2 `AEC max delay`

当前实现里：

```text
ref_size = filter_len + max_delay_samples + 512
```

因此它主要影响：

- 参考历史缓冲区长度
- `tts_end` 之后尾音阶段能持续多久
- 采样率折算和分帧推进时能保留多少旧参考

但要特别注意：

**当前实现不会自动搜索“最佳延迟对齐点”。**

也就是说：

- 把 `AEC max delay` 从 50ms 调到 200ms，
- 不等于滤波器就能突然覆盖 200ms 的真实回声延迟。

真正决定“当前样本能建模多长回声路径”的，还是 `filter_len`。

`max_delay_ms` 更像是“保留多少参考历史”和“尾音窗口留多长”，不是一个自动延迟估计器。

### 7.3 `Microphone frame duration`

麦克风是按 frame 批量处理的，例如默认 40 ms。

它影响的是：

- 一次 `voice_client_aec_process()` 要处理多少样本；
- AEC 延迟和 CPU 峰值；
- 每帧内部“参考位置如何随样本推进”的跨度。

frame 过大时：

- 单次计算量会更大；
- 实时性更差；
- AEC 调参与问题定位都更笨重。

所以不要把 frame 拉得太大。

## 8. 当前实现的优点和边界

### 优点

- 结构简单，容易读懂和调试
- 不需要额外任务或复杂 DSP 依赖
- 能把参考信号直接接到实际播放路径上
- 支持麦克风和扬声器采样率不完全一致
- 有尾音窗口，不会在 `tts_end` 一瞬间硬停

### 边界和限制

1. **没有显式延迟估计**

   当前不会在大范围参考历史里自动搜索最佳对齐点。

2. **没有高质量重采样**

   只是相位累积 + 样本复制/跳过。

3. **没有双讲检测**

   用户和扬声器同时说话时，NLMS 仍然会更新权重，可能导致收敛变慢或短时失真。

4. **没有残余回声抑制**

   线性 FIR 建模不掉的那部分残差，目前没有后级非线性压制。

5. **没有噪声抑制 / AGC**

   环境噪声、麦克风底噪、增益问题都不归 AEC 处理。

6. **CPU 开销与 `filter_len` 成正比**

   当前是时域实现，每个麦克风样本都要做两次 `filter_len` 量级的遍历
   （一次估计、一次更新），所以 `filter_len` 不能无脑拉很大。

7. **参考信号是数字播放前信号，不是“真实声场测量值”**

   这本来就是 AEC 的常见做法，但意味着功放、喇叭非线性、机械失真、房间反射这些现实因素，
   只能靠自适应 FIR 去近似，无法完全精确重建。

## 9. 调参建议

### 现象：回声仍然明显

优先顺序：

1. 先确认扬声器采样率和麦克风采样率别差太远
2. 再增大 `AEC filter length`
3. 再确认麦克风增益是否过高
4. 再看喇叭是否过爆、失真太重

`AEC max delay` 可以增大，但主要帮助参考历史和尾音窗口，不是第一优先级。

### 现象：用户说话时声音发飘、失真、像被“抽空”

优先尝试：

1. 降低 `AEC step size`
2. 降低播放音量，减少强失真
3. 检查麦克风离扬声器是否太近

### 现象：刚开始播报时前几百毫秒效果不稳

这通常是滤波器在重新适应：

- 播放开始时参考历史会清空；
- 权重虽然保留，但新一轮信号仍要再收敛一下。

### 现象：CPU 吃紧

可尝试：

1. 降低 `AEC filter length`
2. 降低输入采样率
3. 缩短 frame，但不要极端

## 10. 日志怎么看

AEC 自己会打几类日志：

- `created: ...`
  - 看创建参数是否符合预期
- `speaker rate changed: ...`
  - 看 TTS 采样率切换是否发生
- `destroy: processed=... fed=...`
  - 看整个生命周期里一共处理了多少麦克风样本、喂了多少参考样本

外围模块也会打和 AEC 强相关的日志：

- `tts_start ... sample_rate=...`
- `tts_end ... peak_in / peak_out / limited / elapsed_ms`

这些日志能帮助判断：

- 播放路径是否真的活跃
- 参考样本是否在持续进入 AEC
- 播放音量是否已经接近削顶

## 11. 如果后面要继续增强，优先做什么

如果未来要把这套 AEC 从“够用的轻量实现”继续往前推，最值得加的通常是：

1. 显式延迟估计或可配置延迟偏移
2. 更像样的参考重采样
3. 双讲检测（DTD）
4. 残余回声抑制
5. 更低复杂度的块处理 / 频域实现

## 12. 一句话总结

当前 `voice_client` 的 AEC 是一套“嵌在播放/采集路径里的轻量时域 NLMS 回声抵消器”：

- 参考来自实际要播放的 TTS PCM；
- 目标是播放期间仍保持麦克风上行；
- 通过自适应 FIR 从麦克风里减掉估计回声；
- 已经具备基本的全双工能力；
- 但还没有延迟估计、正式重采样、双讲检测和残余回声抑制。

如果把它看成“一个可维护、可继续增强的第一版 AEC”，这个定位是准确的。
