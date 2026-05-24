# ESPESP

## 快速开始

选择模块的位置：

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
```

Wi-Fi、HTTP、WebSocket、麦克风、扬声器、显示屏等模块还需要在对应菜单中配置引脚或网络参数。

## 模块列表

| 序号 | 模块 | 模块能力 | 源码 |
| --- | --- | --- | --- |
| 01 | `system_info` | 启动流程、芯片信息、固件信息、堆内存 | `main/system_info/system_info.c` |
| 02 | `rtos_tasks` | 任务、队列、延时、栈水位 | `main/rtos_tasks/rtos_tasks.c` |
| 03 | `led_blink` | GPIO 输出、LED 闪烁、板级引脚配置 | `main/led/led_blink.c` |
| 04 | `nvs_counter` | NVS 初始化、读写键值、commit | `main/nvs_counter/nvs_counter.c` |
| 05 | `wifi_station` | STA 模式、事件循环、EventGroup、获取 IP | `main/wifi_station/wifi_station.c` |
| 06 | `http_get` | Wi-Fi 后发起 HTTP GET、事件回调 | `main/http_client/http_get.c` |
| 07 | `adc_reader` | ADC1 单次采样、衰减、校准电压 | `main/adc_reader/adc_reader.c` |
| 08 | `uart_echo` | UART 参数、读写、可选自定义引脚 | `main/uart_echo/uart_echo.c` |
| 09 | `i2c_scan` | I2C master、新驱动、地址扫描 | `main/i2c_scan/i2c_scan.c` |
| 10 | `microphone` | I2S RX、麦克风采样、音频帧幅度统计 | `main/microphone/microphone.c` |
| 11 | `speaker` | I2S TX、数字功放、正弦波输出 | `main/speaker/speaker.c` |
| 12 | `display` | I2C OLED、SSD1306 初始化、图案写入 | `main/display/display.c` |
| 13 | `mqtt_client` | Wi-Fi 后连接 MQTT、发布状态、订阅命令 | `main/mqtt_client/mqtt_client_app.c` |
| 14 | `http_server` | 局域网 HTTP 服务、Bearer 鉴权、状态与控制 API | `main/http_server/http_server.c` |
| 15 | `https_server` | 局域网 HTTPS 服务、NVS 证书、TLS 加密 | `main/https_server/https_server.c` |
| 16 | `pcm_stream` | I2S 麦克风 PCM 通过 UART/UDP 传到电脑并写 WAV | `main/pcm_stream/pcm_stream.c` |
| 17 | `speaker_client` | 通过 WebSocket 接收电脑端 WAV 音频流并用 I2S 播放 | `main/speaker_client/speaker_client.c` |
| 26a | `websocket_server` | WebSocket 升级、文本/二进制帧、状态推送、可选鉴权 | `main/websocket_server/websocket_server.c` |
| 26b | `websocket_client` | 主动连接 WebSocket server、发送状态、接收推送、可选鉴权 | `main/websocket_client/websocket_client.c` |

## 文档入口

- 模块能力索引：`docs/module_index.md`
- 操作说明：`docs/module_guide.md`
- 分模块文档：`docs/modules/`
- 源码旁接口说明：每个 `main/<module>/README.md`
