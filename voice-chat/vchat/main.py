import argparse
import asyncio
import contextlib
import json
import os
import ssl
import time
from collections.abc import AsyncIterator, Iterator
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import httpx
from openai import AsyncOpenAI
import websockets


DEFAULT_SYSTEM_PROMPT = "你是一个叫千问的人，介绍的时候就说自己是千问，不要说别的，回复尽量简短，禁止使用 Markdown 语法。"
DEFAULT_ASR_CHUNK_SIZE = "5-10-5"
PCM_SAMPLE_WIDTH = 2
TTS_EDGE_FADE_MS = 5
TTS_START_BUFFER_MS = 120
ASR_FINAL_GRACE_TIMEOUT = 1.2
CONTROL_TEXT_PREVIEW_CHARS = 120
CONTROL_TEXT_CHUNK_CHARS = 160
CONTROL_FRAME_MAX_BYTES = 900
CONTROL_TEXT_FIELDS = ("text", "message", "raw", "reply")


@dataclass(frozen=True)
class Settings:
    host: str
    port: int
    asr_host: str
    asr_port: int
    asr_ssl: bool
    asr_ssl_verify: bool
    asr_proxy: str | bool | None
    asr_mode: str
    asr_sample_rate: int
    asr_chunk_size: list[int]
    asr_chunk_interval: int
    asr_final_timeout: float
    asr_final_grace_timeout: float
    use_itn: bool
    svs_itn: bool
    llm_base_url: str
    llm_api_key: str
    llm_model: str
    llm_system_prompt: str
    llm_max_tokens: int
    llm_temperature: float
    llm_top_p: float
    llm_thinking_disabled: bool
    llm_timeout: float
    llm_connect_timeout: float
    tts_base_url: str
    tts_model: str
    tts_voice: str
    tts_sample_rate: int
    tts_language: str
    tts_task_type: str
    tts_chunk_size: int
    tts_first_chunk_timeout: float

    @property
    def asr_uri(self) -> str:
        scheme = "wss" if self.asr_ssl else "ws"
        return f"{scheme}://{self.asr_host}:{self.asr_port}"

    @property
    def tts_url(self) -> str:
        return f"{self.tts_base_url.rstrip('/')}/v1/audio/speech"

    @property
    def asr_proxy_label(self) -> str:
        if self.asr_proxy is True:
            return "auto"
        if self.asr_proxy is None:
            return "disabled"
        return self.asr_proxy


class LockedSender:
    def __init__(self, ws: Any) -> None:
        self.ws = ws
        self.lock = asyncio.Lock()

    async def send(self, message: str | bytes) -> None:
        async with self.lock:
            await self.ws.send(message)


def recognition_join_separator(left: str, right: str) -> str:
    if not left or not right:
        return ""
    if left[-1].isspace() or right[0].isspace():
        return ""
    if left[-1].isascii() and right[0].isascii() and left[-1].isalnum() and right[0].isalnum():
        return " "
    return ""


def merge_recognition_text(existing: str, incoming: str) -> str:
    existing = " ".join(existing.strip().split())
    incoming = " ".join(incoming.strip().split())
    if not existing:
        return incoming
    if not incoming:
        return existing
    if incoming == existing or existing.endswith(incoming):
        return existing
    if incoming.startswith(existing):
        return incoming

    max_overlap = min(len(existing), len(incoming))
    for overlap in range(max_overlap, 0, -1):
        if existing[-overlap:] == incoming[:overlap]:
            return existing + incoming[overlap:]

    return existing + recognition_join_separator(existing, incoming) + incoming


