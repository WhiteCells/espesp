from __future__ import annotations

import argparse
import asyncio
import logging
import sys
from dataclasses import dataclass

from amqtt.client import MQTTClient
from amqtt.errors import AMQTTError, ClientError, ConnectError

DEFAULT_URI = "mqtt://127.0.0.1:1883"
DEFAULT_CLIENT_ID = "espesp-python-client"
DEFAULT_STATUS_TOPIC = "espesp/device/status"
DEFAULT_CMD_TOPIC = "espesp/device/cmd"
DEFAULT_PAYLOAD = "ping"


@dataclass(frozen=True)
class ClientOptions:
    uri: str
    client_id: str
    status_topic: str
    cmd_topic: str
    payload: str
    qos: int
    timeout: float
    count: int
    keepalive: int
    listen_only: bool
    publish_only: bool
    verbose: bool


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Test the ESPESP MQTT module with an aMQTT client.",
    )
    parser.add_argument(
        "uri",
        nargs="?",
        default=DEFAULT_URI,
        help=f"MQTT broker URI, default: {DEFAULT_URI}",
    )
    parser.add_argument(
        "--client-id",
        default=DEFAULT_CLIENT_ID,
        help=f"MQTT client id, default: {DEFAULT_CLIENT_ID}",
    )
    parser.add_argument(
        "--status-topic",
        default=DEFAULT_STATUS_TOPIC,
        help=f"topic to subscribe for ESP32 status, default: {DEFAULT_STATUS_TOPIC}",
    )
    parser.add_argument(
        "--cmd-topic",
        default=DEFAULT_CMD_TOPIC,
        help=f"topic used to publish commands to ESP32, default: {DEFAULT_CMD_TOPIC}",
    )
    parser.add_argument(
        "--payload",
        default=DEFAULT_PAYLOAD,
        help=f"command payload to publish, default: {DEFAULT_PAYLOAD}",
    )
    parser.add_argument("--qos", type=int, choices=(0, 1, 2), default=0, help="MQTT QoS, default: 0")
    parser.add_argument(
        "--timeout",
        type=float,
        default=15.0,
        help="seconds to wait for status messages; 0 waits forever, default: 15",
    )
    parser.add_argument(
        "--count",
        type=int,
        default=1,
        help="number of status messages to wait for; 0 means until timeout, default: 1",
    )
    parser.add_argument("--keepalive", type=int, default=60, help="MQTT keepalive seconds, default: 60")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--listen-only", action="store_true", help="subscribe and print status messages without publishing")
    mode.add_argument("--publish-only", action="store_true", help="publish the command and exit without subscribing")
    parser.add_argument("--verbose", action="store_true", help="enable aMQTT debug logging")
    return parser


def parse_args(argv: list[str]) -> ClientOptions:
    args = build_parser().parse_args(argv)

    if args.timeout < 0:
        raise ValueError("--timeout must be >= 0")
    if args.count < 0:
        raise ValueError("--count must be >= 0")
    if args.keepalive < 2:
        raise ValueError("--keepalive must be >= 2")
    if not args.client_id:
        raise ValueError("--client-id must not be empty")
    if not args.publish_only and not args.status_topic:
        raise ValueError("--status-topic must not be empty")
    if not args.listen_only and not args.cmd_topic:
        raise ValueError("--cmd-topic must not be empty")

    return ClientOptions(
        uri=args.uri,
        client_id=args.client_id,
        status_topic=args.status_topic,
        cmd_topic=args.cmd_topic,
        payload=args.payload,
        qos=args.qos,
        timeout=args.timeout,
        count=args.count,
        keepalive=args.keepalive,
        listen_only=args.listen_only,
        publish_only=args.publish_only,
        verbose=args.verbose,
    )


def build_client(options: ClientOptions) -> MQTTClient:
    return MQTTClient(
        client_id=options.client_id,
        config={
            "keep_alive": options.keepalive,
            "ping_delay": 1,
            "default_qos": options.qos,
            "default_retain": False,
            "auto_reconnect": False,
            "connection_timeout": 10,
            "reconnect_retries": 0,
            "cleansession": True,
            "plugins": {},
        },
    )


def decode_payload(data: bytes | bytearray) -> str:
    return bytes(data).decode("utf-8", errors="replace")


def print_message(topic: str, qos: int | None, retain: bool, payload: str) -> None:
    print(f"message topic={topic} qos={qos} retain={retain}")
    print(payload.rstrip())
    print()


async def wait_for_messages(client: MQTTClient, options: ClientOptions) -> int:
    if options.publish_only:
        return 0

    if options.timeout == 0:
        deadline: float | None = None
    else:
        deadline = asyncio.get_running_loop().time() + options.timeout

    received = 0
    while options.count == 0 or received < options.count:
        if deadline is None:
            remaining = None
        else:
            remaining = deadline - asyncio.get_running_loop().time()
            if remaining <= 0:
                break

        try:
            message = await client.deliver_message(timeout_duration=remaining)
        except asyncio.TimeoutError:
            break

        if message is None:
            break

        received += 1
        print_message(message.topic, message.qos, message.retain, decode_payload(message.data))

    return received


async def exercise_broker(options: ClientOptions) -> int:
    client = build_client(options)
    connected = False
    try:
        return_code = await client.connect(options.uri)
        connected = True
        print(f"connected uri={options.uri} client_id={options.client_id} return_code={return_code}")

        if not options.publish_only:
            granted_qos = await client.subscribe([(options.status_topic, options.qos)])
            print(f"subscribed topic={options.status_topic} requested_qos={options.qos} granted_qos={granted_qos}")

        if not options.listen_only:
            await client.publish(options.cmd_topic, options.payload.encode("utf-8"), qos=options.qos)
            print(f"published topic={options.cmd_topic} qos={options.qos} payload={options.payload}")

        return await wait_for_messages(client, options)
    finally:
        if connected:
            await client.disconnect()


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
        print(f"usage: python -m mqtt_client [{DEFAULT_URI}] [--payload {DEFAULT_PAYLOAD}]", file=sys.stderr)
        raise SystemExit(2) from exc

    configure_logging(options.verbose)

    try:
        received = asyncio.run(exercise_broker(options))
    except KeyboardInterrupt:
        raise SystemExit(130) from None
    except (AMQTTError, ClientError, ConnectError, OSError, asyncio.TimeoutError) as exc:
        print(f"MQTT client failed: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc

    if not options.publish_only and options.count > 0 and received < options.count:
        print(
            f"timeout waiting for status messages: received={received} expected={options.count}",
            file=sys.stderr,
        )
        raise SystemExit(1)


if __name__ == "__main__":
    main()
