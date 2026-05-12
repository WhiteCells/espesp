# 06 http_get: HTTP Client GET 请求

## 学什么

- 先连接 Wi-Fi，再发 HTTP 请求。
- `esp_http_client_init()` 创建 client。
- `esp_http_client_perform()` 执行请求。
- HTTP 数据通过事件回调分段到达。
- `esp_http_client_cleanup()` 释放资源。

## 怎么运行

```text
idf.py menuconfig
  -> Case2 ESP Learning
  -> Module selector
  -> 06 http_get
```

配置 Wi-Fi 和 URL：

```text
Case2 ESP Learning
  -> WiFi
Case2 ESP Learning
  -> HTTP client module
```

## 看哪段代码

- `main/http_client/http_get.c`
- Wi-Fi 复用：`main/wifi_station/wifi_station.c`

## 接口介绍

- `http_get_run()`：连接 Wi-Fi 后执行一次 GET。
- 常用接口：`esp_http_client_init()`、`esp_http_client_perform()`、`esp_http_client_cleanup()`。

## 日志现象

成功时会看到 HTTP header、响应前若干字节、status code 和 content length。

## 注意事项

- 默认使用 HTTP 明文地址，HTTPS 需要证书配置。
- 响应体可能分段到达，不要假设一次回调就是完整 body。

## 练习

- 把 URL 改成自己的 HTTP 服务。
- 把打印响应长度改成 128 字节。
- 在 `HTTP_EVENT_ON_DATA` 中统计总接收字节数。
