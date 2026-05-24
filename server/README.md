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

## HTTP Client

Use this client to test the ESP32 HTTP or HTTPS LAN service from your computer.

First run the ESP32 module and note the IP printed in the serial monitor:

```text
got ip: 192.168.1.45
```

Then run:

```bash
cd server
python -m http_client http://192.168.1.45:80 --token <token>
python -m http_client https://192.168.1.45:443 --token <token> --ca servercert.pem
```

For quick self-signed HTTPS debugging only:

```bash
python -m http_client https://192.168.1.45:443 --token <token> --insecure
```

The client calls:

- `GET /`
- `GET /health`
- `GET /api/v1/status`
- `POST /api/v1/control`

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

## MQTT Client

Use this client to publish a command to the ESP32 command topic and wait for
status messages from the ESP32 status topic:

```bash
cd server
python -m mqtt_client mqtt://127.0.0.1:1883 --payload ping
```

Defaults match the ESP32 module Kconfig values:

- status topic: `espesp/device/status`
- command topic: `espesp/device/cmd`
- client id: `espesp-python-client`

When running multiple Python MQTT clients at the same time, give each one a
different `--client-id` so the broker does not take over the older connection.

Useful variants:

```bash
python -m mqtt_client --listen-only --count 0
python -m mqtt_client --publish-only --payload ping
python -m mqtt_client mqtt://192.168.1.23:1883 --status-topic espesp/device/status --cmd-topic espesp/device/cmd
```

## WebSocket Server

Run a local WebSocket server that mirrors the ESP32 `websocket_server` module
behavior and can be used as a target for the ESP32 `websocket_client` module:

```bash
cd server
python -m ws_server
```

The server listens on `0.0.0.0:8080/ws` by default, sends a `hello` JSON frame
after each connection, periodically broadcasts `status` JSON frames, and echoes
text or binary frames.

Useful variants:

```bash
python -m ws_server --host 127.0.0.1 --port 8080 --path /ws
python -m ws_server --token <token>
```

## WebSocket Client

Use this client to test either the ESP32 `websocket_server` module or the local
`ws_server`:

```bash
cd server
python -m ws_client ws://192.168.1.45:8080/ws --payload ping
```

If the ESP32 module has a bearer token configured:

```bash
python -m ws_client ws://192.168.1.45:8080/ws --token <token> --payload ping
```

Useful variants:

```bash
python -m ws_client --listen-only --count 0
python -m ws_client --send-only --payload ping
python -m ws_client ws://127.0.0.1:8080/ws --binary --payload hello
```

## PCM Recorder

Use this recorder with the ESP32 `pcm_stream` module. It writes incoming PCM
packets to a `.wav` file on the computer so the ESP32 does not need to write
audio to flash.

UART:

```bash
cd server
pip install -e .
python -m pcm_recorder uart /dev/ttyUSB0 out.wav --baud 921600 --seconds 10
```

UDP:

```bash
cd server
python -m pcm_recorder udp out.wav --bind 0.0.0.0 --port 8765 --seconds 10
```

For UDP mode, leave `ESPESP_PCM_UDP_HOST` as `255.255.255.255` first to
broadcast on the local Wi-Fi/LAN. After the recorder prints a local IPv4
candidate and receives packets, you can switch the ESP target to that address
for unicast.

## Speaker Server

Use this server with the ESP32 `speaker_client` module. It streams a local PCM
WAV file over WebSocket as `pcm_s16le` mono chunks and sends JSON metadata
before and after the binary audio stream.

```bash
cd server
python -m speaker_server input.wav --host 0.0.0.0 --port 8082 --path /audio --sample-rate 16000
```

Use your computer's LAN IP in ESP32 menuconfig:

```text
ws://192.168.1.23:8082/audio
```

The input WAV sample rate must match the ESP32 speaker sample rate. The server
can downmix multi-channel PCM WAV files to mono and convert 8/24/32-bit PCM WAV
samples to 16-bit, but it does not resample MP3, FLAC, or mismatched WAV files.
