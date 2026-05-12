# registry

## 模块接口

### `const app_case_t *app_registry_selected(void)`

根据 `menuconfig -> Case2 ESP Learning -> Module selector` 的选择返回当前模块。

参数：无。

返回值：

- 指向 `app_case_t` 的只读指针。
- 如果没有匹配到配置项，默认返回 `system_info`。

依赖的配置宏：

- `CONFIG_CASE2_MODULE_SYSTEM_INFO`
- `CONFIG_CASE2_MODULE_RTOS_TASKS`
- `CONFIG_CASE2_MODULE_LED_BLINK`
- `CONFIG_CASE2_MODULE_NVS_COUNTER`
- `CONFIG_CASE2_MODULE_WIFI_STATION`
- `CONFIG_CASE2_MODULE_HTTP_GET`
- `CONFIG_CASE2_MODULE_MQTT_CLIENT`
- `CONFIG_CASE2_MODULE_ADC_READER`
- `CONFIG_CASE2_MODULE_UART_ECHO`
- `CONFIG_CASE2_MODULE_I2C_SCAN`
- `CONFIG_CASE2_MODULE_MICROPHONE`
- `CONFIG_CASE2_MODULE_SPEAKER`
- `CONFIG_CASE2_MODULE_DISPLAY`

### `const app_case_t *app_registry_all(size_t *count)`

返回完整模块表。

参数：

- `count`：输出参数。传入非 `NULL` 时，函数会写入模块数量；传 `NULL` 表示只取数组指针。

返回值：

- 指向内部静态 `app_case_t` 数组的首元素。

## 相关结构体

### `app_case_t`

字段说明：

- `key`：模块 key，建议与文件夹名一致，便于日志和文档定位。
- `title`：模块标题，显示在启动 banner。
- `doc_path`：配套文档路径。
- `run`：模块运行函数。签名必须是 `esp_err_t xxx_run(void)`。
- `needs_wifi`：是否需要联网。
- `runs_forever`：是否常驻运行。

## 新增模块步骤

1. 新建 `main/<module>/<module>.c`、`.h` 和 `README.md`。
2. 在 `main/CMakeLists.txt` 添加源文件。
3. 在 `main/Kconfig.projbuild` 添加 `CONFIG_CASE2_MODULE_<NAME>` 和参数项。
4. 在 `app_registry.c` include 头文件，并添加 `app_case_t` 条目。
5. 在 `docs/modules/` 添加对应文档。

## 注意事项

- `app_registry_all()` 返回的是内部静态数组，不要释放或修改。
- `doc_path` 和实际文档不一致时，串口 banner 会误导使用者。
