#include "chat/chat.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "chat/chat_audio.h"
#include "chat/chat_playback.h"
#include "chat/chat_protocol.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "wifi_station/wifi_station.h"

const char *CHAT_TAG = "chat";

static uint32_t chat_abs_i16_local(int16_t sample)
{
    return sample == INT16_MIN ? 32768U : (uint32_t)abs(sample);
}

static uint32_t chat_prebuffer_frame_count(uint32_t sample_rate_hz, uint32_t frame_samples)
{
    if (CONFIG_ESPESP_CHAT_PRE_SPEECH_MS == 0 || sample_rate_hz == 0 || frame_samples == 0) {
        return 0;
    }

    uint32_t samples = (sample_rate_hz * CONFIG_ESPESP_CHAT_PRE_SPEECH_MS) / 1000U;
    uint32_t frames = (samples + frame_samples - 1U) / frame_samples;
    if (frames == 0) {
        frames = 1;
    }
    return frames;
}

static void chat_prebuffer_store(int16_t *prebuffer,
                                 uint32_t prebuffer_frames,
                                 uint32_t frame_samples,
                                 uint32_t *write_index,
                                 uint32_t *count,
                                 const int16_t *pcm)
{
    if (prebuffer == NULL || prebuffer_frames == 0 || frame_samples == 0 ||
        write_index == NULL || count == NULL || pcm == NULL) {
        return;
    }

    memcpy(prebuffer + (size_t)(*write_index) * frame_samples,
           pcm,
           (size_t)frame_samples * sizeof(int16_t));
    *write_index = (*write_index + 1U) % prebuffer_frames;
    if (*count < prebuffer_frames) {
        (*count)++;
    }
}

static esp_err_t chat_prebuffer_send(chat_context_t *ctx,
                                     const int16_t *prebuffer,
                                     uint32_t prebuffer_frames,
                                     uint32_t frame_samples,
                                     uint32_t write_index,
                                     uint32_t count)
{
    if (ctx == NULL || prebuffer == NULL || prebuffer_frames == 0 ||
        frame_samples == 0 || count == 0 || !ctx->session_active) {
        return ESP_OK;
    }

    /* VADNet reports speech after a confirmation window. Send the circular
     * prebuffer oldest-first so the ASR still receives the beginning of speech.
     */
    uint32_t start = (write_index + prebuffer_frames - count) % prebuffer_frames;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t index = (start + i) % prebuffer_frames;
        esp_err_t ret = chat_send_audio_frame(ctx,
                                              prebuffer + (size_t)index * frame_samples,
                                              frame_samples);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    return ESP_OK;
}

static void chat_cleanup(chat_context_t *ctx,
                         int32_t *raw_samples,
                         int16_t *pcm_samples,
                         int16_t *prebuffer)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->client != NULL) {
        esp_websocket_client_stop(ctx->client);
        esp_websocket_client_destroy(ctx->client);
        ctx->client = NULL;
    }

    chat_playback_stop_task(ctx);

    if (ctx->tx_channel != NULL) {
        if (ctx->tx_enabled) {
            (void)i2s_channel_disable(ctx->tx_channel);
            ctx->tx_enabled = false;
        }
        (void)i2s_del_channel(ctx->tx_channel);
        ctx->tx_channel = NULL;
    }

    if (ctx->rx_channel != NULL) {
        (void)i2s_channel_disable(ctx->rx_channel);
        (void)i2s_del_channel(ctx->rx_channel);
        ctx->rx_channel = NULL;
    }

    if (ctx->aec != NULL) {
        voice_client_aec_destroy(ctx->aec);
        ctx->aec = NULL;
    }

    chat_audio_cleanup(ctx);

    if (ctx->playback_queue != NULL) {
        chat_playback_chunk_t chunk = { 0 };
        while (xQueueReceive(ctx->playback_queue, &chunk, 0) == pdTRUE) {
            free(chunk.data);
        }
        vQueueDelete(ctx->playback_queue);
        ctx->playback_queue = NULL;
    }

    if (ctx->playback_lock != NULL) {
        vSemaphoreDelete(ctx->playback_lock);
        ctx->playback_lock = NULL;
    }

    if (ctx->event_group != NULL) {
        vEventGroupDelete(ctx->event_group);
        ctx->event_group = NULL;
    }

    free(raw_samples);
    free(pcm_samples);
    free(prebuffer);
}

