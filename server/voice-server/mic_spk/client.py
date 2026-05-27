from __future__ import annotations

import argparse
import asyncio
import json
import signal
import sys
from contextlib import suppress
from typing import Any

from websockets.asyncio.client import connect
from websockets.exceptions import ConnectionClosed


def _optional_text(value: str | None) -> str | None:
    return value if value not in {None, ""} else None


def _drop_none(payload: dict[str, Any]) -> dict[str, Any]:
    return {key: value for key, value in payload.items() if value is not None}


def _coerce_device(value: str | None) -> int | str | None:
    if value is None:
        return None
    with suppress(ValueError):
        return int(value)
    return value


def _load_sounddevice() -> Any:
    try:
        import sounddevice as sd
    except ImportError as exc:
        raise RuntimeError(
            "Missing dependency: sounddevice. Install it with "
            "`uv add sounddevice` or run with `uv run --with sounddevice ...`."
        ) from exc
    return sd


class Playback:
    def __init__(
        self,
        sd: Any,
        *,
        device: int | str | None,
        channels: int,
        fallback_sample_rate: int,
    ) -> None:
        self.sd = sd
        self.device = device
        self.channels = channels
        self.fallback_sample_rate = fallback_sample_rate
        self.sample_rate: int | None = None
        self.stream: Any | None = None

    def configure(self, sample_rate: int | None) -> None:
        sample_rate = sample_rate or self.fallback_sample_rate
        if self.stream is not None and self.sample_rate == sample_rate:
            return

        self.close()
        self.sample_rate = sample_rate
        self.stream = self.sd.RawOutputStream(
            samplerate=sample_rate,
            channels=self.channels,
            dtype="int16",
            device=self.device,
        )
        self.stream.start()

    async def write(self, chunk: bytes) -> None:
        if self.stream is None:
            self.configure(None)
        await asyncio.to_thread(self.stream.write, chunk)

    def close(self) -> None:
        if self.stream is None:
            return
        with suppress(Exception):
            self.stream.stop()
        with suppress(Exception):
            self.stream.close()
        self.stream = None


async def _send_json(websocket: Any, send_lock: asyncio.Lock, payload: dict[str, Any]) -> None:
    async with send_lock:
        await websocket.send(json.dumps(payload, ensure_ascii=False))


async def _send_audio_loop(
    websocket: Any,
    send_lock: asyncio.Lock,
    audio_queue: asyncio.Queue[bytes | None],
) -> None:
    while True:
        chunk = await audio_queue.get()
        try:
            if chunk is None:
                return
            async with send_lock:
                await websocket.send(chunk)
        finally:
            audio_queue.task_done()


async def _stdin_control_loop(
    websocket: Any,
    send_lock: asyncio.Lock,
    stop_event: asyncio.Event,
) -> None:
    print("输入 Enter 提交当前语音段；输入 q 后 Enter 结束；输入 p 后 Enter 发送 ping。")
    while not stop_event.is_set():
        line = await asyncio.to_thread(sys.stdin.readline)
        if line == "":
            return

        command = line.strip().lower()
        if command in {"q", "quit", "stop", "exit"}:
            stop_event.set()
            return
        if command in {"p", "ping"}:
            await _send_json(websocket, send_lock, {"type": "ping"})
            continue
        if command == "":
            await _send_json(websocket, send_lock, {"type": "commit"})
            print("已提交当前语音段。")
            continue

        print("可用命令：Enter=commit，p=ping，q=stop。")