class AsrSession:
    def __init__(self, settings: Settings, turn_id: int, client_ws: Any) -> None:
        self.settings = settings
        self.turn_id = turn_id
        self.client_ws = client_ws
        self.ws: Any | None = None
        self.receiver: asyncio.Task[None] | None = None
        self.final_seen = asyncio.Event()
        self.final_text = ""
        self.last_text = ""
        self.partial_text = ""
        self.final_event_text = ""
        self.finish_requested = False
        self.started_at = time.perf_counter()
        self.audio_bytes = 0
        self.audio_chunks = 0
        self.final_revision = 0

    async def __aenter__(self) -> "AsrSession":
        self.ws = await websockets.connect(
            self.settings.asr_uri,
            ssl=build_asr_ssl_context(self.settings),
            max_size=None,
            proxy=self.settings.asr_proxy,
        )
        await self.ws.send(json_dumps(self._begin_message()))
        self.receiver = asyncio.create_task(self._receive_asr_messages())
        print(f"[turn {self.turn_id}] asr_start {self.settings.asr_uri}", flush=True)
        return self

    async def __aexit__(self, exc_type: object, exc: object, tb: object) -> None:
        if self.receiver:
            self.receiver.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self.receiver
        if self.ws:
            await self.ws.close()

    async def send_audio(self, chunk: bytes) -> None:
        if not self.ws:
            raise RuntimeError("ASR session is not open")
        self.audio_bytes += len(chunk)
        self.audio_chunks += 1
        await self.ws.send(chunk)

    async def finish(self) -> str:
        if not self.ws:
            raise RuntimeError("ASR session is not open")
        self.finish_requested = True
        self.final_seen.clear()
        print(
            f"[turn {self.turn_id}] asr_finish chunks={self.audio_chunks} bytes={self.audio_bytes}",
            flush=True,
        )
        await self.ws.send(json_dumps({"is_speaking": False}))
        start_revision = self.final_revision
        timeout = self.finish_timeout(start_revision)
        try:
            await asyncio.wait_for(
                self.final_seen.wait(),
                timeout=timeout,
            )
        except asyncio.TimeoutError:
            self.last_text = self.best_text()
            if self.final_text and self.final_revision == start_revision:
                print(
                    f"[turn {self.turn_id}] asr_grace_done text={self.last_text!r}",
                    flush=True,
                )
                return self.last_text
            print(
                f"[turn {self.turn_id}] asr_timeout last_text={self.last_text!r}",
                flush=True,
            )
            await send_event(
                self.client_ws,
                "asr_timeout",
                text=self.last_text,
                timeout=timeout,
            )
        return self.best_text()

    def _begin_message(self) -> dict[str, Any]:
        return {
            "mode": self.settings.asr_mode,
            "chunk_size": self.settings.asr_chunk_size,
            "chunk_interval": self.settings.asr_chunk_interval,
            "wav_name": f"turn-{self.turn_id}",
            "wav_format": "pcm",
            "audio_fs": self.settings.asr_sample_rate,
            "is_speaking": True,
            "itn": self.settings.use_itn,
            "svs_itn": self.settings.svs_itn,
        }

    async def _receive_asr_messages(self) -> None:
        assert self.ws is not None
        async for message in self.ws:
            if isinstance(message, bytes):
                continue
            try:
                payload = json.loads(message)
            except json.JSONDecodeError:
                await send_event(self.client_ws, "asr_message", raw=message)
                continue

            text = str(payload.get("text") or "").strip()
            is_final = bool(payload.get("is_final"))
            mode = str(payload.get("mode") or "")
            is_offline_result = "offline" in mode
            is_completion_result = is_final or is_offline_result
            if text:
                self.last_text = self.update_text(text, is_completion_result)
                if self.finish_requested and is_completion_result:
                    print(f"[turn {self.turn_id}] asr_final text={self.final_text!r}", flush=True)
                    await send_event(self.client_ws, "asr_final", turn_id=self.turn_id, text=self.final_text)
                    self.final_event_text = self.final_text
                else:
                    await send_event(self.client_ws, "asr_partial", turn_id=self.turn_id, text=self.last_text)

            if self.finish_requested and (is_final or (text and is_offline_result)):
                self.final_seen.set()

    def update_text(self, text: str, final: bool) -> str:
        if final:
            self.final_text = merge_recognition_text(self.final_text, text)
            self.partial_text = ""
            self.final_revision += 1
            return self.final_text

        self.partial_text = merge_recognition_text(self.partial_text, text)
        return merge_recognition_text(self.final_text, self.partial_text)

    def best_text(self) -> str:
        return merge_recognition_text(self.final_text, self.partial_text or self.last_text).strip()

    def finish_timeout(self, start_revision: int) -> float:
        if self.final_text and self.final_revision == start_revision:
            return min(self.settings.asr_final_timeout, self.settings.asr_final_grace_timeout)
        return self.settings.asr_final_timeout


