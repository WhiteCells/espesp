#include "websocket_client/websocket_client_transport.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "sdkconfig.h"
#include "websocket_client/websocket_client_context.h"
#include "websocket_client/websocket_client_messages.h"

bool websocket_client_uri_is_valid(const char *uri)
{
    return uri != NULL &&
           (strncmp(uri, "ws://", strlen("ws://")) == 0 ||
            strncmp(uri, "wss://", strlen("wss://")) == 0);
}

esp_err_t websocket_client_make_headers(char *headers, size_t headers_len)
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
        ESP_LOGE(WEBSOCKET_CLIENT_TAG, "websocket client auth token is too long");
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static void websocket_client_handle_connected(websocket_client_context_t *ctx)
{
    ESP_LOGI(WEBSOCKET_CLIENT_TAG, "connected to %s", CONFIG_ESPESP_WS_CLIENT_URI);
    if (ctx != NULL && ctx->event_group != NULL) {
        xEventGroupClearBits(ctx->event_group, WEBSOCKET_CLIENT_ERROR_BIT);
        xEventGroupSetBits(ctx->event_group, WEBSOCKET_CLIENT_CONNECTED_BIT);
    }
}

static void websocket_client_handle_disconnected(websocket_client_context_t *ctx)
{
    ESP_LOGW(WEBSOCKET_CLIENT_TAG, "disconnected from %s", CONFIG_ESPESP_WS_CLIENT_URI);
    if (ctx != NULL && ctx->event_group != NULL) {
        xEventGroupClearBits(ctx->event_group, WEBSOCKET_CLIENT_CONNECTED_BIT);
    }
}

static void websocket_client_handle_error(websocket_client_context_t *ctx,
                                          const esp_websocket_event_data_t *data)
{
    if (data != NULL) {
        ESP_LOGE(WEBSOCKET_CLIENT_TAG,
                 "websocket error type=%d status=%d sock_errno=%d tls_err=0x%x",
                 data->error_handle.error_type,
                 data->error_handle.esp_ws_handshake_status_code,
                 data->error_handle.esp_transport_sock_errno,
                 data->error_handle.esp_tls_last_esp_err);
    } else {
        ESP_LOGE(WEBSOCKET_CLIENT_TAG, "websocket client error");
    }

    if (ctx != NULL && ctx->event_group != NULL) {
        xEventGroupSetBits(ctx->event_group, WEBSOCKET_CLIENT_ERROR_BIT);
    }
}

void websocket_client_event_handler(void *handler_args,
                                    esp_event_base_t base,
                                    int32_t event_id,
                                    void *event_data)
{
    websocket_client_context_t *ctx = (websocket_client_context_t *)handler_args;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    (void)base;

    switch ((esp_websocket_event_id_t)event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        websocket_client_handle_connected(ctx);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        websocket_client_handle_disconnected(ctx);
        break;
    case WEBSOCKET_EVENT_DATA:
        websocket_client_log_incoming_data(data, ctx);
        break;
    case WEBSOCKET_EVENT_ERROR:
        websocket_client_handle_error(ctx, data);
        break;
    case WEBSOCKET_EVENT_CLOSED:
        ESP_LOGI(WEBSOCKET_CLIENT_TAG, "websocket closed by peer");
        if (ctx != NULL && ctx->event_group != NULL) {
            xEventGroupClearBits(ctx->event_group, WEBSOCKET_CLIENT_CONNECTED_BIT);
        }
        break;
    case WEBSOCKET_EVENT_BEFORE_CONNECT:
        ESP_LOGD(WEBSOCKET_CLIENT_TAG, "websocket before connect");
        break;
    case WEBSOCKET_EVENT_BEGIN:
        ESP_LOGD(WEBSOCKET_CLIENT_TAG, "websocket transport begin");
        break;
    case WEBSOCKET_EVENT_FINISH:
        ESP_LOGD(WEBSOCKET_CLIENT_TAG, "websocket transport finish");
        break;
    default:
        ESP_LOGD(WEBSOCKET_CLIENT_TAG, "websocket event id=%" PRId32, event_id);
        break;
    }
}
