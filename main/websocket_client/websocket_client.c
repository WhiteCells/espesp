#include "websocket_client/websocket_client.h"

#include <inttypes.h>
#include <stdbool.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "websocket_client/websocket_client_context.h"
#include "websocket_client/websocket_client_messages.h"
#include "websocket_client/websocket_client_transport.h"
#include "wifi_station/wifi_station.h"

const char *WEBSOCKET_CLIENT_TAG = "websocket_client";

static void websocket_client_cleanup(websocket_client_context_t *ctx,
                                     esp_websocket_client_handle_t client,
                                     bool client_started)
{
    if (client != NULL) {
        if (client_started) {
            esp_err_t stop_ret = esp_websocket_client_stop(client);
            if (stop_ret != ESP_OK) {
                ESP_LOGW(WEBSOCKET_CLIENT_TAG,
                         "stop websocket client failed during cleanup: %s",
                         esp_err_to_name(stop_ret));
            }
        }
        esp_websocket_client_destroy(client);
    }

    if (ctx != NULL && ctx->event_group != NULL) {
        vEventGroupDelete(ctx->event_group);
        ctx->event_group = NULL;
    }
}

esp_err_t websocket_client_run(void)
{
    if (!websocket_client_uri_is_valid(CONFIG_ESPESP_WS_CLIENT_URI)) {
        ESP_LOGE(WEBSOCKET_CLIENT_TAG, "WebSocket client URI must start with ws:// or wss://");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(wifi_station_connect(), WEBSOCKET_CLIENT_TAG, "connect Wi-Fi");

    websocket_client_context_t ctx = {
        .event_group = xEventGroupCreate(),
        .received_frames = 0,
    };
    if (ctx.event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_websocket_client_handle_t client = NULL;
    bool client_started = false;
    esp_err_t ret = ESP_OK;

    char headers[WEBSOCKET_CLIENT_AUTH_HEADER_MAX];
    ret = websocket_client_make_headers(headers, sizeof(headers));
    if (ret != ESP_OK) {
        goto cleanup;
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

    client = esp_websocket_client_init(&config);
    if (client == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    ret = esp_websocket_register_events(client,
                                        WEBSOCKET_EVENT_ANY,
                                        websocket_client_event_handler,
                                        &ctx);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ESP_LOGI(WEBSOCKET_CLIENT_TAG, "connect uri=%s", CONFIG_ESPESP_WS_CLIENT_URI);
    ret = esp_websocket_client_start(client);
    if (ret != ESP_OK) {
        goto cleanup;
    }
    client_started = true;

    EventBits_t bits = xEventGroupWaitBits(ctx.event_group,
                                           WEBSOCKET_CLIENT_CONNECTED_BIT | WEBSOCKET_CLIENT_ERROR_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(CONFIG_ESPESP_WS_CLIENT_CONNECT_TIMEOUT_MS));
    if ((bits & WEBSOCKET_CLIENT_CONNECTED_BIT) == 0) {
        ESP_LOGE(WEBSOCKET_CLIENT_TAG, "WebSocket connect timeout or error");
        ret = (bits & WEBSOCKET_CLIENT_ERROR_BIT) ? ESP_FAIL : ESP_ERR_TIMEOUT;
        goto cleanup;
    }

    ret = websocket_client_send_initial_payload(client);
    if (ret != ESP_OK) {
        ESP_LOGW(WEBSOCKET_CLIENT_TAG, "initial payload failed, continuing");
    }

    uint32_t sequence = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_ESPESP_WS_CLIENT_PUBLISH_PERIOD_MS));

        if (!esp_websocket_client_is_connected(client)) {
            ESP_LOGW(WEBSOCKET_CLIENT_TAG, "waiting for WebSocket reconnect, received_frames=%" PRIu32,
                     ctx.received_frames);
            continue;
        }

        ret = websocket_client_send_status(client, sequence++);
        if (ret != ESP_OK) {
            ESP_LOGW(WEBSOCKET_CLIENT_TAG, "status publish failed: %s", esp_err_to_name(ret));
        }
    }

cleanup:
    websocket_client_cleanup(&ctx, client, client_started);
    return ret;
}