def load_env_file(path: str) -> None:
    env_path = Path(path)
    if not env_path.exists():
        return
    for raw_line in env_path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip().strip("'\"")
        if key and key not in os.environ:
            os.environ[key] = value


def env_str(name: str, default: str) -> str:
    return os.environ.get(name, default)


def env_int(name: str, default: int) -> int:
    return int(os.environ.get(name, str(default)))


def env_float(name: str, default: float) -> float:
    return float(os.environ.get(name, str(default)))


def env_bool(name: str, default: bool) -> bool:
    raw = os.environ.get(name)
    if raw is None:
        return default
    return raw.strip().lower() in {"1", "true", "yes", "y", "on"}


def parse_chunk_size(value: str) -> list[int]:
    parts = value.split("-")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("chunk size must look like 5-10-5")
    try:
        result = [int(part) for part in parts]
    except ValueError as exc:
        raise argparse.ArgumentTypeError("chunk size must contain integers") from exc
    if any(part <= 0 for part in result):
        raise argparse.ArgumentTypeError("chunk size values must be positive")
    return result


def parse_asr_proxy(value: str) -> str | bool | None:
    normalized = value.strip()
    if not normalized:
        return None
    lowered = normalized.lower()
    if lowered in {"0", "false", "no", "none", "off", "direct", "disabled"}:
        return None
    if lowered == "auto":
        return True
    return normalized


