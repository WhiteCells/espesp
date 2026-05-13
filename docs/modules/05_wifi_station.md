# 05 wifi_station: Wi-Fi STA 入网

## 模块概览

- STA 模式是 ESP 作为客户端连接路由器。
- Wi-Fi 连接依赖 NVS、`esp_netif`、默认事件循环。
- `WIFI_EVENT_STA_DISCONNECTED` 用于重连。
- `IP_EVENT_STA_GOT_IP` 表示拿到 IP。
- `EventGroup` 可以把异步事件变成同步等待。

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> 05 wifi_station
```

配置 Wi-Fi：

```text
ESPESP Menu
  -> WiFi
```

## 源码位置

- `main/wifi_station/wifi_station.c`
- 公共初始化：`main/core/app_common.c`

## 当前模块接口参考

- `wifi_station_connect()`：连接 Wi-Fi 并等待成功或失败。
- `wifi_station_run()`：单独运行入网流程。
- `event_handler()`：处理 `WIFI_EVENT_STA_START`、`WIFI_EVENT_STA_DISCONNECTED` 和 `IP_EVENT_STA_GOT_IP`。

## 常用接口说明

- `app_common_init_nvs()`：初始化 Wi-Fi 依赖的 NVS。
- `app_common_init_netif()`：初始化 `esp_netif` 和默认事件循环。
- `esp_netif_create_default_wifi_sta()`：创建默认 STA 网络接口。
- `esp_wifi_init()`：初始化 Wi-Fi 驱动。
- `esp_event_handler_instance_register()`：注册 Wi-Fi 和 IP 事件回调。
- `esp_wifi_set_mode()`：设置 Wi-Fi 模式为 STA。
- `esp_wifi_set_config()`：写入 SSID、密码和认证阈值。
- `esp_wifi_start()`：启动 Wi-Fi 驱动并触发连接流程。
- `xEventGroupWaitBits()`：等待连接成功或失败事件位。

## 配置项

- `CONFIG_ESPESP_WIFI_SSID`：STA 要连接的 Wi-Fi SSID。
- `CONFIG_ESPESP_WIFI_PASSWORD`：Wi-Fi 密码，开放网络可为空。
- `CONFIG_ESPESP_WIFI_MAXIMUM_RETRY`：断开后的最大重试次数，超过后返回失败。

## 日志现象

成功时会看到：

- connecting to WiFi SSID
- got ip
- connected

失败时会看到重试次数，超过上限后返回失败。

## 注意事项

- SSID 为空会直接返回错误。
- 拿到 IP 才说明网络层可用。

## 扩展方向

- 把最大重试次数改成 2。
- 故意填错密码，观察断开事件和失败路径。
- 在拿到 IP 后打印网关和 netmask。
