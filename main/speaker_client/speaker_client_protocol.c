#include "speaker_client/speaker_client_protocol.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "speaker_client/speaker_client_audio.h"

static const char *speaker_client_find_json_value(const char *json, const char *key)
{
    if (json == NULL || key == NULL) {
        return NULL;
    }

    char pattern[64];
    int written = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (written < 0 || written >= (int)sizeof(pattern)) {
        return NULL;
    }

    const char *key_pos = strstr(json, pattern);
    if (key_pos == NULL) {
        return NULL;
    }

    const char *colon = strchr(key_pos + written, ':');
    if (colon == NULL) {
        return NULL;
    }

    const char *value = colon + 1;
    while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n') {
        value++;
    }
    return value;
}

bool speaker_client_json_get_string(const char *json, const char *key, char *out, size_t out_len)
{
    const char *value = speaker_client_find_json_value(json, key);
    if (value == NULL || *value != '"' || out == NULL || out_len == 0) {
        return false;
    }

    value++;
    size_t pos = 0;
    while (*value != '\0' && *value != '"') {
        char ch = *value++;
        if (ch == '\\') {
            ch = *value++;
            if (ch == '\0') {
                return false;
            }
        }
        if (pos + 1 >= out_len) {
            return false;
        }
        out[pos++] = ch;
    }

    if (*value != '"') {
        return false;
    }
    out[pos] = '\0';
    return true;
}

bool speaker_client_json_get_u64(const char *json, const char *key, uint64_t *out)
{
    const char *value = speaker_client_find_json_value(json, key);
    if (value == NULL || out == NULL) {
        return false;
    }

    char *end = NULL;
    unsigned long long parsed = strtoull(value, &end, 10);
    if (end == value) {
        return false;
    }

    *out = (uint64_t)parsed;
    return true;
}

bool speaker_client_json_get_u32(const char *json, const char *key, uint32_t *out)
{
    uint64_t value = 0;
    if (!speaker_client_json_get_u64(json, key, &value) || value > UINT32_MAX) {
        return false;
    }

    *out = (uint32_t)value;
    return true;
}

static void speaker_client_handle_audio_start(speaker_client_context_t *ctx, const char *json)
{
    char format[24];
    char source[96] = "";
    uint32_t sample_rate_hz = 0;
    uint32_t channels = 0;
    uint32_t sample_width_bits = 0;
    uint32_t chunk_bytes = 0;
    uint64_t frames = 0;

    if (!speaker_client_json_get_string(json, "format", format, sizeof(format)) ||
        !speaker_client_json_get_u32(json, "sample_rate_hz", &sample_rate_hz) ||
        !speaker_client_json_get_u32(json, "channels", &channels) ||
        !speaker_client_json_get_u32(json, "sample_width_bits", &sample_width_bits)) {
        ESP_LOGE(SPEAKER_CLIENT_TAG, "audio_start metadata missing required fields: %s", json);
        if (ctx != NULL) {
            ctx->streaming = false;
            ctx->binary_payload_active = false;
        }
        return;
    }

    (void)speaker_client_json_get_u32(json, "chunk_bytes", &chunk_bytes);
    (void)speaker_client_json_get_u64(json, "frames", &frames);
    (void)speaker_client_json_get_string(json, "source", source, sizeof(source));

    if (ctx != NULL && (ctx->streaming || ctx->has_last_output_sample || ctx->has_pending_byte)) {
        esp_err_t ret = speaker_client_finish_stream(ctx, "audio_restart");
        if (ret != ESP_OK) {
            ESP_LOGW(SPEAKER_CLIENT_TAG, "finish previous stream before restart failed: %s", esp_err_to_name(ret));
        }
    }

    if (strcmp(format, "pcm_s16le") != 0 || channels != 1 || sample_width_bits != 16) {
        ESP_LOGE(SPEAKER_CLIENT_TAG,
                 "unsupported audio format: format=%s channels=%" PRIu32 " bits=%" PRIu32,
                 format,
                 channels,
                 sample_width_bits);
        if (ctx != NULL) {
            ctx->streaming = false;
            ctx->binary_payload_active = false;
        }
        return;
    }

    if (sample_rate_hz != CONFIG_ESPESP_SPK_SAMPLE_RATE_HZ) {
        ESP_LOGE(SPEAKER_CLIENT_TAG,
                 "sample rate mismatch: server=%" PRIu32 " Hz local_i2s=%d Hz",
                 sample_rate_hz,
                 CONFIG_ESPESP_SPK_SAMPLE_RATE_HZ);
        if (ctx != NULL) {
            ctx->streaming = false;
            ctx->binary_payload_active = false;
        }
        return;
    }

    esp_err_t ret = speaker_client_prepare_stream(ctx, sample_rate_hz, frames);
    if (ret != ESP_OK) {
        ESP_LOGE(SPEAKER_CLIENT_TAG, "speaker stream setup failed: %s", esp_err_to_name(ret));
        if (ctx != NULL) {
            ctx->streaming = false;
            ctx->binary_payload_active = false;
        }
        return;
    }

    ESP_LOGI(SPEAKER_CLIENT_TAG,
             "audio_start source=%s format=%s sample_rate=%" PRIu32 " channels=%" PRIu32
             " bits=%" PRIu32 " frames=%" PRIu64 " chunk_bytes=%" PRIu32 " declick_ms=%u",
             source[0] != '\0' ? source : "(unknown)",
             format,
             sample_rate_hz,
             channels,
             sample_width_bits,
             frames,
             chunk_bytes,
             SPEAKER_CLIENT_DECLICK_MS);
}

