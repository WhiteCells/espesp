#include "speaker_client/speaker_client.h"

#include <inttypes.h>
#include <stdbool.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "speaker_client/speaker_client_audio.h"
#include "speaker_client/speaker_client_context.h"
#include "speaker_client/speaker_client_transport.h"
#include "wifi_station/wifi_station.h"

const char *SPEAKER_CLIENT_TAG = "speaker_client";

static void speaker_client_cleanup(speaker_client_context_t *ctx,
                                   esp_websocket_client_handle_t client,
                                   bool client_started)
{
    if (client != NULL) {
        if (client_started) {
            esp_err_t stop_ret = esp_websocket_client_stop(client);
            if (stop_ret != ESP_OK) {
                ESP_LOGW(SPEAKER_CLIENT_TAG,
                         "stop websocket client failed during cleanup: %s",
                         esp_err_to_name(stop_ret));
            }
        }
        esp_websocket_client_destroy(client);
    }

    if (ctx != NULL && ctx->tx_channel != NULL) {
        esp_err_t disable_ret = ESP_OK;
        if (ctx->tx_enabled) {
            disable_ret = i2s_channel_disable(ctx->tx_channel);
        }
        if (disable_ret != ESP_OK) {
            ESP_LOGW(SPEAKER_CLIENT_TAG,
                     "disable I2S channel failed during cleanup: %s",
                     esp_err_to_name(disable_ret));
        }
        i2s_del_channel(ctx->tx_channel);
        ctx->tx_channel = NULL;
    }

    if (ctx != NULL && ctx->event_group != NULL) {
        vEventGroupDelete(ctx->event_group);
        ctx->event_group = NULL;
    }
}

esp_err_t speaker_client_run(void)
{
    if (!speaker_client_uri_is_valid(CONFIG_ESPESP_SPEAKER_CLIENT_URI)) {
        ESP_LOGE(SPEAKER_CLIENT_TAG, "speaker client URI must start with ws:// or wss://");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(wifi_station_connect(), SPEAKER_CLIENT_TAG, "connect Wi-Fi");
    esp_err_t ps_ret = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ps_ret != ESP_OK) {
        ESP_LOGW(SPEAKER_CLIENT_TAG, "disable Wi-Fi power save failed: %s", esp_err_to_name(ps_ret));
    }

    speaker_client_context_t ctx = {
        .event_group = xEventGroupCreate(),
        .tx_channel = NULL,
        .tx_enabled = false,
        .streaming = false,
        .binary_payload_active = false,
        .warned_drop_without_stream = false,
        .has_pending_byte = false,
        .pending_byte = 0,
        .sample_rate_hz = CONFIG_ESPESP_SPK_SAMPLE_RATE_HZ,
        .ramp_total_samples = 0,
        .ramp_in_remaining = 0,
        .expected_frames = 0,
        .received_bytes = 0,
        .written_bytes = 0,
        .declick_written_bytes = 0,
        .received_chunks = 0,
        .last_output_sample = 0,
        .has_last_output_sample = false,
        .stream_started_us = 0,
        .last_stats_us = 0,
    };
    if (ctx.event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_websocket_client_handle_t client = NULL;
    bool client_started = false;
    esp_err_t ret = ESP_OK;

    ret = speaker_client_create_i2s_channel(&ctx.tx_channel);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ESP_LOGI(SPEAKER_CLIENT_TAG,
             "I2S speaker client: BCLK=GPIO%d, WS=GPIO%d, DOUT=GPIO%d, sample_rate=%d Hz, TX starts muted",
             CONFIG_ESPESP_SPK_BCLK_GPIO,
             CONFIG_ESPESP_SPK_WS_GPIO,
             CONFIG_ESPESP_SPK_DOUT_GPIO,
             CONFIG_ESPESP_SPK_SAMPLE_RATE_HZ);

    char headers[SPEAKER_CLIENT_AUTH_HEADER_MAX];
    ret = speaker_client_make_headers(headers, sizeof(headers));
    if (ret != ESP_OK) {
        goto cleanup;
    }

    esp_websocket_client_config_t config = {
        .uri = CONFIG_ESPESP_SPEAKER_CLIENT_URI,
        .headers = headers[0] != '\0' ? headers : NULL,
        .buffer_size = CONFIG_ESPESP_SPEAKER_CLIENT_BUFFER_SIZE,
        .task_stack = CONFIG_ESPESP_SPEAKER_CLIENT_TASK_STACK_SIZE,
        .network_timeout_ms = CONFIG_ESPESP_SPEAKER_CLIENT_NETWORK_TIMEOUT_MS,
        .reconnect_timeout_ms = CONFIG_ESPESP_SPEAKER_CLIENT_RECONNECT_TIMEOUT_MS,
        .ping_interval_sec = CONFIG_ESPESP_SPEAKER_CLIENT_PING_INTERVAL_SEC,
        .user_context = &ctx,
    };

    client = esp_websocket_client_init(&config);
    if (client == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    ret = esp_websocket_register_events(client,
                                        WEBSOCKET_EVENT_ANY,
                                        speaker_client_event_handler,
                                        &ctx);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ESP_LOGI(SPEAKER_CLIENT_TAG, "connect uri=%s", CONFIG_ESPESP_SPEAKER_CLIENT_URI);
    ret = esp_websocket_client_start(client);
    if (ret != ESP_OK) {
        goto cleanup;
    }
    client_started = true;

    EventBits_t bits = xEventGroupWaitBits(ctx.event_group,
                                           SPEAKER_CLIENT_CONNECTED_BIT | SPEAKER_CLIENT_ERROR_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(CONFIG_ESPESP_SPEAKER_CLIENT_CONNECT_TIMEOUT_MS));
    if ((bits & SPEAKER_CLIENT_CONNECTED_BIT) == 0) {
        ESP_LOGE(SPEAKER_CLIENT_TAG, "speaker WebSocket connect timeout or error");
        ret = (bits & SPEAKER_CLIENT_ERROR_BIT) ? ESP_FAIL : ESP_ERR_TIMEOUT;
        goto cleanup;
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_ESPESP_SPEAKER_CLIENT_STATUS_PERIOD_MS));

        if (!esp_websocket_client_is_connected(client)) {
            ESP_LOGW(SPEAKER_CLIENT_TAG,
                     "waiting for speaker_server reconnect, received=%" PRIu64 " written=%" PRIu64,
                     ctx.received_bytes,
                     ctx.written_bytes);
            continue;
        }

        ret = speaker_client_send_status(client, &ctx);
        if (ret != ESP_OK) {
            ESP_LOGW(SPEAKER_CLIENT_TAG, "status publish failed: %s", esp_err_to_name(ret));
        }
    }

cleanup:
    speaker_client_cleanup(&ctx, client, client_started);
    return ret;
}