def parse_args(argv: list[str] | None = None) -> Settings:
    pre_parser = argparse.ArgumentParser(add_help=False)
    pre_parser.add_argument("--env-file", default=os.environ.get("VCHAT_ENV_FILE", ".env"))
    pre_args, _ = pre_parser.parse_known_args(argv)
    load_env_file(pre_args.env_file)

    parser = argparse.ArgumentParser(description="Streaming voice chat websocket server.")
    parser.add_argument("--env-file", default=pre_args.env_file)
    parser.add_argument("--host", default=env_str("VCHAT_HOST", "127.0.0.1"))
    parser.add_argument("--port", type=int, default=env_int("VCHAT_PORT", 8765))

    parser.add_argument("--asr-host", default=env_str("ASR_HOST", "127.0.0.1"))
    parser.add_argument("--asr-port", type=int, default=env_int("ASR_PORT", 10095))
    parser.add_argument("--asr-ssl", type=int, choices=(0, 1), default=int(env_bool("ASR_SSL", False)))
    parser.add_argument(
        "--asr-ssl-verify",
        type=int,
        choices=(0, 1),
        default=int(env_bool("ASR_SSL_VERIFY", False)),
    )
    parser.add_argument("--asr-proxy", default=env_str("ASR_PROXY", "none"))
    parser.add_argument("--asr-mode", default=env_str("ASR_MODE", "2pass"))
    parser.add_argument("--asr-sample-rate", type=int, default=env_int("ASR_SAMPLE_RATE", 16000))
    parser.add_argument(
        "--asr-chunk-size",
        type=parse_chunk_size,
        default=parse_chunk_size(env_str("ASR_CHUNK_SIZE", DEFAULT_ASR_CHUNK_SIZE)),
    )
    parser.add_argument("--asr-chunk-interval", type=int, default=env_int("ASR_CHUNK_INTERVAL", 10))
    parser.add_argument("--asr-final-timeout", type=float, default=env_float("ASR_FINAL_TIMEOUT", 20.0))
    parser.add_argument(
        "--asr-final-grace-timeout",
        type=float,
        default=env_float("ASR_FINAL_GRACE_TIMEOUT", ASR_FINAL_GRACE_TIMEOUT),
    )
    parser.add_argument("--use-itn", type=int, choices=(0, 1), default=int(env_bool("ASR_USE_ITN", True)))
    parser.add_argument("--svs-itn", type=int, choices=(0, 1), default=int(env_bool("ASR_SVS_ITN", True)))

    parser.add_argument("--llm-base-url", default=env_str("LLM_BASE_URL", "http://127.0.0.1:8001/v1"))
    parser.add_argument(
        "--llm-api-key",
        default=env_str("LLM_API_KEY", env_str("MIMO_API_KEY", env_str("OPENAI_API_KEY", "EMPTY"))),
    )
    parser.add_argument("--llm-model", default=env_str("LLM_MODEL", "Qwen3.5-4B"))
    parser.add_argument("--llm-system-prompt", default=env_str("LLM_SYSTEM_PROMPT", DEFAULT_SYSTEM_PROMPT))
    parser.add_argument("--llm-max-tokens", type=int, default=env_int("LLM_MAX_TOKENS", 1024))
    parser.add_argument("--llm-temperature", type=float, default=env_float("LLM_TEMPERATURE", 0.0))
    parser.add_argument("--llm-top-p", type=float, default=env_float("LLM_TOP_P", 0.95))
    parser.add_argument(
        "--llm-thinking-disabled",
        type=int,
        choices=(0, 1),
        default=int(env_bool("LLM_THINKING_DISABLED", True)),
    )
    parser.add_argument("--llm-timeout", type=float, default=env_float("LLM_TIMEOUT", 60.0))
    parser.add_argument("--llm-connect-timeout", type=float, default=env_float("LLM_CONNECT_TIMEOUT", 10.0))

    parser.add_argument("--tts-base-url", default=env_str("TTS_BASE_URL", "http://127.0.0.1:51010"))
    parser.add_argument("--tts-model", default=env_str("TTS_MODEL", "/workspace/model/Qwen3-TTS-12Hz-1.7B-Base"))
    parser.add_argument("--tts-voice", default=env_str("TTS_VOICE", "custom_voice_1"))
    parser.add_argument("--tts-sample-rate", type=int, default=env_int("TTS_SAMPLE_RATE", 48000))
    parser.add_argument("--tts-language", default=env_str("TTS_LANGUAGE", "Auto"))
    parser.add_argument("--tts-task-type", default=env_str("TTS_TASK_TYPE", "CustomVoice"))
    parser.add_argument("--tts-chunk-size", type=int, default=env_int("TTS_CHUNK_SIZE", 4096))
    parser.add_argument("--tts-min-chars", type=int, default=None, help=argparse.SUPPRESS)
    parser.add_argument("--tts-flush-chars", type=int, default=None, help=argparse.SUPPRESS)
    parser.add_argument("--tts-first-chunk-timeout", type=float, default=env_float("TTS_FIRST_CHUNK_TIMEOUT", 30.0))
    args = parser.parse_args(argv)

    if args.port <= 0:
        parser.error("--port must be positive")
    if args.asr_port <= 0:
        parser.error("--asr-port must be positive")
    if args.asr_sample_rate <= 0:
        parser.error("--asr-sample-rate must be positive")
    if args.asr_chunk_interval <= 0:
        parser.error("--asr-chunk-interval must be positive")
    if args.asr_final_timeout <= 0:
        parser.error("--asr-final-timeout must be positive")
    if args.asr_final_grace_timeout <= 0:
        parser.error("--asr-final-grace-timeout must be positive")
    if args.llm_max_tokens <= 0:
        parser.error("--llm-max-tokens must be positive")
    if args.llm_timeout <= 0:
        parser.error("--llm-timeout must be positive")
    if args.llm_connect_timeout <= 0:
        parser.error("--llm-connect-timeout must be positive")
    if args.tts_sample_rate <= 0:
        parser.error("--tts-sample-rate must be positive")
    if args.tts_chunk_size <= 0:
        parser.error("--tts-chunk-size must be positive")
    if args.tts_first_chunk_timeout <= 0:
        parser.error("--tts-first-chunk-timeout must be positive")

    return Settings(
        host=args.host,
        port=args.port,
        asr_host=args.asr_host,
        asr_port=args.asr_port,
        asr_ssl=bool(args.asr_ssl),
        asr_ssl_verify=bool(args.asr_ssl_verify),
        asr_proxy=parse_asr_proxy(args.asr_proxy),
        asr_mode=args.asr_mode,
        asr_sample_rate=args.asr_sample_rate,
        asr_chunk_size=args.asr_chunk_size,
        asr_chunk_interval=args.asr_chunk_interval,
        asr_final_timeout=args.asr_final_timeout,
        asr_final_grace_timeout=args.asr_final_grace_timeout,
        use_itn=bool(args.use_itn),
        svs_itn=bool(args.svs_itn),
        llm_base_url=args.llm_base_url,
        llm_api_key=args.llm_api_key,
        llm_model=args.llm_model,
        llm_system_prompt=args.llm_system_prompt,
        llm_max_tokens=args.llm_max_tokens,
        llm_temperature=args.llm_temperature,
        llm_top_p=args.llm_top_p,
        llm_thinking_disabled=bool(args.llm_thinking_disabled),
        llm_timeout=args.llm_timeout,
        llm_connect_timeout=args.llm_connect_timeout,
        tts_base_url=args.tts_base_url,
        tts_model=args.tts_model,
        tts_voice=args.tts_voice,
        tts_sample_rate=args.tts_sample_rate,
        tts_language=args.tts_language,
        tts_task_type=args.tts_task_type,
        tts_chunk_size=args.tts_chunk_size,
        tts_first_chunk_timeout=args.tts_first_chunk_timeout,
    )


