from __future__ import annotations

import argparse
import asyncio
import hmac
import json
import logging
import os
import re
import socket
import struct
import subprocess
import sys
import time
import wave
from dataclasses import dataclass, field
from http import HTTPStatus
from pathlib import Path
from typing import Iterator
from urllib.parse import urlsplit

try:
    from websockets.exceptions import WebSocketException
except ModuleNotFoundError:
    class WebSocketException(Exception):
        pass

DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 8082
DEFAULT_PATH = "/audio"
DEFAULT_SAMPLE_RATE_HZ = 16000
DEFAULT_CHUNK_BYTES = 1024
DEFAULT_MAX_CLIENTS = 2
DEFAULT_MAX_SIZE = 65536
BINARY_PREVIEW_BYTES = 16

LOGGER = logging.getLogger("speaker_server")


@dataclass(frozen=True)
class AudioInfo:
    path: Path
    sample_rate_hz: int
    channels: int
    sample_width_bytes: int
    frames: int

    @property
    def output_bytes(self) -> int:
        return self.frames * 2


@dataclass(frozen=True)
class ServerOptions:
    file: Path
    host: str
    port: int
    path: str
    token: str
    sample_rate_hz: int
    chunk_bytes: int
    max_clients: int
    max_size: int
    pace: bool
    verbose: bool


@dataclass
class ServerState:
    started_at: float = field(default_factory=time.monotonic)
    connections: set[ServerConnection] = field(default_factory=set)


def env_int(name: str, fallback: int) -> int:
    value = os.environ.get(name)
    return int(value) if value else fallback


def _is_usable_ipv4(address: str) -> bool:
    return address != "0.0.0.0" and not address.startswith("127.")


def get_local_ipv4_candidates() -> list[str]:
    candidates: list[tuple[str, str]] = []
    seen: set[tuple[str, str]] = set()

    def add_candidate(interface: str, address: str) -> None:
        if not _is_usable_ipv4(address):
            return
        key = (interface, address)
        if key not in seen:
            seen.add(key)
            candidates.append(key)

    try:
        result = subprocess.run(
            ["ip", "-o", "-4", "addr", "show"],
            check=False,
            capture_output=True,
            text=True,
            timeout=1,
        )
        if result.returncode == 0:
            for line in result.stdout.splitlines():
                parts = line.split()
                if len(parts) >= 4 and parts[2] == "inet":
                    add_candidate(parts[1].rstrip(":"), parts[3].split("/", maxsplit=1)[0])
    except (OSError, subprocess.SubprocessError):
        pass

    try:
        result = subprocess.run(
            ["ifconfig"],
            check=False,
            capture_output=True,
            text=True,
            timeout=1,
        )
        if result.returncode == 0:
            interface = ""
            for line in result.stdout.splitlines():
                if line and not line[0].isspace() and ":" in line:
                    interface = line.split(":", maxsplit=1)[0]
                    continue
                match = re.search(r"\binet\s+(\d+\.\d+\.\d+\.\d+)", line)
                if match:
                    add_candidate(interface, match.group(1))
    except (OSError, subprocess.SubprocessError):
        pass

    try:
        hostname = socket.gethostname()
        for family, _type, _proto, _canonname, sockaddr in socket.getaddrinfo(hostname, None, socket.AF_INET):
            if family == socket.AF_INET:
                add_candidate("hostname", sockaddr[0])
    except OSError:
        pass

    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.connect(("8.8.8.8", 80))
            add_candidate("default-route", sock.getsockname()[0])
    except OSError:
        pass

    return [f"{interface}={address}" if interface else address for interface, address in candidates]


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Stream a local WAV file to ESPESP speaker_client over WebSocket.")
    parser.add_argument("file", type=Path, help="input WAV file")
    parser.add_argument("--host", default=os.environ.get("SPEAKER_HOST", os.environ.get("HOST", DEFAULT_HOST)))
    parser.add_argument("--port", type=int, default=env_int("SPEAKER_PORT", env_int("PORT", DEFAULT_PORT)))
    parser.add_argument("--path", default=os.environ.get("SPEAKER_PATH", DEFAULT_PATH))
    parser.add_argument("--token", default=os.environ.get("SPEAKER_AUTH_TOKEN", ""), help="optional bearer token")
    parser.add_argument(
        "--sample-rate",
        type=int,
        default=env_int("SPEAKER_SAMPLE_RATE_HZ", DEFAULT_SAMPLE_RATE_HZ),
        help=f"required WAV sample rate, default: {DEFAULT_SAMPLE_RATE_HZ}",
    )
    parser.add_argument(
        "--chunk-bytes",
        type=int,
        default=env_int("SPEAKER_CHUNK_BYTES", DEFAULT_CHUNK_BYTES),
        help=f"PCM bytes per WebSocket binary frame, default: {DEFAULT_CHUNK_BYTES}",
    )
    parser.add_argument("--max-clients", type=int, default=env_int("SPEAKER_MAX_CLIENTS", DEFAULT_MAX_CLIENTS))
    parser.add_argument("--max-size", type=int, default=env_int("SPEAKER_MAX_SIZE", DEFAULT_MAX_SIZE))
    parser.add_argument("--no-pace", action="store_true", help="send chunks as fast as TCP accepts them")
    parser.add_argument("--verbose", action="store_true", help="enable debug logging")
    return parser


