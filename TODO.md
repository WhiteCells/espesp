# ESPESP 完善 TODO

这份 TODO 用来规划 ESPESP 大合集后续还需要补齐的模块和工程收尾项。
当前项目已经覆盖 01-15：

- `system_info`
- `rtos_tasks`
- `led_blink`
- `nvs_counter`
- `wifi_station`
- `http_get`
- `adc_reader`
- `uart_echo`
- `i2c_scan`
- `microphone`
- `speaker`
- `display`
- `mqtt_client`
- `http_server`
- `https_server`

后续完善的目标不是把所有 ESP-IDF API 都堆进一个工程，而是继续保持“一个模块讲清一个能力”的风格：

- 每个模块都能通过 `menuconfig -> ESPESP Menu -> Module selector` 独立选择。
- 每个模块都有 `main/<module>/` 源码目录、源码旁 README、`docs/modules/<nn_module>.md` 文档。
- 每个模块尽量能在串口日志、电脑测试脚本、手机工具或外设现象中看到明确结果。
- 网络、NVS、LAN service、注册表等共用逻辑优先复用现有公共模块。
- 模块编号继续从 16 往后排。

## P0 工程收尾

这些不是新功能模块，但会影响后续维护体验，建议优先处理。

### TODO-00-01 修正文档中的旧入口文件名

当前文档中还有旧路径 `main/hello_world_main.c`，实际入口已经是 `main/main.c`。

需要检查：

- `README.md`
- `docs/module_index.md`
- `docs/modules/01_system_info.md`

验收标准：

- 全仓库搜索不到 `hello_world_main.c`。
- 文档中的入口说明统一指向 `main/main.c`。

### TODO-00-02 补齐 `sdkconfig.ci`

当前 `sdkconfig.ci` 是空文件。后续如果要在 CI 或本地 smoke test 里稳定构建，需要给它一个最小可构建配置。

建议内容：

- 默认选择 `system_info` 或 `led_blink` 这类不依赖网络和外设的模块。
- 不写入个人 Wi-Fi SSID、密码、token。
- 固定目标芯片相关的必要默认项，避免 CI 受本机 `sdkconfig` 影响。

验收标准：

- `sdkconfig.ci` 不包含个人隐私配置。
- 使用 CI 配置能跑通一次 `idf.py build`。

### TODO-00-03 增加新增模块模板说明

在 `main/registry/README.md` 或单独文档里补充新增模块的固定步骤。

建议模板：

- 新增 `main/<module>/<module>.c`
- 新增 `main/<module>/<module>.h`
- 新增 `main/<module>/README.md`
- 新增 `docs/modules/<nn_module>.md`
- 更新 `main/CMakeLists.txt`
- 更新 `main/Kconfig.projbuild`
- 更新 `main/registry/app_registry.c`
- 更新 `README.md`
- 更新 `docs/module_index.md`
- 必要时更新 `docs/module_guide.md`

验收标准：

- 后续新增模块时可以按清单逐项完成，不容易漏掉注册表或文档入口。

## P1 基础硬件控制模块

P1 模块建议先做。它们是很多后续机器人、传感器、交互设备的基础能力。

## 16 `button_input`: 按键输入、中断与消抖

### 模块目标

补齐 GPIO 输入能力。当前已有 `led_blink` 演示 GPIO 输出，但缺少最常见的输入场景：按键、限位开关、门磁、人体触发信号等。

这个模块要讲清：

- GPIO 输入模式。
- 内部上拉/下拉。
- 机械按键抖动。
- 轮询和中断的差异。
- ISR 中不能做复杂工作，应把事件交给任务处理。
- 短按、长按、双击等状态机可以如何扩展。

### 建议源码

- `main/button_input/button_input.c`
- `main/button_input/button_input.h`
- `main/button_input/README.md`
- `docs/modules/16_button_input.md`

### 建议配置项

