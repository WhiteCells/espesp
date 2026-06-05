#include "chat/chat_playback.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "chat/chat_audio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CHAT_PLAYBACK_STOP_LEN ((size_t)UINT32_MAX)
#define CHAT_PLAYBACK_STREAM_END_LEN ((size_t)(UINT32_MAX - 1U))
#define CHAT_PLAYBACK_WORK_SAMPLES 256U

static void chat_playback_free_chunk(chat_playback_chunk_t *chunk)
{
    if (chunk == NULL) {
        return;
    }
    free(chunk->data);
    chunk->data = NULL;
    chunk->len = 0;
    chunk->message_done = false;
}

static uint32_t chat_abs_i16(int16_t sample)
{
    return sample == INT16_MIN ? 32768U : (uint32_t)abs(sample);
}

static int16_t chat_load_s16le(const uint8_t *data)
{
    uint16_t raw = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    return (int16_t)raw;
}

static void chat_store_s16le(uint8_t *data, int16_t sample)
{
    uint16_t raw = (uint16_t)sample;
    data[0] = (uint8_t)(raw & 0xff);
    data[1] = (uint8_t)(raw >> 8);
}

static int16_t chat_process_playback_sample(chat_context_t *ctx, int16_t sample)
{
    int32_t scaled = ((int32_t)sample * CONFIG_ESPESP_CHAT_TTS_VOLUME_PERCENT) / 100;
    int32_t limit = ((int32_t)INT16_MAX * CONFIG_ESPESP_CHAT_TTS_LIMIT_PERCENT) / 100;
    uint32_t input_peak = chat_abs_i16(sample);

    if (limit <= 0 || limit > INT16_MAX) {
        limit = INT16_MAX;
    }

    if (scaled > limit) {
        scaled = limit;
        ctx->playback_limited_samples++;
    } else if (scaled < -limit) {
        scaled = -limit;
        ctx->playback_limited_samples++;
    }

    int16_t processed = (int16_t)scaled;
    uint32_t output_peak = chat_abs_i16(processed);
    if (input_peak > ctx->playback_input_peak) {
        ctx->playback_input_peak = input_peak;
    }
    if (output_peak > ctx->playback_output_peak) {
        ctx->playback_output_peak = output_peak;
    }
    ctx->playback_samples++;
    return processed;
}

static esp_err_t chat_write_all_i2s(chat_context_t *ctx, const uint8_t *data, size_t len)
{
    size_t total_written = 0;
    while (total_written < len) {
        size_t bytes_written = 0;
        esp_err_t ret = i2s_channel_write(ctx->tx_channel,
                                          data + total_written,
                                          len - total_written,
                                          &bytes_written,
                                          pdMS_TO_TICKS(CONFIG_ESPESP_CHAT_I2S_WRITE_TIMEOUT_MS));
        if (ret != ESP_OK) {
            return ret;
        }
        if (bytes_written == 0) {
            return ESP_ERR_TIMEOUT;
        }
        total_written += bytes_written;
    }

    ctx->playback_written_bytes += total_written;
    return ESP_OK;
}

