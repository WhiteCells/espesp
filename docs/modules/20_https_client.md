# 20 https_client: HTTPS Client 安全 GET 请求

## 模块概览

- 先连接 Wi-Fi，再发起 HTTPS GET 请求。
- 复用 `esp_http_client` 的事件回调模型处理 header 和 body 分片。
- 支持两种 TLS 校验方式：ESP 内置根证书包，或 NVS 中的自定义 CA 证书。
- 适合访问公网站点，也适合访问自签证书的局域网 HTTPS 服务。

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> 20 https_client
```

配置 Wi-Fi 和 HTTPS client：

```text
ESPESP Menu
  -> WiFi
ESPESP Menu
  -> HTTPS client module
```

### 访问公网站点

保持默认校验模式 `Use ESP x509 certificate bundle`，URL 可以直接使用：

```text
https://httpbin.org/get
```

### 访问自签证书服务

把校验模式切换到 `Load trusted CA cert from NVS`，再把 PEM 格式证书写入：

- namespace：`CONFIG_ESPESP_HTTPS_CLIENT_CERT_NVS_NAMESPACE`
- key：`CONFIG_ESPESP_HTTPS_CLIENT_CERT_NVS_CERT_KEY`

如果你是在联调自签证书 HTTPS server，可以把服务端 PEM 证书本身作为 trusted CA 写入 NVS。

## 源码位置

- HTTPS client 入口：`main/https_client/https_client.c`
- TLS 安全策略：`main/https_client/https_client_security.c`
- TLS 安全头：`main/https_client/https_client_security.h`
- 通用 HTTP request 生命周期：`main/http_client/http_request.c`
- 通用 HTTP 响应日志：`main/http_client/http_response_log.c`
- Wi-Fi 复用：`main/wifi_station/wifi_station.c`

## 当前模块接口参考

- `https_client_run()`：连接 Wi-Fi，准备 TLS 校验策略，执行一次 HTTPS GET。
- `https_client_security_prepare()`：选择证书 bundle 或 NVS CA 校验模式。
- `https_client_security_apply()`：把 TLS 参数挂到 `esp_http_client_config_t`。
- `http_request_perform()`：创建 client、执行请求、读取状态码和 content length、清理资源。
- `http_response_log_event_handler()`：处理 header 和 body 分片日志。

## 常用接口说明

- `wifi_station_connect()`：请求前复用 Wi-Fi 入网流程。
- `esp_http_client_config_t`：配置 URL、TLS 校验策略、超时和事件回调。
- `esp_http_client_perform()`：执行 HTTPS 请求并驱动事件回调。
- `esp_crt_bundle_attach()`：挂载 ESP-IDF 内置根证书包。
- `app_common_init_nvs()`：NVS CA 模式下确保 NVS 已初始化。
- `nvs_open()` / `nvs_get_str()`：从 NVS 读取 PEM 证书。

## 配置项

- `CONFIG_ESPESP_HTTPS_CLIENT_URL`：HTTPS GET URL。
- `CONFIG_ESPESP_HTTPS_CLIENT_TIMEOUT_MS`：HTTPS 请求超时时间，单位 ms。
- `CONFIG_ESPESP_HTTPS_CLIENT_PRINT_LIMIT`：最多打印的响应体字节数。
- `CONFIG_ESPESP_HTTPS_CLIENT_SKIP_COMMON_NAME_CHECK`：是否跳过证书 common name 校验。
- `CONFIG_ESPESP_HTTPS_CLIENT_CERT_NVS_NAMESPACE`：trusted CA 的 NVS namespace。
- `CONFIG_ESPESP_HTTPS_CLIENT_CERT_NVS_CERT_KEY`：trusted CA 的 NVS key。

## 日志现象

成功时会看到：

- TLS 校验模式说明
- HTTP header
- 响应体前若干字节
- status code 和 content length

## 注意事项

- URL 必须以 `https://` 开头。
- 公网站点优先使用证书 bundle 校验模式。
- 自签证书服务优先使用 NVS CA 校验模式，不建议把 TLS 校验做成“完全不验证”。
- 如果证书 subject 和 URL 里的主机名或 IP 不匹配，才考虑临时开启 `Skip certificate common name check`。
- 用 ESP32 访问电脑本地 HTTPS 服务时，要填电脑局域网 IP，不要填 `127.0.0.1`。