- `CONFIG_ESPESP_BUTTON_GPIO`
- `CONFIG_ESPESP_BUTTON_ACTIVE_LOW`
- `CONFIG_ESPESP_BUTTON_PULLUP`
- `CONFIG_ESPESP_BUTTON_DEBOUNCE_MS`
- `CONFIG_ESPESP_BUTTON_LONG_PRESS_MS`

### 建议实现范围

- 配置一个 GPIO 为输入。
- 根据 `ACTIVE_LOW` 判断按下状态。
- 使用 GPIO interrupt 捕获边沿。
- ISR 只发送事件到 FreeRTOS queue。
- 任务中做消抖、短按、长按判断。
- 串口打印 `pressed`、`released`、`short_press`、`long_press`。

### 验收标准

- 按下和松开按键时，串口能稳定打印事件。
- 按住超过配置时间能打印长按事件。
- 快速抖动不会产生大量错误触发。
- 不接按键时不会疯狂触发。

### 注意事项

- 很多开发板 BOOT 键连接方式是低电平按下，默认可用 `ACTIVE_LOW=y`。
- 不同芯片有些 GPIO 是 strapping pin，文档里要提醒谨慎使用。
- ISR 里不要调用 `ESP_LOGI`。

## 17 `ledc_pwm`: PWM 调光、舵机和蜂鸣器

### 模块目标

补齐 PWM 输出能力。它能覆盖 LED 呼吸灯、舵机角度控制、有源/无源蜂鸣器、简单电机调速等入门场景。

这个模块要讲清：

- LEDC timer 和 channel 的关系。
- PWM 频率、分辨率、占空比。
- 渐变调光和普通占空比设置的区别。
- 舵机脉宽和角度映射。

### 建议源码

- `main/ledc_pwm/ledc_pwm.c`
- `main/ledc_pwm/ledc_pwm.h`
- `main/ledc_pwm/README.md`
- `docs/modules/17_ledc_pwm.md`

### 建议配置项

- `CONFIG_ESPESP_PWM_GPIO`
- `CONFIG_ESPESP_PWM_FREQ_HZ`
- `CONFIG_ESPESP_PWM_DUTY_MIN`
- `CONFIG_ESPESP_PWM_DUTY_MAX`
- `CONFIG_ESPESP_PWM_STEP_MS`
- `CONFIG_ESPESP_PWM_MODE_LED`
- `CONFIG_ESPESP_PWM_MODE_SERVO`
- `CONFIG_ESPESP_PWM_MODE_BUZZER`

### 建议实现范围

- 默认实现 LED 呼吸灯。
- 可选模式输出舵机常见 50 Hz 控制波。
- 可选模式输出蜂鸣器频率。
- 串口打印当前 duty、频率、模式。

### 验收标准

- LED 能明显由暗到亮再由亮到暗。
- 舵机模式下可以在最小/中间/最大角度之间切换。
- 蜂鸣器模式下频率可配置。
- duty 和频率边界值有参数校验。

### 注意事项

- 舵机和电机不要直接从 ESP32 GPIO 取大电流。
- 舵机需要独立供电并共地。
- PWM 分辨率越高，可选频率范围越受限。

## 18 `spi_bus`: SPI 总线与设备通信

### 模块目标

补齐 SPI 能力。当前已有 I2C 扫描和 I2C OLED，但没有 SPI。很多屏幕、Flash、SD 卡、RF 模块和传感器都依赖 SPI。

这个模块要讲清：

- SPI host、bus、device 的关系。
- MISO、MOSI、SCLK、CS 的职责。
- mode 0/1/2/3、时钟频率、半双工/全双工。
- 一条 SPI 总线上可以挂多个设备，但每个设备需要独立 CS。

### 建议源码

- `main/spi_bus/spi_bus.c`
- `main/spi_bus/spi_bus.h`
- `main/spi_bus/README.md`
- `docs/modules/18_spi_bus.md`

### 建议配置项

