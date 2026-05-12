# ESP 必学项学习路线

这份路线按“能运行 -> 能读懂 -> 能改参数 -> 能接外设 -> 能联网”的顺序安排。
每个阶段都对应本项目里的单模块，可以只改一个模块、只观察一类日志或硬件现象。

## 1. 工程与启动

先学：

- `app_main()` 是应用入口。
- `menuconfig` 生成 `sdkconfig`，代码里通过 `CONFIG_*` 宏读取配置。
- `ESP_LOGI/ESP_LOGW/ESP_LOGE` 是 ESP-IDF 最常用的调试方式。

对应模块：

- `system_info`
- 源码：`main/hello_world_main.c`
- 源码：`main/system_info/system_info.c`

## 2. FreeRTOS 基础

ESP-IDF 应用运行在 FreeRTOS 上。必须先理解：

- task：并发执行单元。
- priority：任务优先级。
- queue：任务之间传数据。
- `vTaskDelay()`：让出 CPU，而不是忙等。
- stack high water mark：检查任务栈余量。

对应模块：

- `rtos_tasks`
- 源码：`main/rtos_tasks/rtos_tasks.c`

## 3. LED、GPIO 和板级差异

GPIO 是接 LED、按键、继电器、传感器的基础。必须理解：

- GPIO 编号不是开发板丝印编号。
- 有些板载 LED 不是 GPIO2。
- 输出模式、上下拉、中断是不同配置。

对应模块：

- `led_blink`
- 源码：`main/led/led_blink.c`

## 4. NVS 持久化

NVS 适合保存少量配置，例如 Wi-Fi 配网结果、设备编号、运行计数。
必须理解：

- NVS 需要初始化。
- namespace 用来隔离数据。
- 写入后要 `nvs_commit()`。

对应模块：

- `nvs_counter`
- 源码：`main/nvs_counter/nvs_counter.c`

## 5. 硬件输入输出

先把常见硬件链路跑通：

- `adc_reader`：读取模拟电压。
- `uart_echo`：串口读写。
- `i2c_scan`：确认 I2C 设备地址。
- `microphone`：读取 I2S 麦克风音频帧。
- `speaker`：通过 I2S 数字功放输出声音。
- `display`：通过 I2C 写 SSD1306 OLED。

## 6. Wi-Fi 与事件模型

联网不是“调用 connect 就结束”，而是事件驱动流程：

- 初始化 NVS。
- 初始化 `esp_netif` 和默认事件循环。
- 注册 Wi-Fi 和 IP 事件回调。
- 等待连接成功或失败事件。

对应模块：

- `wifi_station`
- 源码：`main/wifi_station/wifi_station.c`

## 7. HTTP Client

HTTP 模块训练“先联网，再请求，再处理回调”的流程。先用 HTTP 明文地址学习
client API，等流程熟悉后再扩展 HTTPS 证书。

对应模块：

- `http_get`
- 源码：`main/http_client/http_get.c`

## 8. MQTT Client

MQTT 模块训练长连接、发布订阅和设备状态同步。它比 HTTP 更适合设备状态、
远程控制、遥测和后续对话机器人的状态流。

对应模块：

- `mqtt_client`
- 源码：`main/mqtt_client_demo/mqtt_client_demo.c`
