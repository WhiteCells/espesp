# wifi_station

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> wifi_station: connect to Wi-Fi
```

配置网络：

```text
ESPESP Menu
  -> WiFi module
```

然后执行：

```sh
idf.py build flash monitor
```

## 当前模块已有接口

### `esp_err_t wifi_station_connect(void)`

初始化网络依赖，启动 Wi-Fi STA，等待连接成功或失败。

参数：无。SSID、密码和重试次数来自 Kconfig。

返回值：

- `ESP_OK`：连接成功并获取 IP。
- `ESP_ERR_INVALID_ARG`：SSID 为空。
- `ESP_ERR_NO_MEM`：事件组创建失败。
- `ESP_FAIL`：超过最大重试次数仍未连接。
- 其他 `esp_err_t`：NVS、netif、Wi-Fi 初始化或事件注册失败。

### `esp_err_t wifi_station_run(void)`

单独运行 Wi-Fi 入网流程，内部调用 `wifi_station_connect()`。

参数：无。

返回值：同 `wifi_station_connect()`。

## 常用接口说明

### `wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT()`

Wi-Fi 驱动初始化配置。

说明：

- `WIFI_INIT_CONFIG_DEFAULT()` 会填好驱动需要的 buffer、队列、任务优先级等默认值。
- 常规 STA 入网场景优先使用默认宏，不建议手动拼字段。

### `esp_err_t esp_wifi_init(const wifi_init_config_t *config)`

初始化 Wi-Fi 驱动。

参数：

- `config`：Wi-Fi 初始化配置，本模块传 `&cfg`。

返回值：

- `ESP_OK`：初始化成功。
- 其他错误：内存不足、状态错误或参数错误。

### `wifi_config_t`

Wi-Fi 模式配置结构体。本模块使用 `wifi_config.sta`。

关键字段：

- `sta.ssid`：目标路由器 SSID，最大长度由 ESP-IDF 结构体定义。
- `sta.password`：目标路由器密码。
- `sta.threshold.authmode`：最低认证模式。本模块空密码时使用 `WIFI_AUTH_OPEN`，否则使用 `WIFI_AUTH_WPA2_PSK`。

本模块用 `snprintf()` 写入 SSID/密码，避免字符串越界。

### `esp_wifi_set_mode()`、`esp_wifi_set_config()`、`esp_wifi_start()`

`esp_wifi_set_mode(WIFI_MODE_STA)`：

- 设置为 station 模式，也就是 ESP 作为客户端连接路由器。

`esp_wifi_set_config(WIFI_IF_STA, &wifi_config)`：

- 把 SSID、密码和认证阈值应用到 STA 接口。

`esp_wifi_start()`：

- 启动 Wi-Fi 驱动，启动后会触发 `WIFI_EVENT_STA_START`。

### `esp_event_handler_instance_register()`

注册事件回调。

参数：

- `event_base`：事件类型。本模块使用 `WIFI_EVENT` 和 `IP_EVENT`。
- `event_id`：事件 ID。Wi-Fi 使用 `ESP_EVENT_ANY_ID`，IP 使用 `IP_EVENT_STA_GOT_IP`。
- `event_handler`：回调函数。
- `event_handler_arg`：传给回调的用户参数，本模块传 `NULL`。
- `instance`：输出 handler 实例句柄，本模块不需要，传 `NULL`。

### `xEventGroupWaitBits()`

等待异步事件结果。

本模块等待：

- `WIFI_CONNECTED_BIT`：拿到 IP。
- `WIFI_FAIL_BIT`：超过重试次数。

关键参数：

- `pdFALSE`：等待结束后不自动清除 bit。
- `pdFALSE`：任意一个 bit 满足就返回。
- `portMAX_DELAY`：一直等待。

## 可配置项

- `CONFIG_ESPESP_WIFI_SSID`：Wi-Fi 名称。
- `CONFIG_ESPESP_WIFI_PASSWORD`：Wi-Fi 密码。
- `CONFIG_ESPESP_WIFI_MAXIMUM_RETRY`：断开后的最大重试次数。

## 注意事项

- Wi-Fi 连接成功不代表网络可用，拿到 `IP_EVENT_STA_GOT_IP` 后才说明 IP 层可用。
- 如果路由器只支持 WPA3 或企业认证，需要调整 `wifi_config_t`。
- `esp_netif_create_default_wifi_sta()` 通常只调用一次，重复创建会产生多余 netif。