static bool chat_is_connected(chat_context_t *ctx)
{
    return ctx != NULL && ctx->client != NULL && esp_websocket_client_is_connected(ctx->client);
}

esp_err_t chat_run(void)
{
    if (!chat_uri_is_valid(CONFIG_ESPESP_CHAT_URI)) {
        ESP_LOGE(CHAT_TAG, "chat URI must start with ws:// or wss://");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(wifi_station_connect(), CHAT_TAG, "connect Wi-Fi");
    esp_err_t ps_ret = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ps_ret != ESP_OK) {
        ESP_LOGW(CHAT_TAG, "disable Wi-Fi power save failed: %s", esp_err_to_name(ps_ret));
    }

    chat_context_t ctx = {
        .event_group = xEventGroupCreate(),
        .playback_lock = xSemaphoreCreateMutex(),
        .playback_queue = xQueueCreate(CONFIG_ESPESP_CHAT_PLAYBACK_QUEUE_LENGTH,
                                       sizeof(chat_playback_chunk_t)),
        .output_sample_rate_hz = CONFIG_ESPESP_CHAT_SPK_SAMPLE_RATE_HZ,
        .server_tts_sample_rate_hz = CONFIG_ESPESP_CHAT_SPK_SAMPLE_RATE_HZ,
        .last_mic_stats_us = esp_timer_get_time(),
    };
    if (ctx.event_group == NULL || ctx.playback_lock == NULL || ctx.playback_queue == NULL) {
        chat_cleanup(&ctx, NULL, NULL, NULL);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = chat_audio_init_vadnet(&ctx);
    if (ret != ESP_OK) {
        chat_cleanup(&ctx, NULL, NULL, NULL);
        return ret;
    }

    uint32_t frame_samples = ctx.vad_frame_samples;
    if (frame_samples == 0) {
        chat_cleanup(&ctx, NULL, NULL, NULL);
        return ESP_ERR_INVALID_ARG;
    }

#if CONFIG_ESPESP_CHAT_AEC_ENABLED
    ctx.aec = voice_client_aec_create(CONFIG_ESPESP_CHAT_AEC_FILTER_LEN,
                                      CONFIG_ESPESP_CHAT_AEC_STEP_SIZE_X256,
                                      ctx.input_sample_rate_hz,
                                      CONFIG_ESPESP_CHAT_SPK_SAMPLE_RATE_HZ,
                                      CONFIG_ESPESP_CHAT_AEC_MAX_DELAY_MS);
    if (ctx.aec == NULL) {
        ESP_LOGW(CHAT_TAG, "AEC create failed, running without echo cancellation");
    }
#endif

    int32_t *raw_samples = calloc(frame_samples, sizeof(raw_samples[0]));
    int16_t *pcm_samples = calloc(frame_samples, sizeof(pcm_samples[0]));
    uint32_t prebuffer_frames = chat_prebuffer_frame_count(ctx.input_sample_rate_hz, frame_samples);
    int16_t *prebuffer = NULL;
    if (prebuffer_frames > 0) {
        prebuffer = calloc((size_t)prebuffer_frames * frame_samples, sizeof(prebuffer[0]));
    }
    if (raw_samples == NULL || pcm_samples == NULL || (prebuffer_frames > 0 && prebuffer == NULL)) {
        chat_cleanup(&ctx, raw_samples, pcm_samples, prebuffer);
        return ESP_ERR_NO_MEM;
    }

    ret = chat_audio_create_rx_channel(&ctx);
    if (ret != ESP_OK) {
        chat_cleanup(&ctx, raw_samples, pcm_samples, prebuffer);
        return ret;
    }

    ret = chat_audio_create_tx_channel(&ctx);
    if (ret != ESP_OK) {
        chat_cleanup(&ctx, raw_samples, pcm_samples, prebuffer);
        return ret;
    }

    ret = i2s_channel_enable(ctx.rx_channel);
    if (ret != ESP_OK) {
        chat_cleanup(&ctx, raw_samples, pcm_samples, prebuffer);
        return ret;
    }

    ret = i2s_channel_enable(ctx.tx_channel);
    if (ret != ESP_OK) {
        chat_cleanup(&ctx, raw_samples, pcm_samples, prebuffer);
        return ret;
    }
    ctx.tx_enabled = true;

    ret = chat_playback_start_task(&ctx);
    if (ret != ESP_OK) {
        chat_cleanup(&ctx, raw_samples, pcm_samples, prebuffer);
        return ret;
    }

    ESP_LOGI(CHAT_TAG,
             "I2S microphone: BCLK=GPIO%d WS=GPIO%d DIN=GPIO%d sample_rate=%" PRIu32
             " slot=%s shift=%d vad_frame=%" PRIu32 " samples prebuffer=%" PRIu32 " frames",
             CONFIG_ESPESP_MIC_BCLK_GPIO,
             CONFIG_ESPESP_MIC_WS_GPIO,
             CONFIG_ESPESP_MIC_DIN_GPIO,
             ctx.input_sample_rate_hz,
             CHAT_MIC_SLOT_NAME,
             CONFIG_ESPESP_CHAT_MIC_SAMPLE_SHIFT_BITS,
             frame_samples,
             prebuffer_frames);
    ESP_LOGI(CHAT_TAG,
             "I2S speaker: BCLK=GPIO%d WS=GPIO%d DOUT=GPIO%d sample_rate=%d",
             CONFIG_ESPESP_SPK_BCLK_GPIO,
             CONFIG_ESPESP_SPK_WS_GPIO,
             CONFIG_ESPESP_SPK_DOUT_GPIO,
             CONFIG_ESPESP_CHAT_SPK_SAMPLE_RATE_HZ);

    char headers[CHAT_AUTH_HEADER_MAX];
    ret = chat_make_headers(headers, sizeof(headers));
    if (ret != ESP_OK) {
        chat_cleanup(&ctx, raw_samples, pcm_samples, prebuffer);
        return ret;
    }

    esp_websocket_client_config_t config = {
        .uri = CONFIG_ESPESP_CHAT_URI,
        .headers = headers[0] != '\0' ? headers : NULL,
        .buffer_size = CONFIG_ESPESP_CHAT_BUFFER_SIZE,
        .task_stack = CONFIG_ESPESP_CHAT_WS_TASK_STACK_SIZE,
        .network_timeout_ms = CONFIG_ESPESP_CHAT_NETWORK_TIMEOUT_MS,
        .reconnect_timeout_ms = CONFIG_ESPESP_CHAT_RECONNECT_TIMEOUT_MS,
        .ping_interval_sec = CONFIG_ESPESP_CHAT_PING_INTERVAL_SEC,
        .user_context = &ctx,
    };

    ctx.client = esp_websocket_client_init(&config);
    if (ctx.client == NULL) {
        chat_cleanup(&ctx, raw_samples, pcm_samples, prebuffer);
        return ESP_ERR_NO_MEM;
    }

    ret = esp_websocket_register_events(ctx.client, WEBSOCKET_EVENT_ANY, chat_event_handler, &ctx);
    if (ret != ESP_OK) {
        chat_cleanup(&ctx, raw_samples, pcm_samples, prebuffer);
        return ret;
    }

    ESP_LOGI(CHAT_TAG, "connect uri=%s", CONFIG_ESPESP_CHAT_URI);
    ret = esp_websocket_client_start(ctx.client);
    if (ret != ESP_OK) {
        chat_cleanup(&ctx, raw_samples, pcm_samples, prebuffer);
        return ret;
    }

    EventBits_t bits = xEventGroupWaitBits(ctx.event_group,
                                           CHAT_CONNECTED_BIT | CHAT_ERROR_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(CONFIG_ESPESP_CHAT_CONNECT_TIMEOUT_MS));
    if ((bits & CHAT_CONNECTED_BIT) == 0) {
        ESP_LOGW(CHAT_TAG,
                 "initial chat WebSocket connection failed or timed out; "
                 "keep running and wait for reconnect");
    }

    ESP_LOGI(CHAT_TAG, "chat listening for VADNet speech; say something to start a turn");

    bool speech_active = false;
    bool has_vad_state = false;
    vad_state_t last_vad_state = VAD_SILENCE;
    uint32_t prebuffer_write = 0;
    uint32_t prebuffer_count = 0;

    while (true) {
        size_t bytes_read = 0;
        ret = i2s_channel_read(ctx.rx_channel,
                               raw_samples,
                               (size_t)frame_samples * sizeof(raw_samples[0]),
                               &bytes_read,
                               CONFIG_ESPESP_CHAT_I2S_READ_TIMEOUT_MS);
        if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGW(CHAT_TAG, "I2S microphone read timeout");
            continue;
        }
        if (ret != ESP_OK) {
            ESP_LOGE(CHAT_TAG, "I2S microphone read failed: %s", esp_err_to_name(ret));
            break;
        }

        size_t sample_count = bytes_read / sizeof(raw_samples[0]);
        if (sample_count != frame_samples) {
            ESP_LOGW(CHAT_TAG,
                     "short I2S frame: expected=%" PRIu32 " samples, got=%u",
                     frame_samples,
                     (unsigned int)sample_count);
            continue;
        }

        uint64_t sum_abs = 0;
        uint32_t peak = 0;
        for (size_t i = 0; i < sample_count; i++) {
            pcm_samples[i] = chat_audio_convert_sample(raw_samples[i]);
        }

        if (ctx.aec != NULL) {
            (void)voice_client_aec_process(ctx.aec, pcm_samples, pcm_samples, sample_count);
        }

        for (size_t i = 0; i < sample_count; i++) {
            uint32_t magnitude = chat_abs_i16_local(pcm_samples[i]);
            sum_abs += magnitude;
            if (magnitude > peak) {
                peak = magnitude;
            }
        }
        uint32_t avg_abs = sample_count > 0 ? (uint32_t)(sum_abs / sample_count) : 0;

        vad_state_t state = ctx.vadnet_iface->detect(ctx.vadnet_model, pcm_samples);
        bool state_changed = !has_vad_state || state != last_vad_state;
        has_vad_state = true;
        last_vad_state = state;

        if (state_changed) {
            ESP_LOGI(CHAT_TAG,
                     "vadnet state=%s avg_abs=%" PRIu32 " peak=%" PRIu32,
                     state == VAD_SPEECH ? "speech" : "silence",
                     avg_abs,
                     peak);
        }

        if (state == VAD_SPEECH) {
            if (!speech_active) {
                speech_active = true;
                ctx.speech_segments++;
                chat_playback_interrupt(&ctx, "local VAD barge-in");
                (void)chat_send_cancel_response(&ctx, "local_vad_barge_in");

                if (chat_is_connected(&ctx)) {
                    ret = chat_send_audio_start(&ctx);
                    if (ret == ESP_OK) {
                        ret = chat_prebuffer_send(&ctx,
                                                  prebuffer,
                                                  prebuffer_frames,
                                                  frame_samples,
                                                  prebuffer_write,
                                                  prebuffer_count);
                        if (ret != ESP_OK) {
                            ESP_LOGW(CHAT_TAG, "prebuffer send failed: %s", esp_err_to_name(ret));
                        }
                    } else {
                        ESP_LOGW(CHAT_TAG, "audio_start failed: %s", esp_err_to_name(ret));
                    }
                } else {
                    ESP_LOGW(CHAT_TAG, "speech detected while websocket is disconnected");
                }
            }

            if (ctx.session_active) {
                (void)chat_send_audio_frame(&ctx, pcm_samples, sample_count);
            }
        } else {
            if (speech_active) {
                if (ctx.session_active) {
                    (void)chat_send_audio_frame(&ctx, pcm_samples, sample_count);
                    (void)chat_send_audio_end(&ctx);
                }
                speech_active = false;
                prebuffer_count = 0;
                prebuffer_write = 0;
            }
            chat_prebuffer_store(prebuffer,
                                 prebuffer_frames,
                                 frame_samples,
                                 &prebuffer_write,
                                 &prebuffer_count,
                                 pcm_samples);
        }

        chat_log_mic_progress(&ctx, speech_active, avg_abs, peak);
    }

    chat_cleanup(&ctx, raw_samples, pcm_samples, prebuffer);
    return ret;
}
