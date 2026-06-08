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
#define CHAT_PLAYBACK_DECLICK_MS 5U
#define CHAT_PLAYBACK_ABORT_POLL_MS 20U

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

static uint32_t chat_playback_fade_samples(uint32_t sample_rate_hz)
{
    if (sample_rate_hz == 0) {
        sample_rate_hz = CONFIG_ESPESP_CHAT_SPK_SAMPLE_RATE_HZ;
    }

    uint32_t samples = (sample_rate_hz * CHAT_PLAYBACK_DECLICK_MS) / 1000U;
    return samples > 0 ? samples : 1U;
}

static int16_t chat_scale_sample(int16_t sample, uint32_t numerator, uint32_t denominator)
{
    if (denominator == 0 || numerator >= denominator) {
        return sample;
    }

    int32_t scaled = (int32_t)sample * (int32_t)numerator;
    int32_t rounding = (int32_t)(denominator / 2U);
    if (scaled >= 0) {
        scaled += rounding;
    } else {
        scaled -= rounding;
    }
    scaled /= (int32_t)denominator;

    if (scaled > INT16_MAX) {
        scaled = INT16_MAX;
    } else if (scaled < INT16_MIN) {
        scaled = INT16_MIN;
    }
    return (int16_t)scaled;
}

static int16_t chat_apply_playback_fade_in(chat_context_t *ctx, int16_t sample)
{
    if (ctx == NULL || ctx->playback_fade_in_remaining == 0 ||
        ctx->playback_fade_in_total == 0) {
        return sample;
    }

    uint32_t progress = ctx->playback_fade_in_total - ctx->playback_fade_in_remaining;
    sample = chat_scale_sample(sample, progress + 1U, ctx->playback_fade_in_total);
    ctx->playback_fade_in_remaining--;
    return sample;
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

    int16_t processed = chat_apply_playback_fade_in(ctx, (int16_t)scaled);
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

static esp_err_t chat_write_all_i2s(chat_context_t *ctx,
                                    const uint8_t *data,
                                    size_t len,
                                    bool abortable)
{
    size_t total_written = 0;
    int64_t last_progress_us = esp_timer_get_time();
    uint32_t write_timeout_ms = CONFIG_ESPESP_CHAT_I2S_WRITE_TIMEOUT_MS;
    if (write_timeout_ms == 0 || write_timeout_ms > CHAT_PLAYBACK_ABORT_POLL_MS) {
        write_timeout_ms = CHAT_PLAYBACK_ABORT_POLL_MS;
    }

    while (total_written < len) {
        if (abortable && ctx->playback_abort_requested) {
            return ESP_ERR_INVALID_STATE;
        }

        size_t bytes_written = 0;
        esp_err_t ret = i2s_channel_write(ctx->tx_channel,
                                          data + total_written,
                                          len - total_written,
                                          &bytes_written,
                                          write_timeout_ms);
        if (ret == ESP_ERR_TIMEOUT) {
            if (abortable && ctx->playback_abort_requested) {
                return ESP_ERR_INVALID_STATE;
            }
            int64_t now_us = esp_timer_get_time();
            if (now_us - last_progress_us >=
                (int64_t)CONFIG_ESPESP_CHAT_I2S_WRITE_TIMEOUT_MS * 1000LL) {
                return ESP_ERR_TIMEOUT;
            }
            continue;
        } else if (ret != ESP_OK) {
            return ret;
        }

        if (bytes_written == 0) {
            if (abortable && ctx->playback_abort_requested) {
                return ESP_ERR_INVALID_STATE;
            }
            int64_t now_us = esp_timer_get_time();
            if (now_us - last_progress_us >=
                (int64_t)CONFIG_ESPESP_CHAT_I2S_WRITE_TIMEOUT_MS * 1000LL) {
                return ESP_ERR_TIMEOUT;
            }
            continue;
        }

        size_t total_after = total_written + bytes_written;
        size_t aligned_after = total_after - (total_after % CHAT_SAMPLE_WIDTH_BYTES);
        if (aligned_after >= CHAT_SAMPLE_WIDTH_BYTES && aligned_after > total_written) {
            size_t last_sample_offset = aligned_after - CHAT_SAMPLE_WIDTH_BYTES;
            ctx->playback_last_sample = chat_load_s16le(data + last_sample_offset);
            ctx->playback_has_last_sample = true;
        }

        total_written += bytes_written;
        ctx->playback_written_bytes += bytes_written;
        last_progress_us = esp_timer_get_time();
    }

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

        size_t bytes_to_write = sample_count * CHAT_SAMPLE_WIDTH_BYTES;
        esp_err_t ret = chat_write_all_i2s(ctx, output, bytes_to_write, true);
        if (ret != ESP_OK) {
            return ret;
        }
        if (ctx->aec != NULL) {
            (void)voice_client_aec_feed_reference(ctx->aec, ref_samples, sample_count);
        }
        cursor += bytes_to_write;
        remaining -= bytes_to_write;
    }

    return ESP_OK;
}

