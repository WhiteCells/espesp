# 14 http_server: 局域网 HTTP 服务

## 模块概览

- 先连接 Wi-Fi，再在 ESP32 上启动 HTTP server。
- 用统一的 LAN service 路由层提供状态和控制 API。
- 对状态和控制接口使用 Bearer token 鉴权。
- 设置请求体大小、socket 数量、超时和安全响应头。
- 理解 HTTP 明文服务只适合受控局域网，敏感控制优先使用 HTTPS。

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> 14 http_server
```

配置 Wi-Fi、LAN service token 和 HTTP server：

```text
ESPESP Menu
  -> WiFi
ESPESP Menu
  -> LAN service
ESPESP Menu
  -> HTTP server module
```

刷入并打开串口后，记录日志里的 ESP32 IP，例如：

```text
got ip: 192.168.1.45
```

电脑端运行测试 client：

```sh
cd server
python -m http_client http://192.168.1.45:80 --token <token>
```

## 源码位置

- HTTP server 入口：`main/http_server/http_server.c`
- HTTP server runtime：`main/http_server/http_server_runtime.c`
- HTTP server 私有头：`main/http_server/http_server_runtime.h`
- 共享路由：`main/lan_service/lan_service.c`
- Wi-Fi 复用：`main/wifi_station/wifi_station.c`
- 测试 client：`server/http_client/`

## 当前模块接口参考

- `http_server_run()`：连接 Wi-Fi，启动 HTTP server，注册 LAN service 路由后常驻运行。
- `lan_service_register_handlers()`：注册共享 LAN service 路由。
- `lan_service_config_t`：传入服务名、协议、token、body 上限和鉴权开关。

## 常用接口说明

- `wifi_station_connect()`：HTTP server 启动前复用 Wi-Fi 入网流程。
- `HTTPD_DEFAULT_CONFIG()`：生成 HTTP server 默认配置。
- `httpd_config_t`：配置端口、控制端口、栈大小、socket 数量、超时和 URI 匹配函数。
- `httpd_start()`：启动 HTTP server。
- `httpd_register_uri_handler()`：注册 `GET /health`、`GET /api/v1/status`、`POST /api/v1/control` 等路由。
- `httpd_req_get_hdr_value_str()`：读取 `Authorization` header。
- `httpd_req_recv()`：读取 POST 请求体。
- `httpd_resp_set_hdr()`：设置安全响应头。
- `httpd_resp_sendstr()`、`httpd_resp_send_err()`：发送正常响应或错误响应。
- `httpd_stop()`：启动后注册失败时停止 server。

## 配置项

LAN service 通用配置：

- `CONFIG_ESPESP_LAN_SERVICE_REQUIRE_AUTH`：是否要求 `/api/v1/status` 和 `/api/v1/control` 使用 Bearer token。
- `CONFIG_ESPESP_LAN_SERVICE_AUTH_TOKEN`：Bearer token；开启鉴权时不能为空。
- `CONFIG_ESPESP_LAN_SERVICE_MAX_BODY_LEN`：POST 请求体最大长度，超过后返回错误。
- `CONFIG_ESPESP_LAN_SERVICE_RECV_TIMEOUT_SEC`：socket 接收超时，单位秒。
- `CONFIG_ESPESP_LAN_SERVICE_SEND_TIMEOUT_SEC`：socket 发送超时，单位秒。

HTTP server 配置：

- `CONFIG_ESPESP_HTTP_SERVER_PORT`：HTTP 监听端口。
- `CONFIG_ESPESP_HTTP_SERVER_CTRL_PORT`：ESP HTTP server 内部控制端口，需和公开端口不同。
- `CONFIG_ESPESP_HTTP_SERVER_STACK_SIZE`：HTTP server 任务栈大小。
- `CONFIG_ESPESP_HTTP_SERVER_MAX_OPEN_SOCKETS`：最大打开 socket 数量，影响并发连接上限。

## 可用路由

- `GET /`：纯文本服务说明。
- `GET /health`：公开健康检查，不含敏感信息。
- `GET /api/v1/status`：受 Bearer token 保护的状态接口。
- `POST /api/v1/control`：受 Bearer token 保护的控制接口。

## 日志现象

成功时会看到：

- Wi-Fi 获取 IP。
- HTTP server 启动端口。
- 电脑端访问 `/api/v1/control` 时，串口打印控制请求。

## 注意事项

- HTTP 是明文传输，Bearer token 也会以明文经过局域网；生产控制链路优先使用 `https_server`。
- 电脑访问 ESP32 要用 ESP32 的局域网 IP，不要用 `127.0.0.1`。
- 如果电脑访问失败，确认电脑和 ESP32 在同一网络，路由器没有开启 AP/client 隔离。

## 扩展方向

- 把监听端口从 80 改成 8080。
- 把 `/api/v1/control` 解析成具体命令，再转发给 LED 或机器人状态模块。
- 在 `/api/v1/status` 中加入 RSSI、当前 Wi-Fi SSID 或业务状态。
