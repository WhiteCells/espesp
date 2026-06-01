# 26 websocket_server: WebSocket 服务端实时通信

## 模块目标

HTTP 更像请求-响应，MQTT 更像发布订阅。`websocket_server` 让 ESP32 监听
WebSocket upgrade 请求，适合浏览器、桌面端或另一个设备主动连接 ESP32，做状态推送、
即时控制和低开销交互。

这个模块要讲清：

- HTTP upgrade 到 WebSocket。
- 文本帧和二进制帧。
- 心跳推送、断线日志和多客户端限制。
- 可选鉴权。
- 它和 HTTP REST、MQTT 的适用场景差异。

## 建议源码

- `main/websocket_server/websocket_server.c`
- `main/websocket_server/websocket_server.h`
- `main/websocket_server/websocket_server_runtime.c`
- `main/websocket_server/websocket_server_handlers.c`
- `main/websocket_server/websocket_server_messages.c`
- `main/websocket_server/README.md`

## 建议配置项

- `CONFIG_ESPESP_WS_SERVER_PORT`
- `CONFIG_ESPESP_WS_SERVER_PATH`
- `CONFIG_ESPESP_WS_SERVER_AUTH_TOKEN`
- `CONFIG_ESPESP_WS_SERVER_PUBLISH_PERIOD_MS`
- `CONFIG_ESPESP_WS_SERVER_MAX_CLIENTS`

## 建议实现范围

- 连接 Wi-Fi。
- 启动 HTTP server 并注册 WebSocket route。
- 接收客户端文本命令并打印。
- 接收二进制帧并打印摘要。
- 周期发送 status JSON。
- 超过最大客户端数时拒绝新的握手。
- 可选在握手前检查 Bearer token。

## 验收标准

- 电脑端 WebSocket client 能连接 ESP32。
- 客户端发送文本后串口能看到内容。
- ESP32 能周期推送状态 JSON。
- 超过最大客户端数时连接会被拒绝。

## 注意事项

- WebSocket 长连接会占用 socket 和内存。
- 当前示例使用明文 `ws://`。
- 浏览器如果是通过 HTTPS 页面连接，通常要改成 `wss://`。
- 这个模块默认只处理完整文本/二进制帧，分片重组可以后续增强。
