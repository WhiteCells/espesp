# 06 http_client: HTTP Client GET 请求

## 模块概览

- 先连接 Wi-Fi，再发 HTTP 请求。
- `esp_http_client_init()` 创建 client。
- `esp_http_client_perform()` 执行请求。
- HTTP 数据通过事件回调分段到达。
- `esp_http_client_cleanup()` 释放资源。

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> 06 http_client
```

配置 Wi-Fi 和 URL：

```text
ESPESP Menu
  -> WiFi
ESPESP Menu
  -> HTTP client module
```

## 源码位置

- `main/http_client/http_client.c`
- `main/http_client/http_request.c`
- `main/http_client/http_response_log.c`
- Wi-Fi 复用：`main/wifi_station/wifi_station.c`

## 当前模块接口参考

- `http_client_run()`：连接 Wi-Fi 后执行一次 GET。
- `http_request_perform()`：封装 `esp_http_client` 的创建、执行、结果读取和清理。
- `http_response_log_event_handler()`：处理 HTTP 连接、响应头、响应体分片、完成和断开事件。
- `http_response_log_t`：记录已经打印的 body 字节数和打印上限。

## 常用接口说明

- `wifi_station_connect()`：请求前复用 Wi-Fi 入网流程。
- `esp_http_client_config_t`：配置 URL、事件回调、用户上下文和超时。
- `esp_http_client_init()`：创建 HTTP client 句柄。
- `esp_http_client_perform()`：执行请求并驱动事件回调。
- `esp_http_client_get_status_code()`：读取 HTTP 状态码。
- `esp_http_client_get_content_length()`：读取响应体长度，chunked 响应可能未知。
- `esp_http_client_cleanup()`：释放 client 资源。
- `esp_http_client_event_t`：事件回调参数，包含 header、data、data_len 和 user_data。

## 配置项

- `CONFIG_ESPESP_WIFI_SSID` / `CONFIG_ESPESP_WIFI_PASSWORD`：请求前使用的 Wi-Fi 入网参数。
- `CONFIG_ESPESP_HTTP_URL`：GET 请求 URL，默认是明文 HTTP 地址。
- `CONFIG_ESPESP_HTTP_TIMEOUT_MS`：HTTP 请求超时时间，单位 ms。
- `CONFIG_ESPESP_HTTP_PRINT_LIMIT`：最多打印的响应体字节数，避免串口输出过长。

## 日志现象

成功时会看到 HTTP header、响应前若干字节、status code 和 content length。

## 注意事项

- 默认使用 HTTP 明文地址，HTTPS 需要证书配置。
- 响应体可能分段到达，不要假设一次回调就是完整 body。

## 扩展方向

- 把 URL 改成自己的 HTTP 服务。
- 把打印响应长度改成 128 字节。
- 在 `http_response_log_t` 中继续补充总接收字节数或响应摘要统计。
