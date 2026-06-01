# http_client

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> http_client: Wi-Fi + HTTP GET
```

配置 Wi-Fi 和 URL：

```text
ESPESP Menu
  -> WiFi module
ESPESP Menu
  -> HTTP client module
```

然后执行：

```sh
idf.py build flash monitor
```

## 当前模块已有接口

### `esp_err_t http_client_run(void)`

先连接 Wi-Fi，再执行一次 HTTP GET。模块入口只负责装配配置与编排，HTTP 请求生命周期和响应日志策略分别下沉到独立文件。

参数：无。URL、超时和打印长度来自 Kconfig。

返回值：

- `ESP_OK`：请求完成。
- `ESP_ERR_NO_MEM`：HTTP client 创建失败。
- 其他 `esp_err_t`：Wi-Fi 连接或 HTTP 请求失败。

## 当前内部结构

- `http_client.c`：模块入口，负责 Wi-Fi、默认配置和结果汇总。
- `http_request.c`：负责 `esp_http_client_init()` / `perform()` / `cleanup()` 生命周期。
- `http_response_log.c`：负责事件回调、响应头日志和 body 输出限流。

## 本模块结构体

### `http_request_t`

描述一次 HTTP 请求如何执行。

字段说明：

- `operation_name`：日志里的请求动作名，例如 `GET`。
- `log_tag`：当前请求使用的日志 tag。
- `client_config`：传给 `esp_http_client_init()` 的配置。

### `http_response_meta_t`

保存请求完成后的响应元信息。

字段说明：

- `status_code`：HTTP 状态码。
- `content_length`：响应体长度；chunked 响应可能为 `-1`。

### `http_response_log_t`

用于在 HTTP 事件回调里保存打印进度和输出策略。

字段说明：

- `tag`：事件回调使用的日志 tag。
- `printed_bytes`：已经打印的响应体字节数。
- `print_limit`：最多打印多少字节，来自 `CONFIG_ESPESP_HTTP_PRINT_LIMIT`。
- `limit_reached`：是否已经提示过输出截断。

## 常用接口说明

### `esp_http_client_config_t`

HTTP client 配置。

本模块使用字段：

- `url`：请求 URL，来自 `CONFIG_ESPESP_HTTP_URL`。
- `event_handler`：事件回调函数，本模块是 `http_response_log_event_handler()`。
- `user_data`：传给事件回调的用户上下文，本模块传 `http_response_log_t`。
- `timeout_ms`：请求超时时间，来自 `CONFIG_ESPESP_HTTP_TIMEOUT_MS`。

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

- `CONFIG_ESPESP_HTTP_URL`：请求地址。
- `CONFIG_ESPESP_HTTP_TIMEOUT_MS`：请求超时。
- `CONFIG_ESPESP_HTTP_PRINT_LIMIT`：最多打印响应体字节数。

## 注意事项

- 默认 URL 是 HTTP 明文；HTTPS 需要配置证书，否则验证会失败。
- `HTTP_EVENT_ON_DATA` 不保证一次给完整响应体，必须按 chunk 处理。
- `http_client_run()` 现在只保留模块编排职责，后续如果增加 POST/鉴权/证书逻辑，优先放到 `http_request.c` 或新的策略文件。
- 用 ESP32 访问电脑本地服务时，要填电脑局域网 IP，不要填 `127.0.0.1`。