- `CONFIG_ESPESP_SPI_HOST`
- `CONFIG_ESPESP_SPI_MOSI_GPIO`
- `CONFIG_ESPESP_SPI_MISO_GPIO`
- `CONFIG_ESPESP_SPI_SCLK_GPIO`
- `CONFIG_ESPESP_SPI_CS_GPIO`
- `CONFIG_ESPESP_SPI_CLOCK_HZ`
- `CONFIG_ESPESP_SPI_MODE`

### 建议实现范围

优先做一个不依赖特定复杂外设的最小演示：

- 初始化 SPI bus。
- 注册一个 SPI device。
- 发送一段固定字节。
- 如果配置了 MISO，读取回包并打印。

可选增强：

- 支持常见 SPI Flash JEDEC ID 读取。
- 支持简单 ST7789/ILI9341 屏幕初始化演示。

### 验收标准

- 无 MISO 外设时，模块至少能完成发送并打印 transaction 结果。
- 接 SPI Flash 或支持固定寄存器读取的外设时，能打印读到的 ID 或寄存器值。
- 文档明确说明接线和 SPI mode。

### 注意事项

- ESP32 系列不同芯片可用 SPI host 和默认 IO 不完全相同。
- SPI 屏幕通常还需要 DC、RST、BL 等额外引脚，初版不要把范围拉太大。

## 19 `timer_watchdog`: 定时器、软件定时与看门狗

### 模块目标

补齐时间控制和系统可靠性能力。当前 `rtos_tasks` 里有 `vTaskDelay()`，但还缺少更准确的定时器和看门狗概念。

这个模块要讲清：

- `vTaskDelay()` 适合任务节拍，不适合高精度计时。
- `esp_timer` 适合微秒级软件定时回调。
- GPTimer 适合硬件定时器场景。
- Task Watchdog 用来发现任务卡死。
- 回调里不要做耗时工作。

### 建议源码

- `main/timer_watchdog/timer_watchdog.c`
- `main/timer_watchdog/timer_watchdog.h`
- `main/timer_watchdog/README.md`
- `docs/modules/19_timer_watchdog.md`

### 建议配置项

- `CONFIG_ESPESP_TIMER_PERIOD_MS`
- `CONFIG_ESPESP_TIMER_USE_ESP_TIMER`
- `CONFIG_ESPESP_TIMER_USE_GPTIMER`
- `CONFIG_ESPESP_WATCHDOG_ENABLE`
- `CONFIG_ESPESP_WATCHDOG_TIMEOUT_SEC`

### 建议实现范围

- 使用 `esp_timer` 周期性递增计数并打印。
- 可选 GPTimer 演示硬件定时 tick。
- 可选开启 Task WDT，正常喂狗。
- 提供一个配置项模拟任务卡住，观察 watchdog 行为。

### 验收标准

- 串口能看到稳定周期日志。
- 开启模拟卡死后能看到 watchdog 报错或复位现象。
- 文档清楚解释为什么不要在 timer callback 里做重活。

### 注意事项

- Watchdog 演示可能导致设备复位，默认不要开启危险模式。
- 日志打印本身会影响高频定时精度。

## 20 `sleep_power`: 低功耗、休眠与唤醒

### 模块目标

补齐低功耗能力。ESP32 很多真实项目靠电池运行，必须理解 light sleep、deep sleep 和唤醒源。

这个模块要讲清：

- light sleep 和 deep sleep 的区别。
- deep sleep 后应用会重新启动。
- RTC memory 可以保存少量跨 deep sleep 状态。
- timer wakeup、GPIO wakeup、touch wakeup 的适用场景。
- Wi-Fi、外设、电源指示灯都会影响功耗。

### 建议源码

- `main/sleep_power/sleep_power.c`
- `main/sleep_power/sleep_power.h`
- `main/sleep_power/README.md`
- `docs/modules/20_sleep_power.md`

### 建议配置项

- `CONFIG_ESPESP_SLEEP_MODE_LIGHT`
- `CONFIG_ESPESP_SLEEP_MODE_DEEP`
- `CONFIG_ESPESP_SLEEP_WAKEUP_TIMER_SEC`
- `CONFIG_ESPESP_SLEEP_WAKEUP_GPIO`
- `CONFIG_ESPESP_SLEEP_WAKEUP_GPIO_LEVEL`