static void speaker_client_handle_audio_end(speaker_client_context_t *ctx, const char *json)
{
    uint64_t frames = 0;
    uint64_t byte_count = 0;
    (void)speaker_client_json_get_u64(json, "frames", &frames);
    (void)speaker_client_json_get_u64(json, "bytes", &byte_count);

    int64_t elapsed_ms = 0;
    uint64_t local_frames = 0;
    uint64_t local_bytes = 0;
    uint64_t declick_bytes = 0;

    if (ctx != NULL) {
        elapsed_ms = ctx->stream_started_us > 0 ? (esp_timer_get_time() - ctx->stream_started_us) / 1000 : 0;
        local_frames = ctx->written_bytes / SPEAKER_CLIENT_SAMPLE_WIDTH_BYTES;
        local_bytes = ctx->written_bytes;
        esp_err_t ret = speaker_client_finish_stream(ctx, "audio_end");
        if (ret != ESP_OK) {
            ESP_LOGW(SPEAKER_CLIENT_TAG, "tail declick failed at audio_end: %s", esp_err_to_name(ret));
        }
        declick_bytes = ctx->declick_written_bytes;
    }

    ESP_LOGI(SPEAKER_CLIENT_TAG,
             "audio_end server_frames=%" PRIu64 " server_bytes=%" PRIu64
             " local_frames=%" PRIu64 " local_bytes=%" PRIu64 " declick_bytes=%" PRIu64
             " elapsed_ms=%" PRId64,
             frames,
             byte_count,
             local_frames,
             local_bytes,
             declick_bytes,
             elapsed_ms);
}

void speaker_client_handle_control_text(speaker_client_context_t *ctx, const char *json)
{
    char type[32];
    if (!speaker_client_json_get_string(json, "type", type, sizeof(type))) {
        ESP_LOGW(SPEAKER_CLIENT_TAG, "control text without type: %s", json);
        return;
    }

    if (strcmp(type, "audio_start") == 0) {
        speaker_client_handle_audio_start(ctx, json);
    } else if (strcmp(type, "audio_end") == 0) {
        speaker_client_handle_audio_end(ctx, json);
    } else if (strcmp(type, "error") == 0) {
        char message[160] = "";
        (void)speaker_client_json_get_string(json, "message", message, sizeof(message));
        ESP_LOGE(SPEAKER_CLIENT_TAG, "speaker_server error: %s", message[0] != '\0' ? message : json);
        if (ctx != NULL) {
            esp_err_t ret = speaker_client_finish_stream(ctx, "server_error");
            if (ret != ESP_OK) {
                ESP_LOGW(SPEAKER_CLIENT_TAG, "tail declick failed after server error: %s", esp_err_to_name(ret));
            }
        }
    } else {
        ESP_LOGI(SPEAKER_CLIENT_TAG, "control text type=%s payload=%s", type, json);
    }
}
