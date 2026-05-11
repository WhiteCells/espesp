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
  -> Demo selector
  -> 06 http_get
```

配置 Wi-Fi 和 URL：

```text
Case2 ESP Learning
  -> WiFi
Case2 ESP Learning
  -> HTTP client demo
```

## 看哪段代码

- `main/demos/http_get_demo.c`
- Wi-Fi 复用：`main/wifi_sta.c`

## 日志现象

成功时会看到 HTTP header、响应前若干字节、status code 和 content length。

## 练习

- 把 URL 改成自己的 HTTP 服务。
- 把打印响应长度改成 128 字节。
- 在 `HTTP_EVENT_ON_DATA` 中统计总接收字节数。