### 建议实现范围

- 打印启动原因 `esp_sleep_get_wakeup_cause()`。
- 使用 RTC memory 记录 deep sleep 唤醒次数。
- 默认 timer wakeup，休眠数秒后自动醒来。
- 可选 GPIO 唤醒。

### 验收标准

- 首次启动、timer 唤醒、GPIO 唤醒能在日志中区分。
- deep sleep 唤醒计数能递增。
- 文档说明开发板 USB、稳压器、电源灯会让实际功耗高于芯片数据手册。

### 注意事项

- 不同 ESP32 芯片支持的唤醒 GPIO 不完全一样。
- deep sleep 后 RAM 普通变量会丢失，不能按普通任务恢复理解。

## P2 网络和设备管理模块

P2 模块让 ESPESP 从“能跑 demo”走向“能作为真实设备被配置、升级和发现”。

## 21 `wifi_ap_provisioning`: SoftAP 配网

### 模块目标

当前 Wi-Fi SSID 和密码主要依赖 `menuconfig` 固化。真实设备需要用户在首次使用时配置网络。

这个模块要讲清：

- ESP32 SoftAP 模式。
- STA + AP 共存。
- 通过 HTTP 页面或 API 提交 Wi-Fi SSID/password。
- 用 NVS 保存配网结果。
- 重启后读取 NVS 并自动连接。

### 建议源码

- `main/wifi_ap_provisioning/wifi_ap_provisioning.c`
- `main/wifi_ap_provisioning/wifi_ap_provisioning.h`
- `main/wifi_ap_provisioning/README.md`
- `docs/modules/21_wifi_ap_provisioning.md`

### 建议配置项

- `CONFIG_ESPESP_PROV_AP_SSID`
- `CONFIG_ESPESP_PROV_AP_PASSWORD`
- `CONFIG_ESPESP_PROV_HTTP_PORT`
- `CONFIG_ESPESP_PROV_NVS_NAMESPACE`
- `CONFIG_ESPESP_PROV_RESET_GPIO`

### 建议实现范围

- 启动 SoftAP。
- 启动一个最小 HTTP server。
- 提供 `GET /` 简单配网页或纯文本说明。
- 提供 `POST /api/v1/wifi` 保存 SSID/password。
- 保存到 NVS 后尝试 STA 连接。
- 可选长按按键清除配网。

### 验收标准

- 手机或电脑能连上 ESP32 创建的 AP。
- 能提交 Wi-Fi 信息并保存到 NVS。
- 重启后设备能读取 NVS 并连接目标 Wi-Fi。
- 文档明确说明不要把真实密码提交到 Git。

### 注意事项

- SoftAP 配网页如果走 HTTP，密码会明文传输，只适合首次局域配置演示。
- 需要避免和现有 `wifi_station` 初始化流程互相冲突。

## 22 `ble_gatt`: BLE GATT 外设

### 模块目标

补齐 BLE 基础能力。很多手机控制、近场配置和低功耗传感器都依赖 BLE。

这个模块要讲清：

- BLE advertising。
- GATT service、characteristic、UUID。
- read、write、notify。
- MTU 和短数据包限制。
- BLE 配网和 Wi-Fi 配网的差异。

### 建议源码

- `main/ble_gatt/ble_gatt.c`
- `main/ble_gatt/ble_gatt.h`
- `main/ble_gatt/README.md`
- `docs/modules/22_ble_gatt.md`

### 建议配置项

- `CONFIG_ESPESP_BLE_DEVICE_NAME`
- `CONFIG_ESPESP_BLE_SERVICE_UUID`
- `CONFIG_ESPESP_BLE_STATUS_CHAR_UUID`
- `CONFIG_ESPESP_BLE_CONTROL_CHAR_UUID`
- `CONFIG_ESPESP_BLE_NOTIFY_PERIOD_MS`

### 建议实现范围

