# http_server

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> http_server: production-style LAN HTTP service
```

配置 Wi-Fi、Bearer token 和 HTTP server：

```text
ESPESP Menu
  -> WiFi module
ESPESP Menu
  -> LAN service
ESPESP Menu
  -> HTTP server module
```

然后执行：

```sh
idf.py build flash monitor
```

电脑端测试：

```sh
cd server
python -m http_client http://<esp-ip>:80 --token <token>
```

## 当前模块已有接口

### `esp_err_t http_server_run(void)`

先连接 Wi-Fi，再启动 ESP-IDF HTTP server，注册共享 LAN service 路由，常驻等待局域网请求。

HTTP 适合受控局域网调试和非敏感状态查询；涉及控制命令、凭据或敏感状态时应使用 `https_server`。

## 可用路由

- `GET /`：服务说明。
- `GET /health`：公开健康检查。
- `GET /api/v1/status`：需要 `Authorization: Bearer <token>`。
- `POST /api/v1/control`：需要 `Authorization: Bearer <token>`。

## 可配置项

- `CONFIG_ESPESP_LAN_SERVICE_REQUIRE_AUTH`：状态和控制接口是否启用 Bearer token。
- `CONFIG_ESPESP_LAN_SERVICE_AUTH_TOKEN`：Bearer token。
- `CONFIG_ESPESP_LAN_SERVICE_MAX_BODY_LEN`：POST body 上限。
- `CONFIG_ESPESP_HTTP_SERVER_PORT`：HTTP server 对外端口。
- `CONFIG_ESPESP_HTTP_SERVER_CTRL_PORT`：内部控制端口。
- `CONFIG_ESPESP_HTTP_SERVER_STACK_SIZE`：HTTP server 任务栈大小。
- `CONFIG_ESPESP_HTTP_SERVER_MAX_OPEN_SOCKETS`：最大 socket 数。

## 常用接口说明

- `HTTPD_DEFAULT_CONFIG()`：创建默认 HTTP server 配置。
- `httpd_start()`：启动 HTTP server。
- `httpd_register_uri_handler()`：注册共享 LAN service 路由。
- `httpd_uri_match_wildcard()`：允许带 query string 的 URI 匹配路由。

## 注意事项

- 默认启用鉴权，token 为空时服务拒绝启动。
- HTTP 明文传输无法保护 token，生产控制链路应切换到 HTTPS。
- 电脑访问 ESP32 时要使用串口日志里的局域网 IP。