static esp_err_t chat_write_processed_pcm_i2s(chat_context_t *ctx, const uint8_t *data, size_t len)
{
    if (ctx == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((len % CHAT_SAMPLE_WIDTH_BYTES) != 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t output[CHAT_PLAYBACK_WORK_SAMPLES * CHAT_SAMPLE_WIDTH_BYTES];
    int16_t ref_samples[CHAT_PLAYBACK_WORK_SAMPLES];
    const uint8_t *cursor = data;
    size_t remaining = len;

    while (remaining > 0) {
        size_t sample_count = remaining / CHAT_SAMPLE_WIDTH_BYTES;
        if (sample_count > CHAT_PLAYBACK_WORK_SAMPLES) {
            sample_count = CHAT_PLAYBACK_WORK_SAMPLES;
        }

        for (size_t i = 0; i < sample_count; i++) {
            int16_t sample = chat_load_s16le(cursor + i * CHAT_SAMPLE_WIDTH_BYTES);
            sample = chat_process_playback_sample(ctx, sample);
            ref_samples[i] = sample;
            chat_store_s16le(output + i * CHAT_SAMPLE_WIDTH_BYTES, sample);
        }

        if (ctx->aec != NULL) {
            (void)voice_client_aec_feed_reference(ctx->aec, ref_samples, sample_count);
        }

        size_t bytes_to_write = sample_count * CHAT_SAMPLE_WIDTH_BYTES;
        ESP_RETURN_ON_ERROR(chat_write_all_i2s(ctx, output, bytes_to_write),
                            CHAT_TAG,
                            "write TTS PCM");
        cursor += bytes_to_write;
        remaining -= bytes_to_write;
    }

    return ESP_OK;
}

static void chat_log_playback_progress(chat_context_t *ctx)
{
    int64_t now_us = esp_timer_get_time();
    if (ctx->last_playback_stats_us != 0 &&
        now_us - ctx->last_playback_stats_us < CHAT_STATS_PERIOD_US) {
        return;
    }

    ctx->last_playback_stats_us = now_us;
    ESP_LOGI(CHAT_TAG,
             "playback chunks=%" PRIu32 " received=%" PRIu64 " written=%" PRIu64
             " queued=%u dropped=%" PRIu64 " sample_rate=%" PRIu32
             " peak_in=%" PRIu32 " peak_out=%" PRIu32 " limited=%" PRIu64,
             ctx->playback_chunks,
             ctx->playback_received_bytes,
             ctx->playback_written_bytes,
             (unsigned int)uxQueueMessagesWaiting(ctx->playback_queue),
             ctx->playback_dropped_chunks,
             ctx->output_sample_rate_hz,
             ctx->playback_input_peak,
             ctx->playback_output_peak,
             ctx->playback_limited_samples);
}

static bool chat_playback_generation_active(chat_context_t *ctx, uint32_t generation)
{
    return ctx != NULL &&
           ctx->playback_queue != NULL &&
           ctx->playback_task != NULL &&
           ctx->playback_streaming &&
           ctx->playback_pcm &&
           ctx->playback_generation == generation;
}

static void chat_playback_finish_stream(chat_context_t *ctx, const char *reason)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->playback_lock != NULL) {
        xSemaphoreTake(ctx->playback_lock, portMAX_DELAY);
    }

    int64_t elapsed_ms = ctx->playback_started_us > 0 ?
                         (esp_timer_get_time() - ctx->playback_started_us) / 1000 :
                         0;

    ctx->playback_streaming = false;
    ctx->playback_pcm = false;
    ctx->binary_payload_active = false;
    ctx->warned_drop_binary = false;
    ctx->has_pending_byte = false;

    if (ctx->aec != NULL) {
        voice_client_aec_playback_end(ctx->aec);
    }

    if (ctx->playback_lock != NULL) {
        xSemaphoreGive(ctx->playback_lock);
    }

    ESP_LOGI(CHAT_TAG,
             "tts playback end reason=%s received=%" PRIu64 " written=%" PRIu64
             " chunks=%" PRIu32 " peak_in=%" PRIu32 " peak_out=%" PRIu32
             " limited=%" PRIu64 "/%" PRIu64 " elapsed_ms=%" PRId64,
             reason != NULL ? reason : "done",
             ctx->playback_received_bytes,
             ctx->playback_written_bytes,
             ctx->playback_chunks,
             ctx->playback_input_peak,
             ctx->playback_output_peak,
             ctx->playback_limited_samples,
             ctx->playback_samples,
             elapsed_ms);
}

