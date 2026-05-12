# 01 system_info: 系统信息与启动流程

## 学什么

- `app_main()` 是 ESP-IDF 应用入口。
- `esp_chip_info()` 读取芯片能力。
- `esp_flash_get_size()` 读取 flash 大小。
- `esp_get_free_heap_size()` 观察堆内存。
- `esp_app_get_description()` 读取固件描述。

## 怎么运行

```text
idf.py menuconfig
  -> Case2 ESP Learning
  -> Module selector
  -> 01 system_info
```

然后：

```sh
idf.py build flash monitor
```

## 看哪段代码

- 入口：`main/hello_world_main.c`
- 模块：`main/system_info/system_info.c`
- 公共 banner：`main/core/app_common.c`

## 接口介绍

- `system_info_run()`：读取并打印系统、固件和内存信息。
- 常用接口：`esp_chip_info()`、`esp_flash_get_size()`、`esp_app_get_description()`。

## 注意事项

- `flash size` 来自 flash 驱动探测，和分区表大小不是同一个概念。
- `minimum free heap since boot` 可用于观察模块启动过程中的最低内存余量。

## 练习

- 增加一行日志，打印 `CONFIG_FREERTOS_HZ`。
- 改成同时打印 Wi-Fi SoftAP MAC。
- 对比不同模块启动后的 `minimum free heap since boot`。