def parse_args(argv: list[str]) -> ServerOptions:
    args = build_parser().parse_args(argv)
    if not args.path.startswith("/"):
        raise ValueError("--path must start with '/'")
    if not 1 <= args.port <= 65535:
        raise ValueError("--port must be between 1 and 65535")
    if args.sample_rate <= 0:
        raise ValueError("--sample-rate must be > 0")
    if args.chunk_bytes < 2 or args.chunk_bytes % 2 != 0:
        raise ValueError("--chunk-bytes must be an even value >= 2")
    if args.max_clients < 1:
        raise ValueError("--max-clients must be >= 1")
    if args.max_size < 1:
        raise ValueError("--max-size must be >= 1")
    return ServerOptions(
        file=args.file,
        host=args.host,
        port=args.port,
        path=args.path,
        token=args.token,
        sample_rate_hz=args.sample_rate,
        chunk_bytes=args.chunk_bytes,
        max_clients=args.max_clients,
        max_size=args.max_size,
        pace=not args.no_pace,
        verbose=args.verbose,
    )


def read_wav_info(path: Path, required_sample_rate_hz: int) -> AudioInfo:
    if not path.exists():
        raise FileNotFoundError(path)
    if not path.is_file():
        raise ValueError(f"not a file: {path}")

    try:
        with wave.open(str(path), "rb") as wav:
            info = AudioInfo(
                path=path,
                sample_rate_hz=wav.getframerate(),
                channels=wav.getnchannels(),
                sample_width_bytes=wav.getsampwidth(),
                frames=wav.getnframes(),
            )
    except wave.Error as exc:
        raise ValueError(f"{path} is not a supported PCM WAV file: {exc}") from exc

    if info.sample_rate_hz != required_sample_rate_hz:
        raise ValueError(
            f"WAV sample rate {info.sample_rate_hz} Hz does not match --sample-rate "
            f"{required_sample_rate_hz} Hz. Resample the file first."
        )
    if info.channels < 1:
        raise ValueError("WAV file must have at least one channel")
    if info.sample_width_bytes not in (1, 2, 3, 4):
        raise ValueError(f"unsupported WAV sample width: {info.sample_width_bytes * 8} bits")

    return info


def clamp_s16(value: int) -> int:
    if value > 32767:
        return 32767
    if value < -32768:
        return -32768
    return value


def decode_sample(raw: bytes, sample_width_bytes: int) -> int:
    if sample_width_bytes == 1:
        return (raw[0] - 128) << 8

    value = int.from_bytes(raw, "little", signed=True)
    if sample_width_bytes > 2:
        value >>= 8 * (sample_width_bytes - 2)
    return value


