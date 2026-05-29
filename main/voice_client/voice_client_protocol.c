#include "voice_client/voice_client_protocol.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "voice_client/voice_client_audio.h"

static const char *voice_client_find_json_value(const char *json, const char *key)
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

bool voice_client_json_get_string(const char *json, const char *key, char *out, size_t out_len)
{
    const char *value = voice_client_find_json_value(json, key);
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

bool voice_client_json_get_u64(const char *json, const char *key, uint64_t *out)
{
    const char *value = voice_client_find_json_value(json, key);
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

bool voice_client_json_get_u32(const char *json, const char *key, uint32_t *out)
{
    uint64_t value = 0;
    if (!voice_client_json_get_u64(json, key, &value) || value > UINT32_MAX) {
        return false;
    }

    *out = (uint32_t)value;
    return true;
}

static esp_err_t voice_client_appendf(char *out,
                                      size_t out_len,
                                      size_t *pos,
                                      const char *fmt,
                                      ...)
{
    if (out == NULL || pos == NULL || *pos >= out_len) {
        return ESP_ERR_INVALID_ARG;
    }

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(out + *pos, out_len - *pos, fmt, args);
    va_end(args);

    if (written < 0 || written >= (int)(out_len - *pos)) {
        return ESP_ERR_INVALID_SIZE;
    }

    *pos += (size_t)written;
    return ESP_OK;
}

static esp_err_t voice_client_append_json_string(char *out,
                                                 size_t out_len,
                                                 size_t *pos,
                                                 const char *value)
{
    ESP_RETURN_ON_ERROR(voice_client_appendf(out, out_len, pos, "\""),
                        VOICE_CLIENT_TAG,
                        "append JSON quote");

    for (const unsigned char *cursor = (const unsigned char *)value; cursor != NULL && *cursor != '\0'; cursor++) {
        unsigned char ch = *cursor;
        if (ch == '"' || ch == '\\') {
            ESP_RETURN_ON_ERROR(voice_client_appendf(out, out_len, pos, "\\%c", ch),
                                VOICE_CLIENT_TAG,
                                "append JSON escape");
        } else if (ch == '\n') {
            ESP_RETURN_ON_ERROR(voice_client_appendf(out, out_len, pos, "\\n"),
                                VOICE_CLIENT_TAG,
                                "append JSON newline");
        } else if (ch == '\r') {
            ESP_RETURN_ON_ERROR(voice_client_appendf(out, out_len, pos, "\\r"),
                                VOICE_CLIENT_TAG,
                                "append JSON carriage return");
        } else if (ch == '\t') {
            ESP_RETURN_ON_ERROR(voice_client_appendf(out, out_len, pos, "\\t"),
                                VOICE_CLIENT_TAG,
                                "append JSON tab");
        } else if (ch < 0x20) {
            ESP_RETURN_ON_ERROR(voice_client_appendf(out, out_len, pos, "\\u%04x", ch),
                                VOICE_CLIENT_TAG,
                                "append JSON control");
        } else {
            ESP_RETURN_ON_ERROR(voice_client_appendf(out, out_len, pos, "%c", ch),
                                VOICE_CLIENT_TAG,
                                "append JSON char");
        }
    }

    return voice_client_appendf(out, out_len, pos, "\"");
}

esp_err_t voice_client_build_start_event(char *payload, size_t payload_len)
{
    if (payload == NULL || payload_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int segment_ms = CONFIG_ESPESP_VOICE_CLIENT_SEGMENT_MS;
    size_t pos = 0;

    ESP_RETURN_ON_ERROR(
        voice_client_appendf(payload,
                             payload_len,
                             &pos,
                             "{\"type\":\"start\","
                             "\"audio_format\":{"
                             "\"encoding\":\"pcm_s16le\","
                             "\"sample_rate\":%d,"
                             "\"channels\":%u,"
                             "\"container\":\"raw\"},"
                             "\"segment_seconds\":%d.%03d",
                             CONFIG_ESPESP_VOICE_CLIENT_INPUT_SAMPLE_RATE_HZ,
                             VOICE_CLIENT_CHANNELS,
                             segment_ms / 1000,
                             segment_ms % 1000),
        VOICE_CLIENT_TAG,
        "build start event");

    if (CONFIG_ESPESP_VOICE_CLIENT_ASR_LANGUAGE[0] != '\0') {
        ESP_RETURN_ON_ERROR(voice_client_appendf(payload, payload_len, &pos, ",\"asr\":{\"language\":"),
                            VOICE_CLIENT_TAG,
                            "append ASR");
        ESP_RETURN_ON_ERROR(voice_client_append_json_string(payload,
                                                           payload_len,
                                                           &pos,
                                                           CONFIG_ESPESP_VOICE_CLIENT_ASR_LANGUAGE),
                            VOICE_CLIENT_TAG,
                            "append ASR language");
        ESP_RETURN_ON_ERROR(voice_client_appendf(payload, payload_len, &pos, "}"),
                            VOICE_CLIENT_TAG,
                            "close ASR");
    }

    ESP_RETURN_ON_ERROR(voice_client_appendf(payload, payload_len, &pos, ",\"tts\":{"),
                        VOICE_CLIENT_TAG,
                        "append TTS");
    if (CONFIG_ESPESP_VOICE_CLIENT_TTS_VOICE[0] != '\0') {
        ESP_RETURN_ON_ERROR(voice_client_appendf(payload, payload_len, &pos, "\"voice\":"),
                            VOICE_CLIENT_TAG,
                            "append TTS voice key");
        ESP_RETURN_ON_ERROR(voice_client_append_json_string(payload,
                                                           payload_len,
                                                           &pos,
                                                           CONFIG_ESPESP_VOICE_CLIENT_TTS_VOICE),
                            VOICE_CLIENT_TAG,
                            "append TTS voice");
        ESP_RETURN_ON_ERROR(voice_client_appendf(payload, payload_len, &pos, ","),
                            VOICE_CLIENT_TAG,
                            "append TTS comma");
    }
    ESP_RETURN_ON_ERROR(voice_client_appendf(payload, payload_len, &pos, "\"response_format\":\"pcm\"}}"),
                        VOICE_CLIENT_TAG,
                        "close start event");

    return ESP_OK;
}

static void voice_client_handle_ready(const char *json)
{
    uint32_t input_sample_rate = 0;
    uint32_t tts_sample_rate = 0;
    char tts_response_format[24] = "";

    (void)voice_client_json_get_u32(json, "input_sample_rate", &input_sample_rate);
    (void)voice_client_json_get_u32(json, "tts_sample_rate", &tts_sample_rate);
    (void)voice_client_json_get_string(json, "tts_response_format", tts_response_format, sizeof(tts_response_format));

    ESP_LOGI(VOICE_CLIENT_TAG,
             "server ready defaults input_sample_rate=%" PRIu32 " tts_sample_rate=%" PRIu32
             " tts_format=%s",
             input_sample_rate,
             tts_sample_rate,
             tts_response_format[0] != '\0' ? tts_response_format : "(unknown)");
}

static void voice_client_handle_started(const char *json)
{
    uint32_t sample_rate = 0;
    uint32_t channels = 0;
    (void)voice_client_json_get_u32(json, "sample_rate", &sample_rate);
    (void)voice_client_json_get_u32(json, "channels", &channels);

    ESP_LOGI(VOICE_CLIENT_TAG,
             "server started input sample_rate=%" PRIu32 " channels=%" PRIu32,
             sample_rate,
             channels);
}

static void voice_client_handle_tts_start(voice_client_context_t *ctx, const char *json)
{
    char response_format[24] = "pcm";
    char text[160] = "";
    uint32_t sample_rate = 0;

    (void)voice_client_json_get_string(json, "response_format", response_format, sizeof(response_format));
    (void)voice_client_json_get_string(json, "text", text, sizeof(text));
    (void)voice_client_json_get_u32(json, "sample_rate", &sample_rate);

    ctx->playback_streaming = false;
    ctx->playback_pcm = strcmp(response_format, "pcm") == 0;
    ctx->binary_payload_active = false;
    ctx->warned_drop_binary = false;

    if (!ctx->playback_pcm) {
        ESP_LOGE(VOICE_CLIENT_TAG, "unsupported TTS response_format=%s; only pcm can be played directly", response_format);
        return;
    }

    if (sample_rate == 0) {
        sample_rate = ctx->output_sample_rate_hz;
    }

    esp_err_t ret = voice_client_set_output_sample_rate(ctx, sample_rate);
    if (ret != ESP_OK) {
        ESP_LOGE(VOICE_CLIENT_TAG, "speaker sample rate setup failed: %s", esp_err_to_name(ret));
        ctx->playback_pcm = false;
        return;
    }

    voice_client_reset_playback_stats(ctx);
    ctx->playback_streaming = true;
    ESP_LOGI(VOICE_CLIENT_TAG,
             "tts_start format=%s sample_rate=%" PRIu32 " volume=%d%% limit=%d%% text=%s",
             response_format,
             sample_rate,
             CONFIG_ESPESP_VOICE_CLIENT_TTS_VOLUME_PERCENT,
             CONFIG_ESPESP_VOICE_CLIENT_TTS_LIMIT_PERCENT,
             text[0] != '\0' ? text : "(empty)");
}

static void voice_client_handle_tts_end(voice_client_context_t *ctx, const char *json)
{
    uint64_t server_bytes = 0;
    (void)voice_client_json_get_u64(json, "bytes", &server_bytes);

    int64_t elapsed_ms = ctx->playback_started_us > 0 ?
                         (esp_timer_get_time() - ctx->playback_started_us) / 1000 :
                         0;
    if (ctx->has_pending_byte) {
        ESP_LOGW(VOICE_CLIENT_TAG, "dropping pending odd byte at tts_end");
        ctx->has_pending_byte = false;
    }

    ctx->playback_streaming = false;
    ctx->binary_payload_active = false;
    ESP_LOGI(VOICE_CLIENT_TAG,
             "tts_end server_bytes=%" PRIu64 " received=%" PRIu64 " written=%" PRIu64
             " chunks=%" PRIu32 " peak_in=%" PRIu32 " peak_out=%" PRIu32
             " limited=%" PRIu64 "/%" PRIu64 " elapsed_ms=%" PRId64,
             server_bytes,
             ctx->tts_received_bytes,
             ctx->tts_written_bytes,
             ctx->tts_chunks,
             ctx->tts_input_peak,
             ctx->tts_output_peak,
             ctx->tts_limited_samples,
             ctx->tts_samples,
             elapsed_ms);
    if (ctx->tts_input_peak > 30000 || ctx->tts_limited_samples > 0) {
        ESP_LOGW(VOICE_CLIENT_TAG,
                 "TTS PCM has little headroom; reduce Voice client module -> TTS playback volume percent if playback is distorted");
    }
}

void voice_client_handle_control_text(voice_client_context_t *ctx, const char *json)
{
    char type[32];
    if (!voice_client_json_get_string(json, "type", type, sizeof(type))) {
        ESP_LOGW(VOICE_CLIENT_TAG, "control text without type: %s", json);
        return;
    }

    if (strcmp(type, "ready") == 0) {
        voice_client_handle_ready(json);
    } else if (strcmp(type, "started") == 0) {
        voice_client_handle_started(json);
    } else if (strcmp(type, "transcript") == 0) {
        char text[160] = "";
        (void)voice_client_json_get_string(json, "text", text, sizeof(text));
        ESP_LOGI(VOICE_CLIENT_TAG, "transcript: %s", text[0] != '\0' ? text : "(empty)");
    } else if (strcmp(type, "tts_start") == 0) {
        voice_client_handle_tts_start(ctx, json);
    } else if (strcmp(type, "tts_end") == 0) {
        voice_client_handle_tts_end(ctx, json);
    } else if (strcmp(type, "committed") == 0) {
        uint32_t queued = 0;
        (void)voice_client_json_get_u32(json, "queued", &queued);
        ESP_LOGI(VOICE_CLIENT_TAG, "committed queued=%" PRIu32, queued);
    } else if (strcmp(type, "pong") == 0) {
        ESP_LOGD(VOICE_CLIENT_TAG, "pong");
    } else if (strcmp(type, "error") == 0) {
        char code[64] = "";
        char message[200] = "";
        (void)voice_client_json_get_string(json, "code", code, sizeof(code));
        (void)voice_client_json_get_string(json, "message", message, sizeof(message));
        ESP_LOGE(VOICE_CLIENT_TAG,
                 "voice_server error code=%s message=%s",
                 code[0] != '\0' ? code : "(unknown)",
                 message[0] != '\0' ? message : json);
    } else {
        ESP_LOGI(VOICE_CLIENT_TAG, "control type=%s payload=%s", type, json);
    }
}