static esp_err_t chat_write_playback_tail(chat_context_t *ctx, bool allow_while_aborting)
{
    if (ctx == NULL || ctx->tx_channel == NULL || !ctx->tx_enabled ||
        !ctx->playback_has_last_sample || ctx->playback_last_sample == 0 ||
        (!allow_while_aborting && ctx->playback_abort_requested)) {
        return ESP_OK;
    }

    uint32_t fade_samples = chat_playback_fade_samples(ctx->output_sample_rate_hz);
    if (fade_samples < 2U) {
        fade_samples = 2U;
    }

    uint8_t output[CHAT_PLAYBACK_WORK_SAMPLES * CHAT_SAMPLE_WIDTH_BYTES];
    int16_t ref_samples[CHAT_PLAYBACK_WORK_SAMPLES];
    uint32_t produced = 0;

    while (produced < fade_samples) {
        if (!allow_while_aborting && ctx->playback_abort_requested) {
            return ESP_ERR_INVALID_STATE;
        }

        uint32_t sample_count = fade_samples - produced;
        if (sample_count > CHAT_PLAYBACK_WORK_SAMPLES) {
            sample_count = CHAT_PLAYBACK_WORK_SAMPLES;
        }

        for (uint32_t i = 0; i < sample_count; i++) {
            uint32_t ramp_index = produced + i;
            int16_t sample = chat_scale_sample(ctx->playback_last_sample,
                                               (fade_samples - 1U) - ramp_index,
                                               fade_samples - 1U);
            ref_samples[i] = sample;
            chat_store_s16le(output + i * CHAT_SAMPLE_WIDTH_BYTES, sample);
        }

        size_t bytes_to_write = (size_t)sample_count * CHAT_SAMPLE_WIDTH_BYTES;
        ESP_RETURN_ON_ERROR(chat_write_all_i2s(ctx, output, bytes_to_write, !allow_while_aborting),
                            CHAT_TAG,
                            "write TTS tail fade");
        if (ctx->aec != NULL) {
            (void)voice_client_aec_feed_reference(ctx->aec, ref_samples, sample_count);
        }
        produced += sample_count;
    }

    ctx->playback_last_sample = 0;
    ctx->playback_has_last_sample = true;
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
           !ctx->playback_abort_requested &&
           ctx->playback_generation == generation;
}

