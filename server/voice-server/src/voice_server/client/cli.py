from __future__ import annotations

import argparse
import asyncio
import json
import logging
import sys
import wave
from pathlib import Path
from typing import Any

from pydantic import Field
from pydantic_settings import BaseSettings, SettingsConfigDict
from websockets.asyncio.client import connect

from voice_server.audio import save_pcm_as_wav

logger = logging.getLogger(__name__)


class ClientDefaults(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=".env",
        env_file_encoding="utf-8",
        extra="ignore",
    )

    asr_model: str | None = Field(default=None, validation_alias="ASR_MODEL")
    asr_language: str | None = Field(default=None, validation_alias="ASR_LANGUAGE")
    asr_sample_rate: int | None = Field(default=None, validation_alias="ASR_SAMPLE_RATE")
    asr_channels: int | None = Field(default=None, validation_alias="ASR_CHANNELS")
    asr_segment_seconds: float | None = Field(default=None, validation_alias="ASR_SEGMENT_SECONDS")
    tts_voice: str | None = Field(default=None, validation_alias="TTS_VOICE")
    tts_response_format: str | None = Field(default=None, validation_alias="TTS_RESPONSE_FORMAT")
    tts_sample_rate: int | None = Field(default=None, validation_alias="TTS_SAMPLE_RATE")


def _drop_none(payload: dict[str, Any]) -> dict[str, Any]:
    return {key: value for key, value in payload.items() if value is not None}


def _require_raw_audio_args(args: argparse.Namespace) -> None:
    if args.input_sample_rate is not None and args.input_channels is not None:
        return
    raise ValueError(
        "Set ASR_SAMPLE_RATE/ASR_CHANNELS or pass --input-sample-rate/--input-channels "
        "for raw PCM input."
    )


def _raw_audio_format(args: argparse.Namespace) -> dict[str, Any]:
    _require_raw_audio_args(args)
    return {
        "encoding": "pcm_s16le",
        "sample_rate": args.input_sample_rate,
        "channels": args.input_channels,
        "container": "raw",
    }


async def _send_start(
    websocket: Any,
    audio_format: dict[str, Any],
    args: argparse.Namespace,
) -> None:
    payload: dict[str, Any] = {
        "type": "start",
        "audio_format": audio_format,
    }
    if args.segment_seconds is not None:
        payload["segment_seconds"] = args.segment_seconds

    asr = _drop_none(
        {
            "language": args.language,
            "model": args.asr_model,
        }
    )
    tts = _drop_none(
        {
            "voice": args.voice,
            "response_format": args.tts_format,
        }
    )
    if asr:
        payload["asr"] = asr
    if tts:
        payload["tts"] = tts

    await websocket.send(
        json.dumps(
            payload,
            ensure_ascii=False,
        )
    )


async def _paced_send(
    websocket: Any,
    chunk: bytes,
    args: argparse.Namespace,
    bytes_per_second: int,
) -> None:
    await websocket.send(chunk)
    if args.real_time:
        await asyncio.sleep(len(chunk) / max(bytes_per_second, 1))


