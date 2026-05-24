from __future__ import annotations

import argparse
import re
import socket
import struct
import subprocess
import sys
import time
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, Iterator

MAGIC = 0x314D4350
HEADER_STRUCT = struct.Struct("<IHHIIHHII")
HEADER_SIZE = HEADER_STRUCT.size
DEFAULT_BAUD = 921600
DEFAULT_UDP_BIND = "0.0.0.0"
DEFAULT_UDP_PORT = 8765
DEFAULT_SECONDS = 10.0
SERIAL_CHUNK_BYTES = 1024


@dataclass(frozen=True)
class Packet:
    sequence: int
    sample_rate_hz: int
    channels: int
    sample_width_bits: int
    frame_samples: int
    payload: bytes


@dataclass(frozen=True)
class RecorderOptions:
    mode: str
    output: Path
    seconds: float
    quiet: bool
    port: str
    baud: int
    bind: str
    udp_port: int


@dataclass(frozen=True)
class RecordingStats:
    frames: int
    sample_rate_hz: int
    dropped_packets: int


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
    parser = argparse.ArgumentParser(description="Record ESPESP PCM stream packets into a WAV file.")
    subparsers = parser.add_subparsers(dest="mode", required=True)

    uart = subparsers.add_parser("uart", help="read packets from a serial port")
    uart.add_argument("port", help="serial port, for example /dev/ttyUSB0 or COM5")
    uart.add_argument("output", type=Path, help="output .wav path")
    uart.add_argument("--baud", type=int, default=DEFAULT_BAUD, help=f"serial baud rate, default: {DEFAULT_BAUD}")
    uart.add_argument("--seconds", type=float, default=DEFAULT_SECONDS, help=f"recording length, default: {DEFAULT_SECONDS}")
    uart.add_argument("--quiet", action="store_true", help="suppress progress output")

    udp = subparsers.add_parser("udp", help="read packets from UDP")
    udp.add_argument("output", type=Path, help="output .wav path")
    udp.add_argument("--bind", default=DEFAULT_UDP_BIND, help=f"listen address, default: {DEFAULT_UDP_BIND}")
    udp.add_argument("--port", type=int, default=DEFAULT_UDP_PORT, help=f"UDP port, default: {DEFAULT_UDP_PORT}")
    udp.add_argument("--seconds", type=float, default=DEFAULT_SECONDS, help=f"recording length, default: {DEFAULT_SECONDS}")
    udp.add_argument("--quiet", action="store_true", help="suppress progress output")

    return parser


def parse_args(argv: list[str]) -> RecorderOptions:
    args = build_parser().parse_args(argv)
    if args.seconds <= 0:
        raise ValueError("--seconds must be > 0")

    if args.mode == "uart":
        if args.baud <= 0:
            raise ValueError("--baud must be > 0")
        return RecorderOptions(
            mode=args.mode,
            output=args.output,
            seconds=args.seconds,
            quiet=args.quiet,
            port=args.port,
            baud=args.baud,
            bind="",
            udp_port=0,
        )

    if args.port <= 0 or args.port > 65535:
        raise ValueError("--port must be in 1..65535")
    return RecorderOptions(
        mode=args.mode,
        output=args.output,
        seconds=args.seconds,
        quiet=args.quiet,
        port="",
        baud=0,
        bind=args.bind,
        udp_port=args.port,
    )


