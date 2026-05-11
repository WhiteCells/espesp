# 05 wifi_sta: Wi-Fi STA 入网

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
  -> Demo selector
  -> 05 wifi_sta
```

配置 Wi-Fi：

```text
Case2 ESP Learning
  -> WiFi
```

## 看哪段代码

- `main/wifi_sta.c`
- 公共初始化：`main/demos/demo_common.c`

## 日志现象

成功时会看到：

- connecting to WiFi SSID
- got ip
- connected

失败时会看到重试次数，超过上限后返回失败。

## 练习

- 把最大重试次数改成 2。
- 故意填错密码，观察断开事件和失败路径。
- 在拿到 IP 后打印网关和 netmask。
