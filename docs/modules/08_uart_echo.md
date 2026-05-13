# 08 uart_echo: UART 回显

## 模块概览

- `uart_driver_install()` 安装 UART 驱动。
- `uart_param_config()` 配置波特率和帧格式。
- `uart_set_pin()` 绑定 TX/RX 引脚。
- `uart_read_bytes()` 阻塞读取。
- `uart_write_bytes()` 写回数据。

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> 08 uart_echo
```

可调参数：

```text
ESPESP Menu
  -> UART echo module
```

## 源码位置

- `main/uart_echo/uart_echo.c`

## 当前模块接口参考

- `uart_echo_run()`：读取 UART 数据并写回。

## 常用接口说明

- `uart_config_t`：配置波特率、数据位、校验位、停止位和流控。
- `uart_driver_install()`：安装 UART 驱动并分配 RX/TX 缓冲。
- `uart_param_config()`：应用 `uart_config_t` 参数。
- `uart_set_pin()`：绑定 TX、RX、RTS、CTS 引脚。
- `uart_read_bytes()`：从 UART RX 缓冲读取数据，可设置超时。
- `uart_write_bytes()`：向 UART TX 写数据。
- `uart_flush_input()`：清空 RX 输入缓冲，调试异常数据时常用。

## 配置项

- `CONFIG_ESPESP_UART_PORT_NUM`：UART 控制器编号，默认使用 UART1。
- `CONFIG_ESPESP_UART_BAUD_RATE`：波特率。
- `CONFIG_ESPESP_UART_USE_CUSTOM_PINS`：是否使用 menuconfig 指定 TX/RX 引脚。
- `CONFIG_ESPESP_UART_TX_GPIO`：自定义 TX GPIO。
- `CONFIG_ESPESP_UART_RX_GPIO`：自定义 RX GPIO。

## 接线

默认配置：

- ESP TX GPIO17 -> USB-TTL RX
- ESP RX GPIO18 -> USB-TTL TX
- ESP GND -> USB-TTL GND

## 注意事项

- TX/RX 要交叉连接。
- UART0 常用于日志，外设连接建议 UART1。

## 扩展方向

- 改波特率为 9600。
- 关闭自定义引脚，观察默认 UART 引脚行为。
- 给收到的数据增加前缀再回显。
