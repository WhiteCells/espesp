# Case2 ESP-IDF 必学项学习工程

这个项目把 ESP 入门必学内容做成了可单独运行的 demo。每次只在 `menuconfig`
里选择一个 demo，编译烧录后通过串口日志学习该模块。

## 快速开始

```sh
. /home/cells/esp/v5.5.2/esp-idf/export.sh
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py flash monitor
```

选择 demo 的位置：

```text
idf.py menuconfig
  -> Case2 ESP Learning
  -> Demo selector
```

Wi-Fi 和 HTTP demo 还需要配置：

```text
Case2 ESP Learning
  -> WiFi
```

## Demo 列表

| 序号 | demo | 学习重点 | 源码 |
| --- | --- | --- | --- |
| 01 | `system_info` | 启动流程、芯片信息、固件信息、堆内存 | `main/demos/system_info_demo.c` |
| 02 | `freertos_tasks` | 任务、队列、延时、栈水位 | `main/demos/freertos_tasks_demo.c` |
| 03 | `gpio_blink` | GPIO 输出、LED 闪烁、板级引脚配置 | `main/demos/gpio_blink_demo.c` |
| 04 | `nvs_counter` | NVS 初始化、读写键值、commit | `main/demos/nvs_counter_demo.c` |
| 05 | `wifi_sta` | STA 模式、事件循环、EventGroup、获取 IP | `main/wifi_sta.c` |
| 06 | `http_get` | Wi-Fi 后发起 HTTP GET、事件回调 | `main/demos/http_get_demo.c` |
| 07 | `adc_oneshot` | ADC1 单次采样、衰减、校准电压 | `main/demos/adc_oneshot_demo.c` |
| 08 | `uart_echo` | UART 参数、读写、可选自定义引脚 | `main/demos/uart_echo_demo.c` |
| 09 | `i2c_scan` | I2C master、新驱动、地址扫描 | `main/demos/i2c_scan_demo.c` |

## 文档入口

- 学习路线：`docs/learning_path.md`
- Demo 操作说明：`docs/demo_guide.md`
- 分模块文档：`docs/modules/`

## 项目结构

```text
main/
  hello_world_main.c       # 统一入口：读取 menuconfig 选择并运行 demo
  Kconfig.projbuild        # demo 选择项和每个 demo 的参数
  wifi_sta.c               # Wi-Fi 连接模块，也可单独作为 demo
  demos/
    demo_registry.c        # demo 注册表
    demo_common.c          # NVS/网络公共初始化与 banner
    *_demo.c               # 每个必学项一个独立 demo
docs/
  learning_path.md
  demo_guide.md
  modules/
```

## 建议学习顺序

先跑 `system_info`，确认串口、目标芯片和工程可用；再跑 `freertos_tasks` 和
`gpio_blink` 建立基础手感；之后学习 `nvs_counter`、`wifi_sta`、`http_get`；
最后根据手头外设选择 `adc_oneshot`、`uart_echo`、`i2c_scan`。
