#include "speaker_client/speaker_client_transport.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"
#include "speaker_client/speaker_client_audio.h"
#include "speaker_client/speaker_client_protocol.h"

#define SPEAKER_CLIENT_OPCODE_CONTINUATION 0x0
#define SPEAKER_CLIENT_OPCODE_TEXT 0x1
#define SPEAKER_CLIENT_OPCODE_BINARY 0x2
#define SPEAKER_CLIENT_OPCODE_CLOSE 0x8
#define SPEAKER_CLIENT_OPCODE_PING 0x9
#define SPEAKER_CLIENT_OPCODE_PONG 0xA

bool speaker_client_uri_is_valid(const char *uri)
{
    return uri != NULL &&
           (strncmp(uri, "ws://", strlen("ws://")) == 0 ||
            strncmp(uri, "wss://", strlen("wss://")) == 0);
}

esp_err_t speaker_client_make_headers(char *headers, size_t headers_len)
{
    if (headers == NULL || headers_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    headers[0] = '\0';
    if (CONFIG_ESPESP_SPEAKER_CLIENT_AUTH_TOKEN[0] == '\0') {
        return ESP_OK;
    }

    int written = snprintf(headers,
                           headers_len,
                           "Authorization: Bearer %s\r\n",
                           CONFIG_ESPESP_SPEAKER_CLIENT_AUTH_TOKEN);
    if (written < 0 || written >= (int)headers_len) {
        ESP_LOGE(SPEAKER_CLIENT_TAG, "speaker client auth token is too long");
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static void speaker_client_handle_text_event(speaker_client_context_t *ctx,
                                             const esp_websocket_event_data_t *data)
{
    if (data == NULL || data->data_ptr == NULL) {
        return;
    }

    if (data->payload_offset != 0 || data->data_len != data->payload_len) {
        ESP_LOGW(SPEAKER_CLIENT_TAG,
                 "fragmented control text is not supported offset=%d len=%d payload_len=%d",
                 data->payload_offset,
                 data->data_len,
                 data->payload_len);
        return;
    }

    if (data->data_len <= 0 || data->data_len >= SPEAKER_CLIENT_CONTROL_MAX) {
        ESP_LOGW(SPEAKER_CLIENT_TAG, "control text too large len=%d", data->data_len);
        return;
    }

    char text[SPEAKER_CLIENT_CONTROL_MAX];
    memcpy(text, data->data_ptr, (size_t)data->data_len);
    text[data->data_len] = '\0';
    speaker_client_handle_control_text(ctx, text);
}

static void speaker_client_handle_data(speaker_client_context_t *ctx,
                                       const esp_websocket_event_data_t *data)
{
    if (ctx == NULL || data == NULL) {
        return;
    }

    bool message_done = data->payload_len <= 0 ||
                        data->payload_offset + data->data_len >= data->payload_len;

    switch (data->op_code) {
    case SPEAKER_CLIENT_OPCODE_TEXT:
        speaker_client_handle_text_event(ctx, data);
        break;
    case SPEAKER_CLIENT_OPCODE_BINARY:
        if (data->payload_offset == 0) {
            ctx->binary_payload_active = true;
        }
        if (speaker_client_write_audio_chunk(ctx,
                                             (const uint8_t *)data->data_ptr,
                                             data->data_len,
                                             message_done) != ESP_OK) {
            ESP_LOGE(SPEAKER_CLIENT_TAG, "audio chunk write failed");
        }
        if (message_done) {
            ctx->binary_payload_active = false;
        }
        break;
    case SPEAKER_CLIENT_OPCODE_CONTINUATION:
        if (ctx->binary_payload_active) {
            if (speaker_client_write_audio_chunk(ctx,
                                                 (const uint8_t *)data->data_ptr,
                                                 data->data_len,
                                                 message_done) != ESP_OK) {
                ESP_LOGE(SPEAKER_CLIENT_TAG, "audio continuation write failed");
            }
            if (message_done) {
                ctx->binary_payload_active = false;
            }
        } else {
            ESP_LOGW(SPEAKER_CLIENT_TAG, "ignoring continuation frame without active binary payload");
        }
        break;
    case SPEAKER_CLIENT_OPCODE_CLOSE:
        ESP_LOGI(SPEAKER_CLIENT_TAG, "close frame received");
        break;
    case SPEAKER_CLIENT_OPCODE_PING:
        ESP_LOGD(SPEAKER_CLIENT_TAG, "ping frame received");
        break;
    case SPEAKER_CLIENT_OPCODE_PONG:
        ESP_LOGD(SPEAKER_CLIENT_TAG, "pong frame received");
        break;
    default:
        ESP_LOGW(SPEAKER_CLIENT_TAG,
                 "unsupported websocket opcode=0x%x data_len=%d payload_len=%d",
                 data->op_code,
                 data->data_len,
                 data->payload_len);
        break;
    }
}

static void speaker_client_handle_connected(speaker_client_context_t *ctx)
{
    ESP_LOGI(SPEAKER_CLIENT_TAG, "connected to %s", CONFIG_ESPESP_SPEAKER_CLIENT_URI);
    if (ctx != NULL && ctx->event_group != NULL) {
        xEventGroupClearBits(ctx->event_group, SPEAKER_CLIENT_ERROR_BIT);
        xEventGroupSetBits(ctx->event_group, SPEAKER_CLIENT_CONNECTED_BIT);
    }
}

static void speaker_client_handle_disconnected(speaker_client_context_t *ctx)
{
    ESP_LOGW(SPEAKER_CLIENT_TAG, "disconnected from %s", CONFIG_ESPESP_SPEAKER_CLIENT_URI);
    if (ctx != NULL) {
        esp_err_t ret = speaker_client_finish_stream(ctx, "disconnect");
        if (ret != ESP_OK) {
            ESP_LOGW(SPEAKER_CLIENT_TAG, "tail declick failed on disconnect: %s", esp_err_to_name(ret));
        }
    }
    if (ctx != NULL && ctx->event_group != NULL) {
        xEventGroupClearBits(ctx->event_group, SPEAKER_CLIENT_CONNECTED_BIT);
    }
}

static void speaker_client_handle_error(speaker_client_context_t *ctx,
                                        const esp_websocket_event_data_t *data)
{
    if (data != NULL) {
        ESP_LOGE(SPEAKER_CLIENT_TAG,
                 "websocket error type=%d status=%d sock_errno=%d tls_err=0x%x",
                 data->error_handle.error_type,
                 data->error_handle.esp_ws_handshake_status_code,
                 data->error_handle.esp_transport_sock_errno,
                 data->error_handle.esp_tls_last_esp_err);
    } else {
        ESP_LOGE(SPEAKER_CLIENT_TAG, "websocket client error");
    }

    if (ctx != NULL) {
        esp_err_t ret = speaker_client_finish_stream(ctx, "transport_error");
        if (ret != ESP_OK) {
            ESP_LOGW(SPEAKER_CLIENT_TAG, "tail declick failed on transport error: %s", esp_err_to_name(ret));
        }
    }
    if (ctx != NULL && ctx->event_group != NULL) {
        xEventGroupSetBits(ctx->event_group, SPEAKER_CLIENT_ERROR_BIT);
    }
}

void speaker_client_event_handler(void *handler_args,
                                  esp_event_base_t base,
                                  int32_t event_id,
                                  void *event_data)
{
    speaker_client_context_t *ctx = (speaker_client_context_t *)handler_args;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    (void)base;

    switch ((esp_websocket_event_id_t)event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        speaker_client_handle_connected(ctx);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        speaker_client_handle_disconnected(ctx);
        break;
    case WEBSOCKET_EVENT_DATA:
        speaker_client_handle_data(ctx, data);
        break;
    case WEBSOCKET_EVENT_ERROR:
        speaker_client_handle_error(ctx, data);
        break;
    case WEBSOCKET_EVENT_CLOSED:
        ESP_LOGI(SPEAKER_CLIENT_TAG, "websocket closed by peer");
        if (ctx != NULL) {
            esp_err_t ret = speaker_client_finish_stream(ctx, "closed_by_peer");
            if (ret != ESP_OK) {
                ESP_LOGW(SPEAKER_CLIENT_TAG, "tail declick failed on close: %s", esp_err_to_name(ret));
            }
        }
        if (ctx != NULL && ctx->event_group != NULL) {
            xEventGroupClearBits(ctx->event_group, SPEAKER_CLIENT_CONNECTED_BIT);
        }
        break;
    case WEBSOCKET_EVENT_BEFORE_CONNECT:
        ESP_LOGD(SPEAKER_CLIENT_TAG, "websocket before connect");
        break;
    case WEBSOCKET_EVENT_BEGIN:
        ESP_LOGD(SPEAKER_CLIENT_TAG, "websocket transport begin");
        break;
    case WEBSOCKET_EVENT_FINISH:
        ESP_LOGD(SPEAKER_CLIENT_TAG, "websocket transport finish");
        break;
    default:
        ESP_LOGD(SPEAKER_CLIENT_TAG, "websocket event id=%" PRId32, event_id);
        break;
    }
}

esp_err_t speaker_client_send_status(esp_websocket_client_handle_t client,
                                     const speaker_client_context_t *ctx)
{
    if (client == NULL || ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char payload[256];
    int written = snprintf(
        payload,
        sizeof(payload),
        "{\"type\":\"status\",\"service\":\"speaker_client\",\"streaming\":%s,"
        "\"received_bytes\":%" PRIu64 ",\"written_bytes\":%" PRIu64 ",\"declick_bytes\":%" PRIu64
        ",\"chunks\":%" PRIu32 ",\"free_heap\":%" PRIu32 ",\"uptime_ms\":%" PRId64 "}",
        ctx->streaming ? "true" : "false",
        ctx->received_bytes,
        ctx->written_bytes,
        ctx->declick_written_bytes,
        ctx->received_chunks,
        esp_get_free_heap_size(),
        esp_timer_get_time() / 1000);
    if (written < 0 || written >= (int)sizeof(payload)) {
        ESP_LOGE(SPEAKER_CLIENT_TAG, "status payload too large");
        return ESP_FAIL;
    }

    int sent = esp_websocket_client_send_text(client,
                                              payload,
                                              written,
                                              pdMS_TO_TICKS(CONFIG_ESPESP_SPEAKER_CLIENT_NETWORK_TIMEOUT_MS));
    if (sent < 0) {
        ESP_LOGW(SPEAKER_CLIENT_TAG, "send status failed");
        return ESP_FAIL;
    }

    ESP_LOGI(SPEAKER_CLIENT_TAG,
             "status streaming=%s received=%" PRIu64 " written=%" PRIu64
             " declick=%" PRIu64 " chunks=%" PRIu32,
             ctx->streaming ? "true" : "false",
             ctx->received_bytes,
             ctx->written_bytes,
             ctx->declick_written_bytes,
             ctx->received_chunks);
    return ESP_OK;
}
