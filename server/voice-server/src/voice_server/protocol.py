from __future__ import annotations

from enum import StrEnum
from typing import Any

from pydantic import BaseModel, Field


class ClientEventType(StrEnum):
    START = "start"
    AUDIO = "audio"
    COMMIT = "commit"
    STOP = "stop"
    PING = "ping"
    TEXT = "text"


class ServerEventType(StrEnum):
    READY = "ready"
    STARTED = "started"
    TRANSCRIPT = "transcript"
    TTS_START = "tts_start"
    TTS_END = "tts_end"
    AUDIO = "audio"
    COMMITTED = "committed"
    STOPPED = "stopped"
    PONG = "pong"
    ERROR = "error"


class AudioFormat(BaseModel):
    encoding: str = "pcm_s16le"
    sample_rate: int
    channels: int
    container: str = "raw"


class StartEvent(BaseModel):
    type: ClientEventType = ClientEventType.START
    audio_format: AudioFormat
    asr: dict[str, Any] = Field(default_factory=dict)
    tts: dict[str, Any] = Field(default_factory=dict)
    segment_seconds: float | None = None


class TextEvent(BaseModel):
    type: ClientEventType = ClientEventType.TEXT
    text: str
    tts: dict[str, Any] = Field(default_factory=dict)


def event(event_type: ServerEventType | str, **payload: Any) -> dict[str, Any]:
    return {"type": str(event_type), **payload}
