from __future__ import annotations

import asyncio
import base64
import json
import logging
from collections.abc import AsyncIterator
from typing import Any

import httpx
import websockets
from websockets.asyncio.client import connect as ws_connect

from voice_server.audio import PcmFormat, pcm_to_wav_bytes
from voice_server.config import Settings

logger = logging.getLogger(__name__)


class UpstreamError(RuntimeError):
    pass


def _auth_headers(api_key: str | None) -> dict[str, str]:
    if not api_key:
        return {}
    return {"Authorization": f"Bearer {api_key}"}


def _extract_text(payload: Any) -> str:
    if isinstance(payload, str):
        return payload.strip()
    if isinstance(payload, dict):
        for key in ("text", "transcript", "content"):
            value = payload.get(key)
            if isinstance(value, str) and value.strip():
                return value.strip()

        choices = payload.get("choices")
        if isinstance(choices, list):
            parts: list[str] = []
            for choice in choices:
                if not isinstance(choice, dict):
                    continue
                delta = choice.get("delta")
                if isinstance(delta, dict):
                    content = delta.get("content")
                    if isinstance(content, str):
                        parts.append(content)
                text = choice.get("text")
                if isinstance(text, str):
                    parts.append(text)
            joined = "".join(parts).strip()
            if joined:
                return joined

    return ""


class QwenAsrClient:
    def __init__(self, settings: Settings) -> None:
        self.settings = settings

    async def transcribe_pcm(
        self,
        audio: bytes,
        fmt: PcmFormat,
        overrides: dict[str, Any] | None = None,
    ) -> str:
        overrides = overrides or {}
        wav_bytes = pcm_to_wav_bytes(audio, fmt)
        data: dict[str, Any] = {
            "response_format": overrides.get("response_format", self.settings.asr_response_format),
            "stream": overrides.get("stream", self.settings.asr_stream),
            "language": overrides.get("language", self.settings.asr_language),
        }
        data["model"] = overrides.get("model") or self.settings.asr_model
        for key, value in overrides.items():
            if key not in {"response_format", "stream", "language", "model"} and value is not None:
                data[key] = value

        files = {"file": ("audio.wav", wav_bytes, "audio/wav")}
        timeout = httpx.Timeout(
            self.settings.request_timeout_seconds,
            connect=self.settings.connect_timeout_seconds,
        )

        async with httpx.AsyncClient(timeout=timeout) as client:
            try:
                response = await client.post(
                    self.settings.asr_transcriptions_url,
                    headers=_auth_headers(self.settings.asr_api_key),
                    data={
                        key: str(value).lower() if isinstance(value, bool) else value
                        for key, value in data.items()
                        if value is not None
                    },
                    files=files,
                )
            except httpx.HTTPError as exc:
                raise UpstreamError(f"ASR request failed: {exc}") from exc

        if response.status_code >= 400:
            raise UpstreamError(
                f"ASR request failed with HTTP {response.status_code}: {response.text[:500]}"
            )

        content_type = response.headers.get("content-type", "")
        if "application/json" in content_type:
            return _extract_text(response.json())
        return response.text.strip()

    async def transcribe_pcm_ws(
        self,
        audio: bytes,
        fmt: PcmFormat,
        overrides: dict[str, Any] | None = None,
    ) -> str:
        overrides = overrides or {}
        payload = {
            "type": "input_audio_buffer.append",
            "audio": base64.b64encode(pcm_to_wav_bytes(audio, fmt)).decode("ascii"),
        }
        headers = _auth_headers(self.settings.asr_api_key)

        try:
            async with ws_connect(
                self.settings.asr_ws_url,
                additional_headers=headers,
                open_timeout=self.settings.connect_timeout_seconds,
                ping_interval=20,
                ping_timeout=20,
            ) as websocket:
                session: dict[str, Any] = {
                    "modalities": ["text"],
                    "input_audio_format": "wav",
                    "model": overrides.get("model") or self.settings.asr_model,
                }
                if self.settings.asr_language:
                    session["language"] = self.settings.asr_language
                await websocket.send(json.dumps({"type": "session.update", "session": session}))
                await websocket.send(json.dumps(payload))
                await websocket.send(json.dumps({"type": "input_audio_buffer.commit"}))

                transcript_parts: list[str] = []
                async for raw_message in websocket:
                    if isinstance(raw_message, bytes):
                        continue
                    message = json.loads(raw_message)
                    message_type = message.get("type")
                    if message_type == "error":
                        raise UpstreamError(f"ASR websocket error: {message}")
                    text = _extract_text(message)
                    if text:
                        transcript_parts.append(text)
                    if message_type in {
                        "response.done",
                        "conversation.item.input_audio_transcription.completed",
                        "transcription.completed",
                    }:
                        break
                return "".join(transcript_parts).strip()
        except (OSError, websockets.WebSocketException, asyncio.TimeoutError) as exc:
            raise UpstreamError(f"ASR websocket request failed: {exc}") from exc


