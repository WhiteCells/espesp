# Demo 操作说明

## 切换 demo

```sh
idf.py menuconfig
```

进入：

```text
Case2 ESP Learning
  -> Demo selector
```

选中一个 demo 后保存退出，再执行：

```sh
idf.py build flash monitor
```

## 常用 menuconfig 参数

GPIO blink：

```text
Case2 ESP Learning
  -> GPIO blink demo
```

Wi-Fi：

```text
Case2 ESP Learning
  -> WiFi
```

ADC：

```text
Case2 ESP Learning
  -> ADC oneshot demo
```

UART：

```text
Case2 ESP Learning
  -> UART echo demo
```

I2C：

```text
Case2 ESP Learning
  -> I2C scan demo
```

## 观察日志

所有 demo 启动时都会打印 banner，包含：

- 当前 demo 名称。
- 配套文档路径。
- 如何切换 demo。

`system_info` 还会输出 `Hello world!`，兼容原来的基础 smoke test。

## 常见问题

编译时出现 `__FILE` 或 `_READ_WRITE_RETURN_TYPE` 冲突：

- 先执行 `. /home/cells/esp/v5.5.2/esp-idf/export.sh`。
- 再执行 `idf.py fullclean`。
- 最后重新执行 `idf.py build`。
- 这个问题通常来自旧 `build/` 缓存里的 libc 编译参数和当前 `sdkconfig` 不一致。

Wi-Fi 报 SSID 为空：

- 进入 `Case2 ESP Learning -> WiFi` 设置 SSID 和密码。

GPIO 不闪：

- 检查开发板 LED 引脚，不同板子可能不是 GPIO2。
- 有些板载 LED 是低电平点亮，看到日志翻转但灯相反是正常的。

ADC 值不变：

- 检查选择的是 ADC channel，不是 GPIO number。
- ESP32-S3 的 ADC1 channel 2 通常对应 GPIO3。
- 输入电压不要超过芯片允许范围。

I2C 扫不到设备：

- 确认 SDA/SCL 没接反。
- 确认模块供电和 GND 共地。
- I2C 需要上拉电阻，内部上拉只适合低速和短线学习。

UART 没回显：

- TX/RX 交叉连接。
- 波特率一致。
- UART0 常用于日志，外设学习建议 UART1。
