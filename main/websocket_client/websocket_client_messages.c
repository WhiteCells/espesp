#include "websocket_client/websocket_client_messages.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define WEBSOCKET_CLIENT_OPCODE_CONTINUATION 0x0
#define WEBSOCKET_CLIENT_OPCODE_TEXT 0x1
#define WEBSOCKET_CLIENT_OPCODE_BINARY 0x2
#define WEBSOCKET_CLIENT_OPCODE_CLOSE 0x8
#define WEBSOCKET_CLIENT_OPCODE_PING 0x9
#define WEBSOCKET_CLIENT_OPCODE_PONG 0xA

static void websocket_client_binary_preview(const char *data, int len, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return;
    }

    if (data == NULL || len <= 0) {
        snprintf(out, out_len, "(empty)");
        return;
    }

    int preview_len = len < WEBSOCKET_CLIENT_BINARY_PREVIEW_BYTES ? len : WEBSOCKET_CLIENT_BINARY_PREVIEW_BYTES;
    size_t pos = 0;

    for (int i = 0; i < preview_len && pos + 4 < out_len; i++) {
        int written = snprintf(out + pos,
                               out_len - pos,
                               "%02X%s",
                               (unsigned char)data[i],
                               i + 1 < preview_len ? " " : "");
        if (written < 0) {
            break;
        }

        if ((size_t)written >= out_len - pos) {
            pos = out_len - 1;
            break;
        }

        pos += (size_t)written;
    }

    if (len > preview_len && pos + 5 < out_len) {
        snprintf(out + pos, out_len - pos, " ...");
    } else {
        out[pos] = '\0';
    }
}

static esp_err_t websocket_client_send_text_frame(esp_websocket_client_handle_t client,
                                                  const char *payload)
{
    if (client == NULL || payload == NULL || payload[0] == '\0') {
        return ESP_OK;
    }

    int len = strlen(payload);
    int sent = esp_websocket_client_send_text(client,
                                              payload,
                                              len,
                                              pdMS_TO_TICKS(CONFIG_ESPESP_WS_CLIENT_NETWORK_TIMEOUT_MS));
    if (sent != len) {
        ESP_LOGW(WEBSOCKET_CLIENT_TAG, "send text failed: sent=%d expected=%d", sent, len);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t websocket_client_send_initial_payload(esp_websocket_client_handle_t client)
{
    const char *payload = CONFIG_ESPESP_WS_CLIENT_INITIAL_PAYLOAD;
    esp_err_t ret = websocket_client_send_text_frame(client, payload);
    if (ret != ESP_OK) {
        return ret;
    }

    if (payload != NULL && payload[0] != '\0') {
        ESP_LOGI(WEBSOCKET_CLIENT_TAG, "sent initial payload len=%u payload=%s",
                 (unsigned int)strlen(payload),
                 payload);
    }
    return ESP_OK;
}

esp_err_t websocket_client_send_status(esp_websocket_client_handle_t client, uint32_t sequence)
{
    char payload[WEBSOCKET_CLIENT_STATUS_PAYLOAD_MAX];
    int written = snprintf(payload,
                           sizeof(payload),
                           "{\"type\":\"status\",\"service\":\"websocket_client\",\"seq\":%" PRIu32
                           ",\"free_heap\":%" PRIu32 ",\"uptime_ms\":%" PRId64 "}",
                           sequence,
                           esp_get_free_heap_size(),
                           esp_timer_get_time() / 1000);
    if (written < 0 || written >= (int)sizeof(payload)) {
        ESP_LOGE(WEBSOCKET_CLIENT_TAG, "status payload too large");
        return ESP_FAIL;
    }

    esp_err_t ret = websocket_client_send_text_frame(client, payload);
    if (ret != ESP_OK) {
        ESP_LOGW(WEBSOCKET_CLIENT_TAG, "send status failed: seq=%" PRIu32, sequence);
        return ret;
    }

    ESP_LOGI(WEBSOCKET_CLIENT_TAG, "sent status seq=%" PRIu32, sequence);
    return ESP_OK;
}

void websocket_client_log_incoming_data(const esp_websocket_event_data_t *data,
                                        websocket_client_context_t *ctx)
{
    if (data == NULL) {
        ESP_LOGW(WEBSOCKET_CLIENT_TAG, "websocket data event without payload metadata");
        return;
    }

    if (ctx != NULL) {
        ctx->received_frames++;
    }

    switch (data->op_code) {
    case WEBSOCKET_CLIENT_OPCODE_TEXT:
        ESP_LOGI(WEBSOCKET_CLIENT_TAG,
                 "text frame chunk offset=%d data_len=%d payload_len=%d payload=%.*s",
                 data->payload_offset,
                 data->data_len,
                 data->payload_len,
                 data->data_len,
                 data->data_ptr != NULL ? data->data_ptr : "");
        break;
    case WEBSOCKET_CLIENT_OPCODE_BINARY: {
        char preview[WEBSOCKET_CLIENT_BINARY_PREVIEW_BYTES * 3 + 8];
        websocket_client_binary_preview(data->data_ptr, data->data_len, preview, sizeof(preview));
        ESP_LOGI(WEBSOCKET_CLIENT_TAG,
                 "binary frame chunk offset=%d data_len=%d payload_len=%d preview=%s",
                 data->payload_offset,
                 data->data_len,
                 data->payload_len,
                 preview);
        break;
    }
    case WEBSOCKET_CLIENT_OPCODE_CONTINUATION:
        ESP_LOGI(WEBSOCKET_CLIENT_TAG,
                 "continuation frame chunk offset=%d data_len=%d payload_len=%d",
                 data->payload_offset,
                 data->data_len,
                 data->payload_len);
        break;
    case WEBSOCKET_CLIENT_OPCODE_CLOSE:
        ESP_LOGI(WEBSOCKET_CLIENT_TAG, "close frame received");
        break;
    case WEBSOCKET_CLIENT_OPCODE_PING:
        ESP_LOGD(WEBSOCKET_CLIENT_TAG, "ping frame received");
        break;
    case WEBSOCKET_CLIENT_OPCODE_PONG:
        ESP_LOGD(WEBSOCKET_CLIENT_TAG, "pong frame received");
        break;
    default:
        ESP_LOGW(WEBSOCKET_CLIENT_TAG,
                 "unsupported websocket opcode=0x%x data_len=%d payload_len=%d",
                 data->op_code,
                 data->data_len,
                 data->payload_len);
        break;
    }
}
