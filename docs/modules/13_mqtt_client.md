# 13 mqtt_client: MQTT 发布订阅

## 学什么

- 先连接 Wi-Fi，再连接 MQTT broker。
- `esp_mqtt_client_init()` 创建 MQTT client。
- `esp_mqtt_client_register_event()` 注册事件回调。
- `esp_mqtt_client_subscribe()` 订阅命令 topic。
- `esp_mqtt_client_publish()` 发布状态消息。
- MQTT 适合设备状态、遥测、远程控制和机器人状态同步。

## 怎么运行

```text
idf.py menuconfig
  -> Case2 ESP Learning
  -> Module selector
  -> 13 mqtt_client
```

配置 Wi-Fi 和 MQTT：

```text
Case2 ESP Learning
  -> WiFi
Case2 ESP Learning
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

## 看哪段代码

- `main/mqtt_client_demo/mqtt_client_demo.c`
- Wi-Fi 复用：`main/wifi_station/wifi_station.c`
- 测试 broker：`server/mqtt_broker/`

## 接口介绍

- `mqtt_client_demo_run()`：连接 Wi-Fi，启动 MQTT client，订阅命令 topic，并周期性发布状态。
- 常用接口：`esp_mqtt_client_init()`、`esp_mqtt_client_start()`、`esp_mqtt_client_subscribe()`、`esp_mqtt_client_publish()`。

## 日志现象

成功时会看到：

- 连接 broker 成功。
- 订阅命令 topic。
- 周期性向状态 topic 发布 JSON。
- 如果 broker 或其他 MQTT client 向命令 topic 发消息，串口会打印 topic 和 data。

## 注意事项

- 不要从 ESP32 填 `127.0.0.1` 访问电脑 broker，那会指向 ESP32 自己。
- 默认是明文 MQTT，适合局域网学习；正式设备要考虑 TLS、用户名密码和 topic 权限。
- MQTT 连接是长连接，Wi-Fi 抖动时要关注断线、重连和消息 QoS。

## 练习

- 把发布周期改成 10 秒。
- 修改状态 payload，加入 RSSI 或机器人状态。
- 用另一个 MQTT client 向命令 topic 发送 `ping`，观察串口日志。
- 把 QoS 0 改成 QoS 1，观察发布确认事件。
