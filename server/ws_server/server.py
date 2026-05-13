from __future__ import annotations

import argparse
import asyncio
import hmac
import json
import logging
import os
import sys
import time
from dataclasses import dataclass, field
from http import HTTPStatus
from urllib.parse import urlsplit

from websockets.asyncio.server import ServerConnection, serve
from websockets.exceptions import ConnectionClosed, WebSocketException
from websockets.http11 import Request, Response

DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 8081
DEFAULT_PATH = "/ws"
DEFAULT_PERIOD = 1.0
DEFAULT_MAX_CLIENTS = 4
DEFAULT_MAX_SIZE = 65536
BINARY_PREVIEW_BYTES = 16


@dataclass(frozen=True)
class ServerOptions:
    host: str
    port: int
    path: str
    token: str
    period: float
    max_clients: int
    max_size: int
    verbose: bool


@dataclass
class ServerState:
    started_at: float = field(default_factory=time.monotonic)
    connections: set[ServerConnection] = field(default_factory=set)

    @property
    def uptime_ms(self) -> int:
        return int((time.monotonic() - self.started_at) * 1000)


def env_int(name: str, fallback: int) -> int:
    value = os.environ.get(name)
    return int(value) if value else fallback


def env_float(name: str, fallback: float) -> float:
    value = os.environ.get(name)
    return float(value) if value else fallback


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run an ESPESP-style WebSocket test server.")
    parser.add_argument("--host", default=os.environ.get("WS_HOST", os.environ.get("HOST", DEFAULT_HOST)))
    parser.add_argument("--port", type=int, default=env_int("WS_PORT", env_int("PORT", DEFAULT_PORT)))
    parser.add_argument("--path", default=os.environ.get("WS_PATH", DEFAULT_PATH))
    parser.add_argument("--token", default=os.environ.get("WS_AUTH_TOKEN", ""), help="optional bearer token")
    parser.add_argument(
        "--period",
        type=float,
        default=env_float("WS_PUBLISH_PERIOD_SEC", DEFAULT_PERIOD),
        help="status broadcast period in seconds",
    )
    parser.add_argument("--max-clients", type=int, default=env_int("WS_MAX_CLIENTS", DEFAULT_MAX_CLIENTS))
    parser.add_argument("--max-size", type=int, default=env_int("WS_MAX_SIZE", DEFAULT_MAX_SIZE))
    parser.add_argument("--verbose", action="store_true", help="enable debug logging")
    return parser


def parse_args(argv: list[str]) -> ServerOptions:
    args = build_parser().parse_args(argv)
    if not args.path.startswith("/"):
        raise ValueError("--path must start with '/'")
    if not 1 <= args.port <= 65535:
        raise ValueError("--port must be between 1 and 65535")
    if args.period <= 0:
        raise ValueError("--period must be > 0")
    if args.max_clients < 1:
        raise ValueError("--max-clients must be >= 1")
    if args.max_size < 1:
        raise ValueError("--max-size must be >= 1")
    return ServerOptions(
        host=args.host,
        port=args.port,
        path=args.path,
        token=args.token,
        period=args.period,
        max_clients=args.max_clients,
        max_size=args.max_size,
        verbose=args.verbose,
    )


def binary_preview(data: bytes) -> str:
    if not data:
        return "(empty)"

    preview = " ".join(f"{value:02X}" for value in data[:BINARY_PREVIEW_BYTES])
    if len(data) > BINARY_PREVIEW_BYTES:
        return f"{preview} ..."
    return preview


def make_payload(kind: str, state: ServerState, options: ServerOptions, sequence: int | None = None) -> str:
    payload: dict[str, object] = {
        "type": kind,
        "service": "websocket_server",
        "path": options.path,
        "clients": len(state.connections),
        "uptime_ms": state.uptime_ms,
    }
    if sequence is not None:
        payload["seq"] = sequence
    return json.dumps(payload, separators=(",", ":"))


def request_path(request: Request) -> str:
    return urlsplit(request.path).path


def validate_handshake(options: ServerOptions, state: ServerState):
    def process_request(connection: ServerConnection, request: Request) -> Response | None:
        if request_path(request) != options.path:
            return connection.respond(HTTPStatus.NOT_FOUND, "websocket path not found\n")

        if options.token:
            expected = f"Bearer {options.token}"
            actual = request.headers.get("Authorization", "")
            if not hmac.compare_digest(actual, expected):
                response = connection.respond(HTTPStatus.UNAUTHORIZED, "unauthorized\n")
                response.headers["WWW-Authenticate"] = "Bearer"
                return response

        if len(state.connections) >= options.max_clients:
            return connection.respond(HTTPStatus.SERVICE_UNAVAILABLE, "too many websocket clients\n")

        return None

    return process_request


async def send_status_loop(state: ServerState, options: ServerOptions) -> None:
    sequence = 0
    while True:
        await asyncio.sleep(options.period)
        if not state.connections:
            continue

        payload = make_payload("status", state, options, sequence)
        sequence += 1
        for websocket in tuple(state.connections):
            try:
                await websocket.send(payload)
            except ConnectionClosed:
                state.connections.discard(websocket)


async def handle_connection(websocket: ServerConnection, state: ServerState, options: ServerOptions) -> None:
    state.connections.add(websocket)
    remote = websocket.remote_address
    print(f"websocket connected remote={remote} clients={len(state.connections)}")

    try:
        await websocket.send(make_payload("hello", state, options))
        async for message in websocket:
            if isinstance(message, str):
                print(f"text frame remote={remote} len={len(message)} payload={message}")
            else:
                print(f"binary frame remote={remote} len={len(message)} preview={binary_preview(message)}")
            await websocket.send(message)
    finally:
        state.connections.discard(websocket)
        print(f"websocket disconnected remote={remote} clients={len(state.connections)}")


async def serve_forever(options: ServerOptions) -> None:
    state = ServerState()
    handler = lambda websocket: handle_connection(websocket, state, options)
    status_task: asyncio.Task[None] | None = None

    async with serve(
        handler,
        options.host,
        options.port,
        process_request=validate_handshake(options, state),
        max_size=options.max_size,
    ) as server:
        status_task = asyncio.create_task(send_status_loop(state, options))
        sockets = ", ".join(str(sock.getsockname()) for sock in server.sockets or [])
        print(
            f"WebSocket server listening on ws://{options.host}:{options.port}{options.path} "
            f"sockets=[{sockets}] max_clients={options.max_clients}"
        )
        if options.token:
            print("WebSocket auth enabled with Authorization: Bearer <token>")
        else:
            print("WebSocket auth disabled")
        await asyncio.Future()

    if status_task is not None:
        status_task.cancel()
        try:
            await status_task
        except asyncio.CancelledError:
            pass


def configure_logging(verbose: bool) -> None:
    logging.basicConfig(
        level=logging.DEBUG if verbose else logging.WARNING,
        format="%(asctime)s %(levelname)s [%(name)s] %(message)s",
    )


def main() -> None:
    try:
        options = parse_args(sys.argv[1:])
    except ValueError as exc:
        print(exc, file=sys.stderr)
        print("usage: python -m ws_server [--host 0.0.0.0] [--port 8080] [--path /ws]", file=sys.stderr)
        raise SystemExit(2) from exc

    configure_logging(options.verbose)

    try:
        asyncio.run(serve_forever(options))
    except KeyboardInterrupt:
        pass
    except (OSError, WebSocketException) as exc:
        print(f"WebSocket server failed: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc


if __name__ == "__main__":
    main()
