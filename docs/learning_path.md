# ESP 必学项学习路线

这份路线按“能运行 -> 能读懂 -> 能改参数 -> 能接外设 -> 能联网”的顺序安排。
每个阶段都对应本项目里的单 demo，可以只改一个模块、只观察一类日志。

## 1. 工程与启动

先学：

- `app_main()` 是应用入口。
- `menuconfig` 生成 `sdkconfig`，代码里通过 `CONFIG_*` 宏读取配置。
- `ESP_LOGI/ESP_LOGW/ESP_LOGE` 是 ESP-IDF 最常用的调试方式。

对应 demo：

- `system_info`
- 源码：`main/hello_world_main.c`
- 源码：`main/demos/system_info_demo.c`

## 2. FreeRTOS 基础

ESP-IDF 应用运行在 FreeRTOS 上。必须先理解：

- task：并发执行单元。
- priority：任务优先级。
- queue：任务之间传数据。
- `vTaskDelay()`：让出 CPU，而不是忙等。
- stack high water mark：检查任务栈余量。

对应 demo：

- `freertos_tasks`
- 源码：`main/demos/freertos_tasks_demo.c`

## 3. GPIO 和板级差异

GPIO 是接 LED、按键、继电器、传感器的基础。必须理解：

- GPIO 编号不是开发板丝印编号。
- 有些板载 LED 不是 GPIO2。
- 输出模式、上下拉、中断是不同配置。

对应 demo：

- `gpio_blink`
- 源码：`main/demos/gpio_blink_demo.c`

## 4. NVS 持久化

NVS 适合保存少量配置，例如 Wi-Fi 配网结果、设备编号、运行计数。
必须理解：

- NVS 需要初始化。
- namespace 用来隔离数据。
- 写入后要 `nvs_commit()`。

对应 demo：

- `nvs_counter`
- 源码：`main/demos/nvs_counter_demo.c`

## 5. Wi-Fi 与事件模型

联网不是“调用 connect 就结束”，而是事件驱动流程：

- 初始化 NVS。
- 初始化 `esp_netif` 和默认事件循环。
- 注册 Wi-Fi 和 IP 事件回调。
- 等待连接成功或失败事件。

对应 demo：

- `wifi_sta`
- 源码：`main/wifi_sta.c`

## 6. HTTP Client

HTTP demo 训练“先联网，再请求，再处理回调”的流程。先用 HTTP 明文地址学习
client API，等流程熟悉后再扩展 HTTPS 证书。

对应 demo：

- `http_get`
- 源码：`main/demos/http_get_demo.c`

## 7. ADC

ADC 用来读取模拟电压。必须理解：

- ADC channel 不是 GPIO number。
- 衰减影响可测电压范围。
- 校准不是所有芯片或 eFuse 都可用，raw 值仍然有用。

对应 demo：

- `adc_oneshot`
- 源码：`main/demos/adc_oneshot_demo.c`

## 8. UART

UART 是调试模块、GPS、蓝牙透传、串口屏常见接口。必须理解：

- baud rate、data bits、parity、stop bits。
- TX/RX 要交叉连接。
- UART0 常被日志占用，学习外设时建议用 UART1。

对应 demo：

- `uart_echo`
- 源码：`main/demos/uart_echo_demo.c`

## 9. I2C

I2C 常用于 OLED、温湿度、IMU、EEPROM。必须理解：

- SDA/SCL 需要上拉。
- 地址是 7-bit 地址，不包含读写位。
- 扫描只能证明设备响应 ACK，不等于驱动已经正确。

对应 demo：

- `i2c_scan`
- 源码：`main/demos/i2c_scan_demo.c`
