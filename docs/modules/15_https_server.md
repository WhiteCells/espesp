# 15 https_server: 安全局域网 HTTPS 服务

## 模块概览

- 先连接 Wi-Fi，再启动 ESP-IDF HTTPS server。
- 使用 `httpd_ssl_start()` 提供 TLS 加密。
- 从 NVS 读取 PEM 格式的服务器证书和私钥，不把私钥写进源码仓库。
- 复用 LAN service 的 Bearer token 鉴权、请求限制和状态/控制 API。

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> 15 https_server
```

配置 Wi-Fi、LAN service token 和 HTTPS 参数：

```text
ESPESP Menu
  -> WiFi
ESPESP Menu
  -> LAN service
ESPESP Menu
  -> HTTPS server module
```

HTTPS server 启动前需要把证书和私钥写入 NVS。默认位置：

- namespace：`https_srv`
- server certificate key：`servercert`
- private key key：`prvtkey`

刷入并打开串口后，记录日志里的 ESP32 IP，例如：

```text
got ip: 192.168.1.45
```

电脑端运行测试 client：

```sh
cd server
python -m http_client https://192.168.1.45:443 --token <token> --ca servercert.pem
```

自签证书快速联调可以临时使用：

```sh
python -m http_client https://192.168.1.45:443 --token <token> --insecure
```

## 源码位置

- HTTPS server 入口：`main/https_server/https_server.c`
- HTTPS server runtime：`main/https_server/https_server_runtime.c`
- HTTPS server 私有头：`main/https_server/https_server_runtime.h`
- TLS 凭据读取：`main/https_server/https_server_credentials.c`
- TLS 凭据头：`main/https_server/https_server_credentials.h`
- 共享路由：`main/lan_service/lan_service.c`
- 测试 client：`server/http_client/`

## 当前模块接口参考

- `https_server_run()`：连接 Wi-Fi，启动 HTTPS server，注册 LAN service 路由后常驻运行。
- `https_server_runtime_prepare()`：校验鉴权配置，构造 HTTPS runtime 默认配置。
- `https_server_runtime_start()`：加载 TLS 凭据，启动 HTTPS server 并注册共享 LAN service 路由。
- `https_server_credentials_load()`：从 NVS namespace 读取 server certificate 和 private key。
- `https_server_credentials_validate()`：检查证书和私钥是否像 PEM 格式。
- `https_server_credentials_release()`：释放临时证书和私钥缓冲区。
- `lan_service_register_handlers()`：注册共享 LAN service 路由。

## 常用接口说明

- `wifi_station_connect()`：HTTPS server 启动前复用 Wi-Fi 入网流程。
- `app_common_init_nvs()`：读取 TLS 凭据前确保 NVS 已初始化。
- `nvs_open()`：打开 TLS 凭据所在 namespace。
- `nvs_get_str()`：读取 PEM 格式证书和私钥。
- `nvs_close()`：关闭 NVS handle。
- `HTTPD_SSL_CONFIG_DEFAULT()`：生成 HTTPS server 默认配置。
- `httpd_ssl_config_t`：配置 TLS 证书、私钥、端口、握手超时和底层 HTTP server 参数。
- `httpd_ssl_start()`：启动 HTTPS server。
- `httpd_ssl_stop()`：注册路由失败时停止 HTTPS server。
- `httpd_register_uri_handler()`：HTTPS server 复用 HTTP server 的路由注册机制。

## 配置项

LAN service 通用配置：

- `CONFIG_ESPESP_LAN_SERVICE_REQUIRE_AUTH`：是否要求 `/api/v1/status` 和 `/api/v1/control` 使用 Bearer token。
- `CONFIG_ESPESP_LAN_SERVICE_AUTH_TOKEN`：Bearer token；开启鉴权时不能为空。
- `CONFIG_ESPESP_LAN_SERVICE_MAX_BODY_LEN`：POST 请求体最大长度，超过后返回错误。
- `CONFIG_ESPESP_LAN_SERVICE_RECV_TIMEOUT_SEC`：socket 接收超时，单位秒。
- `CONFIG_ESPESP_LAN_SERVICE_SEND_TIMEOUT_SEC`：socket 发送超时，单位秒。

HTTPS server 配置：

- `CONFIG_ESPESP_HTTPS_SERVER_PORT`：HTTPS 监听端口。
- `CONFIG_ESPESP_HTTPS_SERVER_CTRL_PORT`：ESP HTTPS server 内部控制端口，需和公开端口不同。
- `CONFIG_ESPESP_HTTPS_SERVER_STACK_SIZE`：HTTPS server 任务栈大小，通常应大于 HTTP server。
- `CONFIG_ESPESP_HTTPS_SERVER_MAX_OPEN_SOCKETS`：最大 TLS socket 数量。
- `CONFIG_ESPESP_HTTPS_HANDSHAKE_TIMEOUT_MS`：TLS 握手超时，单位 ms。
- `CONFIG_ESPESP_HTTPS_CERT_NVS_NAMESPACE`：TLS 凭据所在 NVS namespace。
- `CONFIG_ESPESP_HTTPS_CERT_NVS_CERT_KEY`：PEM 服务器证书的 NVS key。
- `CONFIG_ESPESP_HTTPS_CERT_NVS_PRIVATE_KEY`：PEM 私钥的 NVS key。

## 可用路由

- `GET /`：纯文本服务说明。
- `GET /health`：公开健康检查。
- `GET /api/v1/status`：受 Bearer token 保护的状态接口。
- `POST /api/v1/control`：受 Bearer token 保护的控制接口。

## 注意事项

- 生产部署时不要复用公开示例证书；每台设备应使用自己的证书和私钥。
- 私钥应通过 NVS 分区、产线工具或安全配置流程写入，不应提交到 Git。
- 自签证书联调时，优先用 `--ca servercert.pem` 显式信任证书；`--insecure` 只适合临时排障。
- HTTPS 加密传输不等于完整安全体系，仍需要 Bearer token、请求大小限制和业务层权限判断。
