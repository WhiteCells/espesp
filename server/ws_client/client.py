from __future__ import annotations

import argparse
import asyncio
import logging
import os
import sys
from dataclasses import dataclass

from websockets.asyncio.client import connect
from websockets.exceptions import ConnectionClosed, InvalidHandshake, InvalidURI, WebSocketException

DEFAULT_URI = "ws://127.0.0.1:8081/ws"
DEFAULT_PAYLOAD = "ping"
BINARY_PREVIEW_BYTES = 16


@dataclass(frozen=True)
class ClientOptions:
    uri: str
    token: str
    payload: str
    binary: bool
    listen_only: bool
    send_only: bool
    count: int
    timeout: float
    verbose: bool


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Test an ESPESP WebSocket endpoint.")
    parser.add_argument("uri", nargs="?", default=DEFAULT_URI, help=f"WebSocket URI, default: {DEFAULT_URI}")
    parser.add_argument("--token", default=os.environ.get("WS_AUTH_TOKEN", ""), help="optional bearer token")
    parser.add_argument("--payload", default=DEFAULT_PAYLOAD, help=f"message to send, default: {DEFAULT_PAYLOAD}")
    parser.add_argument("--binary", action="store_true", help="send payload as a binary frame")
    parser.add_argument("--listen-only", action="store_true", help="receive messages without sending payload")
    parser.add_argument("--send-only", action="store_true", help="send payload and exit without receiving")
    parser.add_argument(
        "--count",
        type=int,
        default=3,
        help="number of messages to receive; 0 means until timeout, default: 3",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=10.0,
        help="seconds to wait for messages; 0 waits forever, default: 10",
    )
    parser.add_argument("--verbose", action="store_true", help="enable debug logging")
    return parser


def parse_args(argv: list[str]) -> ClientOptions:
    args = build_parser().parse_args(argv)
    if args.listen_only and args.send_only:
        raise ValueError("--listen-only and --send-only cannot be used together")
    if args.count < 0:
        raise ValueError("--count must be >= 0")
    if args.timeout < 0:
        raise ValueError("--timeout must be >= 0")
    return ClientOptions(
        uri=args.uri,
        token=args.token,
        payload=args.payload,
        binary=args.binary,
        listen_only=args.listen_only,
        send_only=args.send_only,
        count=args.count,
        timeout=args.timeout,
        verbose=args.verbose,
    )


def binary_preview(data: bytes) -> str:
    if not data:
        return "(empty)"

    preview = " ".join(f"{value:02X}" for value in data[:BINARY_PREVIEW_BYTES])
    if len(data) > BINARY_PREVIEW_BYTES:
        return f"{preview} ..."
    return preview


def print_message(message: str | bytes) -> None:
    if isinstance(message, str):
        print(f"message text len={len(message)}")
        print(message.rstrip())
        print()
        return

    print(f"message binary len={len(message)} preview={binary_preview(message)}")
    print()


async def receive_messages(websocket, options: ClientOptions) -> int:
    if options.send_only:
        return 0

    deadline = None if options.timeout == 0 else asyncio.get_running_loop().time() + options.timeout
    received = 0

    while options.count == 0 or received < options.count:
        if deadline is None:
            remaining = None
        else:
            remaining = deadline - asyncio.get_running_loop().time()
            if remaining <= 0:
                break

        try:
            message = await asyncio.wait_for(websocket.recv(), timeout=remaining)
        except TimeoutError:
            break
        except ConnectionClosed:
            break

        received += 1
        print_message(message)

    return received


async def exercise_endpoint(options: ClientOptions) -> int:
    headers = {"Authorization": f"Bearer {options.token}"} if options.token else None
    async with connect(options.uri, additional_headers=headers, proxy=None) as websocket:
        print(f"connected uri={options.uri}")

        if not options.listen_only:
            message: str | bytes = options.payload.encode("utf-8") if options.binary else options.payload
            await websocket.send(message)
            kind = "binary" if options.binary else "text"
            print(f"sent {kind} len={len(message)} payload={options.payload}")

        return await receive_messages(websocket, options)


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
        print(f"usage: python -m ws_client [{DEFAULT_URI}] [--payload {DEFAULT_PAYLOAD}]", file=sys.stderr)
        raise SystemExit(2) from exc

    configure_logging(options.verbose)

    try:
        received = asyncio.run(exercise_endpoint(options))
    except KeyboardInterrupt:
        raise SystemExit(130) from None
    except (OSError, InvalidHandshake, InvalidURI, WebSocketException) as exc:
        print(f"WebSocket client failed: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc

    if not options.send_only and options.count > 0 and received < options.count:
        print(f"timeout waiting for messages: received={received} expected={options.count}", file=sys.stderr)
        raise SystemExit(1)


if __name__ == "__main__":
    main()
