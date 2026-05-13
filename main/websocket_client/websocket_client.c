#include "websocket_client/websocket_client.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "sdkconfig.h"
#include "wifi_station/wifi_station.h"

#define WS_CLIENT_CONNECTED_BIT BIT0
#define WS_CLIENT_ERROR_BIT BIT1
#define WS_CLIENT_AUTH_HEADER_MAX 256
#define WS_CLIENT_BINARY_PREVIEW_BYTES 16
#define WS_CLIENT_OPCODE_CONTINUATION 0x0
#define WS_CLIENT_OPCODE_TEXT 0x1
#define WS_CLIENT_OPCODE_BINARY 0x2
#define WS_CLIENT_OPCODE_CLOSE 0x8
#define WS_CLIENT_OPCODE_PING 0x9
#define WS_CLIENT_OPCODE_PONG 0xA

typedef struct {
    EventGroupHandle_t event_group;
    uint32_t received_frames;
} websocket_client_context_t;

static const char *TAG = "websocket_client";

static bool websocket_client_uri_is_valid(const char *uri)
{
    return uri != NULL &&
           (strncmp(uri, "ws://", strlen("ws://")) == 0 ||
            strncmp(uri, "wss://", strlen("wss://")) == 0);
}

static esp_err_t websocket_client_make_headers(char *headers, size_t headers_len)
{
    if (headers == NULL || headers_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    headers[0] = '\0';
    if (CONFIG_ESPESP_WS_CLIENT_AUTH_TOKEN[0] == '\0') {
        return ESP_OK;
    }

    int written = snprintf(headers,
                           headers_len,
                           "Authorization: Bearer %s\r\n",
                           CONFIG_ESPESP_WS_CLIENT_AUTH_TOKEN);
    if (written < 0 || written >= (int)headers_len) {
        ESP_LOGE(TAG, "websocket client auth token is too long");
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static void websocket_client_binary_preview(const char *data, int len, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return;
    }

    if (data == NULL || len <= 0) {
        snprintf(out, out_len, "(empty)");
        return;
    }

    int preview_len = len < WS_CLIENT_BINARY_PREVIEW_BYTES ? len : WS_CLIENT_BINARY_PREVIEW_BYTES;
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

static void websocket_client_log_data(const esp_websocket_event_data_t *data,
                                      websocket_client_context_t *ctx)
{
    if (data == NULL) {
        ESP_LOGW(TAG, "websocket data event without payload metadata");
        return;
    }

    if (ctx != NULL) {
        ctx->received_frames++;
    }

    int payload_offset = data->payload_offset;
    int payload_len = data->payload_len;
    int data_len = data->data_len;

    switch (data->op_code) {
    case WS_CLIENT_OPCODE_TEXT:
        ESP_LOGI(TAG,
                 "text frame chunk offset=%d data_len=%d payload_len=%d payload=%.*s",
                 payload_offset,
                 data_len,
                 payload_len,
                 data_len,
                 data->data_ptr != NULL ? data->data_ptr : "");
        break;
    case WS_CLIENT_OPCODE_BINARY: {
        char preview[WS_CLIENT_BINARY_PREVIEW_BYTES * 3 + 8];
        websocket_client_binary_preview(data->data_ptr, data_len, preview, sizeof(preview));
        ESP_LOGI(TAG,
                 "binary frame chunk offset=%d data_len=%d payload_len=%d preview=%s",
                 payload_offset,
                 data_len,
                 payload_len,
                 preview);
        break;
    }
    case WS_CLIENT_OPCODE_CONTINUATION:
        ESP_LOGI(TAG, "continuation frame chunk offset=%d data_len=%d payload_len=%d",
                 payload_offset, data_len, payload_len);
        break;
    case WS_CLIENT_OPCODE_CLOSE:
        ESP_LOGI(TAG, "close frame received");
        break;
    case WS_CLIENT_OPCODE_PING:
        ESP_LOGD(TAG, "ping frame received");
        break;
    case WS_CLIENT_OPCODE_PONG:
        ESP_LOGD(TAG, "pong frame received");
        break;
    default:
        ESP_LOGW(TAG,
                 "unsupported websocket opcode=0x%x data_len=%d payload_len=%d",
                 data->op_code,
                 data_len,
                 payload_len);
        break;
    }
}

static void websocket_client_event_handler(void *handler_args,
                                           esp_event_base_t base,
                                           int32_t event_id,
                                           void *event_data)
{
    websocket_client_context_t *ctx = (websocket_client_context_t *)handler_args;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    (void)base;

    switch ((esp_websocket_event_id_t)event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected to %s", CONFIG_ESPESP_WS_CLIENT_URI);
        if (ctx != NULL && ctx->event_group != NULL) {
            xEventGroupClearBits(ctx->event_group, WS_CLIENT_ERROR_BIT);
            xEventGroupSetBits(ctx->event_group, WS_CLIENT_CONNECTED_BIT);
        }
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected from %s", CONFIG_ESPESP_WS_CLIENT_URI);
        if (ctx != NULL && ctx->event_group != NULL) {
            xEventGroupClearBits(ctx->event_group, WS_CLIENT_CONNECTED_BIT);
        }
        break;
    case WEBSOCKET_EVENT_DATA:
        websocket_client_log_data(data, ctx);
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "websocket client error");
        if (ctx != NULL && ctx->event_group != NULL) {
            xEventGroupSetBits(ctx->event_group, WS_CLIENT_ERROR_BIT);
        }
        break;
    case WEBSOCKET_EVENT_CLOSED:
        ESP_LOGI(TAG, "websocket closed by peer");
        if (ctx != NULL && ctx->event_group != NULL) {
            xEventGroupClearBits(ctx->event_group, WS_CLIENT_CONNECTED_BIT);
        }
        break;
    case WEBSOCKET_EVENT_BEFORE_CONNECT:
        ESP_LOGD(TAG, "websocket before connect");
        break;
    case WEBSOCKET_EVENT_BEGIN:
        ESP_LOGD(TAG, "websocket transport begin");
        break;
    case WEBSOCKET_EVENT_FINISH:
        ESP_LOGD(TAG, "websocket transport finish");
        break;
    default:
        ESP_LOGD(TAG, "websocket event id=%" PRId32, event_id);
        break;
    }
}

static esp_err_t websocket_client_send_text(esp_websocket_client_handle_t client,
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
    if (sent < 0) {
        ESP_LOGW(TAG, "send text failed: payload=%s", payload);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "sent text len=%d payload=%s", len, payload);
    return ESP_OK;
}

static esp_err_t websocket_client_send_status(esp_websocket_client_handle_t client,
                                              uint32_t sequence)
{
    char payload[192];
    int written = snprintf(payload,
                           sizeof(payload),
                           "{\"type\":\"status\",\"service\":\"websocket_client\",\"seq\":%" PRIu32 ",\"free_heap\":%" PRIu32 ",\"uptime_ms\":%" PRId64 "}",
                           sequence,
                           esp_get_free_heap_size(),
                           esp_timer_get_time() / 1000);
    if (written < 0 || written >= (int)sizeof(payload)) {
        ESP_LOGE(TAG, "status payload too large");
        return ESP_FAIL;
    }

    int sent = esp_websocket_client_send_text(client,
                                              payload,
                                              written,
                                              pdMS_TO_TICKS(CONFIG_ESPESP_WS_CLIENT_NETWORK_TIMEOUT_MS));
    if (sent < 0) {
        ESP_LOGW(TAG, "send status failed: seq=%" PRIu32, sequence);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "sent status seq=%" PRIu32, sequence);
    return ESP_OK;
}

esp_err_t websocket_client_run(void)
{
    if (!websocket_client_uri_is_valid(CONFIG_ESPESP_WS_CLIENT_URI)) {
        ESP_LOGE(TAG, "WebSocket client URI must start with ws:// or wss://");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(wifi_station_connect());

    websocket_client_context_t ctx = {
        .event_group = xEventGroupCreate(),
        .received_frames = 0,
    };
    if (ctx.event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    char headers[WS_CLIENT_AUTH_HEADER_MAX];
    esp_err_t ret = websocket_client_make_headers(headers, sizeof(headers));
    if (ret != ESP_OK) {
        vEventGroupDelete(ctx.event_group);
        return ret;
    }

    esp_websocket_client_config_t config = {
        .uri = CONFIG_ESPESP_WS_CLIENT_URI,
        .headers = headers[0] != '\0' ? headers : NULL,
        .buffer_size = CONFIG_ESPESP_WS_CLIENT_BUFFER_SIZE,
        .task_stack = CONFIG_ESPESP_WS_CLIENT_TASK_STACK_SIZE,
        .network_timeout_ms = CONFIG_ESPESP_WS_CLIENT_NETWORK_TIMEOUT_MS,
        .reconnect_timeout_ms = CONFIG_ESPESP_WS_CLIENT_RECONNECT_TIMEOUT_MS,
        .ping_interval_sec = CONFIG_ESPESP_WS_CLIENT_PING_INTERVAL_SEC,
        .user_context = &ctx,
    };

    esp_websocket_client_handle_t client = esp_websocket_client_init(&config);
    if (client == NULL) {
        vEventGroupDelete(ctx.event_group);
        return ESP_ERR_NO_MEM;
    }

    ret = esp_websocket_register_events(client,
                                        WEBSOCKET_EVENT_ANY,
                                        websocket_client_event_handler,
                                        &ctx);
    if (ret != ESP_OK) {
        esp_websocket_client_destroy(client);
        vEventGroupDelete(ctx.event_group);
        return ret;
    }

    ESP_LOGI(TAG, "connect uri=%s", CONFIG_ESPESP_WS_CLIENT_URI);
    ret = esp_websocket_client_start(client);
    if (ret != ESP_OK) {
        esp_websocket_client_destroy(client);
        vEventGroupDelete(ctx.event_group);
        return ret;
    }

    EventBits_t bits = xEventGroupWaitBits(ctx.event_group,
                                           WS_CLIENT_CONNECTED_BIT | WS_CLIENT_ERROR_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(CONFIG_ESPESP_WS_CLIENT_CONNECT_TIMEOUT_MS));
    if ((bits & WS_CLIENT_CONNECTED_BIT) == 0) {
        ESP_LOGE(TAG, "WebSocket connect timeout or error");
        esp_websocket_client_stop(client);
        esp_websocket_client_destroy(client);
        vEventGroupDelete(ctx.event_group);
        return (bits & WS_CLIENT_ERROR_BIT) ? ESP_FAIL : ESP_ERR_TIMEOUT;
    }

    ret = websocket_client_send_text(client, CONFIG_ESPESP_WS_CLIENT_INITIAL_PAYLOAD);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "initial payload failed, continuing");
    }

    uint32_t sequence = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_ESPESP_WS_CLIENT_PUBLISH_PERIOD_MS));

        if (!esp_websocket_client_is_connected(client)) {
            ESP_LOGW(TAG, "waiting for WebSocket reconnect, received_frames=%" PRIu32,
                     ctx.received_frames);
            continue;
        }

        ret = websocket_client_send_status(client, sequence++);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "status publish failed: %s", esp_err_to_name(ret));
        }
    }

    return ESP_OK;
}
