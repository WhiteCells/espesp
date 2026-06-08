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

#ifndef CONFIG_ESPESP_CHAT_PRE_SPEECH_MS
#define CONFIG_ESPESP_CHAT_PRE_SPEECH_MS 500
#endif

#ifndef CONFIG_ESPESP_CHAT_BARGE_IN_MIN_AVG_ABS
#define CONFIG_ESPESP_CHAT_BARGE_IN_MIN_AVG_ABS 1400
#endif

#ifndef CONFIG_ESPESP_CHAT_BARGE_IN_MIN_PEAK
#define CONFIG_ESPESP_CHAT_BARGE_IN_MIN_PEAK 4200
#endif

#ifndef CONFIG_ESPESP_CHAT_BARGE_IN_CLEAN_MIN_AVG_ABS
#define CONFIG_ESPESP_CHAT_BARGE_IN_CLEAN_MIN_AVG_ABS 650
#endif

#ifndef CONFIG_ESPESP_CHAT_BARGE_IN_CLEAN_MIN_PEAK
#define CONFIG_ESPESP_CHAT_BARGE_IN_CLEAN_MIN_PEAK 2200
#endif

#ifndef CONFIG_ESPESP_CHAT_BARGE_IN_CLEAN_TO_RAW_PERCENT
#define CONFIG_ESPESP_CHAT_BARGE_IN_CLEAN_TO_RAW_PERCENT 35
#endif

#ifndef CONFIG_ESPESP_CHAT_BARGE_IN_CONFIRM_MS
#define CONFIG_ESPESP_CHAT_BARGE_IN_CONFIRM_MS 96
#endif

#ifndef CONFIG_ESPESP_CHAT_AEC_REFERENCE_DELAY_MS
#define CONFIG_ESPESP_CHAT_AEC_REFERENCE_DELAY_MS 30
#endif

#ifndef CONFIG_ESPESP_CHAT_BARGE_IN_WEAK_MIN_AVG_ABS
#define CONFIG_ESPESP_CHAT_BARGE_IN_WEAK_MIN_AVG_ABS 60
#endif

#ifndef CONFIG_ESPESP_CHAT_BARGE_IN_WEAK_MIN_PEAK
#define CONFIG_ESPESP_CHAT_BARGE_IN_WEAK_MIN_PEAK 300
#endif

#ifndef CONFIG_ESPESP_CHAT_BARGE_IN_WEAK_CONFIRM_MS
#define CONFIG_ESPESP_CHAT_BARGE_IN_WEAK_CONFIRM_MS 256
#endif

#ifndef CONFIG_ESPESP_CHAT_BARGE_IN_UPLOAD_CLEAN_MS
#define CONFIG_ESPESP_CHAT_BARGE_IN_UPLOAD_CLEAN_MS 160
#endif

#ifndef CONFIG_ESPESP_CHAT_TURN_END_SILENCE_MS
#define CONFIG_ESPESP_CHAT_TURN_END_SILENCE_MS 400
#endif

#ifndef CONFIG_ESPESP_CHAT_MIC_HIGHPASS_FILTER_ENABLED
#define CONFIG_ESPESP_CHAT_MIC_HIGHPASS_FILTER_ENABLED 1
#endif

#ifndef CONFIG_ESPESP_CHAT_MIC_HIGHPASS_ALPHA_Q15
#define CONFIG_ESPESP_CHAT_MIC_HIGHPASS_ALPHA_Q15 31200
#endif

#ifndef CONFIG_ESPESP_CHAT_MIC_INPUT_LIMIT_PERCENT
#define CONFIG_ESPESP_CHAT_MIC_INPUT_LIMIT_PERCENT 85
#endif

static uint32_t chat_abs_i32_local(int32_t sample)
{
    return sample == INT32_MIN ? (uint32_t)INT32_MAX : (uint32_t)abs(sample);
}

static uint32_t chat_abs_i16_local(int16_t sample)
{
    return sample == INT16_MIN ? 32768U : (uint32_t)abs(sample);
}

static int16_t chat_saturate_i16(int32_t sample)
{
    if (sample > INT16_MAX) {
        return INT16_MAX;
    }
    if (sample < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)sample;
}

