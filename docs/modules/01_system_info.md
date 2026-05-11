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
  -> Demo selector
  -> 01 system_info
```

然后：

```sh
idf.py build flash monitor
```

## 看哪段代码

- 入口：`main/hello_world_main.c`
- demo：`main/demos/system_info_demo.c`
- 公共 banner：`main/demos/demo_common.c`

## 练习

- 增加一行日志，打印 `CONFIG_FREERTOS_HZ`。
- 改成同时打印 Wi-Fi SoftAP MAC。
- 对比重启前后的 `minimum free heap since boot`。