def parse_packet(data: bytes) -> Packet | None:
    if len(data) < HEADER_SIZE:
        return None

    (
        magic,
        version,
        header_size,
        sequence,
        sample_rate_hz,
        channels,
        sample_width_bits,
        frame_samples,
        payload_bytes,
    ) = HEADER_STRUCT.unpack(data[:HEADER_SIZE])

    if magic != MAGIC or version != 1 or header_size != HEADER_SIZE:
        return None
    if sample_width_bits != 16 or channels != 1:
        return None
    if payload_bytes != frame_samples * channels * (sample_width_bits // 8):
        return None
    if len(data) < header_size + payload_bytes:
        return None

    payload = data[header_size : header_size + payload_bytes]
    return Packet(
        sequence=sequence,
        sample_rate_hz=sample_rate_hz,
        channels=channels,
        sample_width_bits=sample_width_bits,
        frame_samples=frame_samples,
        payload=payload,
    )


def iter_uart_packets(port: str, baud: int) -> Iterator[Packet]:
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError("UART mode requires pyserial. Install with `pip install -e .` in server/.") from exc

    with serial.Serial(port=port, baudrate=baud, timeout=1) as ser:
        buffer = bytearray()
        magic = struct.pack("<I", MAGIC)
        while True:
            chunk = ser.read(SERIAL_CHUNK_BYTES)
            if chunk:
                buffer.extend(chunk)

            while True:
                start = buffer.find(magic)
                if start < 0:
                    del buffer[:-3]
                    break
                if start > 0:
                    del buffer[:start]
                if len(buffer) < HEADER_SIZE:
                    break

                header_values = HEADER_STRUCT.unpack(buffer[:HEADER_SIZE])
                header_size = header_values[2]
                payload_bytes = header_values[8]
                packet_bytes = header_size + payload_bytes
                if header_size != HEADER_SIZE or payload_bytes > 4096:
                    del buffer[:1]
                    continue
                if len(buffer) < packet_bytes:
                    break

                packet = parse_packet(bytes(buffer[:packet_bytes]))
                del buffer[:packet_bytes]
                if packet is not None:
                    yield packet


def iter_udp_packets(bind: str, port: int, quiet: bool = False, first_packet_timeout: float | None = None) -> Iterator[Packet]:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind((bind, port))
        sock.settimeout(1)
        started_at = time.monotonic()
        first_packet_deadline = started_at + first_packet_timeout if first_packet_timeout is not None else None
        last_wait_log_at = started_at
        invalid_packets = 0
        first_packet_seen = False
        while True:
            try:
                data, addr = sock.recvfrom(8192)
            except socket.timeout:
                now = time.monotonic()
                if not first_packet_seen and first_packet_deadline is not None and now >= first_packet_deadline:
                    raise TimeoutError("timed out waiting for first PCM packet")
                if not quiet:
                    if now - last_wait_log_at >= 2:
                        print(f"still waiting for UDP packets... elapsed={now - started_at:.1f}s", flush=True)
                        last_wait_log_at = now
                continue
            packet = parse_packet(data)
            if packet is None:
                invalid_packets += 1
                if not quiet and invalid_packets <= 3:
                    print(
                        f"ignored non-PCM UDP packet from {addr[0]}:{addr[1]} bytes={len(data)}",
                        flush=True,
                    )
                continue
            if not quiet and not first_packet_seen:
                print(
                    f"first PCM packet from {addr[0]}:{addr[1]} "
                    f"sequence={packet.sequence} bytes={len(data)}",
                    flush=True,
                )
                first_packet_seen = True
            yield packet


def write_packets_to_wav(packets: Iterator[Packet], wav_file: BinaryIO, options: RecorderOptions) -> RecordingStats:
    if not options.quiet:
        print("waiting for first PCM packet...", flush=True)
    first_packet = next(packets)
    started_at = time.monotonic()
    sample_width_bytes = first_packet.sample_width_bits // 8
    expected_sequence = first_packet.sequence
    written_frames = 0
    dropped_packets = 0
    last_progress_at = -1.0

    with wave.open(wav_file, "wb") as wav:
        wav.setnchannels(first_packet.channels)
        wav.setsampwidth(sample_width_bytes)
        wav.setframerate(first_packet.sample_rate_hz)

        packet = first_packet
        while True:
            if packet.sample_rate_hz != first_packet.sample_rate_hz:
                raise RuntimeError("sample rate changed during recording")
            if packet.channels != first_packet.channels or packet.sample_width_bits != first_packet.sample_width_bits:
                raise RuntimeError("PCM format changed during recording")

            missing = packet.sequence - expected_sequence
            if missing > 0:
                dropped_packets += missing
                silence = b"\x00" * len(packet.payload) * missing
                wav.writeframesraw(silence)
                written_frames += packet.frame_samples * missing
            expected_sequence = packet.sequence + 1

            wav.writeframesraw(packet.payload)
            written_frames += packet.frame_samples

            elapsed = time.monotonic() - started_at
            if not options.quiet and elapsed - last_progress_at >= 0.5:
                last_progress_at = elapsed
                print(
                    f"\rrecording {elapsed:5.1f}s frames={written_frames} dropped_packets={dropped_packets}",
                    end="",
                    flush=True,
                )

            if elapsed >= options.seconds:
                break
            packet = next(packets)

    if not options.quiet:
        print()
    return RecordingStats(
        frames=written_frames,
        sample_rate_hz=first_packet.sample_rate_hz,
        dropped_packets=dropped_packets,
    )


def main() -> None:
    try:
        options = parse_args(sys.argv[1:])
    except ValueError as exc:
        print(exc, file=sys.stderr)
        raise SystemExit(2) from exc

    if options.mode == "uart":
        if not options.quiet:
            print(f"opening UART port={options.port} baud={options.baud}", flush=True)
        packets = iter_uart_packets(options.port, options.baud)
    else:
        if not options.quiet:
            print(f"listening UDP bind={options.bind} port={options.udp_port}", flush=True)
            local_ips = get_local_ipv4_candidates()
            if local_ips:
                print(f"local IPv4 addresses: {', '.join(local_ips)}", flush=True)
            else:
                print("local IPv4 addresses: unavailable", flush=True)
        packets = iter_udp_packets(options.bind, options.udp_port, options.quiet, options.seconds)

    try:
        options.output.parent.mkdir(parents=True, exist_ok=True)
        with options.output.open("wb") as output:
            stats = write_packets_to_wav(packets, output, options)
    except KeyboardInterrupt:
        raise SystemExit(130) from None
    except (OSError, RuntimeError, StopIteration, TimeoutError) as exc:
        print(f"PCM recorder failed: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc

    seconds = stats.frames / stats.sample_rate_hz if stats.frames else 0
    print(
        f"wrote {options.output} frames={stats.frames} "
        f"sample_rate={stats.sample_rate_hz} approx_seconds={seconds:.2f} "
        f"dropped_packets={stats.dropped_packets}"
    )


if __name__ == "__main__":
    main()