async def _send_wav_file(websocket: Any, path: Path, args: argparse.Namespace) -> None:
    with wave.open(str(path), "rb") as wav:
        sample_width = wav.getsampwidth()
        if sample_width != 2:
            raise ValueError("Only 16-bit WAV input is supported.")

        audio_format = {
            "encoding": "pcm_s16le",
            "sample_rate": wav.getframerate(),
            "channels": wav.getnchannels(),
            "container": "raw",
        }
        await _send_start(websocket, audio_format, args)

        frame_size = wav.getnchannels() * sample_width
        frames_per_chunk = max(1, args.chunk_size // frame_size)
        bytes_per_second = wav.getframerate() * frame_size
        while chunk := wav.readframes(frames_per_chunk):
            await _paced_send(websocket, chunk, args, bytes_per_second)


async def _send_raw_file(websocket: Any, path: Path, args: argparse.Namespace) -> None:
    audio_format = _raw_audio_format(args)
    await _send_start(websocket, audio_format, args)
    bytes_per_second = args.input_sample_rate * args.input_channels * 2
    with path.open("rb") as audio_file:
        while chunk := audio_file.read(args.chunk_size):
            await _paced_send(websocket, chunk, args, bytes_per_second)


async def _send_stdin_pcm(websocket: Any, args: argparse.Namespace) -> None:
    audio_format = _raw_audio_format(args)
    await _send_start(websocket, audio_format, args)
    bytes_per_second = args.input_sample_rate * args.input_channels * 2
    while chunk := await asyncio.to_thread(sys.stdin.buffer.read, args.chunk_size):
        await _paced_send(websocket, chunk, args, bytes_per_second)


async def _receive_until_done(
    websocket: Any,
    output_path: Path,
    tts_sample_rate: int | None,
) -> None:
    audio_chunks: list[bytes] = []
    output_format = "pcm"
    current_tts_bytes = 0

    async for message in websocket:
        if isinstance(message, bytes):
            audio_chunks.append(message)
            current_tts_bytes += len(message)
            print(f"audio_chunk bytes={len(message)} total={current_tts_bytes}")
            continue

        server_event = json.loads(message)
        event_type = server_event.get("type")
        if event_type == "transcript":
            print(f"transcript: {server_event.get('text', '')}")
        elif event_type == "tts_start":
            output_format = server_event.get("response_format") or output_format
            tts_sample_rate = int(server_event.get("sample_rate") or tts_sample_rate)
            current_tts_bytes = 0
            print(f"tts_start format={output_format} sample_rate={tts_sample_rate}")
        elif event_type == "tts_end":
            print(f"tts_end bytes={server_event.get('bytes')}")
        elif event_type == "error":
            print(f"error[{server_event.get('code')}]: {server_event.get('message')}")
        else:
            print(json.dumps(server_event, ensure_ascii=False))

        if event_type in {"stopped", "error"}:
            break

    if not audio_chunks:
        print("No audio returned.")
        return

    output_path.parent.mkdir(parents=True, exist_ok=True)
    audio = b"".join(audio_chunks)
    if output_format == "pcm":
        if tts_sample_rate is None:
            raise ValueError("Server did not report TTS sample rate; set --tts-sample-rate.")
        save_pcm_as_wav(str(output_path), audio, sample_rate=tts_sample_rate)
    else:
        output_path.write_bytes(audio)
    print(f"saved: {output_path} ({len(audio)} bytes payload)")


async def run_client(args: argparse.Namespace) -> None:
    output_path = Path(args.output)
    tts_sample_rate = args.tts_sample_rate

    async with connect(args.url, max_size=None) as websocket:
        receiver = asyncio.create_task(_receive_until_done(websocket, output_path, tts_sample_rate))

        if args.text:
            payload: dict[str, Any] = {
                "type": "text",
                "text": args.text,
            }
            tts = _drop_none(
                {
                    "voice": args.voice,
                    "response_format": args.tts_format,
                }
            )
            if tts:
                payload["tts"] = tts
            await websocket.send(
                json.dumps(
                    payload,
                    ensure_ascii=False,
                )
            )
        else:
            if args.stdin_pcm:
                await _send_stdin_pcm(websocket, args)
            elif args.audio:
                audio_path = Path(args.audio)
                if audio_path.suffix.lower() == ".wav":
                    await _send_wav_file(websocket, audio_path, args)
                else:
                    await _send_raw_file(websocket, audio_path, args)
            else:
                raise ValueError(
                    "Provide --audio/--stdin-pcm for ASR->TTS validation "
                    "or --text for TTS-only validation."
                )
            await websocket.send(json.dumps({"type": "commit"}))

        await websocket.send(json.dumps({"type": "stop"}))
        await receiver


def build_parser() -> argparse.ArgumentParser:
    defaults = ClientDefaults()
    parser = argparse.ArgumentParser(description="Validate the local full-duplex voice bridge.")
    parser.add_argument(
        "--url",
        default="ws://127.0.0.1:8765/ws",
        help="Local voice bridge WebSocket URL.",
    )
    parser.add_argument(
        "--audio",
        help="Input 16-bit PCM/WAV audio file for ASR -> TTS validation.",
    )
    parser.add_argument("--stdin-pcm", action="store_true", help="Read live raw PCM from stdin.")
    parser.add_argument("--text", help="Text for TTS-only smoke validation.")
    parser.add_argument(
        "--output",
        default="out/voice-client-output.wav",
        help="Output audio file.",
    )
    parser.add_argument(
        "--voice",
        default=defaults.tts_voice,
        help="VoxCPM2 voice.",
    )
    parser.add_argument(
        "--tts-format",
        default=defaults.tts_response_format,
        choices=["pcm", "wav", "flac", "mp3", "aac", "opus"],
    )
    parser.add_argument(
        "--tts-sample-rate",
        type=int,
        default=defaults.tts_sample_rate,
    )
    parser.add_argument(
        "--language",
        default=defaults.asr_language,
        help="ASR language hint.",
    )
    parser.add_argument(
        "--asr-model",
        default=defaults.asr_model,
        help="Optional ASR model id.",
    )
    parser.add_argument(
        "--segment-seconds",
        type=float,
        default=defaults.asr_segment_seconds,
    )
    parser.add_argument("--chunk-size", type=int, default=32 * 1024)
    parser.add_argument(
        "--real-time",
        action="store_true",
        help="Sleep between chunks to mimic live upload.",
    )
    parser.add_argument(
        "--input-sample-rate",
        type=int,
        default=defaults.asr_sample_rate,
        help="Raw PCM input sample rate.",
    )
    parser.add_argument(
        "--input-channels",
        type=int,
        default=defaults.asr_channels,
        help="Raw PCM input channels.",
    )
    parser.add_argument("--log-level", default="WARNING")
    return parser


def main() -> None:
    args = build_parser().parse_args()
    logging.basicConfig(level=args.log_level.upper())
    asyncio.run(run_client(args))


if __name__ == "__main__":
    main()