- 启动 BLE advertising。
- 暴露一个 status characteristic 可读。
- 暴露一个 control characteristic 可写。
- 可选周期 notify free heap 或 uptime。

### 验收标准

- 手机 BLE 工具能搜索到设备名。
- 能读取 status characteristic。
- 写入 control characteristic 后串口能打印收到的命令。
- 开启 notify 后手机能收到周期状态。

### 注意事项

- ESP-IDF 同时支持 Bluedroid 和 NimBLE，建议选一种并在文档中说明。
- BLE 和 Wi-Fi 共存会增加内存压力，配置要保守。

## 23 `ota_update`: OTA 固件升级

### 模块目标

补齐设备远程升级能力。真实设备一旦部署，OTA 是非常关键的维护能力。

这个模块要讲清：

- 分区表需要 OTA 分区。
- 固件版本号和 `esp_app_desc_t`。
- HTTP/HTTPS OTA 流程。
- OTA 成功后重启。
- app rollback 和版本校验。

### 建议源码

- `main/ota_update/ota_update.c`
- `main/ota_update/ota_update.h`
- `main/ota_update/README.md`
- `docs/modules/23_ota_update.md`
- 可选 `server/ota_server/`

### 建议配置项

- `CONFIG_ESPESP_OTA_URL`
- `CONFIG_ESPESP_OTA_USE_HTTPS`
- `CONFIG_ESPESP_OTA_TIMEOUT_MS`
- `CONFIG_ESPESP_OTA_SKIP_VERSION_CHECK`
- `CONFIG_ESPESP_OTA_REBOOT_AFTER_UPDATE`

### 建议实现范围

- 连接 Wi-Fi。
- 从指定 URL 下载新固件。
- 打印当前版本和目标版本。
- 写入 OTA 分区。
- 成功后设置启动分区并重启。
- 失败时保留当前固件。

### 验收标准

- 能从本地 HTTP server 下载固件并升级。
- 升级后日志显示新版本。
- 下载失败、版本相同、URL 错误时有明确错误日志。
- 文档说明分区表要求。

### 注意事项

- HTTPS OTA 需要证书处理，不能为了省事默认 `skip_cert_common_name_check`。
- OTA 失败不能破坏当前可启动固件。

## 24 `filesystem`: 文件系统、配置文件与 SD 卡

### 模块目标

补齐本地文件读写能力。NVS 适合小键值，文件系统适合配置文件、日志、小型资源文件。

这个模块要讲清：

- NVS 和文件系统的边界。
- SPIFFS、LittleFS、FATFS、SD card 的差异。
- mount、open、read、write、close。
- 文件系统容量、磨损和格式化。

### 建议源码

- `main/filesystem/filesystem.c`
- `main/filesystem/filesystem.h`
- `main/filesystem/README.md`
- `docs/modules/24_filesystem.md`

### 建议配置项

- `CONFIG_ESPESP_FS_BACKEND_SPIFFS`
- `CONFIG_ESPESP_FS_BACKEND_LITTLEFS`
- `CONFIG_ESPESP_FS_BACKEND_SD_CARD`
- `CONFIG_ESPESP_FS_MOUNT_POINT`
- `CONFIG_ESPESP_FS_FORMAT_IF_MOUNT_FAILED`

### 建议实现范围

- 初版优先 SPIFFS 或 LittleFS。
- mount 文件系统。
- 写入一个文本文件。
- 读取并打印文件内容。
- 打印总空间和已用空间。

### 验收标准

- 首次运行能创建文件。
- 重启后能读取上次写入内容。
- mount 失败时日志清晰。
- 文档说明需要分区表支持。

### 注意事项

- 自动格式化会清空已有文件，默认要谨慎。
- SD 卡模块还涉及 SPI/SDMMC，引脚和供电说明要更细。

## 25 `time_mdns`: SNTP 对时与 mDNS 发现

### 模块目标

补齐时间和局域网发现能力。设备状态、日志、证书校验和自动发现都依赖它。

这个模块要讲清：

