# 01 system_info: 系统信息与启动流程

## 模块概览

- `app_main()` 是 ESP-IDF 应用入口。
- `esp_chip_info()` 读取芯片能力。
- `esp_flash_get_size()` 读取 flash 大小。
- `esp_get_free_heap_size()` 观察堆内存。
- `esp_app_get_description()` 读取固件描述。

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> 01 system_info
```

然后：

```sh
idf.py build flash monitor
```

## 源码位置

- 入口：`main/hello_world_main.c`
- 模块：`main/system_info/system_info.c`
- 公共 banner：`main/core/app_common.c`

## 当前模块接口参考

- `system_info_run()`：读取并打印系统、固件和内存信息。

## 常用接口说明

- `esp_app_get_description()`：读取当前 app 的项目名、版本、构建时间和 IDF 版本。
- `esp_chip_info()`：读取芯片型号、核心数、Wi-Fi/BLE 能力和 silicon revision。
- `esp_flash_get_size()`：读取外部 flash 容量。
- `esp_get_free_heap_size()`：读取当前剩余堆内存。
- `esp_get_minimum_free_heap_size()`：读取启动以来最低剩余堆内存，适合观察内存峰值压力。
- `esp_read_mac()`：按类型读取芯片 MAC 地址。

## 配置项

本模块没有专属运行参数，主要受工程级配置影响：

- `CONFIG_IDF_TARGET`：目标芯片，影响芯片型号、外设能力和 MAC 类型。
- 分区表配置：影响 app 分区大小和 NVS 分区布局。
- 日志等级配置：影响是否能看到完整启动日志。

## 注意事项

- `flash size` 来自 flash 驱动探测，和分区表大小不是同一个概念。
- `minimum free heap since boot` 可用于观察模块启动过程中的最低内存余量。

## 扩展方向

- 增加一行日志，打印 `CONFIG_FREERTOS_HZ`。
- 改成同时打印 Wi-Fi SoftAP MAC。
- 对比不同模块启动后的 `minimum free heap since boot`。
