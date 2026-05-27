from __future__ import annotations

import asyncio
import base64
import json
import logging
from typing import Any

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from pydantic import ValidationError
from starlette.websockets import WebSocketState

from voice_server.audio import PcmFormat, maybe_wav_to_pcm, pcm_duration_seconds
from voice_server.config import Settings, get_settings
from voice_server.protocol import (
    ClientEventType,
    ServerEventType,
    StartEvent,
    TextEvent,
    event,
)
from voice_server.upstream import QwenAsrClient, UpstreamError, VoxCpmTtsClient

logger = logging.getLogger(__name__)

app = FastAPI(title="Voice Bridge Server", version="0.1.0")


@app.get("/health")
async def health() -> dict[str, str]:
    return {"status": "ok"}


class VoiceConnection:
    def __init__(self, websocket: WebSocket, settings: Settings) -> None:
        self.websocket = websocket
        self.settings = settings
        self.asr = QwenAsrClient(settings)
        self.tts = VoxCpmTtsClient(settings)

        self.audio_format = PcmFormat(
            sample_rate=settings.asr_sample_rate,
            channels=settings.asr_channels,
        )
        self.input_container = "raw"
        self.segment_seconds = settings.asr_segment_seconds
        self.asr_overrides: dict[str, Any] = {}
        self.tts_overrides: dict[str, Any] = {}

        self.audio_buffer = bytearray()
        self.segment_queue: asyncio.Queue[bytes | None] = asyncio.Queue()
        self.send_lock = asyncio.Lock()
        self.worker_task: asyncio.Task[None] | None = None
        self.started = False
        self.closed = False

    async def run(self) -> None:
        self.worker_task = asyncio.create_task(self._process_segments())
        await self._send_json(
            event(
                ServerEventType.READY,
                protocol={
                    "control": "json text frames",
                    "input_audio": "binary PCM frames or JSON audio base64",
                    "output_audio": "binary audio frames",
                },
                defaults={
                    "input_sample_rate": self.audio_format.sample_rate,
                    "tts_sample_rate": self.settings.tts_sample_rate,
                    "tts_response_format": self.settings.tts_response_format,
                },
            )
        )

        try:
            while True:
                message = await self.websocket.receive()
                if message.get("type") == "websocket.disconnect":
                    break
                if message.get("bytes") is not None:
                    await self._handle_audio_bytes(message["bytes"])
                elif message.get("text") is not None:
                    await self._handle_text_frame(message["text"])
        except WebSocketDisconnect:
            pass
        finally:
            await self.close()

    async def close(self) -> None:
        if self.closed:
            return
        self.closed = True
        await self._flush_audio_buffer()
        await self.segment_queue.put(None)
        if self.worker_task:
            try:
                await asyncio.wait_for(self.worker_task, timeout=2)
            except TimeoutError:
                self.worker_task.cancel()
        if self.websocket.application_state != WebSocketState.DISCONNECTED:
            try:
                await self.websocket.close()
            except RuntimeError:
                pass

    async def _handle_text_frame(self, raw_text: str) -> None:
        try:
            payload = json.loads(raw_text)
        except json.JSONDecodeError:
            await self._send_error("invalid_json", "Control frames must be JSON objects.")
            return

        event_type = payload.get("type")
        if event_type == ClientEventType.START:
            await self._start(payload)
        elif event_type == ClientEventType.AUDIO:
            audio_b64 = payload.get("data") or payload.get("audio")
            if not isinstance(audio_b64, str):
                await self._send_error("invalid_audio", "JSON audio events require base64 data.")
                return
            try:
                await self._handle_audio_bytes(base64.b64decode(audio_b64))
            except ValueError:
                await self._send_error("invalid_audio", "Audio data is not valid base64.")
        elif event_type == ClientEventType.COMMIT:
            await self._flush_audio_buffer()
            await self._send_json(
                event(ServerEventType.COMMITTED, queued=self.segment_queue.qsize())
            )
        elif event_type == ClientEventType.STOP:
            await self._flush_audio_buffer()
            await self.segment_queue.join()
            await self._send_json(event(ServerEventType.STOPPED))
            await self.websocket.close()
        elif event_type == ClientEventType.PING:
            await self._send_json(event(ServerEventType.PONG))
        elif event_type == ClientEventType.TEXT:
            try:
                text_event = TextEvent.model_validate(payload)
            except ValidationError as exc:
                await self._send_error("invalid_text_event", exc.errors())
                return
            await self._synthesize_and_send(text_event.text, text_event.tts)
        else:
            await self._send_error("unknown_event", f"Unknown client event type: {event_type}")

    async def _start(self, payload: dict[str, Any]) -> None:
        payload = self._with_default_audio_format(payload)
        try:
            start = StartEvent.model_validate(payload)
        except ValidationError as exc:
            await self._send_error("invalid_start", exc.errors())
            return

        sample_width = 2
        encoding = start.audio_format.encoding.lower()
        if encoding not in {"pcm_s16le", "pcm16", "s16le"}:
            await self._send_error(
                "unsupported_audio_format",
                "Only 16-bit little-endian PCM input is supported by the bridge.",
            )
            return

        self.audio_format = PcmFormat(
            sample_rate=start.audio_format.sample_rate,
            channels=start.audio_format.channels,
            sample_width=sample_width,
        )
        self.input_container = start.audio_format.container.lower()
        self.segment_seconds = start.segment_seconds or self.settings.asr_segment_seconds
        self.asr_overrides = start.asr
        self.tts_overrides = start.tts
        self.started = True

        await self._send_json(
            event(
                ServerEventType.STARTED,
                audio_format={
                    "encoding": encoding,
                    "sample_rate": self.audio_format.sample_rate,
                    "channels": self.audio_format.channels,
                    "container": self.input_container,
                },
                segment_seconds=self.segment_seconds,
            )
        )

    def _with_default_audio_format(self, payload: dict[str, Any]) -> dict[str, Any]:
        if "audio_format" in payload and not isinstance(payload.get("audio_format"), dict):
            return payload

        audio_format = {
            "encoding": "pcm_s16le",
            "sample_rate": self.settings.asr_sample_rate,
            "channels": self.settings.asr_channels,
            "container": "raw",
        }
        audio_format.update(payload.get("audio_format") or {})
        return {**payload, "audio_format": audio_format}

    async def _handle_audio_bytes(self, audio: bytes) -> None:
        if not self.started:
            await self._start({"type": "start"})

        if not audio:
            return
        if len(self.audio_buffer) + len(audio) > self.settings.max_audio_buffer_bytes:
            await self._send_error("audio_buffer_overflow", "Input audio buffer is too large.")
            self.audio_buffer.clear()
            return

        self.audio_buffer.extend(audio)
        if self.input_container == "wav":
            return

        segment_size = self._segment_size_bytes()
        while len(self.audio_buffer) >= segment_size:
            segment = bytes(self.audio_buffer[:segment_size])
            del self.audio_buffer[:segment_size]
            await self.segment_queue.put(segment)

    async def _flush_audio_buffer(self) -> None:
        if not self.audio_buffer:
            return

        audio = bytes(self.audio_buffer)
        self.audio_buffer.clear()

        if self.input_container == "wav":
            try:
                audio, detected_format = maybe_wav_to_pcm(audio)
            except Exception as exc:
                await self._send_error("invalid_wav", f"Could not parse WAV input: {exc}")
                return
            if detected_format:
                self.audio_format = detected_format

        if audio:
            await self.segment_queue.put(audio)

    async def _process_segments(self) -> None:
        while True:
            segment = await self.segment_queue.get()
            try:
                if segment is None:
                    return
                await self._transcribe_then_synthesize(segment)
            finally:
                self.segment_queue.task_done()

    async def _transcribe_then_synthesize(self, audio: bytes) -> None:
        duration = pcm_duration_seconds(audio, self.audio_format)
        try:
            text = await self.asr.transcribe_pcm(audio, self.audio_format, self.asr_overrides)
        except UpstreamError as exc:
            await self._send_error("asr_error", str(exc))
            return

        await self._send_json(
            event(
                ServerEventType.TRANSCRIPT,
                text=text,
                duration_seconds=round(duration, 3),
                final=True,
            )
        )

        if text:
            await self._synthesize_and_send(text, self.tts_overrides)

    async def _synthesize_and_send(
        self,
        text: str,
        overrides: dict[str, Any] | None = None,
    ) -> None:
        overrides = overrides or {}
        response_format = overrides.get("response_format", self.settings.tts_response_format)
        await self._send_json(
            event(
                ServerEventType.TTS_START,
                text=text,
                response_format=response_format,
                sample_rate=self.settings.tts_sample_rate,
                voice=overrides.get("voice", self.settings.tts_voice),
            )
        )
        total_bytes = 0
        try:
            async for chunk in self.tts.synthesize_stream(text, overrides):
                total_bytes += len(chunk)
                await self._send_bytes(chunk)
        except UpstreamError as exc:
            await self._send_error("tts_error", str(exc))
            return

        await self._send_json(event(ServerEventType.TTS_END, bytes=total_bytes))

    def _segment_size_bytes(self) -> int:
        bytes_per_second = (
            self.audio_format.sample_rate
            * self.audio_format.channels
            * self.audio_format.sample_width
        )
        segment_size = max(int(bytes_per_second * self.segment_seconds), bytes_per_second // 2)
        frame_size = self.audio_format.channels * self.audio_format.sample_width
        return max(frame_size, segment_size - (segment_size % frame_size))

    async def _send_json(self, payload: dict[str, Any]) -> None:
        async with self.send_lock:
            if self.websocket.application_state == WebSocketState.CONNECTED:
                await self.websocket.send_json(payload)

    async def _send_bytes(self, payload: bytes) -> None:
        async with self.send_lock:
            if self.websocket.application_state == WebSocketState.CONNECTED:
                await self.websocket.send_bytes(payload)

    async def _send_error(self, code: str, message: Any) -> None:
        logger.warning("%s: %s", code, message)
        await self._send_json(event(ServerEventType.ERROR, code=code, message=message))


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket) -> None:
    await websocket.accept()
    connection = VoiceConnection(websocket, get_settings())
    await connection.run()