- SNTP 获取网络时间。
- 时区和 UTC 的区别。
- mDNS 让设备可以通过名字访问。
- 服务发现和普通 DNS 的差异。

### 建议源码

- `main/time_mdns/time_mdns.c`
- `main/time_mdns/time_mdns.h`
- `main/time_mdns/README.md`
- `docs/modules/25_time_mdns.md`

### 建议配置项

- `CONFIG_ESPESP_SNTP_SERVER`
- `CONFIG_ESPESP_TIMEZONE`
- `CONFIG_ESPESP_MDNS_HOSTNAME`
- `CONFIG_ESPESP_MDNS_INSTANCE_NAME`
- `CONFIG_ESPESP_MDNS_ENABLE_HTTP_SERVICE`

### 建议实现范围

- 连接 Wi-Fi。
- 初始化 SNTP 并等待时间同步。
- 打印当前本地时间和 UTC 时间。
- 启动 mDNS，注册 hostname。
- 可选注册 `_http._tcp` 服务。

### 验收标准

- 串口能打印有效日期时间。
- 局域网电脑可以解析 `<hostname>.local`。
- 如果同时运行 HTTP server，能通过 mDNS 名称访问。

### 注意事项

- 中国大陆网络环境下默认 NTP server 可能不可用，配置项要允许替换。
- mDNS 在部分路由器、公司网络或跨 VLAN 环境中可能不可用。

## 26 `websocket_server` / `websocket_client`: 实时双向通信

> 状态：已实现，见 `main/websocket_server/`、`main/websocket_client/`、
> `docs/modules/26_websocket_server.md` 和 `docs/modules/26_websocket_client.md`。

### 模块目标

当前 HTTP server 更适合请求-响应，MQTT 更适合发布订阅。WebSocket 适合浏览器或桌面端与 ESP32 做实时状态和控制。

这个模块要讲清：

- HTTP upgrade 到 WebSocket。
- 文本帧和二进制帧。
- 心跳、断线和多客户端限制。
- 和 HTTP REST、MQTT 的适用场景差异。

### 建议源码

- `main/websocket_server/websocket_server.c`
- `main/websocket_server/websocket_server.h`
- `main/websocket_server/README.md`
- `main/websocket_client/websocket_client.c`
- `main/websocket_client/websocket_client.h`
- `main/websocket_client/README.md`
- `docs/modules/26_websocket_server.md`
- `docs/modules/26_websocket_client.md`
- 可选 `server/ws_client/`

### 建议配置项

- `CONFIG_ESPESP_WS_SERVER_PORT`
- `CONFIG_ESPESP_WS_SERVER_PATH`
- `CONFIG_ESPESP_WS_SERVER_AUTH_TOKEN`
- `CONFIG_ESPESP_WS_SERVER_PUBLISH_PERIOD_MS`
- `CONFIG_ESPESP_WS_SERVER_MAX_CLIENTS`
- `CONFIG_ESPESP_WS_CLIENT_URI`
- `CONFIG_ESPESP_WS_CLIENT_AUTH_TOKEN`
- `CONFIG_ESPESP_WS_CLIENT_INITIAL_PAYLOAD`
- `CONFIG_ESPESP_WS_CLIENT_PUBLISH_PERIOD_MS`

### 建议实现范围

- 连接 Wi-Fi。
- 启动 HTTP server 并注册 WebSocket route。
- 作为客户端连接 WebSocket server。
- 接收客户端文本命令并打印。
- 客户端发送初始文本和周期 status JSON。
- 周期发送 status JSON。
- 可选复用 LAN service 的鉴权逻辑。

### 验收标准

- 电脑端 WebSocket client 能连接 ESP32。
- ESP32 WebSocket client 能连接电脑端 `server/ws_server`。
- 客户端发送消息后串口能看到内容。
- ESP32 能周期推送状态。
- 超过最大客户端数时行为明确。

### 注意事项

- WebSocket 长连接会占用 socket 和内存。
- 浏览器访问 HTTPS 页面时连接明文 WS 会受 mixed content 限制，需要考虑 WSS。

