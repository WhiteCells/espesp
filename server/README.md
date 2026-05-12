# Python HTTP GET Server

This is a tiny HTTP server for testing the ESP-IDF HTTP GET module.

## Run

```bash
cd server
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python app.py
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
