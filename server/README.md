# Python Test Servers

This folder contains tiny Python servers for testing the ESP-IDF networking modules.

## HTTP Server

```bash
cd server
python -m venv .venv
source .venv/bin/activate
pip install -e .
python -m http_server
```

The server listens on `0.0.0.0:8000` by default.

Use your computer's LAN IP from the ESP32, for example:

```text
http://192.168.1.23:8000/esp
```

Do not use `127.0.0.1` from the ESP32, because that points back to the ESP32 itself.

## Useful GET URLs

- `GET /esp` returns plain text for ESP monitor body output.
- `GET /json` returns JSON.
- `GET /text?msg=hello` returns custom plain text.
- `GET /echo?name=esp32` returns query parameters as JSON.

## MQTT Broker

Run the built-in lightweight broker:

```bash
cd server
python -m mqtt_broker
```

The broker listens on `0.0.0.0:1883` by default.

Use your computer's LAN IP from the ESP32, for example:

```text
mqtt://192.168.1.23:1883
```

This broker is powered by aMQTT, an asyncio-native MQTT broker. It is configured
for anonymous LAN testing and listens on plain TCP MQTT by default.
