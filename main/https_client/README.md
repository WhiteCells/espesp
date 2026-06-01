# https_client

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> https_client: Wi-Fi + HTTPS GET
```

配置 Wi-Fi 和 HTTPS client：

```text
ESPESP Menu
  -> WiFi module
ESPESP Menu
  -> HTTPS client module
```

然后执行：

```sh
idf.py build flash monitor
```

## 当前模块已有接口

### `esp_err_t https_client_run(void)`

先连接 Wi-Fi，再执行一次 HTTPS GET。模块入口只负责装配 HTTPS client 配置与 TLS 校验策略，HTTP 请求生命周期和响应日志继续复用 `http_client` 模块里的通用实现。

参数：无。URL、TLS 校验模式、超时和打印长度来自 Kconfig。

返回值：

- `ESP_OK`：请求完成。
- `ESP_ERR_INVALID_ARG`：URL 或 TLS 配置不合法。
- `ESP_ERR_NO_MEM`：创建 HTTP client 或加载证书时内存不足。
- 其他 `esp_err_t`：Wi-Fi、NVS、TLS 握手或 HTTP 请求失败。

## 当前内部结构

- `https_client.c`：模块入口，负责 Wi-Fi、TLS 安全策略装配和结果汇总。
- `https_client_security.c`：负责 TLS 校验模式选择、证书 bundle 或 NVS CA 证书加载。
- `https_client_security.h`：TLS 安全策略结构和函数声明。
- `http_client/http_request.c`：复用 HTTP client 生命周期。
- `http_client/http_response_log.c`：复用响应日志和 body 输出限流。

## TLS 校验模式

### ESP 证书包

选择 `Use ESP x509 certificate bundle` 时，客户端使用 ESP-IDF 内置根证书集合校验公网站点证书。

适合：

- `https://httpbin.org/get`
- 其他由公开 CA 签发证书的 HTTPS 服务

### NVS CA 证书

选择 `Load trusted CA cert from NVS` 时，客户端从 NVS 读取 PEM 格式 CA 证书。

默认位置：

- namespace：`CONFIG_ESPESP_HTTPS_CLIENT_CERT_NVS_NAMESPACE`，默认 `https_cli`
- key：`CONFIG_ESPESP_HTTPS_CLIENT_CERT_NVS_CERT_KEY`，默认 `cacert`

如果服务端使用自签证书，可以把服务端 PEM 证书本身作为受信任 CA 证书写入这里。

## 常用接口说明

- `wifi_station_connect()`：请求前复用 Wi-Fi 入网流程。
- `https_client_security_prepare()`：选择 TLS 校验模式，必要时加载 NVS 里的 CA 证书。
- `https_client_security_apply()`：把 TLS 校验参数挂到 `esp_http_client_config_t`。
- `http_request_perform()`：封装 `esp_http_client_init()` / `perform()` / `cleanup()`。
- `http_response_log_event_handler()`：打印 header 和响应体分片。
- `esp_crt_bundle_attach()`：把 ESP-IDF 根证书包挂到 TLS 配置。
- `nvs_get_str()`：读取 PEM 格式 CA 证书。

## 可配置项

- `CONFIG_ESPESP_HTTPS_CLIENT_URL`：HTTPS 请求地址。
- `CONFIG_ESPESP_HTTPS_CLIENT_TIMEOUT_MS`：请求超时。
- `CONFIG_ESPESP_HTTPS_CLIENT_PRINT_LIMIT`：最多打印响应体字节数。
- `CONFIG_ESPESP_HTTPS_CLIENT_SKIP_COMMON_NAME_CHECK`：是否跳过证书 common name 校验。
- `CONFIG_ESPESP_HTTPS_CLIENT_CERT_NVS_NAMESPACE`：NVS CA 证书 namespace。
- `CONFIG_ESPESP_HTTPS_CLIENT_CERT_NVS_CERT_KEY`：NVS CA 证书 key。

## 注意事项

- HTTPS URL 必须以 `https://` 开头。
- 访问公网站点时优先使用证书 bundle 校验模式。
- 访问自签证书服务时，优先把 CA 证书写入 NVS；只有在受控联调场景下才建议关闭 common name 校验。
- 用 ESP32 访问电脑本地 HTTPS 服务时，要填电脑局域网 IP，不要填 `127.0.0.1`。
