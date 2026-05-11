# 08 uart_echo: UART 回显

## 学什么

- `uart_driver_install()` 安装 UART 驱动。
- `uart_param_config()` 配置波特率和帧格式。
- `uart_set_pin()` 绑定 TX/RX 引脚。
- `uart_read_bytes()` 阻塞读取。
- `uart_write_bytes()` 写回数据。

## 怎么运行

```text
idf.py menuconfig
  -> Case2 ESP Learning
  -> Demo selector
  -> 08 uart_echo
```

可调参数：

```text
Case2 ESP Learning
  -> UART echo demo
```

## 看哪段代码

- `main/demos/uart_echo_demo.c`

## 接线

默认配置：

- ESP TX GPIO17 -> USB-TTL RX
- ESP RX GPIO18 -> USB-TTL TX
- ESP GND -> USB-TTL GND

## 练习

- 改波特率为 9600。
- 关闭自定义引脚，观察默认 UART 引脚行为。
- 给收到的数据增加前缀再回显。
