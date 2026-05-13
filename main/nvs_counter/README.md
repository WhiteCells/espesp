# nvs_counter

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> nvs_counter: persistent boot counter
```

然后执行：

```sh
idf.py build flash monitor
```

按开发板 reset 后再次观察 `boot_count`。

## 当前模块已有接口

### `esp_err_t nvs_counter_run(void)`

打开 NVS namespace，读取 `boot_count`，递增后写回。

参数：无。

返回值：

- `ESP_OK`：读取、写入和提交成功。
- 其他 `esp_err_t`：NVS 初始化、打开、读取或写入失败。

## 本模块常量

- `NVS_NAMESPACE = "learn"`：NVS 命名空间，类似一个小型分类。
- `BOOT_KEY = "boot_count"`：保存启动次数的 key。

## 常用接口说明

### `esp_err_t nvs_open(const char *name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle)`

打开 NVS 命名空间。

参数：

- `name`：namespace 名称，本模块是 `learn`。
- `open_mode`：打开模式。本模块使用 `NVS_READWRITE`，表示可读可写。
- `out_handle`：输出句柄，后续读写都需要它。

返回值：

- `ESP_OK`：打开成功。
- `ESP_ERR_NVS_NOT_FOUND`：只读模式打开不存在的 namespace 时可能出现。
- 其他错误：NVS 未初始化、分区异常等。

### `esp_err_t nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *out_value)`

读取 `uint32_t` 值。

参数：

- `handle`：`nvs_open()` 得到的句柄。
- `key`：键名，本模块是 `boot_count`。
- `out_value`：输出变量地址。

返回值：

- `ESP_OK`：读取成功。
- `ESP_ERR_NVS_NOT_FOUND`：key 不存在，本模块将其视为第一次运行。

### `nvs_set_u32()`、`nvs_commit()`、`nvs_close()`

`nvs_set_u32(handle, key, value)`：

- 把一个 `uint32_t` 写入 NVS 缓存。
- 写入后还没有保证落盘。

`nvs_commit(handle)`：

- 提交之前的写操作，掉电保存依赖这一步。

`nvs_close(handle)`：

- 关闭句柄，释放 NVS 资源。

## 注意事项

- NVS 适合少量配置，不适合高频日志或大文件。
- 写入 flash 有寿命限制，不要在高频循环里无节制 `commit()`。
- 重新烧录 app 通常不会擦 NVS，`idf.py erase-flash` 会清空。