## 27 `json_control`: 结构化 JSON 控制命令

### 模块目标

当前 LAN control 路由能接收 body，但还没有把 body 解析成明确的业务命令。这个模块用于把控制协议做成可扩展的结构化入口。

这个模块要讲清：

- JSON 解析和字段校验。
- 命令版本号。
- 错误码和错误响应。
- 控制命令和设备状态的边界。
- 如何把 HTTP、HTTPS、MQTT、WebSocket 收到的命令复用到同一个解析函数。

### 建议源码

- `main/json_control/json_control.c`
- `main/json_control/json_control.h`
- `main/json_control/README.md`
- `docs/modules/27_json_control.md`

### 建议配置项

- `CONFIG_ESPESP_JSON_CONTROL_MAX_LEN`
- `CONFIG_ESPESP_JSON_CONTROL_ALLOW_LED`
- `CONFIG_ESPESP_JSON_CONTROL_ALLOW_AUDIO`
- `CONFIG_ESPESP_JSON_CONTROL_ALLOW_DISPLAY`

### 建议实现范围

- 定义统一命令格式，例如：

```json
{
  "version": 1,
  "command": "led.set",
  "params": {
    "level": 1
  }
}
```

- 使用 cJSON 解析。
- 校验 `version`、`command`、`params`。
- 支持最小命令集：
  - `system.ping`
  - `led.set`
  - `display.text`
  - `audio.tone`
- 返回统一 JSON 响应。

### 验收标准

- 合法命令返回 `{"ok":true,...}`。
- 非法 JSON、缺字段、未知命令、参数越界都有明确错误响应。
- HTTP/HTTPS control route 可以复用解析函数。
- MQTT command topic 后续也能复用同一解析函数。

### 注意事项

- 不要让 JSON 命令直接执行危险操作，例如无限循环、清空 NVS、OTA 任意 URL。
- 请求体长度必须有限制，避免内存被大 body 打爆。

## P3 进阶可选模块

这些模块不是第一阶段必须，但可以让 ESPESP 更接近完整设备开发教程。

## 28 `event_bus`: 统一事件总线

### 模块目标

当模块越来越多时，HTTP、MQTT、BLE、按键、传感器都可能产生事件。需要一个统一事件层，减少模块之间直接互相调用。

建议范围：

- 基于 `esp_event` 自定义 event base。
- 定义设备事件类型。
- 演示一个 producer 和多个 consumer。
- 让 `button_input`、`json_control`、`mqtt_client` 后续能接入。

验收标准：

- 一个模块发布事件，另一个模块能收到。
- 日志能显示事件类型和 payload。

## 29 `sensor_dht`: 温湿度传感器

### 模块目标

提供一个常见单总线/时序敏感传感器例子。DHT11/DHT22 入门常见，但时序要求高，适合讲驱动可靠性。

建议范围：

- GPIO 时序读取。
- 校验和检查。
- 周期打印温湿度。
- 错误重试。

验收标准：

- 接入 DHT11/DHT22 后能稳定打印温湿度。
- 读取失败时不崩溃，日志有错误原因。

## 30 `sensor_env`: I2C 环境传感器

### 模块目标

基于已有 `i2c_scan` 扩展真实 I2C 传感器读取，例如 BME280、SHT30、BH1750。

建议范围：

- 读取芯片 ID。
- 配置测量模式。
- 读取并转换物理量。
- 把数据暴露给 HTTP/MQTT 状态。

验收标准：

- 接入目标传感器后能打印真实读数。
- I2C 设备地址可配置。

## 31 `motor_control`: 电机与驱动板

### 模块目标

为机器人方向补齐执行器控制，覆盖直流电机、H 桥、方向控制和 PWM 调速。

建议范围：

- 两个方向 GPIO。
- 一个 PWM 速度通道。
- 正转、反转、停止、刹车。
- 可选双电机差速控制。

验收标准：

- 控制命令能改变电机方向和速度。
- 文档明确提醒外接驱动板、独立供电和共地。

