from __future__ import annotations

import asyncio
import logging
import os

from amqtt.broker import Broker


def build_config(host: str, port: int) -> dict:
    return {
        "listeners": {
            "default": {
                "type": "tcp",
                "bind": f"{host}:{port}",
            },
        },
        "timeout_disconnect_delay": 0,
        "plugins": {
            "amqtt.plugins.authentication.AnonymousAuthPlugin": {
                "allow_anonymous": True,
            },
            "amqtt.plugins.sys.broker.BrokerSysPlugin": {
                "sys_interval": 20,
            },
        },
    }


async def serve() -> None:
    host = os.environ.get("MQTT_HOST", os.environ.get("HOST", "0.0.0.0"))
    port = int(os.environ.get("MQTT_PORT", os.environ.get("PORT", "1883")))

    broker = Broker(build_config(host, port))
    await broker.start()
    print(f"MQTT broker listening on mqtt://{host}:{port}")

    try:
        await asyncio.Event().wait()
    finally:
        await broker.shutdown()


def main() -> None:
    logging.basicConfig(
        level=getattr(logging, "INFO", logging.INFO),
        format="%(asctime)s %(levelname)s [%(name)s] %(message)s",
    )

    try:
        asyncio.run(serve())
    except KeyboardInterrupt:
        pass