static void chat_convert_mic_frame(chat_context_t *ctx,
                                   const int32_t *raw_samples,
                                   int16_t *pcm_samples,
                                   size_t sample_count)
{
    if (ctx == NULL || raw_samples == NULL || pcm_samples == NULL || sample_count == 0) {
        return;
    }

    uint32_t input_peak = 0;
    uint64_t clipped_samples = 0;
    for (size_t i = 0; i < sample_count; i++) {
        int32_t pcm = raw_samples[i] >> CONFIG_ESPESP_CHAT_MIC_SAMPLE_SHIFT_BITS;
        uint32_t magnitude = chat_abs_i32_local(pcm);
        if (magnitude > input_peak) {
            input_peak = magnitude;
        }
        if (pcm > INT16_MAX || pcm < INT16_MIN) {
            clipped_samples++;
        }
    }

    int32_t limit = ((int32_t)INT16_MAX * CONFIG_ESPESP_CHAT_MIC_INPUT_LIMIT_PERCENT) / 100;
    if (limit <= 0 || limit > INT16_MAX) {
        limit = INT16_MAX;
    }

    int32_t desired_gain_q15 = 32768;
    bool frame_limited = input_peak > (uint32_t)limit;
    if (frame_limited && input_peak > 0) {
        desired_gain_q15 = (int32_t)(((int64_t)limit * 32768LL) / input_peak);
        if (desired_gain_q15 < 1) {
            desired_gain_q15 = 1;
        }
    }

    int32_t current_gain_q15 = ctx->mic_input_gain_q15;
    if (current_gain_q15 <= 0) {
        current_gain_q15 = 32768;
    }

    if (desired_gain_q15 < current_gain_q15) {
        current_gain_q15 = desired_gain_q15;
    } else if (desired_gain_q15 > current_gain_q15) {
        int32_t delta = desired_gain_q15 - current_gain_q15;
        current_gain_q15 += delta > 8 ? delta / 8 : delta;
    }
    ctx->mic_input_gain_q15 = current_gain_q15;

    uint32_t pcm_peak = 0;
    for (size_t i = 0; i < sample_count; i++) {
        int32_t pcm = raw_samples[i] >> CONFIG_ESPESP_CHAT_MIC_SAMPLE_SHIFT_BITS;
        int32_t scaled = (int32_t)(((int64_t)pcm * current_gain_q15) >> 15);
        int16_t sample = chat_saturate_i16(scaled);
        pcm_samples[i] = sample;
        uint32_t magnitude = chat_abs_i16_local(sample);
        if (magnitude > pcm_peak) {
            pcm_peak = magnitude;
        }
    }

    ctx->mic_input_peak = input_peak;
    ctx->mic_pcm_peak = pcm_peak;
    ctx->mic_input_clipped_samples += clipped_samples;
    if (frame_limited) {
        ctx->mic_input_limited_frames++;
    }
}

