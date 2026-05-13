# core

## 使用方式

`core` 是公共支撑模块，不在 menuconfig 的模块选择器中单独运行。其他模块会复用这里的 NVS、网络初始化、banner 和 idle helper。

## 当前模块已有接口

### `esp_err_t app_common_init_nvs(void)`

初始化 NVS flash，供 Wi-Fi、运行计数、设备配置等模块复用。

参数：无。

返回值：

- `ESP_OK`：初始化成功，或之前已经初始化过。
- 其他 `esp_err_t`：底层 `nvs_flash_init()` 或 `nvs_flash_erase()` 返回的错误。

内部调用：

- `nvs_flash_init()`：挂载默认 NVS 分区。
- `nvs_flash_erase()`：当返回 `ESP_ERR_NVS_NO_FREE_PAGES` 或 `ESP_ERR_NVS_NEW_VERSION_FOUND` 时擦除 NVS 后重试。

### `esp_err_t app_common_init_netif(void)`

初始化 TCP/IP 网络接口层和默认事件循环。

参数：无。

返回值：

- `ESP_OK`：初始化成功，或默认事件循环已经存在。
- 其他 `esp_err_t`：`esp_netif_init()` 或 `esp_event_loop_create_default()` 的错误。

内部调用：

- `esp_netif_init()`：初始化 ESP-IDF 网络接口层。
- `esp_event_loop_create_default()`：创建默认事件循环，Wi-Fi/IP 事件依赖它分发。

### `void app_common_print_banner(const app_case_t *selected_case)`

打印当前模块启动信息。

参数：

- `selected_case`：当前模块注册项指针，必须不是 `NULL`。

使用字段：

- `selected_case->title`：中文标题。
- `selected_case->key`：模块 key。
- `selected_case->doc_path`：配套文档路径。

### `void app_common_idle_forever(void)`

永久延时循环，保持系统不退出，方便持续观察串口日志。

参数：无。

返回值：不会返回。

## 相关结构体

### `app_case_t`

定义在 `app_common.h`，注册表和 `app_main()` 共用。

字段说明：

- `key`：模块唯一 key，例如 `system_info`、`led_blink`。
- `title`：启动 banner 中显示的标题。
- `doc_path`：模块说明文档路径。
- `run`：模块运行函数，类型是 `esp_err_t (*)(void)`。
- `needs_wifi`：标记模块是否依赖 Wi-Fi，当前用于说明和后续扩展。
- `runs_forever`：模块是否自己常驻运行；如果为 `false`，`app_main()` 会进入公共 idle 循环。

## 常用接口说明

- `nvs_flash_init()`：初始化默认 NVS 分区。
- `nvs_flash_erase()`：NVS 分区无空闲页或版本变化时擦除后重试初始化。
- `esp_netif_init()`：初始化 TCP/IP 网络接口层。
- `esp_event_loop_create_default()`：创建默认事件循环，Wi-Fi/IP/MQTT 等事件分发依赖它。
- `vTaskDelay()`：在 idle helper 中周期让出 CPU。
- `ESP_LOGW()`：记录 NVS 修复类警告。

## 注意事项

- 示例工程里 NVS 异常时自动擦除可以接受；产品工程里要避免无提示擦除用户配置。
- 默认事件循环全局只能创建一次，重复创建会返回 `ESP_ERR_INVALID_STATE`，本模块将其视为已就绪。
