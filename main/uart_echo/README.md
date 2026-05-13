# uart_echo

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> uart_echo: UART read/write
```

可调参数：

```text
ESPESP Menu
  -> UART echo module
```

然后执行：

```sh
idf.py build flash monitor
```

## 当前模块已有接口

### `esp_err_t uart_echo_run(void)`

安装 UART 驱动，配置串口参数和引脚，收到数据后原样写回。

参数：无。UART 号、波特率和引脚来自 Kconfig。

返回值：

- 正常情况下不会返回。
- UART 初始化、读写失败时会被 `ESP_ERROR_CHECK()` 捕获。

## 常用接口说明

### `uart_config_t`

UART 参数配置。

本模块使用字段：

- `baud_rate`：波特率，来自 `CONFIG_ESPESP_UART_BAUD_RATE`。
- `data_bits`：数据位，本模块使用 `UART_DATA_8_BITS`。
- `parity`：校验位，本模块使用 `UART_PARITY_DISABLE`。
- `stop_bits`：停止位，本模块使用 `UART_STOP_BITS_1`。
- `flow_ctrl`：硬件流控，本模块使用 `UART_HW_FLOWCTRL_DISABLE`。
- `source_clk`：UART 时钟源，本模块使用 `UART_SCLK_DEFAULT`。

### `uart_driver_install(uart_num, rx_buffer_size, tx_buffer_size, queue_size, uart_queue, intr_alloc_flags)`

安装 UART 驱动。

参数：

- `uart_num`：UART 端口号，本模块来自 `CONFIG_ESPESP_UART_PORT_NUM`。
- `rx_buffer_size`：RX ring buffer 大小，本模块为 `RX_BUF_SIZE * 2`。
- `tx_buffer_size`：TX buffer 大小，本模块为 `0`，表示发送阻塞写。
- `queue_size`：UART 事件队列长度，本模块为 `0`，不使用事件队列。
- `uart_queue`：输出事件队列句柄，本模块传 `NULL`。
- `intr_alloc_flags`：中断分配标志，本模块为 `0`。

### `uart_param_config(uart_num, &uart_config)`

应用串口参数。

参数：

- `uart_num`：UART 端口。
- `uart_config`：参数结构体。

### `uart_set_pin(uart_num, tx_io_num, rx_io_num, rts_io_num, cts_io_num)`

绑定 UART 引脚。

参数：

- `tx_io_num`：TX GPIO，本模块来自 `CONFIG_ESPESP_UART_TX_GPIO`。
- `rx_io_num`：RX GPIO，本模块来自 `CONFIG_ESPESP_UART_RX_GPIO`。
- `rts_io_num`：RTS GPIO，本模块不使用，传 `UART_PIN_NO_CHANGE`。
- `cts_io_num`：CTS GPIO，本模块不使用，传 `UART_PIN_NO_CHANGE`。

### `uart_read_bytes()` 和 `uart_write_bytes()`

`uart_read_bytes(port, data, sizeof(data), pdMS_TO_TICKS(1000))`：

- `data`：接收缓冲区。
- `sizeof(data)`：最多读取字节数。
- 最后一个参数：最长等待时间。

`uart_write_bytes(port, (const char *)data, len)`：

- 写出指定长度字节。
- 返回写入字节数，失败时返回负值。

## 可配置项

- `CONFIG_ESPESP_UART_PORT_NUM`：UART 端口号。
- `CONFIG_ESPESP_UART_BAUD_RATE`：波特率。
- `CONFIG_ESPESP_UART_USE_CUSTOM_PINS`：是否启用自定义 TX/RX 引脚。
- `CONFIG_ESPESP_UART_TX_GPIO`：TX GPIO。
- `CONFIG_ESPESP_UART_RX_GPIO`：RX GPIO。

## 注意事项

- TX/RX 需要交叉连接。
- UART0 常用于下载和日志，外设连接建议 UART1。
- 两端波特率、数据位、校验位、停止位必须一致。