async def _receive_loop(
    websocket: Any,
    playback: Playback,
    stop_event: asyncio.Event,
) -> None:
    response_format = "pcm"
    async for message in websocket:
        if isinstance(message, bytes):
            if response_format == "pcm":
                await playback.write(message)
            else:
                print(f"收到 {len(message)} bytes {response_format} 音频，当前客户端只直接播放 pcm。")
            continue

        try:
            event = json.loads(message)
        except json.JSONDecodeError:
            print(f"收到非 JSON 文本帧：{message}")
            continue

        event_type = event.get("type")
        if event_type == "ready":
            defaults = event.get("defaults") or {}
            print(
                "server ready "
                f"input_sample_rate={defaults.get('input_sample_rate')} "
                f"tts_sample_rate={defaults.get('tts_sample_rate')}"
            )
        elif event_type == "started":
            audio_format = event.get("audio_format") or {}
            print(
                "started "
                f"sample_rate={audio_format.get('sample_rate')} "
                f"channels={audio_format.get('channels')} "
                f"segment_seconds={event.get('segment_seconds')}"
            )
        elif event_type == "committed":
            print(f"committed queued={event.get('queued')}")
        elif event_type == "transcript":
            print(f"transcript: {event.get('text', '')}")
        elif event_type == "tts_start":
            response_format = event.get("response_format") or response_format
            sample_rate = event.get("sample_rate")
            playback.configure(int(sample_rate) if sample_rate else None)
            print(
                "tts_start "
                f"format={response_format} "
                f"sample_rate={sample_rate} "
                f"text={event.get('text', '')}"
            )
        elif event_type == "tts_end":
            print(f"tts_end bytes={event.get('bytes')}")
        elif event_type == "pong":
            print("pong")
        elif event_type == "stopped":
            print("stopped")
            stop_event.set()
            return
        elif event_type == "error":
            print(f"error[{event.get('code')}]: {event.get('message')}")
            stop_event.set()
            return
        else:
            print(json.dumps(event, ensure_ascii=False))


def _build_start_event(args: argparse.Namespace) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "type": "start",
        "audio_format": {
            "encoding": "pcm_s16le",
            "sample_rate": args.input_sample_rate,
            "channels": args.input_channels,
            "container": "raw",
        },
        "segment_seconds": args.segment_seconds,
    }

    asr = _drop_none(
        {
            "language": _optional_text(args.language),
            "model": _optional_text(args.asr_model),
        }
    )
    tts = _drop_none(
        {
            "voice": _optional_text(args.voice),
            "response_format": args.tts_format,
        }
    )
    if asr:
        payload["asr"] = asr
    if tts:
        payload["tts"] = tts
    return payload


def _install_signal_handlers(stop_event: asyncio.Event) -> None:
    loop = asyncio.get_running_loop()
    for signum in (signal.SIGINT, signal.SIGTERM):
        with suppress(NotImplementedError):
            loop.add_signal_handler(signum, stop_event.set)


