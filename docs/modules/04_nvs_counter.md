# 04 nvs_counter: NVS 键值存储

## 学什么

- `nvs_flash_init()` 初始化 NVS。
- `nvs_open()` 打开 namespace。
- `nvs_get_u32()` 读取计数。
- `nvs_set_u32()` 写入计数。
- `nvs_commit()` 提交写入。

## 怎么运行

```text
idf.py menuconfig
  -> Case2 ESP Learning
  -> Module selector
  -> 04 nvs_counter
```

然后烧录运行，按开发板 reset，多看几次日志。

## 看哪段代码

- `main/nvs_counter/nvs_counter.c`
- NVS 公共初始化：`main/core/app_common.c`

## 接口介绍

- `nvs_counter_run()`：读取并递增 `boot_count`。
- 常用接口：`nvs_open()`、`nvs_get_u32()`、`nvs_set_u32()`、`nvs_commit()`。

## 日志现象

每重启一次，`boot_count` 增加 1。重新烧录 app 通常不会擦除 NVS；
如果执行整片擦除，计数会从头开始。

## 注意事项

- NVS 适合少量键值配置，不适合高频大数据写入。
- 写入后必须 commit。

## 练习

- 增加一个 `last_reason` 字符串键。
- 改成每 10 次启动打印一次特殊日志。
- 用 `idf.py erase-flash` 后观察计数归零。
