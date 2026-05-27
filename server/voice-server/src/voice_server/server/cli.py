from __future__ import annotations

import argparse

import uvicorn

from voice_server.config import get_settings


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run the full-duplex voice bridge server.")
    parser.add_argument("--host", default=None, help="Bind host. Defaults to VOICE_SERVER_HOST.")
    parser.add_argument(
        "--port",
        default=None,
        type=int,
        help="Bind port. Defaults to VOICE_SERVER_PORT.",
    )
    parser.add_argument("--reload", action="store_true", help="Enable uvicorn reload.")
    return parser


def main() -> None:
    args = build_parser().parse_args()
    settings = get_settings()
    uvicorn.run(
        "voice_server.server.app:app",
        host=args.host or settings.host,
        port=args.port or settings.port,
        log_level=settings.log_level,
        reload=args.reload,
    )


if __name__ == "__main__":
    main()
