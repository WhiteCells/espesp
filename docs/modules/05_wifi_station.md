# 05 wifi_station: Wi-Fi STA 入网

## 学什么

- STA 模式是 ESP 作为客户端连接路由器。
- Wi-Fi 连接依赖 NVS、`esp_netif`、默认事件循环。
- `WIFI_EVENT_STA_DISCONNECTED` 用于重连。
- `IP_EVENT_STA_GOT_IP` 表示拿到 IP。
- `EventGroup` 可以把异步事件变成同步等待。

## 怎么运行

```text
idf.py menuconfig
  -> Case2 ESP Learning
  -> Module selector
  -> 05 wifi_station
```

配置 Wi-Fi：

```text
Case2 ESP Learning
  -> WiFi
```

## 看哪段代码

- `main/wifi_station/wifi_station.c`
- 公共初始化：`main/core/app_common.c`

## 接口介绍

- `wifi_station_connect()`：连接 Wi-Fi 并等待成功或失败。
- `wifi_station_run()`：单独运行入网流程。
- 常用接口：`esp_wifi_init()`、`esp_event_handler_instance_register()`、`esp_wifi_start()`。

## 日志现象

成功时会看到：

- connecting to WiFi SSID
- got ip
- connected

失败时会看到重试次数，超过上限后返回失败。

## 注意事项

- SSID 为空会直接返回错误。
- 拿到 IP 才说明网络层可用。

## 练习

- 把最大重试次数改成 2。
- 故意填错密码，观察断开事件和失败路径。
- 在拿到 IP 后打印网关和 netmask。
