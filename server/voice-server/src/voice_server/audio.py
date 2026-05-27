from __future__ import annotations

import io
import wave
from dataclasses import dataclass

PCM_S16LE_SAMPLE_WIDTH = 2


@dataclass(frozen=True)
class PcmFormat:
    sample_rate: int
    channels: int
    sample_width: int = PCM_S16LE_SAMPLE_WIDTH


def pcm_duration_seconds(audio: bytes, fmt: PcmFormat) -> float:
    if fmt.sample_rate <= 0 or fmt.channels <= 0 or fmt.sample_width <= 0:
        return 0.0
    return len(audio) / (fmt.sample_rate * fmt.channels * fmt.sample_width)


def pcm_to_wav_bytes(audio: bytes, fmt: PcmFormat) -> bytes:
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setnchannels(fmt.channels)
        wav.setsampwidth(fmt.sample_width)
        wav.setframerate(fmt.sample_rate)
        wav.writeframes(audio)
    return output.getvalue()


def maybe_wav_to_pcm(audio: bytes) -> tuple[bytes, PcmFormat | None]:
    if not audio.startswith(b"RIFF"):
        return audio, None

    with wave.open(io.BytesIO(audio), "rb") as wav:
        fmt = PcmFormat(
            sample_rate=wav.getframerate(),
            channels=wav.getnchannels(),
            sample_width=wav.getsampwidth(),
        )
        return wav.readframes(wav.getnframes()), fmt


def save_pcm_as_wav(path: str, audio: bytes, sample_rate: int, channels: int = 1) -> None:
    with wave.open(path, "wb") as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        wav.writeframes(audio)
