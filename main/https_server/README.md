# https_server

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> https_server: secure LAN HTTPS service
```

配置 Wi-Fi、Bearer token 和 HTTPS server：

```text
ESPESP Menu
  -> WiFi module
ESPESP Menu
  -> LAN service
ESPESP Menu
  -> HTTPS server module
```

然后把 PEM 证书和私钥写入 NVS，再执行：

```sh
idf.py build flash monitor
```

电脑端测试：

```sh
cd server
python -m http_client https://<esp-ip>:443 --token <token> --ca servercert.pem
```

## 当前模块已有接口

### `esp_err_t https_server_run(void)`

先连接 Wi-Fi，从 NVS 读取 PEM 格式的服务器证书和私钥，再启动 ESP-IDF HTTPS server，注册共享 LAN service 路由。

## TLS 凭据

默认从 NVS 读取：

- namespace：`CONFIG_ESPESP_HTTPS_CERT_NVS_NAMESPACE`，默认 `https_srv`
- 证书 key：`CONFIG_ESPESP_HTTPS_CERT_NVS_CERT_KEY`，默认 `servercert`
- 私钥 key：`CONFIG_ESPESP_HTTPS_CERT_NVS_PRIVATE_KEY`，默认 `prvtkey`

这样可以避免把私钥提交到源码仓库。产线或部署脚本应在烧录后写入设备专属证书和私钥。

## 可用路由

- `GET /`：服务说明。
- `GET /health`：公开健康检查。
- `GET /api/v1/status`：需要 `Authorization: Bearer <token>`。
- `POST /api/v1/control`：需要 `Authorization: Bearer <token>`。

## 常用接口说明

- `HTTPD_SSL_CONFIG_DEFAULT()`：创建 HTTPS server 配置。
- `httpd_ssl_start()`：启动 HTTPS server。
- `httpd_ssl_stop()`：启动失败或清理时停止 server。
- `nvs_get_str()`：读取 PEM 证书和私钥。

## 注意事项

- 默认启用 Bearer token，token 为空时服务拒绝启动。
- 每台设备应使用独立证书和私钥。
- 自签证书测试时，电脑端优先使用 CA 文件显式信任证书。
