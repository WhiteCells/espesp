# 13 mqtt_client: MQTT 发布订阅

## 模块概览

- 先连接 Wi-Fi，再连接 MQTT broker。
- `esp_mqtt_client_init()` 创建 MQTT client。
- `esp_mqtt_client_register_event()` 注册事件回调。
- `esp_mqtt_client_subscribe()` 订阅命令 topic。
- `esp_mqtt_client_publish()` 发布状态消息。
- MQTT 适合设备状态、遥测、远程控制和机器人状态同步。

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> 13 mqtt_client
```

配置 Wi-Fi 和 MQTT：

```text
ESPESP Menu
  -> WiFi
ESPESP Menu
  -> MQTT client module
```

本项目带了一个测试 broker：

```sh
cd server
python -m mqtt_broker
```

然后把 `MQTT broker URI` 改成电脑局域网 IP，例如：

```text
mqtt://192.168.1.23:1883
```

## 源码位置

- `main/mqtt_client/mqtt_client_app.c`
- Wi-Fi 复用：`main/wifi_station/wifi_station.c`
- 测试 broker：`server/mqtt_broker/`

## 当前模块接口参考

- `mqtt_client_run()`：连接 Wi-Fi，启动 MQTT client，订阅命令 topic，并周期性发布状态。
- `mqtt_event_handler()`：处理连接、断开、订阅确认、发布确认、收到数据和错误事件。
- `mqtt_publish_text()`：封装文本 payload 发布和日志。
- `mqtt_client_context_t`：保存 MQTT 连接状态事件组。

## 常用接口说明

- `wifi_station_connect()`：MQTT 连接 broker 前复用 Wi-Fi 入网流程。
- `esp_mqtt_client_config_t`：配置 broker URI、client id 和 keepalive。
- `esp_mqtt_client_init()`：创建 MQTT client。
- `esp_mqtt_client_register_event()`：注册 MQTT 事件回调。
- `esp_mqtt_client_start()`：启动 MQTT 连接。
- `esp_mqtt_client_subscribe()`：订阅命令 topic。
- `esp_mqtt_client_publish()`：发布状态或业务消息。
- `xEventGroupWaitBits()`：等待 MQTT connected 或 error 事件位。

## 配置项

- `CONFIG_ESPESP_WIFI_SSID` / `CONFIG_ESPESP_WIFI_PASSWORD`：连接 broker 前使用的 Wi-Fi 参数。
- `CONFIG_ESPESP_MQTT_BROKER_URI`：MQTT broker 地址，ESP32 访问电脑 broker 时必须使用电脑局域网 IP。
- `CONFIG_ESPESP_MQTT_CLIENT_ID`：MQTT client id，同一 broker 下建议唯一。
- `CONFIG_ESPESP_MQTT_STATUS_TOPIC`：状态发布 topic。
- `CONFIG_ESPESP_MQTT_CMD_TOPIC`：命令订阅 topic。
- `CONFIG_ESPESP_MQTT_PUBLISH_PERIOD_MS`：状态发布周期，单位 ms。
- `CONFIG_ESPESP_MQTT_CONNECT_TIMEOUT_MS`：等待 MQTT 连接结果的超时时间，单位 ms。
- `CONFIG_ESPESP_MQTT_KEEPALIVE_SEC`：MQTT keepalive，单位秒。

## 日志现象

成功时会看到：

- 连接 broker 成功。
- 订阅命令 topic。
- 周期性向状态 topic 发布 JSON。
- 如果 broker 或其他 MQTT client 向命令 topic 发消息，串口会打印 topic 和 data。

## 注意事项

- 不要从 ESP32 填 `127.0.0.1` 访问电脑 broker，那会指向 ESP32 自己。
- 默认是明文 MQTT，适合受控局域网联调；正式设备要考虑 TLS、用户名密码和 topic 权限。
- MQTT 连接是长连接，Wi-Fi 抖动时要关注断线、重连和消息 QoS。

## 扩展方向

- 把发布周期改成 10 秒。
- 修改状态 payload，加入 RSSI 或机器人状态。
- 用另一个 MQTT client 向命令 topic 发送 `ping`，观察串口日志。
- 把 QoS 0 改成 QoS 1，观察发布确认事件。
