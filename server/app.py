from __future__ import annotations

import os
import time

from flask import Flask, Response, jsonify, request


app = Flask(__name__)


@app.get("/")
def index():
    """Default GET endpoint for quick browser or ESP-IDF testing."""
    return jsonify(
        service="case2-python-http-server",
        protocol="http",
        routes=["/", "/health", "/esp", "/text?msg=hello", "/json", "/echo?name=esp32"],
    )


@app.get("/health")
def health():
    return jsonify(status="ok", timestamp_ms=int(time.time() * 1000))


@app.get("/esp")
def esp_text():
    # Plain text is convenient for esp_http_client body logging modules.
    return Response(
        "ESP HTTP GET OK\n"
        f"server_time_ms={int(time.time() * 1000)}\n"
        "path=/esp\n",
        mimetype="text/plain",
    )


@app.get("/text")
def text():
    msg = request.args.get("msg", "hello from python http server")
    return Response(f"{msg}\n", mimetype="text/plain")


@app.get("/json")
def json_body():
    return jsonify(
        ok=True,
        message="hello from python http server",
        timestamp_ms=int(time.time() * 1000),
    )


@app.get("/echo")
def echo():
    return jsonify(method=request.method, path=request.path, args=request.args.to_dict())


if __name__ == "__main__":
    host = os.environ.get("HOST", "0.0.0.0")
    port = int(os.environ.get("PORT", "8000"))

    print(f"HTTP server listening on http://{host}:{port}")
    app.run(host=host, port=port, debug=False)