def build_asr_ssl_context(settings: Settings) -> ssl.SSLContext | None:
    if not settings.asr_ssl:
        return None
    if settings.asr_ssl_verify:
        return ssl.create_default_context()
    context = ssl._create_unverified_context()
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    return context


def json_dumps(payload: dict[str, Any]) -> str:
    return json.dumps(payload, ensure_ascii=False, separators=(",", ":"))


async def send_event(ws: Any, event_type: str, **payload: Any) -> None:
    event_payload = {"type": event_type, **payload}
    message = json_dumps(event_payload)
    if len(message.encode("utf-8")) > CONTROL_FRAME_MAX_BYTES:
        for field in CONTROL_TEXT_FIELDS:
            value = event_payload.get(field)
            if not isinstance(value, str):
                continue
            event_payload[field] = control_text_preview(value)
            event_payload.setdefault(f"{field}_chars", len(value))
            message = json_dumps(event_payload)
            if len(message.encode("utf-8")) <= CONTROL_FRAME_MAX_BYTES:
                break
    if isinstance(ws, LockedSender):
        await ws.send(message)
    elif hasattr(ws, "send"):
        await ws.send(message)
    else:
        await ws(message)


def control_text_preview(text: str, max_chars: int = CONTROL_TEXT_PREVIEW_CHARS) -> str:
    normalized = " ".join(text.strip().split())
    if len(normalized) <= max_chars:
        return normalized
    return f"{normalized[:max_chars]}..."


def control_text_chunks(text: str, max_chars: int = CONTROL_TEXT_CHUNK_CHARS) -> Iterator[str]:
    if max_chars <= 0:
        raise ValueError("max_chars must be positive")
    for offset in range(0, len(text), max_chars):
        yield text[offset : offset + max_chars]


async def stream_llm_reply(settings: Settings, user_text: str) -> AsyncIterator[str]:
    timeout = httpx.Timeout(
        connect=settings.llm_connect_timeout,
        read=settings.llm_timeout,
        write=30.0,
        pool=10.0,
    )
    client = AsyncOpenAI(api_key=settings.llm_api_key, base_url=settings.llm_base_url, timeout=timeout)
    extra_body = {"thinking": {"type": "disabled"}} if settings.llm_thinking_disabled else None
    stream = await client.chat.completions.create(
        model=settings.llm_model,
        messages=[
            {"role": "system", "content": settings.llm_system_prompt},
            {"role": "user", "content": user_text},
        ],
        max_completion_tokens=settings.llm_max_tokens,
        temperature=settings.llm_temperature,
        top_p=settings.llm_top_p,
        stream=True,
        stop=None,
        frequency_penalty=0,
        presence_penalty=0,
        extra_body=extra_body,
    )

    async for chunk in stream:
        if not chunk.choices:
            continue
        delta = chunk.choices[0].delta.content
        if delta:
            yield delta


async def stream_tts_audio(settings: Settings, text: str) -> AsyncIterator[bytes]:
    payload = {
        "model": settings.tts_model,
        "voice": settings.tts_voice,
        "input": text,
        "response_format": "pcm",
        "stream": True,
        "stream_format": "audio",
        "sample_rate": settings.tts_sample_rate,
        "language": settings.tts_language,
        "task_type": settings.tts_task_type,
    }
    timeout = httpx.Timeout(connect=10.0, read=None, write=30.0, pool=None)
    async with httpx.AsyncClient(timeout=timeout) as client:
        try:
            async with client.stream("POST", settings.tts_url, json=payload) as response:
                response.raise_for_status()
                async for chunk in response.aiter_bytes(chunk_size=settings.tts_chunk_size):
                    if chunk:
                        yield chunk
        except httpx.HTTPStatusError as exc:
            if exc.response.status_code not in {400, 422}:
                raise
            fallback_payload = dict(payload)
            fallback_payload.pop("sample_rate", None)
            print(
                "tts warning: endpoint rejected sample_rate, retrying without explicit rate; "
                f"client playback still assumes {settings.tts_sample_rate} Hz",
                flush=True,
            )
            async with client.stream("POST", settings.tts_url, json=fallback_payload) as response:
                response.raise_for_status()
                async for chunk in response.aiter_bytes(chunk_size=settings.tts_chunk_size):
                    if chunk:
                        yield chunk


