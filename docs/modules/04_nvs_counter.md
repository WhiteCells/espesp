# 04 nvs_counter: NVS 键值存储

## 模块概览

- `nvs_flash_init()` 初始化 NVS。
- `nvs_open()` 打开 namespace。
- `nvs_get_u32()` 读取计数。
- `nvs_set_u32()` 写入计数。
- `nvs_commit()` 提交写入。

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> 04 nvs_counter
```

然后烧录运行，按开发板 reset，多看几次日志。

## 源码位置

- `main/nvs_counter/nvs_counter.c`
- NVS 公共初始化：`main/core/app_common.c`

## 当前模块接口参考

- `nvs_counter_run()`：读取并递增 `boot_count`。

## 常用接口说明

- `app_common_init_nvs()`：项目封装的 NVS 初始化入口，处理首次初始化和分区版本变化。
- `nvs_open()`：打开指定 namespace，拿到后续读写使用的 handle。
- `nvs_get_u32()`：读取 `uint32_t` 键值；键不存在时会返回 `ESP_ERR_NVS_NOT_FOUND`。
- `nvs_set_u32()`：写入 `uint32_t` 键值。
- `nvs_commit()`：提交写入，未 commit 的修改掉电后不保证保存。
- `nvs_close()`：关闭 handle，释放 NVS 资源。

## 配置项

- `CONFIG_ESPESP_NVS_NOTE`：menuconfig 中的说明文本，不参与运行逻辑。
- NVS 分区表：决定可用于键值存储的容量和擦写边界。
- 本模块源码内 namespace 为 `espesp`，key 为 `boot_count`。

## 日志现象

每重启一次，`boot_count` 增加 1。重新烧录 app 通常不会擦除 NVS；
如果执行整片擦除，计数会从头开始。

## 注意事项

- NVS 适合少量键值配置，不适合高频大数据写入。
- 写入后必须 commit。

## 扩展方向

- 增加一个 `last_reason` 字符串键。
- 改成每 10 次启动打印一次特殊日志。
- 用 `idf.py erase-flash` 后观察计数归零。
