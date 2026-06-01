# websocket_client

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> websocket_client: Wi-Fi + WebSocket client
```

配置 Wi-Fi 和 WebSocket client：

```text
ESPESP Menu
  -> WiFi module
ESPESP Menu
  -> WebSocket client module
```

然后执行：

```sh
idf.py build flash monitor
```

## 当前模块已有接口

### `esp_err_t websocket_client_run(void)`

先连接 Wi-Fi，再作为客户端连接到 `CONFIG_ESPESP_WS_CLIENT_URI`。连接成功后，
模块会发送一条初始文本消息，随后周期发送状态 JSON；服务端推送或回显的文本、
二进制帧会打印到串口日志。

## 代码结构

`websocket_client` 对外仍只暴露 `websocket_client_run()`，内部按职责拆成：

- `websocket_client.c`：模块入口和生命周期编排。负责连接 Wi-Fi、创建 client、等待首次连上并驱动状态上报主循环。
- `websocket_client_transport.c`：连接层。负责 URI/header 校验、WebSocket 事件回调、连接 bit 和错误状态同步。
- `websocket_client_messages.c`：消息层。负责初始 payload / status JSON 发送，以及收到的 text/binary chunk 日志摘要。
- `websocket_client_context.h`：内部共享状态、事件 bit 和公共常量。
- `websocket_client_transport.h`、`websocket_client_messages.h`：内部接口声明。

## 可用能力

- 连接 `ws://` 或 `wss://` WebSocket endpoint。
- 可选 Bearer 鉴权，握手时发送 `Authorization: Bearer <token>`。
- 连接成功后发送初始文本 payload。
- 周期发送 `websocket_client` 状态 JSON。
- 打印服务端文本帧、二进制帧摘要和连接状态。
- 使用 `esp_websocket_client` 的自动重连能力。

## 可配置项

- `CONFIG_ESPESP_WS_CLIENT_URI`：目标 WebSocket server 地址。
- `CONFIG_ESPESP_WS_CLIENT_AUTH_TOKEN`：Bearer token，留空表示不发送鉴权头。
- `CONFIG_ESPESP_WS_CLIENT_INITIAL_PAYLOAD`：连接后发送的一次性文本消息。
- `CONFIG_ESPESP_WS_CLIENT_PUBLISH_PERIOD_MS`：状态发送周期。
- `CONFIG_ESPESP_WS_CLIENT_CONNECT_TIMEOUT_MS`：首次连接等待时间。
- `CONFIG_ESPESP_WS_CLIENT_NETWORK_TIMEOUT_MS`：发送等网络操作超时。
- `CONFIG_ESPESP_WS_CLIENT_RECONNECT_TIMEOUT_MS`：断线后的重连间隔。
- `CONFIG_ESPESP_WS_CLIENT_PING_INTERVAL_SEC`：ping 心跳间隔。
- `CONFIG_ESPESP_WS_CLIENT_BUFFER_SIZE`：接收缓冲区大小。
- `CONFIG_ESPESP_WS_CLIENT_TASK_STACK_SIZE`：客户端任务栈大小。

## 常用接口说明

- `esp_websocket_client_init()`：创建 WebSocket client。
- `esp_websocket_register_events()`：注册连接、数据、错误等事件回调。
- `esp_websocket_client_start()`：启动连接流程。
- `esp_websocket_client_send_text()`：发送文本帧。
- `esp_websocket_client_is_connected()`：判断当前是否连接。

## 注意事项

- ESP32 连接电脑端测试 server 时，URI 要填电脑局域网 IP，不要填 `127.0.0.1`。
- 默认配置重点演示明文 `ws://`；生产环境要结合证书配置改成 `wss://`。
- 大消息可能被拆成多个 data event，本示例按 chunk 打印。
- 后续如果要改鉴权头、重连时的事件处理，优先看 `websocket_client_transport.c`；如果要改上报内容或日志摘要，优先看 `websocket_client_messages.c`。