class VoxCpmTtsClient:
    def __init__(self, settings: Settings) -> None:
        self.settings = settings

    async def synthesize_stream(
        self,
        text: str,
        overrides: dict[str, Any] | None = None,
    ) -> AsyncIterator[bytes]:
        overrides = overrides or {}
        if self.settings.tts_transport == "ws":
            async for chunk in self._synthesize_ws(text, overrides):
                yield chunk
            return

        async for chunk in self._synthesize_http(text, overrides):
            yield chunk

    async def _synthesize_http(
        self,
        text: str,
        overrides: dict[str, Any],
    ) -> AsyncIterator[bytes]:
        response_format = overrides.get("response_format", self.settings.tts_response_format)
        stream = response_format == "pcm"
        body: dict[str, Any] = {
            "model": overrides.get("model", self.settings.tts_model),
            "input": text,
            "voice": overrides.get("voice", self.settings.tts_voice),
            "response_format": response_format,
            "speed": overrides.get("speed", self.settings.tts_speed),
            "stream": stream,
            "stream_format": "audio",
        }

        optional = {
            "language": overrides.get("language", self.settings.tts_language),
            "task_type": overrides.get("task_type", self.settings.tts_task_type),
            "ref_text": overrides.get("ref_text", self.settings.tts_ref_text),
            "ref_audio": overrides.get("ref_audio", self.settings.tts_ref_audio),
            "instructions": overrides.get("instructions"),
            "max_new_tokens": overrides.get("max_new_tokens"),
            "initial_codec_chunk_frames": overrides.get("initial_codec_chunk_frames"),
        }
        body.update({key: value for key, value in optional.items() if value is not None})

        timeout = httpx.Timeout(
            self.settings.request_timeout_seconds,
            connect=self.settings.connect_timeout_seconds,
        )
        async with httpx.AsyncClient(timeout=timeout) as client:
            try:
                async with client.stream(
                    "POST",
                    self.settings.tts_speech_url,
                    headers={
                        "Content-Type": "application/json",
                        **_auth_headers(self.settings.tts_api_key),
                    },
                    json=body,
                ) as response:
                    if response.status_code >= 400:
                        body_text = await response.aread()
                        raise UpstreamError(
                            "TTS request failed with HTTP "
                            f"{response.status_code}: {body_text[:500].decode(errors='replace')}"
                        )
                    async for chunk in response.aiter_bytes(self.settings.tts_stream_chunk_size):
                        if chunk:
                            yield chunk
            except httpx.HTTPError as exc:
                raise UpstreamError(f"TTS request failed: {exc}") from exc

    async def _synthesize_ws(
        self,
        text: str,
        overrides: dict[str, Any],
    ) -> AsyncIterator[bytes]:
        headers = _auth_headers(self.settings.tts_api_key)
        try:
            async with ws_connect(
                self.settings.tts_ws_url,
                additional_headers=headers,
                open_timeout=self.settings.connect_timeout_seconds,
                ping_interval=20,
                ping_timeout=20,
            ) as websocket:
                await websocket.send(
                    json.dumps(
                        {
                            "type": "session.update",
                            "model": overrides.get("model", self.settings.tts_model),
                            "voice": overrides.get("voice", self.settings.tts_voice),
                            "response_format": overrides.get(
                                "response_format", self.settings.tts_response_format
                            ),
                            "stream": True,
                            "input": text,
                        },
                        ensure_ascii=False,
                    )
                )
                async for raw_message in websocket:
                    if isinstance(raw_message, bytes):
                        yield raw_message
                        continue
                    message = json.loads(raw_message)
                    if message.get("type") == "error":
                        raise UpstreamError(f"TTS websocket error: {message}")
                    audio_b64 = message.get("audio") or message.get("delta")
                    if isinstance(audio_b64, str):
                        try:
                            yield base64.b64decode(audio_b64)
                        except ValueError:
                            logger.debug("Ignored non-base64 TTS audio payload: %s", audio_b64[:40])
                    if message.get("type") in {"response.done", "audio.done", "tts.done"}:
                        break
        except (OSError, websockets.WebSocketException, asyncio.TimeoutError) as exc:
            raise UpstreamError(f"TTS websocket request failed: {exc}") from exc
