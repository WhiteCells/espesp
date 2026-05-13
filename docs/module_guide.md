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
  -> Speaker module
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
