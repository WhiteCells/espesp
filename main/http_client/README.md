# http_client

## 模块接口

### `esp_err_t http_get_run(void)`

先连接 Wi-Fi，再执行一次 HTTP GET，打印响应头和响应体片段。

参数：无。URL、超时和打印长度来自 Kconfig。

返回值：

- `ESP_OK`：请求完成。
- `ESP_ERR_NO_MEM`：HTTP client 创建失败。
- 其他 `esp_err_t`：Wi-Fi 连接或 HTTP 请求失败。

## 本模块结构体

### `http_get_context_t`

用于在 HTTP 事件回调里保存打印进度。

字段说明：

- `printed_bytes`：已经打印的响应体字节数。
- `print_limit`：最多打印多少字节，来自 `CONFIG_CASE2_HTTP_PRINT_LIMIT`。

## 使用的 ESP-IDF 接口和结构体

### `esp_http_client_config_t`

HTTP client 配置。

本模块使用字段：

- `url`：请求 URL，来自 `CONFIG_CASE2_HTTP_URL`。
- `event_handler`：事件回调函数，本模块是 `http_event_handler`。
- `user_data`：传给事件回调的用户上下文，本模块传 `&ctx`。
- `timeout_ms`：请求超时时间，来自 `CONFIG_CASE2_HTTP_TIMEOUT_MS`。

常见可扩展字段：

- `cert_pem`：HTTPS 服务器证书。
- `method`：HTTP 方法，不设置时默认 GET。
- `buffer_size`：接收缓冲区大小。

### `esp_http_client_init(const esp_http_client_config_t *config)`

创建 HTTP client。

参数：

- `config`：HTTP 配置结构体指针。

返回值：

- 非 `NULL`：client 句柄。
- `NULL`：内存不足或配置错误。

### `esp_http_client_perform(esp_http_client_handle_t client)`

执行请求。

参数：

- `client`：`esp_http_client_init()` 返回的句柄。

返回值：

- `ESP_OK`：请求成功完成。
- 其他错误：DNS、连接、超时、协议等错误。

### `esp_http_client_event_t`

HTTP 事件回调参数。

本模块使用字段：

- `event_id`：事件类型。
- `header_key`、`header_value`：响应头 key/value，仅在 `HTTP_EVENT_ON_HEADER` 有效。
- `data`：收到的数据指针，仅在 `HTTP_EVENT_ON_DATA` 有效。
- `data_len`：本次收到的数据长度。
- `user_data`：`esp_http_client_config_t.user_data` 传入的上下文。

常见事件：

- `HTTP_EVENT_ON_CONNECTED`：已连接。
- `HTTP_EVENT_ON_HEADER`：收到一个 header。
- `HTTP_EVENT_ON_DATA`：收到响应体数据，可能被分成多次。
- `HTTP_EVENT_ON_FINISH`：传输完成。
- `HTTP_EVENT_DISCONNECTED`：连接断开。

## 可配置项

- `CONFIG_CASE2_HTTP_URL`：请求地址。
- `CONFIG_CASE2_HTTP_TIMEOUT_MS`：请求超时。
- `CONFIG_CASE2_HTTP_PRINT_LIMIT`：最多打印响应体字节数。

## 注意事项

- 默认 URL 是 HTTP 明文；HTTPS 需要配置证书，否则验证会失败。
- `HTTP_EVENT_ON_DATA` 不保证一次给完整响应体，必须按 chunk 处理。
- 用 ESP32 访问电脑本地服务时，要填电脑局域网 IP，不要填 `127.0.0.1`。