static bool chat_playback_can_enqueue(chat_context_t *ctx, uint32_t generation)
{
    return ctx != NULL &&
           ctx->playback_queue != NULL &&
           ctx->playback_task != NULL &&
           ctx->playback_streaming &&
           ctx->playback_pcm &&
           !ctx->playback_abort_requested &&
           (!ctx->playback_finishing ||
            ctx->playback_finishing_segment_id != ctx->playback_segment_id) &&
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

    esp_err_t tail_ret = chat_write_playback_tail(ctx, false);
    if (tail_ret != ESP_OK && tail_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(CHAT_TAG, "TTS tail fade failed: %s", esp_err_to_name(tail_ret));
    }

    ctx->playback_streaming = false;
    ctx->playback_pcm = false;
    ctx->playback_finishing = false;
    ctx->playback_abort_requested = false;
    ctx->playback_finishing_segment_id = 0;
    ctx->binary_payload_active = false;
    ctx->warned_drop_binary = false;
    if (ctx->has_pending_byte) {
        ESP_LOGW(CHAT_TAG, "dropping odd trailing byte at end of TTS stream");
        ctx->has_pending_byte = false;
    }

    if (ctx->aec != NULL) {
        voice_client_aec_playback_end(ctx->aec);
    }
    if (ctx->tx_channel != NULL) {
        esp_err_t disable_ret = chat_audio_disable_tx(ctx);
        if (disable_ret != ESP_OK) {
            ESP_LOGW(CHAT_TAG, "disable speaker TX after playback failed: %s", esp_err_to_name(disable_ret));
        }
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
                                               bool allow_finishing,
                                               const char *full_log)
{
    TickType_t wait_ticks = pdMS_TO_TICKS(CONFIG_ESPESP_CHAT_PLAYBACK_QUEUE_SEND_TIMEOUT_MS);
    if (wait_ticks == 0) {
        wait_ticks = 1;
    }

    int64_t last_log_us = esp_timer_get_time();
    while (allow_finishing ?
           chat_playback_generation_active(ctx, generation) :
           chat_playback_can_enqueue(ctx, generation)) {
        if (xQueueSend(ctx->playback_queue, chunk, wait_ticks) == pdTRUE) {
            return ESP_OK;
        }

        int64_t now_us = esp_timer_get_time();
        if (now_us - last_log_us >= CHAT_STATS_PERIOD_US) {
            last_log_us = now_us;
            ESP_LOGW(CHAT_TAG,
                     "%s queued=%u written=%" PRIu64 " received=%" PRIu64,
                     full_log != NULL ? full_log : "playback queue full",
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
    if (!can_play || chunk->generation != ctx->playback_generation) {
        ctx->playback_dropped_chunks++;
        goto done;
    }
    if (ctx->playback_abort_requested) {
        ret = ESP_ERR_INVALID_STATE;
        goto done;
    }

    uint8_t *cursor = chunk->data;
    size_t remaining = chunk->len;

    /* 服务端分片可能拆开一个 16-bit PCM 样本，残留字节要跨整个 TTS 流保留。 */
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
            if (chunk.generation == ctx->playback_generation &&
                chunk.segment_id == ctx->playback_segment_id &&
                ctx->playback_finishing &&
                ctx->playback_finishing_segment_id == chunk.segment_id) {
                chat_playback_finish_stream(ctx, "tts_done");
            }
            chat_playback_free_chunk(&chunk);
            continue;
        }

        esp_err_t ret = chat_playback_write_chunk(ctx, &chunk);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
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
                                CONFIG_ESPESP_CHAT_PLAYBACK_TASK_PRIORITY,
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

    if (ctx->playback_streaming && ctx->playback_pcm &&
        ctx->output_sample_rate_hz != sample_rate_hz) {
        chat_playback_interrupt(ctx, "tts_start sample rate change");
    }

    if (ctx->playback_lock != NULL) {
        xSemaphoreTake(ctx->playback_lock, portMAX_DELAY);
    }

    if (ctx->playback_streaming && ctx->playback_pcm &&
        ctx->output_sample_rate_hz == sample_rate_hz) {
        ctx->playback_finishing = false;
        ctx->playback_finishing_segment_id = 0;
        ctx->playback_segment_id++;
        ctx->playback_segment_received_bytes = 0;
        ctx->binary_payload_active = false;
        ctx->warned_drop_binary = false;
        if (ctx->playback_lock != NULL) {
            xSemaphoreGive(ctx->playback_lock);
        }
        ESP_LOGI(CHAT_TAG, "tts playback continue sample_rate=%" PRIu32, sample_rate_hz);
        return ESP_OK;
    }

    esp_err_t ret = chat_audio_set_output_sample_rate(ctx, sample_rate_hz);
    if (ret != ESP_OK) {
        if (ctx->playback_lock != NULL) {
            xSemaphoreGive(ctx->playback_lock);
        }
        return ret;
    }
    ret = chat_audio_enable_tx(ctx, sample_rate_hz);
    if (ret != ESP_OK) {
        if (ctx->playback_lock != NULL) {
            xSemaphoreGive(ctx->playback_lock);
        }
        return ret;
    }

    ctx->playback_streaming = true;
    ctx->playback_pcm = true;
    ctx->playback_finishing = false;
    ctx->playback_abort_requested = false;
    ctx->playback_finishing_segment_id = 0;
    ctx->binary_payload_active = false;
    ctx->warned_drop_binary = false;
    ctx->has_pending_byte = false;
    ctx->pending_byte = 0;
    ctx->playback_received_bytes = 0;
    ctx->playback_segment_received_bytes = 0;
    ctx->playback_written_bytes = 0;
    ctx->playback_dropped_chunks = 0;
    ctx->playback_samples = 0;
    ctx->playback_limited_samples = 0;
    ctx->playback_chunks = 0;
    ctx->playback_input_peak = 0;
    ctx->playback_output_peak = 0;
    ctx->playback_has_last_sample = false;
    ctx->playback_last_sample = 0;
    ctx->playback_fade_in_total = chat_playback_fade_samples(sample_rate_hz);
    ctx->playback_fade_in_remaining = ctx->playback_fade_in_total;
    ctx->playback_started_us = esp_timer_get_time();
    ctx->last_playback_stats_us = ctx->playback_started_us;
    ctx->playback_generation++;
    ctx->playback_segment_id++;

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

void chat_playback_end(chat_context_t *ctx, const char *reason)
{
    if (ctx == NULL) {
        return;
    }
    (void)reason;

    uint32_t generation = ctx->playback_generation;
    uint32_t segment_id = ctx->playback_segment_id;
    if (!chat_playback_generation_active(ctx, generation)) {
        return;
    }

    chat_playback_chunk_t end = {
        .data = NULL,
        .len = CHAT_PLAYBACK_STREAM_END_LEN,
        .message_done = true,
        .generation = generation,
        .segment_id = segment_id,
    };

    /* Queue the end marker behind already received PCM and return to the
     * WebSocket event task immediately; the playback task will log final stats
     * once the queued audio has really reached I2S.
     */
    ctx->playback_finishing = true;
    ctx->playback_finishing_segment_id = segment_id;
    (void)chat_playback_send_queue_item(ctx,
                                        &end,
                                        generation,
                                        true,
                                        "playback queue full, waiting to enqueue tts_done");
}

void chat_playback_interrupt(chat_context_t *ctx, const char *reason)
{
    if (ctx == NULL) {
        return;
    }

    ctx->playback_abort_requested = true;

    if (ctx->playback_lock != NULL) {
        xSemaphoreTake(ctx->playback_lock, portMAX_DELAY);
    }

    bool had_playback = ctx->playback_streaming ||
                        ctx->playback_pcm ||
                        ctx->playback_finishing;

    if (had_playback) {
        esp_err_t tail_ret = chat_write_playback_tail(ctx, true);
        if (tail_ret != ESP_OK && tail_ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(CHAT_TAG, "TTS interrupt fade failed: %s", esp_err_to_name(tail_ret));
        }
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
    ctx->playback_finishing = false;
    ctx->playback_abort_requested = false;
    ctx->playback_finishing_segment_id = 0;
    ctx->binary_payload_active = false;
    ctx->warned_drop_binary = false;
    ctx->has_pending_byte = false;
    ctx->playback_has_last_sample = false;
    ctx->playback_last_sample = 0;
    ctx->playback_fade_in_remaining = 0;
    ctx->playback_fade_in_total = 0;
    ctx->playback_generation++;

    if (ctx->aec != NULL) {
        voice_client_aec_playback_end(ctx->aec);
    }

    if (had_playback && ctx->tx_channel != NULL) {
        esp_err_t ret = chat_audio_disable_tx(ctx);
        if (ret != ESP_OK) {
            ESP_LOGW(CHAT_TAG, "disable speaker TX during interrupt failed: %s", esp_err_to_name(ret));
        }
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

    uint32_t generation = ctx->playback_generation;
    uint32_t segment_id = ctx->playback_segment_id;
    if (!chat_playback_can_enqueue(ctx, generation)) {
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
        .generation = generation,
        .segment_id = segment_id,
    };

    if (chat_playback_send_queue_item(ctx,
                                      &chunk,
                                      generation,
                                      false,
                                      "playback queue full, applying websocket backpressure") == ESP_OK) {
        ctx->playback_received_bytes += (uint64_t)data_len;
        ctx->playback_segment_received_bytes += (uint64_t)data_len;
        ctx->playback_chunks++;
        return ESP_OK;
    }

    chat_playback_free_chunk(&chunk);
    ctx->playback_dropped_chunks++;
    return ESP_OK;
}
