#include "chat/chat_protocol.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chat/chat_playback.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static const char *chat_find_json_value(const char *json, const char *key)
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

static bool chat_json_get_string(const char *json, const char *key, char *out, size_t out_len)
{
    const char *value = chat_find_json_value(json, key);
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

static bool chat_json_get_u64(const char *json, const char *key, uint64_t *out)
{
    const char *value = chat_find_json_value(json, key);
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

static bool chat_json_get_u32(const char *json, const char *key, uint32_t *out)
{
    uint64_t value = 0;
    if (!chat_json_get_u64(json, key, &value) || value > UINT32_MAX) {
        return false;
    }

    *out = (uint32_t)value;
    return true;
}

bool chat_uri_is_valid(const char *uri)
{
    return uri != NULL &&
           (strncmp(uri, "ws://", strlen("ws://")) == 0 ||
            strncmp(uri, "wss://", strlen("wss://")) == 0);
}

esp_err_t chat_make_headers(char *headers, size_t headers_len)
{
    if (headers == NULL || headers_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    headers[0] = '\0';
    if (CONFIG_ESPESP_CHAT_AUTH_TOKEN[0] == '\0') {
        return ESP_OK;
    }

    int written = snprintf(headers,
                           headers_len,
                           "Authorization: Bearer %s\r\n",
                           CONFIG_ESPESP_CHAT_AUTH_TOKEN);
    if (written < 0 || written >= (int)headers_len) {
        ESP_LOGE(CHAT_TAG, "chat auth token is too long");
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static esp_err_t chat_send_text(chat_context_t *ctx, const char *payload)
{
    if (ctx == NULL || ctx->client == NULL || payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int len = strlen(payload);
    int sent = esp_websocket_client_send_text(ctx->client,
                                              payload,
                                              len,
                                              pdMS_TO_TICKS(CONFIG_ESPESP_CHAT_NETWORK_TIMEOUT_MS));
    if (sent != len) {
        ESP_LOGW(CHAT_TAG, "send text failed: sent=%d expected=%d payload=%s", sent, len, payload);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t chat_send_audio_start(chat_context_t *ctx)
{
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ctx->turn_id++;
    char payload[192];
    int written = snprintf(payload,
                           sizeof(payload),
                           "{\"type\":\"audio_start\","
                           "\"turn_id\":%" PRIu32 ","
                           "\"sample_rate\":%" PRIu32 ","
                           "\"channels\":%u,"
                           "\"encoding\":\"pcm_s16le\","
                           "\"container\":\"raw\"}",
                           ctx->turn_id,
                           ctx->input_sample_rate_hz,
                           CHAT_CHANNELS);
    if (written < 0 || written >= (int)sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_RETURN_ON_ERROR(chat_send_text(ctx, payload), CHAT_TAG, "send audio_start");
    ctx->session_active = true;
    ctx->mic_sent_bytes = 0;
    ctx->mic_sent_chunks = 0;
    ctx->mic_dropped_chunks = 0;
    ctx->speech_started_us = esp_timer_get_time();
    ESP_LOGI(CHAT_TAG, "audio_start turn=%" PRIu32 " sample_rate=%" PRIu32, ctx->turn_id, ctx->input_sample_rate_hz);
    return ESP_OK;
}

esp_err_t chat_send_audio_end(chat_context_t *ctx)
{
    if (ctx == NULL || !ctx->session_active) {
        return ESP_OK;
    }

    char payload[128];
    int written = snprintf(payload,
                           sizeof(payload),
                           "{\"type\":\"audio_end\",\"turn_id\":%" PRIu32 "}",
                           ctx->turn_id);
    if (written < 0 || written >= (int)sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t ret = chat_send_text(ctx, payload);
    ctx->session_active = false;
    int64_t duration_ms = ctx->speech_started_us > 0 ? (esp_timer_get_time() - ctx->speech_started_us) / 1000 : 0;
    ESP_LOGI(CHAT_TAG,
             "audio_end turn=%" PRIu32 " chunks=%" PRIu64 " bytes=%" PRIu64 " dropped=%" PRIu64
             " speech_ms=%" PRId64,
             ctx->turn_id,
             ctx->mic_sent_chunks,
             ctx->mic_sent_bytes,
             ctx->mic_dropped_chunks,
             duration_ms);
    ctx->speech_started_us = 0;
    return ret;
}

esp_err_t chat_send_cancel_response(chat_context_t *ctx, const char *reason)
{
    if (ctx == NULL || ctx->client == NULL || !esp_websocket_client_is_connected(ctx->client)) {
        return ESP_OK;
    }

    char payload[160];
    int written = snprintf(payload,
                           sizeof(payload),
                           "{\"type\":\"cancel_response\",\"reason\":\"%s\"}",
                           reason != NULL ? reason : "barge_in");
    if (written < 0 || written >= (int)sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    return chat_send_text(ctx, payload);
}

esp_err_t chat_send_audio_frame(chat_context_t *ctx, const int16_t *pcm, size_t sample_count)
{
    if (ctx == NULL || ctx->client == NULL || pcm == NULL || sample_count == 0 || !ctx->session_active) {
        return ESP_OK;
    }

    size_t byte_count = sample_count * sizeof(int16_t);
    if (byte_count > INT32_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    int sent = esp_websocket_client_send_bin(ctx->client,
                                             (const char *)pcm,
                                             (int)byte_count,
                                             pdMS_TO_TICKS(CONFIG_ESPESP_CHAT_NETWORK_TIMEOUT_MS));
    if (sent != (int)byte_count) {
        ctx->mic_dropped_chunks++;
        ESP_LOGW(CHAT_TAG,
                 "send audio failed: sent=%d expected=%u dropped=%" PRIu64,
                 sent,
                 (unsigned int)byte_count,
                 ctx->mic_dropped_chunks);
        return ESP_FAIL;
    }

    ctx->mic_sent_chunks++;
    ctx->mic_sent_bytes += byte_count;
    return ESP_OK;
}

void chat_log_mic_progress(chat_context_t *ctx, bool speech_active, uint32_t avg_abs, uint32_t peak)
{
    if (ctx == NULL) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    if (ctx->last_mic_stats_us != 0 && now_us - ctx->last_mic_stats_us < CHAT_STATS_PERIOD_US) {
        return;
    }

    ctx->last_mic_stats_us = now_us;
    ESP_LOGI(CHAT_TAG,
             "mic state=%s turn=%" PRIu32 " chunks=%" PRIu64 " bytes=%" PRIu64
             " dropped=%" PRIu64 " avg_abs=%" PRIu32 " peak=%" PRIu32
             " playback=%s free_heap=%" PRIu32,
             speech_active ? "speech" : "silence",
             ctx->turn_id,
             ctx->mic_sent_chunks,
             ctx->mic_sent_bytes,
             ctx->mic_dropped_chunks,
             avg_abs,
             peak,
             ctx->playback_streaming ? "true" : "false",
             esp_get_free_heap_size());
}

static void chat_handle_ready(chat_context_t *ctx, const char *json)
{
    uint32_t asr_sample_rate = 0;
    uint32_t tts_sample_rate = 0;
    (void)chat_json_get_u32(json, "asr_sample_rate", &asr_sample_rate);
    (void)chat_json_get_u32(json, "tts_sample_rate", &tts_sample_rate);

    if (tts_sample_rate != 0) {
        ctx->server_tts_sample_rate_hz = tts_sample_rate;
    }
    if (asr_sample_rate != 0 && asr_sample_rate != ctx->input_sample_rate_hz) {
        ESP_LOGW(CHAT_TAG,
                 "server ASR sample_rate=%" PRIu32 " differs from VADNet/mic sample_rate=%" PRIu32,
                 asr_sample_rate,
                 ctx->input_sample_rate_hz);
    }

    ESP_LOGI(CHAT_TAG,
             "server ready asr_sample_rate=%" PRIu32 " tts_sample_rate=%" PRIu32,
             asr_sample_rate,
             tts_sample_rate);
}

static void chat_handle_tts_start(chat_context_t *ctx, const char *json)
{
    char text[160] = "";
    uint32_t sample_rate = 0;
    (void)chat_json_get_string(json, "text", text, sizeof(text));
    (void)chat_json_get_u32(json, "sample_rate", &sample_rate);

    if (sample_rate == 0) {
        sample_rate = ctx->server_tts_sample_rate_hz != 0 ?
                      ctx->server_tts_sample_rate_hz :
                      CONFIG_ESPESP_CHAT_SPK_SAMPLE_RATE_HZ;
    }

    chat_playback_interrupt(ctx, "new tts_start");
    esp_err_t ret = chat_playback_begin(ctx, sample_rate);
    if (ret != ESP_OK) {
        ESP_LOGE(CHAT_TAG, "start playback failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(CHAT_TAG,
             "tts_start turn=%" PRIu32 " sample_rate=%" PRIu32 " text=%s",
             ctx->turn_id,
             sample_rate,
             text[0] != '\0' ? text : "(empty)");
}

static void chat_handle_control_text(chat_context_t *ctx, const char *json)
{
    char type[32] = "";
    if (!chat_json_get_string(json, "type", type, sizeof(type))) {
        ESP_LOGW(CHAT_TAG, "control text without type: %s", json);
        return;
    }

    if (strcmp(type, "ready") == 0) {
        chat_handle_ready(ctx, json);
    } else if (strcmp(type, "turn_started") == 0) {
        ESP_LOGI(CHAT_TAG, "server turn_started");
    } else if (strcmp(type, "asr_partial") == 0) {
        char text[160] = "";
        (void)chat_json_get_string(json, "text", text, sizeof(text));
        ESP_LOGI(CHAT_TAG, "asr_partial: %s", text[0] != '\0' ? text : "(empty)");
    } else if (strcmp(type, "asr_final") == 0) {
        char text[160] = "";
        (void)chat_json_get_string(json, "text", text, sizeof(text));
        ESP_LOGI(CHAT_TAG, "asr_final: %s", text[0] != '\0' ? text : "(empty)");
    } else if (strcmp(type, "llm_delta") == 0) {
        char text[120] = "";
        (void)chat_json_get_string(json, "text", text, sizeof(text));
        ESP_LOGI(CHAT_TAG, "llm_delta: %s", text[0] != '\0' ? text : "(empty)");
    } else if (strcmp(type, "tts_start") == 0) {
        chat_handle_tts_start(ctx, json);
    } else if (strcmp(type, "tts_done") == 0 || strcmp(type, "tts_end") == 0) {
        chat_playback_end(ctx, type);
    } else if (strcmp(type, "response_cancelled") == 0) {
        chat_playback_interrupt(ctx, "server response_cancelled");
    } else if (strcmp(type, "turn_done") == 0) {
        ESP_LOGI(CHAT_TAG, "turn_done");
    } else if (strcmp(type, "pong") == 0) {
        ESP_LOGD(CHAT_TAG, "pong");
    } else if (strcmp(type, "warning") == 0) {
        char message[200] = "";
        (void)chat_json_get_string(json, "message", message, sizeof(message));
        ESP_LOGW(CHAT_TAG, "server warning: %s", message[0] != '\0' ? message : json);
    } else if (strcmp(type, "error") == 0) {
        char message[200] = "";
        (void)chat_json_get_string(json, "message", message, sizeof(message));
        ESP_LOGE(CHAT_TAG, "server error: %s", message[0] != '\0' ? message : json);
        chat_playback_interrupt(ctx, "server error");
    } else {
        ESP_LOGI(CHAT_TAG, "control type=%s payload=%s", type, json);
    }
}

static void chat_handle_text_event(chat_context_t *ctx, const esp_websocket_event_data_t *data)
{
    if (data == NULL || data->data_ptr == NULL) {
        return;
    }

    if (data->payload_offset != 0 || data->data_len != data->payload_len) {
        ESP_LOGW(CHAT_TAG,
                 "fragmented control text is not supported offset=%d len=%d payload_len=%d",
                 data->payload_offset,
                 data->data_len,
                 data->payload_len);
        return;
    }

    if (data->data_len <= 0 || data->data_len >= CHAT_CONTROL_MAX) {
        ESP_LOGW(CHAT_TAG, "control text too large len=%d", data->data_len);
        return;
    }

    char text[CHAT_CONTROL_MAX];
    memcpy(text, data->data_ptr, (size_t)data->data_len);
    text[data->data_len] = '\0';
    chat_handle_control_text(ctx, text);
}

static void chat_handle_data(chat_context_t *ctx, const esp_websocket_event_data_t *data)
{
    if (ctx == NULL || data == NULL) {
        return;
    }

    bool message_done = data->payload_len <= 0 ||
                        data->payload_offset + data->data_len >= data->payload_len;

    switch (data->op_code) {
    case CHAT_WS_OPCODE_TEXT:
        chat_handle_text_event(ctx, data);
        break;
    case CHAT_WS_OPCODE_BINARY:
        if (data->payload_offset == 0) {
            ctx->binary_payload_active = true;
        }
        if (chat_playback_enqueue_audio(ctx,
                                        (const uint8_t *)data->data_ptr,
                                        data->data_len,
                                        message_done) != ESP_OK) {
            ESP_LOGW(CHAT_TAG, "enqueue audio chunk failed");
        }
        if (message_done) {
            ctx->binary_payload_active = false;
        }
        break;
    case CHAT_WS_OPCODE_CONTINUATION:
        if (ctx->binary_payload_active) {
            if (chat_playback_enqueue_audio(ctx,
                                            (const uint8_t *)data->data_ptr,
                                            data->data_len,
                                            message_done) != ESP_OK) {
                ESP_LOGW(CHAT_TAG, "enqueue audio continuation failed");
            }
            if (message_done) {
                ctx->binary_payload_active = false;
            }
        } else {
            ESP_LOGW(CHAT_TAG, "ignoring continuation frame without active binary payload");
        }
        break;
    case CHAT_WS_OPCODE_CLOSE:
        ESP_LOGI(CHAT_TAG, "close frame received");
        break;
    case CHAT_WS_OPCODE_PING:
        ESP_LOGD(CHAT_TAG, "ping frame received");
        break;
    case CHAT_WS_OPCODE_PONG:
        ESP_LOGD(CHAT_TAG, "pong frame received");
        break;
    default:
        ESP_LOGW(CHAT_TAG,
                 "unsupported websocket opcode=0x%x data_len=%d payload_len=%d",
                 data->op_code,
                 data->data_len,
                 data->payload_len);
        break;
    }
}

void chat_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    chat_context_t *ctx = (chat_context_t *)handler_args;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    (void)base;

    switch ((esp_websocket_event_id_t)event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(CHAT_TAG, "connected to %s", CONFIG_ESPESP_CHAT_URI);
        if (ctx != NULL && ctx->event_group != NULL) {
            xEventGroupClearBits(ctx->event_group, CHAT_ERROR_BIT);
            xEventGroupSetBits(ctx->event_group, CHAT_CONNECTED_BIT);
        }
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(CHAT_TAG, "disconnected from %s", CONFIG_ESPESP_CHAT_URI);
        if (ctx != NULL) {
            ctx->session_active = false;
            chat_playback_interrupt(ctx, "websocket disconnected");
        }
        if (ctx != NULL && ctx->event_group != NULL) {
            xEventGroupClearBits(ctx->event_group, CHAT_CONNECTED_BIT);
        }
        break;
    case WEBSOCKET_EVENT_DATA:
        chat_handle_data(ctx, data);
        break;
    case WEBSOCKET_EVENT_ERROR:
        if (data != NULL) {
            ESP_LOGE(CHAT_TAG,
                     "websocket error type=%d status=%d sock_errno=%d tls_err=0x%x",
                     data->error_handle.error_type,
                     data->error_handle.esp_ws_handshake_status_code,
                     data->error_handle.esp_transport_sock_errno,
                     data->error_handle.esp_tls_last_esp_err);
        } else {
            ESP_LOGE(CHAT_TAG, "websocket client error");
        }
        if (ctx != NULL && ctx->event_group != NULL) {
            xEventGroupSetBits(ctx->event_group, CHAT_ERROR_BIT);
        }
        break;
    case WEBSOCKET_EVENT_CLOSED:
        ESP_LOGI(CHAT_TAG, "websocket closed by peer");
        if (ctx != NULL && ctx->event_group != NULL) {
            xEventGroupClearBits(ctx->event_group, CHAT_CONNECTED_BIT);
        }
        break;
    case WEBSOCKET_EVENT_BEFORE_CONNECT:
        ESP_LOGD(CHAT_TAG, "websocket before connect");
        break;
    case WEBSOCKET_EVENT_BEGIN:
        ESP_LOGD(CHAT_TAG, "websocket transport begin");
        break;
    case WEBSOCKET_EVENT_FINISH:
        ESP_LOGD(CHAT_TAG, "websocket transport finish");
        break;
    default:
        ESP_LOGD(CHAT_TAG, "websocket event id=%" PRId32, event_id);
        break;
    }
}