## 32 `camera_capture`: 摄像头采集

### 模块目标

如果目标板是 ESP32-CAM 或 ESP32-S3 + camera，可以增加图像采集模块。

建议范围：

- 初始化 camera。
- 捕获一帧 JPEG。
- 通过 HTTP endpoint 返回图片。

验收标准：

- 浏览器访问 endpoint 能看到实时图片。
- 分辨率、JPEG 质量可配置。

注意事项：

- 摄像头模块依赖具体板型和 PSRAM，不能作为默认必选模块。

## 推荐实现顺序

建议先按这个顺序推进：

1. P0 工程收尾。
2. `button_input`。
3. `ledc_pwm`。
4. `timer_watchdog`。
5. `sleep_power`。
6. `spi_bus`。
7. `wifi_ap_provisioning`。
8. `time_mdns`。
9. `json_control`。
10. `websocket_server` / `websocket_client`。
11. `ota_update`。
12. `filesystem`。
13. `ble_gatt`。
14. P3 进阶可选模块。

这个顺序的原因是：先补齐硬件输入输出和系统基础，再补设备配置、发现、控制协议和升级能力。这样每个后续模块都能复用前面的能力，不会越写越散。

## 每个新模块的统一验收清单

新增任何模块时，都按这份清单检查：

- [ ] `main/<module>/<module>.c` 存在。
- [ ] `main/<module>/<module>.h` 存在。
- [ ] `main/<module>/README.md` 存在。
- [ ] `docs/modules/<nn_module>.md` 存在。
- [ ] `main/CMakeLists.txt` 已加入源码。
- [ ] `main/Kconfig.projbuild` 已加入选择项和配置项。
- [ ] `main/registry/app_registry.c` 已加入 include、case 和选择分支。
- [ ] `README.md` 模块列表已更新。
- [ ] `docs/module_index.md` 能说明模块属于哪一类能力。
- [ ] `docs/module_guide.md` 已补充 menuconfig 路径或常见问题。
- [ ] 模块启动时日志能说明当前配置。
- [ ] 参数非法时返回明确错误，不静默失败。
- [ ] 不把 Wi-Fi 密码、token、私钥、证书私钥提交到仓库。
- [ ] 能在没有目标外设时给出清楚错误或说明。
- [ ] 至少跑过一次 `idf.py build`。

## 模块文档建议结构

每个 `docs/modules/<nn_module>.md` 建议保持同一结构：

- 模块概览
- 使用方式
- 源码位置
- 当前模块接口参考
- 常用接口说明
- 配置项
- 接线或电脑端测试方式
- 日志现象
- 注意事项
- 扩展方向

每个 `main/<module>/README.md` 建议更偏源码解释：

- 使用方式
- 当前模块已有接口
- 内部结构体或内部函数
- 常用 ESP-IDF API 解释
- 可配置项
- 注意事项

## 和现有模块的复用关系

后续模块应尽量复用已有能力：

- 需要 Wi-Fi 的模块复用 `wifi_station_connect()`。
- 需要 NVS 的模块复用 `app_common_init_nvs()`。
- 需要 HTTP/HTTPS 状态和控制接口的模块复用 `lan_service`。
- 需要状态上报的模块可以复用或参考 `mqtt_client`。
- 需要 I2C 外设的模块参考 `i2c_scan` 和 `display`。
- 需要 I2S 音频的模块参考 `microphone` 和 `speaker`。
- 需要统一入口的模块都通过 `registry` 接入，不直接改复杂入口逻辑。

## 不建议现在做的事情

为了保持项目清晰，暂时不建议：

- 把多个模块强行合成一个巨大的综合 demo。
- 在没有抽象需求前做过度复杂的框架。
- 把真实 Wi-Fi 密码、Bearer token、TLS 私钥写入示例配置。
- 默认启用会导致设备反复重启的 watchdog 危险演示。
- 默认启用需要特殊板型的 camera、PSRAM、SDMMC 功能。
- 让所有模块互相 include，后续应通过公共服务、事件或命令层解耦。
