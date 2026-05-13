# mqtt_client

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> mqtt_client: Wi-Fi + MQTT publish/subscribe
```

配置 Wi-Fi 和 MQTT：

```text
ESPESP Menu
  -> WiFi module
ESPESP Menu
  -> MQTT client module
```

然后执行：

```sh
idf.py build flash monitor
```

## 当前模块已有接口

### `esp_err_t mqtt_client_run(void)`

先连接 Wi-Fi，再连接 MQTT broker，订阅命令 topic，并周期性发布状态消息。

参数：无。Broker 地址、client id、topic 和发布周期来自 Kconfig。

返回值：

- `ESP_OK`：常驻运行时不会主动返回。
- `ESP_ERR_INVALID_ARG`：MQTT broker URI 为空。
- `ESP_ERR_NO_MEM`：事件组或 MQTT client 创建失败。
- `ESP_ERR_TIMEOUT`：连接 broker 超时。
- 其他 `esp_err_t`：Wi-Fi、MQTT 初始化或启动失败。

## 常用接口说明

### `esp_mqtt_client_config_t`

MQTT client 配置。

本模块使用字段：

- `broker.address.uri`：Broker 地址，例如 `mqtt://192.168.1.23:1883`。
- `credentials.client_id`：客户端 ID。
- `session.keepalive`：MQTT keepalive 秒数。

### `esp_mqtt_client_init(const esp_mqtt_client_config_t *config)`

创建 MQTT client。

返回值：

- 非 `NULL`：client 句柄。
- `NULL`：内存不足或配置错误。

### `esp_mqtt_client_register_event()`

注册 MQTT 事件回调。本模块监听所有事件，用于观察连接、订阅、收消息和错误。

常见事件：

- `MQTT_EVENT_CONNECTED`：已连接 broker。
- `MQTT_EVENT_DISCONNECTED`：连接断开。
- `MQTT_EVENT_SUBSCRIBED`：订阅确认。
- `MQTT_EVENT_DATA`：收到订阅 topic 的消息。
- `MQTT_EVENT_ERROR`：连接或协议错误。

### `esp_mqtt_client_publish()`

发布消息。

本模块使用 QoS 0，适合先理解发布流程。后续可以改成 QoS 1，观察 `MQTT_EVENT_PUBLISHED`。

## 可配置项

- `CONFIG_ESPESP_MQTT_BROKER_URI`：Broker 地址。
- `CONFIG_ESPESP_MQTT_CLIENT_ID`：Client ID。
- `CONFIG_ESPESP_MQTT_STATUS_TOPIC`：状态发布 topic。
- `CONFIG_ESPESP_MQTT_CMD_TOPIC`：命令订阅 topic。
- `CONFIG_ESPESP_MQTT_PUBLISH_PERIOD_MS`：状态发布周期。
- `CONFIG_ESPESP_MQTT_CONNECT_TIMEOUT_MS`：连接等待超时。
- `CONFIG_ESPESP_MQTT_KEEPALIVE_SEC`：MQTT keepalive。

## 注意事项

- ESP32 访问电脑上的测试 broker 时，要填电脑局域网 IP，不要填 `127.0.0.1`。
- 本模块默认使用明文 MQTT。生产环境通常需要 TLS、鉴权和 topic 权限控制。
- QoS 0 不保证必达，适合状态心跳和普通遥测。关键命令通常至少用 QoS 1。
