# lan_service

`lan_service` 是 HTTP 和 HTTPS server 共享的局域网 API 层。

## 使用方式

`lan_service` 不在 menuconfig 的模块选择器中单独运行。`http_server` 和 `https_server` 启动 server 后调用 `lan_service_register_handlers()` 注册共享路由。

可调参数：

```text
ESPESP Menu
  -> LAN service
```

## 当前模块已有接口

### `esp_err_t lan_service_register_handlers(httpd_handle_t server, const lan_service_config_t *config)`

把局域网服务路由注册到已经启动的 HTTP/HTTPS server 上。

参数：

- `server`：`httpd_start()` 或 `httpd_ssl_start()` 返回的 server handle。
- `config`：服务配置，包含服务名、协议、鉴权 token 和请求体上限。

返回值：

- `ESP_OK`：路由注册成功。
- `ESP_ERR_INVALID_ARG`：server 或必要配置为空。
- `ESP_ERR_NO_MEM`：分配上下文失败。
- 其他 `esp_err_t`：路由或错误处理器注册失败。

## 相关结构体

### `lan_service_config_t`

字段说明：

- `service_name`：服务名，会出现在 `/` 和 `/health` 响应中。
- `scheme`：协议名，当前为 `http` 或 `https`。
- `auth_token`：Bearer token。
- `max_body_len`：POST 请求体上限。
- `require_auth`：是否要求鉴权。

### `lan_service_context_t`

模块内部上下文，保存 `lan_service_config_t` 和请求计数。

## 路由

- `GET /`：服务说明。
- `GET /health`：公开健康检查。
- `GET /api/v1/status`：鉴权状态接口。
- `POST /api/v1/control`：鉴权控制接口。

## 内部接口

- `set_common_headers()`：设置 `Cache-Control`、`X-Content-Type-Options` 和 `Connection`。
- `send_plain()`：发送 `text/plain` 响应。
- `send_json()`：发送 `application/json` 响应。
- `secure_equals()`：尽量避免早停比较 token。
- `has_authorization()`：读取并校验 `Authorization: Bearer <token>`。
- `require_authorization()`：鉴权失败时返回 401。
- `read_request_body()`：按 body 上限读取 POST 请求体。
- `register_get()`、`register_post()`：封装 GET/POST 路由注册。

## 常用接口说明

- `httpd_register_uri_handler()`：注册 HTTP 路由。
- `httpd_register_err_handler()`：注册 404 错误处理。
- `httpd_req_get_hdr_value_str()`：读取请求头。
- `httpd_req_recv()`：读取请求体。
- `httpd_resp_set_hdr()`：设置响应头。
- `httpd_resp_set_type()`：设置响应 Content-Type。
- `httpd_resp_sendstr()`：发送字符串响应。
- `httpd_resp_send_err()`：发送错误响应。

## 安全边界

- 状态和控制接口默认要求 `Authorization: Bearer <token>`。
- 所有响应设置 `Cache-Control: no-store` 和 `X-Content-Type-Options: nosniff`。
- POST 请求体受 `CONFIG_ESPESP_LAN_SERVICE_MAX_BODY_LEN` 限制。
- HTTP 和 HTTPS server 都限制 socket 数量和收发超时。
