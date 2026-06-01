# ESP 模块能力索引

这份索引按“能运行 -> 能读懂 -> 能改参数 -> 能接外设 -> 能联网”的顺序组织。
每个阶段都对应本项目里的单模块，可以只启用一个模块，只关注一类日志、网络接口或硬件现象。

## 1. 工程与启动

核心点：

- `app_main()` 是应用入口。
- `menuconfig` 生成 `sdkconfig`，代码里通过 `CONFIG_*` 宏读取配置。
- `ESP_LOGI/ESP_LOGW/ESP_LOGE` 是 ESP-IDF 最常用的调试方式。

对应模块：

- `system_info`
- 源码：`main/hello_world_main.c`
- 源码：`main/system_info/system_info.c`

## 2. FreeRTOS 基础

ESP-IDF 应用运行在 FreeRTOS 上。核心概念包括：

- task：并发执行单元。
- priority：任务优先级。
- queue：任务之间传数据。
- `vTaskDelay()`：让出 CPU，而不是忙等。
- stack high water mark：检查任务栈余量。

对应模块：

- `rtos_tasks`
- 源码：`main/rtos_tasks/rtos_tasks.c`

## 3. LED、GPIO 和板级差异

GPIO 是接 LED、按键、继电器、传感器的基础。核心差异包括：

- GPIO 编号不是开发板丝印编号。
- 有些板载 LED 不是 GPIO2。
- 输出模式、上下拉、中断是不同配置。

对应模块：

- `led_blink`
- 源码：`main/led/led_blink.c`

## 4. NVS 持久化

NVS 适合保存少量配置，例如 Wi-Fi 配网结果、设备编号、运行计数。
关键规则：

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
- `pcm_stream`：把 I2S 麦克风 PCM 通过 UART 或 UDP 传到电脑写 WAV。
- `speaker`：通过 I2S 数字功放输出声音。
- `speaker_client`：通过 WebSocket 接收电脑端 WAV 音频流并用 I2S 播放。
- `voice_client`：通过 WebSocket 接入 voice-server，上送麦克风 PCM 并播放 TTS PCM。
- `voice_callback`：本地全双工麦克风回放，使用低延迟队列和回声门控避免扬声器回灌。
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

## 7. HTTP

HTTP client 模块训练“先联网，再请求，再处理回调”的流程。HTTP/HTTPS server
模块训练“设备入网后监听端口、注册路由、处理请求、鉴权和返回响应”的流程。
HTTP 只适合受控局域网调试；需要传输控制命令或敏感状态时使用 HTTPS。

对应模块：

- `http_get`
- `http_server`
- `https_server`
- `websocket_server`
- `websocket_client`
- `speaker_client`
- `voice_client`
- 源码：`main/http_client/http_get.c`
- 源码：`main/http_server/http_server.c`
- 源码：`main/https_server/https_server.c`
- 源码：`main/websocket_server/websocket_server.c`
- 源码：`main/websocket_client/websocket_client.c`
- 源码：`main/speaker_client/speaker_client.c`
- 源码：`main/voice_client/voice_client.c`

## 8. WebSocket

WebSocket 适合浏览器和桌面客户端与设备做实时双向通信。它比 HTTP 更适合持续推送、
即时控制和轻量心跳；比 MQTT 更适合直接连到一个设备的交互式会话。

对应模块：

- `websocket_server`
- `websocket_client`
- `speaker_client`
- `voice_client`
- 源码：`main/websocket_server/websocket_server.c`
- 源码：`main/websocket_client/websocket_client.c`
- 源码：`main/speaker_client/speaker_client.c`
- 源码：`main/voice_client/voice_client.c`

## 9. MQTT Client

MQTT 模块训练长连接、发布订阅和设备状态同步。它比 HTTP 更适合设备状态、
远程控制、遥测和后续对话机器人的状态流。

对应模块：

- `mqtt_client`
- 源码：`main/mqtt_client/mqtt_client_app.c`