static void chat_highpass_filter(chat_context_t *ctx, int16_t *samples, size_t sample_count)
{
    if (ctx == NULL || samples == NULL || sample_count == 0) {
        return;
    }

#if CONFIG_ESPESP_CHAT_MIC_HIGHPASS_FILTER_ENABLED
    int32_t prev_input = ctx->highpass_prev_input;
    int32_t prev_output = ctx->highpass_prev_output;

    for (size_t i = 0; i < sample_count; i++) {
        int32_t input = samples[i];
        int32_t filtered = input - prev_input +
                           (int32_t)(((int64_t)prev_output *
                                      CONFIG_ESPESP_CHAT_MIC_HIGHPASS_ALPHA_Q15) >> 15);
        prev_input = input;
        if (filtered > INT16_MAX) {
            filtered = INT16_MAX;
        } else if (filtered < INT16_MIN) {
            filtered = INT16_MIN;
        }
        prev_output = filtered;
        samples[i] = (int16_t)filtered;
    }

    ctx->highpass_prev_input = prev_input;
    ctx->highpass_prev_output = prev_output;
#endif
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

static uint32_t chat_frame_duration_ms(size_t sample_count, uint32_t sample_rate_hz)
{
    if (sample_count == 0 || sample_rate_hz == 0) {
        return 1;
    }

    uint32_t duration_ms = (uint32_t)(((uint64_t)sample_count * 1000U) / sample_rate_hz);
    return duration_ms > 0 ? duration_ms : 1U;
}

static bool chat_barge_clean_ratio_pass(uint32_t clean_avg_abs,
                                        uint32_t clean_peak,
                                        uint32_t raw_avg_abs,
                                        uint32_t raw_peak)
{
    uint32_t percent = CONFIG_ESPESP_CHAT_BARGE_IN_CLEAN_TO_RAW_PERCENT;
    if (percent == 0 || (raw_avg_abs == 0 && raw_peak == 0)) {
        return true;
    }

    bool avg_pass = raw_avg_abs == 0 ||
                    (uint64_t)clean_avg_abs * 100ULL >= (uint64_t)raw_avg_abs * percent;
    bool peak_pass = raw_peak == 0 ||
                     (uint64_t)clean_peak * 100ULL >= (uint64_t)raw_peak * percent;
    return avg_pass && peak_pass;
}

static bool chat_barge_weak_level_pass(uint32_t clean_avg_abs,
                                       uint32_t clean_peak,
                                       uint32_t raw_avg_abs,
                                       uint32_t raw_peak)
{
    uint32_t avg = clean_avg_abs > raw_avg_abs ? clean_avg_abs : raw_avg_abs;
    uint32_t peak = clean_peak > raw_peak ? clean_peak : raw_peak;

    return avg >= CONFIG_ESPESP_CHAT_BARGE_IN_WEAK_MIN_AVG_ABS &&
           peak >= CONFIG_ESPESP_CHAT_BARGE_IN_WEAK_MIN_PEAK;
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
                         int16_t *vad_samples,
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
    free(vad_samples);
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
        .mic_input_gain_q15 = 32768,
        .last_mic_stats_us = esp_timer_get_time(),
    };
    if (ctx.event_group == NULL || ctx.playback_lock == NULL || ctx.playback_queue == NULL) {
        chat_cleanup(&ctx, NULL, NULL, NULL, NULL);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = chat_audio_init_vadnet(&ctx);
    if (ret != ESP_OK) {
        chat_cleanup(&ctx, NULL, NULL, NULL, NULL);
        return ret;
    }

    uint32_t frame_samples = ctx.vad_frame_samples;
    if (frame_samples == 0) {
        chat_cleanup(&ctx, NULL, NULL, NULL, NULL);
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
    } else {
        voice_client_aec_set_reference_delay(ctx.aec,
                                             CONFIG_ESPESP_CHAT_AEC_REFERENCE_DELAY_MS);
    }
#endif

    int32_t *raw_samples = calloc(frame_samples, sizeof(raw_samples[0]));
    int16_t *pcm_samples = calloc(frame_samples, sizeof(pcm_samples[0]));
    int16_t *vad_samples = calloc(frame_samples, sizeof(vad_samples[0]));
    uint32_t prebuffer_frames = chat_prebuffer_frame_count(ctx.input_sample_rate_hz, frame_samples);
    int16_t *prebuffer = NULL;
    if (prebuffer_frames > 0) {
        prebuffer = calloc((size_t)prebuffer_frames * frame_samples, sizeof(prebuffer[0]));
    }
    if (raw_samples == NULL || pcm_samples == NULL || vad_samples == NULL ||
        (prebuffer_frames > 0 && prebuffer == NULL)) {
        chat_cleanup(&ctx, raw_samples, pcm_samples, vad_samples, prebuffer);
        return ESP_ERR_NO_MEM;
    }

    ret = chat_audio_create_rx_channel(&ctx);
    if (ret != ESP_OK) {
        chat_cleanup(&ctx, raw_samples, pcm_samples, vad_samples, prebuffer);
        return ret;
    }

    ret = chat_audio_create_tx_channel(&ctx);
    if (ret != ESP_OK) {
        chat_cleanup(&ctx, raw_samples, pcm_samples, vad_samples, prebuffer);
        return ret;
    }

    ret = i2s_channel_enable(ctx.rx_channel);
    if (ret != ESP_OK) {
        chat_cleanup(&ctx, raw_samples, pcm_samples, vad_samples, prebuffer);
        return ret;
    }

    ret = chat_playback_start_task(&ctx);
    if (ret != ESP_OK) {
        chat_cleanup(&ctx, raw_samples, pcm_samples, vad_samples, prebuffer);
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
    ESP_LOGI(CHAT_TAG,
             "mic highpass=%d alpha_q15=%d input_limit=%d%% speaker_tx=on-demand playback_prio=%d",
             CONFIG_ESPESP_CHAT_MIC_HIGHPASS_FILTER_ENABLED,
             CONFIG_ESPESP_CHAT_MIC_HIGHPASS_ALPHA_Q15,
             CONFIG_ESPESP_CHAT_MIC_INPUT_LIMIT_PERCENT,
             CONFIG_ESPESP_CHAT_PLAYBACK_TASK_PRIORITY);
    ESP_LOGI(CHAT_TAG,
             "barge-in gates: vad_min=%dms strong_confirm=%dms weak_confirm=%dms"
             " strong_raw_avg>=%d strong_raw_peak>=%d strong_clean_avg>=%d"
             " strong_clean_peak>=%d weak_avg>=%d weak_peak>=%d"
             " clean_to_raw>=%d%% aec_ref_delay=%dms clean_upload=%dms"
             " turn_end_hold=%dms",
             CONFIG_ESPESP_CHAT_VADNET_MIN_SPEECH_MS,
             CONFIG_ESPESP_CHAT_BARGE_IN_CONFIRM_MS,
             CONFIG_ESPESP_CHAT_BARGE_IN_WEAK_CONFIRM_MS,
             CONFIG_ESPESP_CHAT_BARGE_IN_MIN_AVG_ABS,
             CONFIG_ESPESP_CHAT_BARGE_IN_MIN_PEAK,
             CONFIG_ESPESP_CHAT_BARGE_IN_CLEAN_MIN_AVG_ABS,
             CONFIG_ESPESP_CHAT_BARGE_IN_CLEAN_MIN_PEAK,
             CONFIG_ESPESP_CHAT_BARGE_IN_WEAK_MIN_AVG_ABS,
             CONFIG_ESPESP_CHAT_BARGE_IN_WEAK_MIN_PEAK,
             CONFIG_ESPESP_CHAT_BARGE_IN_CLEAN_TO_RAW_PERCENT,
             CONFIG_ESPESP_CHAT_AEC_REFERENCE_DELAY_MS,
             CONFIG_ESPESP_CHAT_BARGE_IN_UPLOAD_CLEAN_MS,
             CONFIG_ESPESP_CHAT_TURN_END_SILENCE_MS);

    char headers[CHAT_AUTH_HEADER_MAX];
    ret = chat_make_headers(headers, sizeof(headers));
    if (ret != ESP_OK) {
        chat_cleanup(&ctx, raw_samples, pcm_samples, vad_samples, prebuffer);
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
        chat_cleanup(&ctx, raw_samples, pcm_samples, vad_samples, prebuffer);
        return ESP_ERR_NO_MEM;
    }

    ret = esp_websocket_register_events(ctx.client, WEBSOCKET_EVENT_ANY, chat_event_handler, &ctx);
    if (ret != ESP_OK) {
        chat_cleanup(&ctx, raw_samples, pcm_samples, vad_samples, prebuffer);
        return ret;
    }

    ESP_LOGI(CHAT_TAG, "connect uri=%s", CONFIG_ESPESP_CHAT_URI);
    ret = esp_websocket_client_start(ctx.client);
    if (ret != ESP_OK) {
        chat_cleanup(&ctx, raw_samples, pcm_samples, vad_samples, prebuffer);
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

    bool turn_active = false;
    bool has_vad_state = false;
    bool last_vad_speech = false;
    uint32_t prebuffer_write = 0;
    uint32_t prebuffer_count = 0;
    uint32_t playback_barge_in_ms = 0;
    uint32_t turn_silence_ms = 0;
    int64_t last_barge_gate_log_us = 0;
    uint32_t current_turn_clean_frames_remaining = 0;

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
        chat_convert_mic_frame(&ctx, raw_samples, pcm_samples, sample_count);
        chat_highpass_filter(&ctx, pcm_samples, sample_count);
        memcpy(vad_samples, pcm_samples, sample_count * sizeof(vad_samples[0]));

        for (size_t i = 0; i < sample_count; i++) {
            uint32_t magnitude = chat_abs_i16_local(vad_samples[i]);
            sum_abs += magnitude;
            if (magnitude > peak) {
                peak = magnitude;
            }
        }
        ctx.mic_pcm_peak = peak;
        uint32_t avg_abs = sample_count > 0 ? (uint32_t)(sum_abs / sample_count) : 0;

        bool playback_active = ctx.playback_streaming || ctx.playback_finishing;
        uint32_t frame_ms = chat_frame_duration_ms(sample_count, ctx.input_sample_rate_hz);
        bool aec_processed = false;
        const int16_t *clean_samples = pcm_samples;

        bool need_aec_for_barge_in = playback_active && !turn_active;
        bool need_aec_for_clean_upload = turn_active && current_turn_clean_frames_remaining > 0;
        if (ctx.aec != NULL && (need_aec_for_barge_in || need_aec_for_clean_upload)) {
            aec_processed = voice_client_aec_has_reference(ctx.aec, sample_count);
            (void)voice_client_aec_process_with_adaptation(ctx.aec,
                                                           pcm_samples,
                                                           vad_samples,
                                                           sample_count,
                                                           need_aec_for_barge_in);
            clean_samples = vad_samples;
        }

        /* 正常录音阶段使用近端原始麦克风；播放期间的 barge-in 使用 AEC-clean 后的近端语音。 */
        int16_t *detect_samples = (playback_active && !turn_active && aec_processed) ?
                                  vad_samples :
                                  pcm_samples;
        vad_state_t vad_state = ctx.vadnet_iface->detect(ctx.vadnet_model, detect_samples);
        bool speech_detected = vad_state == VAD_SPEECH;

        uint64_t clean_sum_abs = 0;
        uint32_t clean_peak = 0;
        for (size_t i = 0; i < sample_count; i++) {
            uint32_t clean_magnitude = chat_abs_i16_local(clean_samples[i]);
            clean_sum_abs += clean_magnitude;
            if (clean_magnitude > clean_peak) {
                clean_peak = clean_magnitude;
            }
        }
        uint32_t clean_avg_abs = sample_count > 0 ? (uint32_t)(clean_sum_abs / sample_count) : 0;

        if (playback_active && !turn_active && speech_detected) {
            bool raw_gate = avg_abs >= CONFIG_ESPESP_CHAT_BARGE_IN_MIN_AVG_ABS &&
                            peak >= CONFIG_ESPESP_CHAT_BARGE_IN_MIN_PEAK;
            bool clean_gate = clean_avg_abs >= CONFIG_ESPESP_CHAT_BARGE_IN_CLEAN_MIN_AVG_ABS &&
                              clean_peak >= CONFIG_ESPESP_CHAT_BARGE_IN_CLEAN_MIN_PEAK;
            bool clean_ratio_gate = aec_processed ?
                                    chat_barge_clean_ratio_pass(clean_avg_abs,
                                                                clean_peak,
                                                                avg_abs,
                                                                peak) :
                                    false;
            bool weak_level_gate = chat_barge_weak_level_pass(clean_avg_abs,
                                                              clean_peak,
                                                              avg_abs,
                                                              peak);
            bool strong_gate = aec_processed && raw_gate && clean_gate && clean_ratio_gate;
            bool weak_gate = aec_processed && clean_ratio_gate && weak_level_gate;
            bool barge_gate = strong_gate || weak_gate;
            uint32_t required_confirm_ms = strong_gate ?
                                           CONFIG_ESPESP_CHAT_BARGE_IN_CONFIRM_MS :
                                           CONFIG_ESPESP_CHAT_BARGE_IN_WEAK_CONFIRM_MS;
            if (barge_gate) {
                playback_barge_in_ms += frame_ms;
                if (required_confirm_ms > 0 && playback_barge_in_ms < required_confirm_ms) {
                    int64_t now_us = esp_timer_get_time();
                    if (last_barge_gate_log_us == 0 ||
                        now_us - last_barge_gate_log_us >= CHAT_STATS_PERIOD_US) {
                        last_barge_gate_log_us = now_us;
                        ESP_LOGI(CHAT_TAG,
                                 "confirm VAD during playback path=%s %ums/%u"
                                 " strong=%d weak=%d raw_gate=%d clean_gate=%d"
                                 " weak_level=%d ratio=%d raw_avg=%" PRIu32
                                 " raw_peak=%" PRIu32 " clean_avg=%" PRIu32
                                 " clean_peak=%" PRIu32,
                                 strong_gate ? "strong" : "weak",
                                 playback_barge_in_ms,
                                 required_confirm_ms,
                                 strong_gate ? 1 : 0,
                                 weak_gate ? 1 : 0,
                                 raw_gate ? 1 : 0,
                                 clean_gate ? 1 : 0,
                                 weak_level_gate ? 1 : 0,
                                 clean_ratio_gate ? 1 : 0,
                                 avg_abs,
                                 peak,
                                 clean_avg_abs,
                                 clean_peak);
                    }
                    speech_detected = false;
                } else {
                    ESP_LOGI(CHAT_TAG,
                             "barge-in confirmed path=%s confirm=%ums/%u"
                             " raw_avg=%" PRIu32 " raw_peak=%" PRIu32
                             " clean_avg=%" PRIu32 " clean_peak=%" PRIu32,
                             strong_gate ? "strong" : "weak",
                             playback_barge_in_ms,
                             required_confirm_ms,
                             avg_abs,
                             peak,
                             clean_avg_abs,
                             clean_peak);
                }
            } else {
                int64_t now_us = esp_timer_get_time();
                if (last_barge_gate_log_us == 0 ||
                    now_us - last_barge_gate_log_us >= CHAT_STATS_PERIOD_US) {
                    last_barge_gate_log_us = now_us;
                    ESP_LOGI(CHAT_TAG,
                             "ignore VAD during playback strong=%d weak=%d"
                             " raw=%d clean=%d weak_level=%d ratio=%d"
                             " raw_avg=%" PRIu32 "/%d raw_peak=%" PRIu32 "/%d"
                             " clean_avg=%" PRIu32 "/%d clean_peak=%" PRIu32 "/%d"
                             " weak_avg>=%d weak_peak>=%d aec=%d",
                             strong_gate ? 1 : 0,
                             weak_gate ? 1 : 0,
                             raw_gate ? 1 : 0,
                             clean_gate ? 1 : 0,
                             weak_level_gate ? 1 : 0,
                             clean_ratio_gate ? 1 : 0,
                             avg_abs,
                             CONFIG_ESPESP_CHAT_BARGE_IN_MIN_AVG_ABS,
                             peak,
                             CONFIG_ESPESP_CHAT_BARGE_IN_MIN_PEAK,
                             clean_avg_abs,
                             CONFIG_ESPESP_CHAT_BARGE_IN_CLEAN_MIN_AVG_ABS,
                             clean_peak,
                             CONFIG_ESPESP_CHAT_BARGE_IN_CLEAN_MIN_PEAK,
                             CONFIG_ESPESP_CHAT_BARGE_IN_WEAK_MIN_AVG_ABS,
                             CONFIG_ESPESP_CHAT_BARGE_IN_WEAK_MIN_PEAK,
                             aec_processed ? 1 : 0);
                }
                playback_barge_in_ms = 0;
                speech_detected = false;
            }
        } else if (!speech_detected || !playback_active) {
            playback_barge_in_ms = 0;
        }

        bool state_changed = !has_vad_state || speech_detected != last_vad_speech;
        has_vad_state = true;
        last_vad_speech = speech_detected;

        if (state_changed) {
            ESP_LOGI(CHAT_TAG,
                     "vadnet state=%s avg_abs=%" PRIu32 " peak=%" PRIu32
                     " clean_avg=%" PRIu32 " clean_peak=%" PRIu32,
                     speech_detected ? "speech" : "silence",
                     avg_abs,
                     peak,
                     clean_avg_abs,
                     clean_peak);
        }

        if (speech_detected) {
            turn_silence_ms = 0;

            if (!turn_active) {
                turn_active = true;
                ctx.speech_segments++;
                bool audio_turn_started = false;
                bool barge_in_turn = playback_active;
                if (barge_in_turn && aec_processed &&
                    CONFIG_ESPESP_CHAT_BARGE_IN_UPLOAD_CLEAN_MS > 0) {
                    uint32_t clean_frames =
                        (CONFIG_ESPESP_CHAT_BARGE_IN_UPLOAD_CLEAN_MS + frame_ms - 1U) / frame_ms;
                    current_turn_clean_frames_remaining = clean_frames > 0 ? clean_frames : 1U;
                } else {
                    current_turn_clean_frames_remaining = 0;
                }

                if (barge_in_turn) {
                    ctx.response_floor_turn_id = ctx.turn_id + 1U;
                    chat_playback_interrupt(&ctx, "local VAD barge-in");
                    (void)chat_send_cancel_response(&ctx, "local_vad_barge_in");
                }

                if (chat_is_connected(&ctx)) {
                    ret = chat_send_audio_start(&ctx);
                    if (ret == ESP_OK) {
                        audio_turn_started = true;
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

                if (!audio_turn_started) {
                    turn_active = false;
                    turn_silence_ms = 0;
                    current_turn_clean_frames_remaining = 0;
                    ctx.session_active = false;
                    prebuffer_count = 0;
                    prebuffer_write = 0;
                }
            }

            if (ctx.session_active) {
                bool send_clean_frame = current_turn_clean_frames_remaining > 0 && aec_processed;
                const int16_t *upload_samples = send_clean_frame ? vad_samples : pcm_samples;
                (void)chat_send_audio_frame(&ctx, upload_samples, sample_count);
                if (current_turn_clean_frames_remaining > 0) {
                    current_turn_clean_frames_remaining--;
                }
            }
        } else {
            if (turn_active) {
                if (ctx.session_active) {
                    bool send_clean_frame = current_turn_clean_frames_remaining > 0 && aec_processed;
                    const int16_t *upload_samples = send_clean_frame ? vad_samples : pcm_samples;
                    (void)chat_send_audio_frame(&ctx, upload_samples, sample_count);
                    if (current_turn_clean_frames_remaining > 0) {
                        current_turn_clean_frames_remaining--;
                    }
                }

                if (turn_silence_ms < CONFIG_ESPESP_CHAT_TURN_END_SILENCE_MS) {
                    uint32_t remaining = (uint32_t)CONFIG_ESPESP_CHAT_TURN_END_SILENCE_MS - turn_silence_ms;
                    if (frame_ms >= remaining) {
                        turn_silence_ms = (uint32_t)CONFIG_ESPESP_CHAT_TURN_END_SILENCE_MS;
                    } else {
                        turn_silence_ms += frame_ms;
                    }
                }

                if (turn_silence_ms >= CONFIG_ESPESP_CHAT_TURN_END_SILENCE_MS) {
                    if (ctx.session_active) {
                        (void)chat_send_audio_end(&ctx);
                    }
                    turn_active = false;
                    turn_silence_ms = 0;
                    current_turn_clean_frames_remaining = 0;
                    prebuffer_count = 0;
                    prebuffer_write = 0;
                }
            } else {
                turn_silence_ms = 0;
                const int16_t *prebuffer_samples = (playback_active && aec_processed) ?
                                                   vad_samples :
                                                   pcm_samples;
                chat_prebuffer_store(prebuffer,
                                     prebuffer_frames,
                                     frame_samples,
                                     &prebuffer_write,
                                     &prebuffer_count,
                                     prebuffer_samples);
            }
        }

        chat_log_mic_progress(&ctx, turn_active, avg_abs, peak);
        if (aec_processed && playback_active) {
            vTaskDelay(1);
        }
    }

    chat_cleanup(&ctx, raw_samples, pcm_samples, vad_samples, prebuffer);
    return ret;
}
