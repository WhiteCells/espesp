# 26 websocket_client: WebSocket 客户端实时通信

## 模块目标

`websocket_client` 让 ESP32 主动连接一个 WebSocket server，例如电脑端
`server/ws_server/` 或另一个设备提供的 WebSocket 服务。它和
`websocket_server` 分开，便于明确区分“设备监听连接”和“设备主动连接”的两种模式。

这个模块要讲清：

- 先连 Wi-Fi，再建立 WebSocket 长连接。
- WebSocket client 的事件回调模型。
- 文本帧发送、服务端推送接收和二进制帧摘要打印。
- Bearer token 握手鉴权。
- 断线日志、自动重连和心跳配置。

## 建议源码

- `main/websocket_client/websocket_client.c`
- `main/websocket_client/websocket_client.h`
- `main/websocket_client/websocket_client_context.h`
- `main/websocket_client/websocket_client_transport.c`
- `main/websocket_client/websocket_client_transport.h`
- `main/websocket_client/websocket_client_messages.c`
- `main/websocket_client/websocket_client_messages.h`
- `main/websocket_client/README.md`

## 建议配置项

- `CONFIG_ESPESP_WS_CLIENT_URI`
- `CONFIG_ESPESP_WS_CLIENT_AUTH_TOKEN`
- `CONFIG_ESPESP_WS_CLIENT_INITIAL_PAYLOAD`
- `CONFIG_ESPESP_WS_CLIENT_PUBLISH_PERIOD_MS`
- `CONFIG_ESPESP_WS_CLIENT_CONNECT_TIMEOUT_MS`
- `CONFIG_ESPESP_WS_CLIENT_NETWORK_TIMEOUT_MS`
- `CONFIG_ESPESP_WS_CLIENT_RECONNECT_TIMEOUT_MS`
- `CONFIG_ESPESP_WS_CLIENT_PING_INTERVAL_SEC`
- `CONFIG_ESPESP_WS_CLIENT_BUFFER_SIZE`
- `CONFIG_ESPESP_WS_CLIENT_TASK_STACK_SIZE`

## 建议实现范围

- 连接 Wi-Fi。
- 使用 `esp_websocket_client` 连接配置的 WebSocket URI。
- 连接成功后发送初始文本消息。
- 周期发送 status JSON。
- 打印收到的文本帧和二进制帧摘要。
- 连接失败或断开时打印日志，并依赖 client 自动重连。

## 当前内部结构

- `websocket_client.c`：生命周期编排和主循环。
- `websocket_client_transport.c`：URI/header 校验、连接状态同步、事件处理。
- `websocket_client_messages.c`：文本发送、status payload 构造、text/binary chunk 日志摘要。
- `websocket_client_context.h`：内部共享上下文和事件 bit。

## 验收标准

- 启动 `server/ws_server` 后，ESP32 能连接到电脑局域网 IP。
- 服务端能看到 ESP32 发送的初始消息和周期状态。
- 服务端回显或推送消息后，ESP32 串口能看到文本或二进制摘要。
- URI、token、心跳和重连时间能通过 menuconfig 调整。

## 注意事项

- ESP32 访问电脑端服务时，不能使用 `127.0.0.1`，要填电脑在同一局域网内的 IP。
- 默认演示使用明文 `ws://`；生产环境需要结合证书配置使用 `wss://`。
- 大 payload 可能拆成多个 data event，本示例按 chunk 打印而不是重组成完整消息。
