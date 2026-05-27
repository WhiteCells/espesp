from __future__ import annotations

from functools import lru_cache
from typing import Literal, TypeVar

from pydantic import Field, computed_field
from pydantic_settings import BaseSettings, SettingsConfigDict

SettingsT = TypeVar("SettingsT", bound=BaseSettings)


def _load_settings(settings_type: type[SettingsT]) -> SettingsT:
    return settings_type()


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=".env",
        env_file_encoding="utf-8",
        extra="ignore",
    )

    host: str = Field(default="0.0.0.0", validation_alias="VOICE_SERVER_HOST")
    port: int = Field(default=8765, validation_alias="VOICE_SERVER_PORT")
    log_level: str = Field(default="info", validation_alias="VOICE_SERVER_LOG_LEVEL")

    asr_ws_url: str = Field(
        validation_alias="ASR_WS_URL",
    )
    asr_http_base_url: str = Field(
        validation_alias="ASR_HTTP_BASE_URL",
    )
    asr_api_key: str | None = Field(default=None, validation_alias="ASR_API_KEY")
    asr_model: str = Field(validation_alias="ASR_MODEL", min_length=1)
    asr_language: str = Field(validation_alias="ASR_LANGUAGE")
    asr_response_format: Literal["json", "text", "srt", "verbose_json", "vtt"] = Field(
        validation_alias="ASR_RESPONSE_FORMAT",
    )
    asr_stream: bool = Field(default=False, validation_alias="ASR_STREAM")
    asr_sample_rate: int = Field(
        validation_alias="ASR_SAMPLE_RATE",
    )
    asr_channels: int = Field(
        validation_alias="ASR_CHANNELS",
    )
    asr_segment_seconds: float = Field(validation_alias="ASR_SEGMENT_SECONDS")

    tts_ws_url: str = Field(
        validation_alias="TTS_WS_URL",
    )
    tts_http_base_url: str = Field(
        validation_alias="TTS_HTTP_BASE_URL",
    )
    tts_api_key: str | None = Field(default=None, validation_alias="TTS_API_KEY")
    tts_transport: Literal["http", "ws"] = Field(validation_alias="TTS_TRANSPORT")
    tts_model: str = Field(validation_alias="TTS_MODEL")
    tts_voice: str = Field(validation_alias="TTS_VOICE")
    tts_language: str = Field(validation_alias="TTS_LANGUAGE")
    tts_response_format: Literal["pcm", "wav", "flac", "mp3", "aac", "opus"] = Field(
        validation_alias="TTS_RESPONSE_FORMAT",
    )
    tts_sample_rate: int = Field(validation_alias="TTS_SAMPLE_RATE")
    tts_stream_chunk_size: int = Field(default=8192, validation_alias="TTS_STREAM_CHUNK_SIZE")
    tts_speed: float = Field(default=1.0, validation_alias="TTS_SPEED")
    tts_task_type: Literal["CustomVoice", "VoiceDesign", "Base"] | None = Field(
        default=None,
        validation_alias="TTS_TASK_TYPE",
    )
    tts_ref_text: str | None = Field(default=None, validation_alias="TTS_REF_TEXT")
    tts_ref_audio: str | None = Field(default=None, validation_alias="TTS_REF_AUDIO")

    request_timeout_seconds: float = Field(
        default=120.0,
        validation_alias="REQUEST_TIMEOUT_SECONDS",
    )
    connect_timeout_seconds: float = Field(default=10.0, validation_alias="CONNECT_TIMEOUT_SECONDS")
    max_audio_buffer_bytes: int = Field(
        default=20 * 1024 * 1024,
        validation_alias="MAX_AUDIO_BUFFER_BYTES",
    )

    @computed_field
    @property
    def asr_transcriptions_url(self) -> str:
        return f"{self.asr_http_base_url.rstrip('/')}/v1/audio/transcriptions"

    @computed_field
    @property
    def tts_speech_url(self) -> str:
        return f"{self.tts_http_base_url.rstrip('/')}/v1/audio/speech"


@lru_cache
def get_settings() -> Settings:
    return _load_settings(Settings)
