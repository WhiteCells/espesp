#include "voice_client/voice_client.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "voice_client/voice_client_audio.h"
#include "voice_client/voice_client_context.h"
#include "voice_client/voice_client_transport.h"
#include "wifi_station/wifi_station.h"

const char *VOICE_CLIENT_TAG = "voice_client";

static void voice_client_cleanup(voice_client_context_t *ctx,
                                 esp_websocket_client_handle_t client,
                                 bool rx_enabled,
                                 int32_t *raw_samples,
                                 int16_t *pcm_samples)
{
    if (client != NULL) {
        esp_websocket_client_stop(client);
        esp_websocket_client_destroy(client);
    }
    if (ctx->tx_channel != NULL) {
        if (ctx->tx_enabled) {
            i2s_channel_disable(ctx->tx_channel);
        }
        i2s_del_channel(ctx->tx_channel);
    }
    if (ctx->rx_channel != NULL) {
        if (rx_enabled) {
            i2s_channel_disable(ctx->rx_channel);
        }
        i2s_del_channel(ctx->rx_channel);
    }
    free(raw_samples);
    free(pcm_samples);
    if (ctx->event_group != NULL) {
        vEventGroupDelete(ctx->event_group);
    }
}

esp_err_t voice_client_run(void)
{
    if (!voice_client_uri_is_valid(CONFIG_ESPESP_VOICE_CLIENT_URI)) {
        ESP_LOGE(VOICE_CLIENT_TAG, "voice client URI must start with ws:// or wss://");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(wifi_station_connect(), VOICE_CLIENT_TAG, "connect Wi-Fi");
    esp_err_t ps_ret = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ps_ret != ESP_OK) {
        ESP_LOGW(VOICE_CLIENT_TAG, "disable Wi-Fi power save failed: %s", esp_err_to_name(ps_ret));
    }

    const size_t frame_samples =
        (CONFIG_ESPESP_VOICE_CLIENT_INPUT_SAMPLE_RATE_HZ * CONFIG_ESPESP_VOICE_CLIENT_FRAME_MS) / 1000;
    if (frame_samples == 0) {
        ESP_LOGE(VOICE_CLIENT_TAG, "voice frame sample count is zero");
        return ESP_ERR_INVALID_ARG;
    }

    voice_client_context_t ctx = {
        .event_group = xEventGroupCreate(),
        .rx_channel = NULL,
        .tx_channel = NULL,
        .tx_enabled = false,
        .start_pending = false,
        .session_started = false,
        .playback_streaming = false,
        .playback_pcm = false,
        .binary_payload_active = false,
        .warned_drop_binary = false,
        .has_pending_byte = false,
        .pending_byte = 0,
        .output_sample_rate_hz = CONFIG_ESPESP_SPK_SAMPLE_RATE_HZ,
        .mic_sent_bytes = 0,
        .mic_sent_chunks = 0,
        .mic_dropped_chunks = 0,
        .tts_received_bytes = 0,
        .tts_written_bytes = 0,
        .tts_samples = 0,
        .tts_limited_samples = 0,
        .tts_input_peak = 0,
        .tts_output_peak = 0,
        .tts_chunks = 0,
        .playback_started_us = 0,
        .last_mic_stats_us = esp_timer_get_time(),
        .last_playback_stats_us = 0,
    };
    if (ctx.event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    int32_t *raw_samples = calloc(frame_samples, sizeof(int32_t));
    int16_t *pcm_samples = calloc(frame_samples, sizeof(int16_t));
    if (raw_samples == NULL || pcm_samples == NULL) {
        voice_client_cleanup(&ctx, NULL, false, raw_samples, pcm_samples);
        return ESP_ERR_NO_MEM;
    }

    esp_websocket_client_handle_t client = NULL;
    bool rx_enabled = false;
    esp_err_t ret = voice_client_create_rx_channel(&ctx.rx_channel);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = voice_client_create_tx_channel(&ctx.tx_channel);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = i2s_channel_enable(ctx.rx_channel);
    if (ret != ESP_OK) {
        goto cleanup;
    }
    rx_enabled = true;

    ret = i2s_channel_enable(ctx.tx_channel);
    if (ret != ESP_OK) {
        goto cleanup;
    }
    ctx.tx_enabled = true;

    ESP_LOGI(VOICE_CLIENT_TAG,
             "I2S microphone: BCLK=GPIO%d WS=GPIO%d DIN=GPIO%d sample_rate=%dHz slot=%s shift=%d frame=%ums/%u samples",
             CONFIG_ESPESP_MIC_BCLK_GPIO,
             CONFIG_ESPESP_MIC_WS_GPIO,
             CONFIG_ESPESP_MIC_DIN_GPIO,
             CONFIG_ESPESP_VOICE_CLIENT_INPUT_SAMPLE_RATE_HZ,
             VOICE_CLIENT_MIC_SLOT_NAME,
             CONFIG_ESPESP_VOICE_CLIENT_MIC_SAMPLE_SHIFT_BITS,
             CONFIG_ESPESP_VOICE_CLIENT_FRAME_MS,
             (unsigned int)frame_samples);
    ESP_LOGI(VOICE_CLIENT_TAG,
             "I2S speaker: BCLK=GPIO%d WS=GPIO%d DOUT=GPIO%d initial_sample_rate=%dHz",
             CONFIG_ESPESP_SPK_BCLK_GPIO,
             CONFIG_ESPESP_SPK_WS_GPIO,
             CONFIG_ESPESP_SPK_DOUT_GPIO,
             CONFIG_ESPESP_SPK_SAMPLE_RATE_HZ);

    char headers[VOICE_CLIENT_AUTH_HEADER_MAX];
    ret = voice_client_make_headers(headers, sizeof(headers));
    if (ret != ESP_OK) {
        goto cleanup;
    }

    esp_websocket_client_config_t config = {
        .uri = CONFIG_ESPESP_VOICE_CLIENT_URI,
        .headers = headers[0] != '\0' ? headers : NULL,
        .buffer_size = CONFIG_ESPESP_VOICE_CLIENT_BUFFER_SIZE,
        .task_stack = CONFIG_ESPESP_VOICE_CLIENT_TASK_STACK_SIZE,
        .network_timeout_ms = CONFIG_ESPESP_VOICE_CLIENT_NETWORK_TIMEOUT_MS,
        .reconnect_timeout_ms = CONFIG_ESPESP_VOICE_CLIENT_RECONNECT_TIMEOUT_MS,
        .ping_interval_sec = CONFIG_ESPESP_VOICE_CLIENT_PING_INTERVAL_SEC,
        .user_context = &ctx,
    };

    client = esp_websocket_client_init(&config);
    if (client == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    ret = esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, voice_client_event_handler, &ctx);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ESP_LOGI(VOICE_CLIENT_TAG, "connect uri=%s", CONFIG_ESPESP_VOICE_CLIENT_URI);
    ret = esp_websocket_client_start(client);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    EventBits_t bits = xEventGroupWaitBits(ctx.event_group,
                                           VOICE_CLIENT_CONNECTED_BIT | VOICE_CLIENT_ERROR_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(CONFIG_ESPESP_VOICE_CLIENT_CONNECT_TIMEOUT_MS));
    if ((bits & VOICE_CLIENT_CONNECTED_BIT) == 0) {
        ESP_LOGE(VOICE_CLIENT_TAG, "voice WebSocket connect timeout or error");
        ret = (bits & VOICE_CLIENT_ERROR_BIT) ? ESP_FAIL : ESP_ERR_TIMEOUT;
        goto cleanup;
    }

    while (true) {
        if (!esp_websocket_client_is_connected(client)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (ctx.start_pending || !ctx.session_started) {
            ret = voice_client_send_start(client, &ctx);
            if (ret != ESP_OK) {
                ESP_LOGW(VOICE_CLIENT_TAG, "start event failed: %s", esp_err_to_name(ret));
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
        }

        size_t bytes_read = 0;
#if CONFIG_ESPESP_VOICE_CLIENT_PAUSE_MIC_DURING_TTS
        if (ctx.playback_streaming) {
            vTaskDelay(pdMS_TO_TICKS(CONFIG_ESPESP_VOICE_CLIENT_FRAME_MS));
            voice_client_log_mic_progress(&ctx);
            continue;
        }
#endif

        ret = i2s_channel_read(ctx.rx_channel,
                               raw_samples,
                               frame_samples * sizeof(raw_samples[0]),
                               &bytes_read,
                               1000);
        if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGW(VOICE_CLIENT_TAG, "I2S microphone read timeout");
            continue;
        }
        if (ret != ESP_OK) {
            ESP_LOGE(VOICE_CLIENT_TAG, "I2S microphone read failed: %s", esp_err_to_name(ret));
            goto cleanup;
        }

        size_t sample_count = bytes_read / sizeof(raw_samples[0]);
        for (size_t i = 0; i < sample_count; i++) {
            pcm_samples[i] = voice_client_convert_sample(raw_samples[i]);
        }

        (void)voice_client_send_audio_frame(client, &ctx, pcm_samples, sample_count);
        voice_client_log_mic_progress(&ctx);
    }

cleanup:
    voice_client_cleanup(&ctx, client, rx_enabled, raw_samples, pcm_samples);
    return ret;
}