async def run_client(args: argparse.Namespace) -> None:
    sd = _load_sounddevice()

    if args.list_devices:
        print(sd.query_devices())
        return

    stop_event = asyncio.Event()
    _install_signal_handlers(stop_event)

    input_device = _coerce_device(args.input_device)
    output_device = _coerce_device(args.output_device)
    block_frames = max(1, int(args.input_sample_rate * args.chunk_ms / 1000))
    audio_queue: asyncio.Queue[bytes | None] = asyncio.Queue(maxsize=args.queue_size)
    send_lock = asyncio.Lock()
    dropped_chunks = 0

    def put_audio_chunk(chunk: bytes, status_text: str | None) -> None:
        nonlocal dropped_chunks
        if status_text:
            print(f"mic status: {status_text}", file=sys.stderr)
        if stop_event.is_set():
            return
        try:
            audio_queue.put_nowait(chunk)
        except asyncio.QueueFull:
            dropped_chunks += 1
            if dropped_chunks == 1 or dropped_chunks % 50 == 0:
                print(f"麦克风队列已满，已丢弃 {dropped_chunks} 个音频块。", file=sys.stderr)

    loop = asyncio.get_running_loop()

    def audio_callback(indata: Any, _frames: int, _time: Any, status: Any) -> None:
        loop.call_soon_threadsafe(
            put_audio_chunk,
            bytes(indata),
            str(status) if status else None,
        )

    playback = Playback(
        sd,
        device=output_device,
        channels=args.output_channels,
        fallback_sample_rate=args.output_sample_rate or args.input_sample_rate,
    )

    async with connect(args.url, max_size=None) as websocket:
        await _send_json(websocket, send_lock, _build_start_event(args))

        sender_task = asyncio.create_task(_send_audio_loop(websocket, send_lock, audio_queue))
        receiver_task = asyncio.create_task(_receive_loop(websocket, playback, stop_event))
        stop_wait_task = asyncio.create_task(stop_event.wait())
        control_task: asyncio.Task[None] | None = None
        if args.stdin_controls and sys.stdin.isatty():
            control_task = asyncio.create_task(_stdin_control_loop(websocket, send_lock, stop_event))

        print(
            "开始采集麦克风 "
            f"{args.input_sample_rate}Hz/{args.input_channels}ch，"
            f"chunk={args.chunk_ms}ms。"
        )

        try:
            with sd.RawInputStream(
                samplerate=args.input_sample_rate,
                channels=args.input_channels,
                dtype="int16",
                blocksize=block_frames,
                device=input_device,
                callback=audio_callback,
            ):
                done, _pending = await asyncio.wait(
                    {receiver_task, stop_wait_task},
                    return_when=asyncio.FIRST_COMPLETED,
                )
                for task in done:
                    task.result()
        finally:
            stop_event.set()
            if control_task is not None:
                control_task.cancel()
            stop_wait_task.cancel()

            await audio_queue.put(None)
            with suppress(ConnectionClosed):
                await sender_task

            if not receiver_task.done():
                with suppress(ConnectionClosed, OSError):
                    await _send_json(websocket, send_lock, {"type": "commit"})
                    await _send_json(websocket, send_lock, {"type": "stop"})
                with suppress(TimeoutError):
                    await asyncio.wait_for(receiver_task, timeout=args.close_timeout)

            if not receiver_task.done():
                receiver_task.cancel()

            playback.close()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="实时麦克风 -> voice-server -> 扬声器客户端。")
    parser.add_argument("--url", default="ws://127.0.0.1:8765/ws", help="voice-server WebSocket 地址。")
    parser.add_argument("--input-sample-rate", type=int, default=48000, help="麦克风 PCM 采样率。")
    parser.add_argument("--input-channels", type=int, default=1, help="麦克风声道数。")
    parser.add_argument("--output-sample-rate", type=int, help="无 tts_start 时使用的播放采样率。")
    parser.add_argument("--output-channels", type=int, default=1, help="播放声道数。")
    parser.add_argument("--chunk-ms", type=int, default=40, help="每个 WebSocket 音频帧包含的毫秒数。")
    parser.add_argument("--queue-size", type=int, default=100, help="麦克风发送队列最大音频块数量。")
    parser.add_argument("--segment-seconds", type=float, default=3.0, help="服务端 ASR 分段秒数。")
    parser.add_argument("--language", default="zh", help="ASR 语言提示；传空字符串可不发送。")
    parser.add_argument("--asr-model", help="覆盖 ASR 模型。")
    parser.add_argument("--voice", help="覆盖 TTS 音色。")
    parser.add_argument(
        "--tts-format",
        default="pcm",
        choices=["pcm", "wav", "flac", "mp3", "aac", "opus"],
        help="TTS 输出格式；实时扬声器播放请使用 pcm。",
    )
    parser.add_argument("--input-device", help="输入设备 id 或名称；用 --list-devices 查看。")
    parser.add_argument("--output-device", help="输出设备 id 或名称；用 --list-devices 查看。")
    parser.add_argument("--list-devices", action="store_true", help="列出 sounddevice 可用音频设备后退出。")
    parser.add_argument(
        "--no-stdin-controls",
        dest="stdin_controls",
        action="store_false",
        help="禁用 Enter 提交、q 结束、p ping 的终端控制。",
    )
    parser.add_argument("--close-timeout", type=float, default=5.0, help="停止时等待服务端关闭的秒数。")
    parser.set_defaults(stdin_controls=True)
    return parser


def main() -> None:
    args = build_parser().parse_args()
    try:
        asyncio.run(run_client(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