static esp_err_t chat_playback_send_queue_item(chat_context_t *ctx,
                                               chat_playback_chunk_t *chunk,
                                               uint32_t generation,
                                               const char *full_log)
{
    TickType_t wait_ticks = pdMS_TO_TICKS(CONFIG_ESPESP_CHAT_PLAYBACK_QUEUE_SEND_TIMEOUT_MS);
    if (wait_ticks == 0) {
        wait_ticks = 1;
    }

    int64_t last_log_us = esp_timer_get_time();
    while (chat_playback_generation_active(ctx, generation)) {
        if (xQueueSend(ctx->playback_queue, chunk, wait_ticks) == pdTRUE) {
            return ESP_OK;
        }

        int64_t now_us = esp_timer_get_time();
        if (now_us - last_log_us >= CHAT_STATS_PERIOD_US) {
            last_log_us = now_us;
            ESP_LOGW(CHAT_TAG,
                     "%s queued=%u written=%" PRIu64 " received=%" PRIu64,
                     full_log != NULL ? full_log : "playback queue full, applying websocket backpressure",
                     (unsigned int)uxQueueMessagesWaiting(ctx->playback_queue),
                     ctx->playback_written_bytes,
                     ctx->playback_received_bytes);
        }
    }

    return ESP_ERR_INVALID_STATE;
}

static esp_err_t chat_playback_write_chunk(chat_context_t *ctx, chat_playback_chunk_t *chunk)
{
    if (ctx == NULL || chunk == NULL || chunk->data == NULL || chunk->len == 0) {
        return ESP_OK;
    }

    if (ctx->playback_lock != NULL) {
        xSemaphoreTake(ctx->playback_lock, portMAX_DELAY);
    }

    bool can_play = ctx->playback_streaming && ctx->playback_pcm;
    esp_err_t ret = ESP_OK;
    if (!can_play) {
        ctx->playback_dropped_chunks++;
        goto done;
    }

    uint8_t *cursor = chunk->data;
    size_t remaining = chunk->len;

    if (ctx->has_pending_byte && remaining > 0) {
        uint8_t sample[2] = { ctx->pending_byte, cursor[0] };
        ctx->has_pending_byte = false;
        cursor++;
        remaining--;
        ret = chat_write_processed_pcm_i2s(ctx, sample, sizeof(sample));
        if (ret != ESP_OK) {
            goto done;
        }
    }

    if ((remaining % CHAT_SAMPLE_WIDTH_BYTES) != 0) {
        ctx->pending_byte = cursor[remaining - 1];
        ctx->has_pending_byte = true;
        remaining--;
    }

    if (remaining > 0) {
        ret = chat_write_processed_pcm_i2s(ctx, cursor, remaining);
        if (ret != ESP_OK) {
            goto done;
        }
    }

    if (chunk->message_done && ctx->has_pending_byte) {
        ESP_LOGW(CHAT_TAG, "dropping odd trailing byte at end of binary audio frame");
        ctx->has_pending_byte = false;
    }

    chat_log_playback_progress(ctx);

done:
    if (ctx->playback_lock != NULL) {
        xSemaphoreGive(ctx->playback_lock);
    }
    return ret;
}