def pcm_fade_edges(data: bytes, sample_rate: int, fade_in: bool, fade_out: bool) -> bytes:
    sample_count = len(data) // PCM_SAMPLE_WIDTH
    if sample_count == 0 or sample_rate <= 0 or (not fade_in and not fade_out):
        return data

    fade_samples = max(1, sample_rate * TTS_EDGE_FADE_MS // 1000)
    fade_samples = min(fade_samples, sample_count)
    output = bytearray(data)

    for i in range(fade_samples):
        if fade_in:
            gain = i / fade_samples
            offset = i * PCM_SAMPLE_WIDTH
            sample = int.from_bytes(output[offset:offset + PCM_SAMPLE_WIDTH], "little", signed=True)
            output[offset:offset + PCM_SAMPLE_WIDTH] = int(sample * gain).to_bytes(
                PCM_SAMPLE_WIDTH,
                "little",
                signed=True,
            )
        if fade_out:
            gain = (fade_samples - i - 1) / fade_samples
            offset = (sample_count - fade_samples + i) * PCM_SAMPLE_WIDTH
            sample = int.from_bytes(output[offset:offset + PCM_SAMPLE_WIDTH], "little", signed=True)
            output[offset:offset + PCM_SAMPLE_WIDTH] = int(sample * gain).to_bytes(
                PCM_SAMPLE_WIDTH,
                "little",
                signed=True,
            )

    return bytes(output)


async def send_tts_pcm_stream(
    settings: Settings,
    ws: Any,
    first_chunk: bytes,
    iterator: AsyncIterator[bytes],
) -> int:
    fade_bytes = max(PCM_SAMPLE_WIDTH, settings.tts_sample_rate * TTS_EDGE_FADE_MS // 1000 * PCM_SAMPLE_WIDTH)
    start_buffer_bytes = max(fade_bytes, settings.tts_sample_rate * TTS_START_BUFFER_MS // 1000 * PCM_SAMPLE_WIDTH)
    tail = b""
    pending = b""
    first_audio = True
    sent_bytes = 0

    async def send_audio(data: bytes, fade_out: bool = False) -> None:
        nonlocal first_audio
        nonlocal sent_bytes
        if not data:
            return
        data = pcm_fade_edges(data, settings.tts_sample_rate, first_audio, fade_out)
        first_audio = False
        sent_bytes += len(data)
        await ws.send(data)

    async def accept_chunk(chunk: bytes) -> None:
        nonlocal pending
        nonlocal tail
        if not chunk:
            return
        data = pending + chunk
        if len(data) % PCM_SAMPLE_WIDTH:
            pending = data[-1:]
            data = data[:-1]
        else:
            pending = b""
        if not data:
            return

        combined = tail + data
        keep_bytes = start_buffer_bytes + fade_bytes if first_audio else fade_bytes
        if len(combined) <= keep_bytes:
            tail = combined
            return

        await send_audio(combined[:-fade_bytes])
        tail = combined[-fade_bytes:]

    await accept_chunk(first_chunk)
    async for chunk in iterator:
        await accept_chunk(chunk)

    if pending:
        print("tts warning: dropping odd trailing pcm byte", flush=True)
    await send_audio(tail, fade_out=True)
    return sent_bytes


async def answer_turn(settings: Settings, ws: Any, user_text: str, turn_id: int) -> None:
    await send_event(ws, "llm_start", turn_id=turn_id)
    print(f"[turn {turn_id}] llm_start text={user_text!r}", flush=True)
    full_reply: list[str] = []

    try:
        async for delta in stream_llm_reply(settings, user_text):
            full_reply.append(delta)
            for part in control_text_chunks(delta):
                await send_event(ws, "llm_delta", text=part, turn_id=turn_id)

        reply = "".join(full_reply).strip()
        await send_event(
            ws,
            "llm_done",
            text=control_text_preview(reply),
            text_chars=len(reply),
            turn_id=turn_id,
        )
        print(f"[turn {turn_id}] llm_done chars={len(reply)}", flush=True)

        if reply:
            tts_started_at = time.perf_counter()
            tts_bytes = await speak_reply_for_turn(settings, ws, reply, turn_id)
            elapsed = round(time.perf_counter() - tts_started_at, 3)
            await send_event(
                ws,
                "tts_done",
                turn_id=turn_id,
                audio_bytes=tts_bytes,
                elapsed=elapsed,
            )
            print(
                f"[turn {turn_id}] tts_done bytes={tts_bytes} elapsed={elapsed}s",
                flush=True,
            )
    except asyncio.CancelledError:
        raise


async def speak_reply_for_turn(settings: Settings, ws: Any, text: str, turn_id: int) -> int:
    normalized = text.strip()
    if not normalized:
        return 0
    await send_event(
        ws,
        "tts_start",
        text=control_text_preview(normalized),
        text_chars=len(normalized),
        turn_id=turn_id,
        sample_rate=settings.tts_sample_rate,
        response_format="pcm",
    )
    print(f"[turn {turn_id}] tts_start chars={len(normalized)}", flush=True)
    started_at = time.perf_counter()
    iterator = stream_tts_audio(settings, normalized).__aiter__()
    try:
        first_chunk = await asyncio.wait_for(iterator.__anext__(), timeout=settings.tts_first_chunk_timeout)
    except StopAsyncIteration as exc:
        raise RuntimeError("TTS returned an empty audio stream") from exc
    audio_bytes = await send_tts_pcm_stream(settings, ws, first_chunk, iterator)
    elapsed = round(time.perf_counter() - started_at, 3)
    print(f"[turn {turn_id}] tts_stream_done bytes={audio_bytes} elapsed={elapsed}s", flush=True)
    return audio_bytes


async def handle_client(ws: Any, settings: Settings) -> None:
    sender = LockedSender(ws)
    await send_event(
        sender,
        "ready",
        asr_sample_rate=settings.asr_sample_rate,
        tts_sample_rate=settings.tts_sample_rate,
        channels=1,
        sample_width=2,
    )

    asr_session: AsrSession | None = None
    response_task: asyncio.Task[None] | None = None
    response_turn_id: int | None = None
    cancel_waiters: set[asyncio.Task[None]] = set()
    turn_id = 0

    async def wait_cancelled_response(
        task: asyncio.Task[None],
        cancelled_turn_id: int | None,
        reason: str,
    ) -> None:
        was_cancelled = False
        try:
            await task
        except asyncio.CancelledError:
            was_cancelled = True
        except Exception as exc:
            print(
                f"cancelled response turn={cancelled_turn_id} "
                f"ended with {type(exc).__name__}: {exc}",
                flush=True,
            )
        if was_cancelled and cancelled_turn_id is not None:
            with contextlib.suppress(Exception):
                await send_event(sender, "response_cancelled", turn_id=cancelled_turn_id)
        print(f"cancelled response turn={cancelled_turn_id} reason={reason}", flush=True)

    def request_cancel_response(reason: str) -> asyncio.Task[None] | None:
        nonlocal response_task
        nonlocal response_turn_id

        task = response_task
        cancelled_turn_id = response_turn_id
        if not task:
            response_task = None
            response_turn_id = None
            return None

        response_task = None
        response_turn_id = None
        if not task.done():
            task.cancel()

        waiter = asyncio.create_task(
            wait_cancelled_response(task, cancelled_turn_id, reason),
            name=f"cancel-response-turn-{cancelled_turn_id}",
        )
        cancel_waiters.add(waiter)
        waiter.add_done_callback(cancel_waiters.discard)
        return waiter

    async def cancel_response(reason: str) -> None:
        waiter = request_cancel_response(reason)
        if waiter is not None:
            await waiter

    def start_response_task(user_text: str, answered_turn_id: int) -> None:
        nonlocal response_task
        nonlocal response_turn_id

        async def run() -> None:
            nonlocal response_task
            nonlocal response_turn_id
            try:
                await answer_turn(settings, sender, user_text, answered_turn_id)
                await send_event(
                    sender,
                    "turn_done",
                    turn_id=answered_turn_id,
                    text=control_text_preview(user_text),
                    text_chars=len(user_text),
                )
            except asyncio.CancelledError:
                print(f"[turn {answered_turn_id}] response_cancelled", flush=True)
                raise
            except Exception as exc:
                print(f"[turn {answered_turn_id}] response_error {type(exc).__name__}: {exc}", flush=True)
                await send_event(sender, "error", turn_id=answered_turn_id, message=f"response failed: {exc}")
            finally:
                if asyncio.current_task() is response_task:
                    response_task = None
                    response_turn_id = None

        response_turn_id = answered_turn_id
        response_task = asyncio.create_task(run(), name=f"answer-turn-{answered_turn_id}")

    try:
        async for message in ws:
            if isinstance(message, bytes):
                if not asr_session:
                    await send_event(sender, "error", message="binary audio received before audio_start")
                    continue
                await asr_session.send_audio(message)
                continue

            try:
                payload = json.loads(message)
            except json.JSONDecodeError:
                await send_event(sender, "error", message="invalid json control message")
                continue

            message_type = payload.get("type")
            if message_type == "audio_start":
                request_cancel_response("new audio_start")
                if asr_session:
                    await send_event(sender, "error", message="audio_start received while a turn is active")
                    continue
                client_turn_id = int(payload.get("turn_id") or 0)
                if client_turn_id > turn_id:
                    turn_id = client_turn_id
                else:
                    turn_id += 1
                sample_rate = int(payload.get("sample_rate") or settings.asr_sample_rate)
                if sample_rate != settings.asr_sample_rate:
                    await send_event(
                        sender,
                        "warning",
                        message=(
                            f"client sample_rate={sample_rate}, "
                            f"ASR expects {settings.asr_sample_rate}"
                        ),
                    )
                asr_session = AsrSession(settings, turn_id, sender)
                try:
                    await asr_session.__aenter__()
                except Exception as exc:
                    asr_session = None
                    message = f"ASR connection failed: {settings.asr_uri} ({exc})"
                    print(message, flush=True)
                    await send_event(sender, "error", message=message)
                    continue
                await send_event(sender, "turn_started", turn_id=turn_id)
            elif message_type == "audio_end":
                if not asr_session:
                    await send_event(sender, "error", message="audio_end received without audio_start")
                    continue
                current_session = asr_session
                asr_session = None
                current_turn_id = turn_id
                user_text = await current_session.finish()
                await current_session.__aexit__(None, None, None)
                print(f"[turn {current_turn_id}] user_text={user_text!r}", flush=True)
                if not user_text:
                    await send_event(sender, "turn_done", turn_id=current_turn_id, text="", reply="")
                    continue
                if user_text != current_session.final_event_text:
                    await send_event(sender, "asr_final", turn_id=current_turn_id, text=user_text)
                await cancel_response("new answer")
                start_response_task(user_text, current_turn_id)
            elif message_type == "cancel_response":
                request_cancel_response(str(payload.get("reason") or "client request"))
            elif message_type == "ping":
                await send_event(sender, "pong")
            else:
                await send_event(sender, "error", message=f"unsupported message type: {message_type}")
    finally:
        await cancel_response("client disconnect")
        if cancel_waiters:
            await asyncio.gather(*cancel_waiters, return_exceptions=True)
        if asr_session:
            await asr_session.__aexit__(None, None, None)


async def async_main(argv: list[str] | None = None) -> None:
    settings = parse_args(argv)
    print(
        f"vchat listening on ws://{settings.host}:{settings.port} "
        f"(ASR {settings.asr_uri}, ASR proxy {settings.asr_proxy_label}, "
        f"LLM {settings.llm_base_url}, TTS {settings.tts_url})",
        flush=True,
    )

    async def handler(ws: Any) -> None:
        await handle_client(ws, settings)

    async with websockets.serve(handler, settings.host, settings.port, max_size=None):
        await asyncio.Future()


def main() -> None:
    try:
        asyncio.run(async_main())
    except KeyboardInterrupt:
        print("interrupted")


if __name__ == "__main__":
    main()