def convert_frames_to_s16le_mono(raw: bytes, channels: int, sample_width_bytes: int) -> bytes:
    if channels == 1 and sample_width_bytes == 2:
        return raw

    input_frame_bytes = channels * sample_width_bytes
    if input_frame_bytes <= 0 or len(raw) % input_frame_bytes != 0:
        raise ValueError("WAV frame data is not aligned")

    output = bytearray((len(raw) // input_frame_bytes) * 2)
    out_offset = 0
    for frame_offset in range(0, len(raw), input_frame_bytes):
        mixed = 0
        for channel in range(channels):
            sample_offset = frame_offset + channel * sample_width_bytes
            mixed += decode_sample(raw[sample_offset : sample_offset + sample_width_bytes], sample_width_bytes)
        mixed = clamp_s16(int(mixed / channels))
        struct.pack_into("<h", output, out_offset, mixed)
        out_offset += 2
    return bytes(output)


def iter_pcm_chunks(info: AudioInfo, chunk_bytes: int) -> Iterator[bytes]:
    chunk_frames = chunk_bytes // 2
    with wave.open(str(info.path), "rb") as wav:
        while True:
            raw = wav.readframes(chunk_frames)
            if not raw:
                break
            yield convert_frames_to_s16le_mono(raw, info.channels, info.sample_width_bytes)


def binary_preview(data: bytes) -> str:
    if not data:
        return "(empty)"

    preview = " ".join(f"{value:02X}" for value in data[:BINARY_PREVIEW_BYTES])
    if len(data) > BINARY_PREVIEW_BYTES:
        return f"{preview} ..."
    return preview


def make_audio_start_payload(info: AudioInfo, options: ServerOptions) -> str:
    return json.dumps(
        {
            "type": "audio_start",
            "service": "speaker_server",
            "format": "pcm_s16le",
            "sample_rate_hz": info.sample_rate_hz,
            "channels": 1,
            "sample_width_bits": 16,
            "chunk_bytes": options.chunk_bytes,
            "frames": info.frames,
            "bytes": info.output_bytes,
            "source": info.path.name,
        },
        separators=(",", ":"),
    )


def make_audio_end_payload(frames: int, byte_count: int, elapsed_sec: float) -> str:
    return json.dumps(
        {
            "type": "audio_end",
            "service": "speaker_server",
            "frames": frames,
            "bytes": byte_count,
            "elapsed_ms": int(elapsed_sec * 1000),
        },
        separators=(",", ":"),
    )


def make_error_payload(message: str) -> str:
    return json.dumps(
        {
            "type": "error",
            "service": "speaker_server",
            "message": message,
        },
        separators=(",", ":"),
    )


def request_path(request: Request) -> str:
    return urlsplit(request.path).path


def validate_handshake(options: ServerOptions, state: ServerState):
    def process_request(connection: ServerConnection, request: Request) -> Response | None:
        if request_path(request) != options.path:
            return connection.respond(HTTPStatus.NOT_FOUND, "speaker websocket path not found\n")

        if options.token:
            expected = f"Bearer {options.token}"
            actual = request.headers.get("Authorization", "")
            if not hmac.compare_digest(actual, expected):
                response = connection.respond(HTTPStatus.UNAUTHORIZED, "unauthorized\n")
                response.headers["WWW-Authenticate"] = "Bearer"
                return response

        if len(state.connections) >= options.max_clients:
            return connection.respond(HTTPStatus.SERVICE_UNAVAILABLE, "too many speaker clients\n")

        return None

    return process_request


async def stream_audio(websocket: ServerConnection, info: AudioInfo, options: ServerOptions) -> None:
    remote = websocket.remote_address
    await websocket.send(make_audio_start_payload(info, options))
    LOGGER.info(
        "audio_start remote=%s source=%s frames=%d bytes=%d sample_rate=%d chunk_bytes=%d pace=%s",
        remote,
        info.path,
        info.frames,
        info.output_bytes,
        info.sample_rate_hz,
        options.chunk_bytes,
        options.pace,
    )

    started_at = time.monotonic()
    last_progress_at = started_at
    sent_frames = 0
    sent_bytes = 0

    try:
        for chunk in iter_pcm_chunks(info, options.chunk_bytes):
            await websocket.send(chunk)
            sent_bytes += len(chunk)
            sent_frames += len(chunk) // 2

            now = time.monotonic()
            if now - last_progress_at >= 1.0:
                last_progress_at = now
                LOGGER.info(
                    "audio_progress remote=%s frames=%d/%d bytes=%d preview=%s",
                    remote,
                    sent_frames,
                    info.frames,
                    sent_bytes,
                    binary_preview(chunk),
                )

            if options.pace:
                target_elapsed = sent_frames / info.sample_rate_hz
                elapsed = time.monotonic() - started_at
                if target_elapsed > elapsed:
                    await asyncio.sleep(target_elapsed - elapsed)
    except (OSError, ValueError, wave.Error) as exc:
        message = f"audio streaming failed: {exc}"
        LOGGER.exception("audio_failed remote=%s", remote)
        await websocket.send(make_error_payload(message))
        raise

    elapsed_sec = time.monotonic() - started_at
    await websocket.send(make_audio_end_payload(sent_frames, sent_bytes, elapsed_sec))
    LOGGER.info(
        "audio_end remote=%s frames=%d bytes=%d elapsed=%.2fs",
        remote,
        sent_frames,
        sent_bytes,
        elapsed_sec,
    )


async def consume_client_messages(websocket: ServerConnection) -> None:
    remote = websocket.remote_address
    async for message in websocket:
        if isinstance(message, str):
            LOGGER.info("client_text remote=%s len=%d payload=%s", remote, len(message), message)
        else:
            LOGGER.info("client_binary remote=%s len=%d preview=%s", remote, len(message), binary_preview(message))


async def handle_connection(
    websocket: ServerConnection,
    state: ServerState,
    options: ServerOptions,
    info: AudioInfo,
) -> None:
    from websockets.exceptions import ConnectionClosed

    state.connections.add(websocket)
    remote = websocket.remote_address
    LOGGER.info("client_connected remote=%s clients=%d", remote, len(state.connections))

    try:
        await stream_audio(websocket, info, options)
        LOGGER.info("stream_finished remote=%s keeping websocket open for status messages", remote)
        await consume_client_messages(websocket)
    except ConnectionClosed:
        LOGGER.info("client_closed remote=%s", remote)
    finally:
        state.connections.discard(websocket)
        LOGGER.info("client_disconnected remote=%s clients=%d", remote, len(state.connections))


async def serve_forever(options: ServerOptions, info: AudioInfo) -> None:
    try:
        from websockets.asyncio.server import ServerConnection, serve
        from websockets.http11 import Request, Response
    except ModuleNotFoundError as exc:
        raise RuntimeError(
            "speaker_server requires the websockets package. Install the server dependencies first."
        ) from exc

    state = ServerState()
    handler = lambda websocket: handle_connection(websocket, state, options, info)

    async with serve(
        handler,
        options.host,
        options.port,
        process_request=validate_handshake(options, state),
        max_size=options.max_size,
    ) as server:
        sockets = ", ".join(str(sock.getsockname()) for sock in server.sockets or [])
        LOGGER.info("speaker_server listening on ws://%s:%d%s sockets=[%s]", options.host, options.port, options.path, sockets)
        LOGGER.info(
            "audio file=%s input_channels=%d input_width=%d-bit output=pcm_s16le mono %d Hz frames=%d seconds=%.2f",
            info.path,
            info.channels,
            info.sample_width_bytes * 8,
            info.sample_rate_hz,
            info.frames,
            info.frames / info.sample_rate_hz if info.sample_rate_hz else 0,
        )
        local_ips = get_local_ipv4_candidates()
        if local_ips:
            LOGGER.info("local IPv4 candidates: %s", ", ".join(local_ips))
        if options.token:
            LOGGER.info("auth enabled: Authorization: Bearer <token>")
        else:
            LOGGER.info("auth disabled")
        await asyncio.Future()


def configure_logging(verbose: bool) -> None:
    logging.basicConfig(
        level=logging.DEBUG if verbose else logging.INFO,
        format="%(asctime)s %(levelname)s [%(name)s] %(message)s",
    )


def main() -> None:
    try:
        options = parse_args(sys.argv[1:])
        configure_logging(options.verbose)
        info = read_wav_info(options.file, options.sample_rate_hz)
    except ValueError as exc:
        print(exc, file=sys.stderr)
        print("usage: python -m speaker_server input.wav [--host 0.0.0.0] [--port 8082] [--path /audio]", file=sys.stderr)
        raise SystemExit(2) from exc
    except OSError as exc:
        print(f"speaker_server failed to open audio file: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc

    try:
        asyncio.run(serve_forever(options, info))
    except KeyboardInterrupt:
        pass
    except RuntimeError as exc:
        print(f"speaker_server failed: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
    except (OSError, WebSocketException) as exc:
        print(f"speaker_server failed: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc


if __name__ == "__main__":
    main()