static void chat_playback_task(void *arg)
{
    chat_context_t *ctx = (chat_context_t *)arg;
    chat_playback_chunk_t chunk = { 0 };

    while (true) {
        if (xQueueReceive(ctx->playback_queue, &chunk, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (chunk.len == CHAT_PLAYBACK_STOP_LEN) {
            chat_playback_free_chunk(&chunk);
            break;
        }

        if (chunk.len == CHAT_PLAYBACK_STREAM_END_LEN) {
            chat_playback_finish_stream(ctx, "tts_done");
            chat_playback_free_chunk(&chunk);
            continue;
        }

        esp_err_t ret = chat_playback_write_chunk(ctx, &chunk);
        if (ret != ESP_OK) {
            ESP_LOGW(CHAT_TAG, "playback write failed: %s", esp_err_to_name(ret));
        }
        chat_playback_free_chunk(&chunk);
    }

    ctx->playback_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t chat_playback_start_task(chat_context_t *ctx)
{
    if (ctx == NULL || ctx->playback_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    BaseType_t ok = xTaskCreate(chat_playback_task,
                                "chat_playback",
                                CONFIG_ESPESP_CHAT_PLAYBACK_TASK_STACK_SIZE,
                                ctx,
                                5,
                                &ctx->playback_task);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void chat_playback_stop_task(chat_context_t *ctx)
{
    if (ctx == NULL || ctx->playback_queue == NULL || ctx->playback_task == NULL) {
        return;
    }

    chat_playback_interrupt(ctx, "stop playback task");

    chat_playback_chunk_t stop = {
        .data = NULL,
        .len = CHAT_PLAYBACK_STOP_LEN,
        .message_done = true,
    };
    if (xQueueSend(ctx->playback_queue, &stop, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(CHAT_TAG, "failed to send playback task stop marker");
        return;
    }

    for (int i = 0; i < 50 && ctx->playback_task != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t chat_playback_begin(chat_context_t *ctx, uint32_t sample_rate_hz)
{
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (sample_rate_hz == 0) {
        sample_rate_hz = CONFIG_ESPESP_CHAT_SPK_SAMPLE_RATE_HZ;
    }

    if (ctx->playback_lock != NULL) {
        xSemaphoreTake(ctx->playback_lock, portMAX_DELAY);
    }

    esp_err_t ret = chat_audio_set_output_sample_rate(ctx, sample_rate_hz);
    if (ret != ESP_OK) {
        if (ctx->playback_lock != NULL) {
            xSemaphoreGive(ctx->playback_lock);
        }
        return ret;
    }

    ctx->playback_streaming = true;
    ctx->playback_pcm = true;
    ctx->binary_payload_active = false;
    ctx->warned_drop_binary = false;
    ctx->has_pending_byte = false;
    ctx->pending_byte = 0;
    ctx->playback_received_bytes = 0;
    ctx->playback_written_bytes = 0;
    ctx->playback_dropped_chunks = 0;
    ctx->playback_samples = 0;
    ctx->playback_limited_samples = 0;
    ctx->playback_chunks = 0;
    ctx->playback_input_peak = 0;
    ctx->playback_output_peak = 0;
    ctx->playback_started_us = esp_timer_get_time();
    ctx->last_playback_stats_us = ctx->playback_started_us;
    ctx->playback_generation++;

    if (ctx->aec != NULL) {
        voice_client_aec_set_speaker_rate(ctx->aec, sample_rate_hz);
        voice_client_aec_playback_start(ctx->aec);
    }

    if (ctx->playback_lock != NULL) {
        xSemaphoreGive(ctx->playback_lock);
    }

    ESP_LOGI(CHAT_TAG,
             "tts playback begin sample_rate=%" PRIu32 " volume=%d%% limit=%d%%",
             sample_rate_hz,
             CONFIG_ESPESP_CHAT_TTS_VOLUME_PERCENT,
             CONFIG_ESPESP_CHAT_TTS_LIMIT_PERCENT);
    return ESP_OK;
}

static void chat_playback_wait_until_finished(chat_context_t *ctx, uint32_t generation)
{
    if (ctx == NULL || ctx->playback_queue == NULL) {
        return;
    }

    int64_t last_log_us = esp_timer_get_time();
    while (chat_playback_generation_active(ctx, generation)) {
        int64_t now_us = esp_timer_get_time();
        if (now_us - last_log_us >= CHAT_STATS_PERIOD_US) {
            last_log_us = now_us;
            ESP_LOGI(CHAT_TAG,
                     "waiting for playback finish queued=%u written=%" PRIu64 "/%" PRIu64,
                     (unsigned int)uxQueueMessagesWaiting(ctx->playback_queue),
                     ctx->playback_written_bytes,
                     ctx->playback_received_bytes);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void chat_playback_end(chat_context_t *ctx, const char *reason)
{
    if (ctx == NULL) {
        return;
    }

    uint32_t generation = ctx->playback_generation;
    if (!chat_playback_generation_active(ctx, generation)) {
        return;
    }

    chat_playback_chunk_t end = {
        .data = NULL,
        .len = CHAT_PLAYBACK_STREAM_END_LEN,
        .message_done = true,
    };
    if (chat_playback_send_queue_item(ctx,
                                      &end,
                                      generation,
                                      "playback queue full, waiting to enqueue tts_done") == ESP_OK) {
        chat_playback_wait_until_finished(ctx, generation);
    }
}

void chat_playback_interrupt(chat_context_t *ctx, const char *reason)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->playback_lock != NULL) {
        xSemaphoreTake(ctx->playback_lock, portMAX_DELAY);
    }

    if (ctx->playback_queue != NULL) {
        chat_playback_chunk_t chunk = { 0 };
        while (xQueueReceive(ctx->playback_queue, &chunk, 0) == pdTRUE) {
            chat_playback_free_chunk(&chunk);
            ctx->playback_dropped_chunks++;
        }
        xQueueReset(ctx->playback_queue);
    }

    ctx->playback_streaming = false;
    ctx->playback_pcm = false;
    ctx->binary_payload_active = false;
    ctx->warned_drop_binary = false;
    ctx->has_pending_byte = false;
    ctx->playback_generation++;

    if (ctx->aec != NULL) {
        voice_client_aec_playback_end(ctx->aec);
    }

    if (ctx->tx_channel != NULL && ctx->tx_enabled) {
        esp_err_t ret = i2s_channel_disable(ctx->tx_channel);
        if (ret == ESP_OK) {
            ret = i2s_channel_enable(ctx->tx_channel);
        }
        if (ret != ESP_OK) {
            ESP_LOGW(CHAT_TAG, "reset speaker DMA failed during interrupt: %s", esp_err_to_name(ret));
        }
        ctx->tx_enabled = true;
    }

    if (ctx->playback_lock != NULL) {
        xSemaphoreGive(ctx->playback_lock);
    }

    ESP_LOGI(CHAT_TAG, "playback interrupted: %s", reason != NULL ? reason : "barge-in");
}

esp_err_t chat_playback_enqueue_audio(chat_context_t *ctx,
                                      const uint8_t *data,
                                      int data_len,
                                      bool message_done)
{
    if (ctx == NULL || data == NULL || data_len <= 0) {
        return ESP_OK;
    }

    if (!ctx->playback_streaming || !ctx->playback_pcm) {
        if (!ctx->warned_drop_binary) {
            ESP_LOGW(CHAT_TAG, "dropping binary audio because no active pcm tts_start was received");
            ctx->warned_drop_binary = true;
        }
        ctx->playback_dropped_chunks++;
        return ESP_OK;
    }

    uint8_t *copy = heap_caps_malloc((size_t)data_len, MALLOC_CAP_8BIT);
    if (copy == NULL) {
        ctx->playback_dropped_chunks++;
        return ESP_ERR_NO_MEM;
    }
    memcpy(copy, data, (size_t)data_len);

    chat_playback_chunk_t chunk = {
        .data = copy,
        .len = (size_t)data_len,
        .message_done = message_done,
    };

    uint32_t generation = ctx->playback_generation;
    if (chat_playback_send_queue_item(ctx,
                                      &chunk,
                                      generation,
                                      "playback queue full, applying websocket backpressure") == ESP_OK) {
        ctx->playback_received_bytes += (uint64_t)data_len;
        ctx->playback_chunks++;
        return ESP_OK;
    }

    chat_playback_free_chunk(&chunk);
    ctx->playback_dropped_chunks++;
    return ESP_OK;
}
