# websocket_server

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> websocket_server: WebSocket upgrade service
```

配置 Wi-Fi 和 WebSocket server：

```text
ESPESP Menu
  -> WiFi module
ESPESP Menu
  -> WebSocket server module
```

然后执行：

```sh
idf.py build flash monitor
```

## 当前模块已有接口

### `esp_err_t websocket_server_run(void)`

先连接 Wi-Fi，再启动 HTTP server 并注册 WebSocket 升级路由。客户端连上以后，
模块会周期推送状态 JSON，收到文本或二进制帧时会打印并回显。

## 当前实现拆分

- `websocket_server.c`：模块入口，只负责启动流程编排和周期广播循环。
- `websocket_server_runtime.c`：运行时配置校验、HTTPD 启动和路由挂载。
- `websocket_server_handlers.c`：握手鉴权、连接关闭、帧接收与回显。
- `websocket_server_messages.c`：欢迎消息、状态广播、客户端快照统计。
- `websocket_server_context.h`：模块内部共享的配置和运行时上下文。

## 可用能力

- `GET /ws`：WebSocket 升级入口。
- 文本帧：打印并回显。
- 二进制帧：打印摘要并回显。
- 周期状态推送：向所有已连接客户端广播 JSON。
- 可选鉴权：如果 `CONFIG_ESPESP_WS_SERVER_AUTH_TOKEN` 非空，握手需要
  `Authorization: Bearer <token>`。

## 可配置项

- `CONFIG_ESPESP_WS_SERVER_PORT`：WebSocket server 端口。
- `CONFIG_ESPESP_WS_SERVER_PATH`：升级路径，默认 `/ws`。
- `CONFIG_ESPESP_WS_SERVER_AUTH_TOKEN`：Bearer token，留空表示不启用鉴权。
- `CONFIG_ESPESP_WS_SERVER_PUBLISH_PERIOD_MS`：状态推送周期。
- `CONFIG_ESPESP_WS_SERVER_MAX_CLIENTS`：最大 WebSocket 客户端数。

## 常用接口说明

- `httpd_uri_t.is_websocket`：把 URI 标记成 WebSocket 端点。
- `ws_pre_handshake_cb`：在握手前做鉴权和客户端数检查。
- `ws_post_handshake_cb`：握手后发送欢迎消息。
- `httpd_ws_recv_frame()`：读取文本/二进制帧。
- `httpd_ws_send_frame()`：在请求上下文里回显帧。
- `httpd_ws_send_data()`：在普通任务里向指定 socket 推送数据。
- `httpd_ws_get_fd_info()`：判断 socket 当前是 HTTP 还是 WebSocket。

## 注意事项

- 当前示例使用明文 `ws://`，不是 `wss://`。
- 如果页面本身通过 HTTPS 打开，浏览器通常会限制混用明文 WS。
- 示例重点放在完整文本/二进制帧；更复杂的分片重组可以后续再扩展。
