#include "voice_client/voice_client_transport.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "voice_client/voice_client_aec.h"
#include "voice_client/voice_client_audio.h"
#include "voice_client/voice_client_protocol.h"

bool voice_client_uri_is_valid(const char *uri)
{
    return uri != NULL &&
           (strncmp(uri, "ws://", strlen("ws://")) == 0 ||
            strncmp(uri, "wss://", strlen("wss://")) == 0);
}

esp_err_t voice_client_make_headers(char *headers, size_t headers_len)
{
    if (headers == NULL || headers_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    headers[0] = '\0';
    if (CONFIG_ESPESP_VOICE_CLIENT_AUTH_TOKEN[0] == '\0') {
        return ESP_OK;
    }

    int written = snprintf(headers,
                           headers_len,
                           "Authorization: Bearer %s\r\n",
                           CONFIG_ESPESP_VOICE_CLIENT_AUTH_TOKEN);
    if (written < 0 || written >= (int)headers_len) {
        ESP_LOGE(VOICE_CLIENT_TAG, "voice client auth token is too long");
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t voice_client_send_start(esp_websocket_client_handle_t client, voice_client_context_t *ctx)
{
    char payload[VOICE_CLIENT_START_PAYLOAD_MAX];
    ESP_RETURN_ON_ERROR(voice_client_build_start_event(payload, sizeof(payload)),
                        VOICE_CLIENT_TAG,
                        "build start payload");

    int len = strlen(payload);
    int sent = esp_websocket_client_send_text(client,
                                              payload,
                                              len,
                                              pdMS_TO_TICKS(CONFIG_ESPESP_VOICE_CLIENT_NETWORK_TIMEOUT_MS));
    if (sent != len) {
        ESP_LOGW(VOICE_CLIENT_TAG, "send start failed: sent=%d expected=%d", sent, len);
        return ESP_FAIL;
    }

    ctx->start_pending = false;
    ctx->session_started = true;
    ctx->warned_drop_binary = false;
    ESP_LOGI(VOICE_CLIENT_TAG,
             "sent start: input=pcm_s16le/%dHz/%uch raw, segment_ms=%d, tts=pcm",
             CONFIG_ESPESP_VOICE_CLIENT_INPUT_SAMPLE_RATE_HZ,
             VOICE_CLIENT_CHANNELS,
             CONFIG_ESPESP_VOICE_CLIENT_SEGMENT_MS);
    return ESP_OK;
}

static void voice_client_handle_text_event(voice_client_context_t *ctx,
                                           const esp_websocket_event_data_t *data)
{
    if (data == NULL || data->data_ptr == NULL) {
        return;
    }

    if (data->payload_offset != 0 || data->data_len != data->payload_len) {
        ESP_LOGW(VOICE_CLIENT_TAG,
                 "fragmented control text is not supported offset=%d len=%d payload_len=%d",
                 data->payload_offset,
                 data->data_len,
                 data->payload_len);
        return;
    }

    if (data->data_len <= 0 || data->data_len >= VOICE_CLIENT_CONTROL_MAX) {
        ESP_LOGW(VOICE_CLIENT_TAG, "control text too large len=%d", data->data_len);
        return;
    }

    char text[VOICE_CLIENT_CONTROL_MAX];
    memcpy(text, data->data_ptr, (size_t)data->data_len);
    text[data->data_len] = '\0';
    voice_client_handle_control_text(ctx, text);
}

static void voice_client_handle_data(voice_client_context_t *ctx,
                                     const esp_websocket_event_data_t *data)
{
    if (ctx == NULL || data == NULL) {
        return;
    }

    bool message_done = data->payload_len <= 0 ||
                        data->payload_offset + data->data_len >= data->payload_len;

    switch (data->op_code) {
    case VOICE_CLIENT_OPCODE_TEXT:
        voice_client_handle_text_event(ctx, data);
        break;
    case VOICE_CLIENT_OPCODE_BINARY:
        if (data->payload_offset == 0) {
            ctx->binary_payload_active = true;
        }
        if (voice_client_write_audio_chunk(ctx,
                                           (const uint8_t *)data->data_ptr,
                                           data->data_len,
                                           message_done) != ESP_OK) {
            ESP_LOGE(VOICE_CLIENT_TAG, "audio chunk write failed");
        }
        if (message_done) {
            ctx->binary_payload_active = false;
        }
        break;
    case VOICE_CLIENT_OPCODE_CONTINUATION:
        if (ctx->binary_payload_active) {
            if (voice_client_write_audio_chunk(ctx,
                                               (const uint8_t *)data->data_ptr,
                                               data->data_len,
                                               message_done) != ESP_OK) {
                ESP_LOGE(VOICE_CLIENT_TAG, "audio continuation write failed");
            }
            if (message_done) {
                ctx->binary_payload_active = false;
            }
        } else {
            ESP_LOGW(VOICE_CLIENT_TAG, "ignoring continuation frame without active binary payload");
        }
        break;
    case VOICE_CLIENT_OPCODE_CLOSE:
        ESP_LOGI(VOICE_CLIENT_TAG, "close frame received");
        break;
    case VOICE_CLIENT_OPCODE_PING:
        ESP_LOGD(VOICE_CLIENT_TAG, "ping frame received");
        break;
    case VOICE_CLIENT_OPCODE_PONG:
        ESP_LOGD(VOICE_CLIENT_TAG, "pong frame received");
        break;
    default:
        ESP_LOGW(VOICE_CLIENT_TAG,
                 "unsupported websocket opcode=0x%x data_len=%d payload_len=%d",
                 data->op_code,
                 data->data_len,
                 data->payload_len);
        break;
    }
}

void voice_client_event_handler(void *handler_args,
                                esp_event_base_t base,
                                int32_t event_id,
                                void *event_data)
{
    voice_client_context_t *ctx = (voice_client_context_t *)handler_args;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    (void)base;

    switch ((esp_websocket_event_id_t)event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(VOICE_CLIENT_TAG, "connected to %s", CONFIG_ESPESP_VOICE_CLIENT_URI);
        if (ctx != NULL) {
            ctx->start_pending = true;
            ctx->session_started = false;
        }
        if (ctx != NULL && ctx->event_group != NULL) {
            xEventGroupClearBits(ctx->event_group, VOICE_CLIENT_ERROR_BIT);
            xEventGroupSetBits(ctx->event_group, VOICE_CLIENT_CONNECTED_BIT);
        }
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(VOICE_CLIENT_TAG, "disconnected from %s", CONFIG_ESPESP_VOICE_CLIENT_URI);
        if (ctx != NULL) {
            ctx->session_started = false;
            ctx->playback_streaming = false;
            ctx->playback_pcm = false;
            ctx->binary_payload_active = false;
            ctx->awaiting_tts_end = false;
            ctx->has_pending_byte = false;
            ctx->warned_drop_binary = false;
            if (ctx->aec != NULL) {
                voice_client_aec_playback_end(ctx->aec);
            }
        }
        if (ctx != NULL && ctx->event_group != NULL) {
            xEventGroupClearBits(ctx->event_group, VOICE_CLIENT_CONNECTED_BIT);
        }
        break;
    case WEBSOCKET_EVENT_DATA:
        voice_client_handle_data(ctx, data);
        break;
    case WEBSOCKET_EVENT_ERROR:
        if (data != NULL) {
            ESP_LOGE(VOICE_CLIENT_TAG,
                     "websocket error type=%d status=%d sock_errno=%d tls_err=0x%x",
                     data->error_handle.error_type,
                     data->error_handle.esp_ws_handshake_status_code,
                     data->error_handle.esp_transport_sock_errno,
                     data->error_handle.esp_tls_last_esp_err);
        } else {
            ESP_LOGE(VOICE_CLIENT_TAG, "websocket client error");
        }
        if (ctx != NULL && ctx->event_group != NULL) {
            xEventGroupSetBits(ctx->event_group, VOICE_CLIENT_ERROR_BIT);
        }
        break;
    case WEBSOCKET_EVENT_CLOSED:
        ESP_LOGI(VOICE_CLIENT_TAG, "websocket closed by peer");
        if (ctx != NULL && ctx->event_group != NULL) {
            xEventGroupClearBits(ctx->event_group, VOICE_CLIENT_CONNECTED_BIT);
        }
        break;
    case WEBSOCKET_EVENT_BEFORE_CONNECT:
        ESP_LOGD(VOICE_CLIENT_TAG, "websocket before connect");
        break;
    case WEBSOCKET_EVENT_BEGIN:
        ESP_LOGD(VOICE_CLIENT_TAG, "websocket transport begin");
        break;
    case WEBSOCKET_EVENT_FINISH:
        ESP_LOGD(VOICE_CLIENT_TAG, "websocket transport finish");
        break;
    default:
        ESP_LOGD(VOICE_CLIENT_TAG, "websocket event id=%" PRId32, event_id);
        break;
    }
}

esp_err_t voice_client_send_audio_frame(esp_websocket_client_handle_t client,
                                        voice_client_context_t *ctx,
                                        const int16_t *pcm,
                                        size_t sample_count)
{
    if (client == NULL || ctx == NULL || pcm == NULL || sample_count == 0) {
        return ESP_OK;
    }

    size_t byte_count = sample_count * sizeof(int16_t);
    if (byte_count > INT32_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    int sent = esp_websocket_client_send_bin(client,
                                             (const char *)pcm,
                                             (int)byte_count,
                                             pdMS_TO_TICKS(CONFIG_ESPESP_VOICE_CLIENT_NETWORK_TIMEOUT_MS));
    if (sent != (int)byte_count) {
        ctx->mic_dropped_chunks++;
        ESP_LOGW(VOICE_CLIENT_TAG,
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

void voice_client_log_mic_progress(voice_client_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    if (now_us - ctx->last_mic_stats_us < VOICE_CLIENT_STATS_PERIOD_US) {
        return;
    }

    ctx->last_mic_stats_us = now_us;
    ESP_LOGI(VOICE_CLIENT_TAG,
             "mic stream chunks=%" PRIu64 " sent=%" PRIu64 " dropped=%" PRIu64
             " playback=%s free_heap=%" PRIu32,
             ctx->mic_sent_chunks,
             ctx->mic_sent_bytes,
             ctx->mic_dropped_chunks,
             ctx->playback_streaming ? "true" : "false",
             esp_get_free_heap_size());
}
