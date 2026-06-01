#include "speaker_client/speaker_client_audio.h"

#include <inttypes.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static uint32_t speaker_client_calculate_ramp_samples(uint32_t sample_rate_hz)
{
    if (sample_rate_hz == 0) {
        sample_rate_hz = CONFIG_ESPESP_SPK_SAMPLE_RATE_HZ;
    }

    uint32_t ramp_samples = (sample_rate_hz * SPEAKER_CLIENT_DECLICK_MS) / 1000U;
    if (ramp_samples == 0) {
        ramp_samples = 1;
    }
    return ramp_samples;
}

static uint32_t speaker_client_calculate_duration_samples(uint32_t sample_rate_hz, uint32_t duration_ms)
{
    if (sample_rate_hz == 0) {
        sample_rate_hz = CONFIG_ESPESP_SPK_SAMPLE_RATE_HZ;
    }

    uint64_t samples = ((uint64_t)sample_rate_hz * duration_ms + 999U) / 1000U;
    if (samples == 0) {
        samples = 1;
    }
    if (samples > UINT32_MAX) {
        samples = UINT32_MAX;
    }
    return (uint32_t)samples;
}

static int16_t speaker_client_load_s16le(const uint8_t *data)
{
    uint16_t raw = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    return (int16_t)raw;
}

static void speaker_client_store_s16le(uint8_t *data, int16_t sample)
{
    uint16_t raw = (uint16_t)sample;
    data[0] = (uint8_t)(raw & 0xff);
    data[1] = (uint8_t)(raw >> 8);
}

static int16_t speaker_client_scale_sample(int16_t sample, uint32_t numerator, uint32_t denominator)
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

static esp_err_t speaker_client_write_raw_i2s(speaker_client_context_t *ctx,
                                              const uint8_t *data,
                                              size_t len,
                                              bool stream_bytes)
{
    if (ctx == NULL || ctx->tx_channel == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ctx->tx_enabled) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t total_written = 0;
    while (total_written < len) {
        size_t bytes_written = 0;
        esp_err_t ret = i2s_channel_write(ctx->tx_channel,
                                          data + total_written,
                                          len - total_written,
                                          &bytes_written,
                                          CONFIG_ESPESP_SPEAKER_CLIENT_I2S_WRITE_TIMEOUT_MS);
        if (ret != ESP_OK) {
            return ret;
        }
        if (bytes_written == 0) {
            return ESP_ERR_TIMEOUT;
        }
        total_written += bytes_written;
    }

    if (stream_bytes) {
        ctx->written_bytes += total_written;
    } else {
        ctx->declick_written_bytes += total_written;
    }
    return ESP_OK;
}

static uint32_t speaker_client_get_dma_bytes(const speaker_client_context_t *ctx)
{
    if (ctx == NULL || ctx->tx_channel == NULL) {
        return 0;
    }

    i2s_chan_info_t info = {0};
    if (i2s_channel_get_info(ctx->tx_channel, &info) != ESP_OK || info.total_dma_buf_size == 0) {
        return 0;
    }

    return (uint32_t)info.total_dma_buf_size;
}

static esp_err_t speaker_client_preload_silence(speaker_client_context_t *ctx, size_t target_bytes)
{
    if (ctx == NULL || ctx->tx_channel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t silence[SPEAKER_CLIENT_AUDIO_WORK_SAMPLES * SPEAKER_CLIENT_SAMPLE_WIDTH_BYTES] = {0};
    size_t remaining = target_bytes;
    while (remaining > 0) {
        size_t chunk_bytes = remaining;
        if (chunk_bytes > sizeof(silence)) {
            chunk_bytes = sizeof(silence);
        }

        size_t bytes_loaded = 0;
        esp_err_t ret = i2s_channel_preload_data(ctx->tx_channel, silence, chunk_bytes, &bytes_loaded);
        if (ret != ESP_OK) {
            return ret;
        }
        if (bytes_loaded == 0) {
            break;
        }
        remaining -= bytes_loaded;
    }

    return ESP_OK;
}

static esp_err_t speaker_client_write_silence(speaker_client_context_t *ctx,
                                              uint32_t sample_count,
                                              bool stream_bytes)
{
    if (ctx == NULL || sample_count == 0) {
        return ESP_OK;
    }

    uint8_t silence[SPEAKER_CLIENT_AUDIO_WORK_SAMPLES * SPEAKER_CLIENT_SAMPLE_WIDTH_BYTES] = {0};
    uint32_t remaining = sample_count;
    while (remaining > 0) {
        uint32_t chunk_samples = remaining;
        if (chunk_samples > SPEAKER_CLIENT_AUDIO_WORK_SAMPLES) {
            chunk_samples = SPEAKER_CLIENT_AUDIO_WORK_SAMPLES;
        }

        size_t bytes_to_write = (size_t)chunk_samples * SPEAKER_CLIENT_SAMPLE_WIDTH_BYTES;
        ESP_RETURN_ON_ERROR(speaker_client_write_raw_i2s(ctx, silence, bytes_to_write, stream_bytes),
                            SPEAKER_CLIENT_TAG,
                            "write silence to I2S");
        remaining -= chunk_samples;
    }

    ctx->last_output_sample = 0;
    ctx->has_last_output_sample = true;
    return ESP_OK;
}

static esp_err_t speaker_client_enable_tx(speaker_client_context_t *ctx, uint32_t sample_rate_hz)
{
    if (ctx == NULL || ctx->tx_channel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ctx->tx_enabled) {
        return ESP_OK;
    }

    uint32_t prime_samples = speaker_client_calculate_duration_samples(sample_rate_hz, SPEAKER_CLIENT_START_PRIME_MS);
    size_t prime_bytes = (size_t)prime_samples * SPEAKER_CLIENT_SAMPLE_WIDTH_BYTES;
    uint32_t dma_bytes = speaker_client_get_dma_bytes(ctx);
    if (dma_bytes > prime_bytes) {
        prime_bytes = dma_bytes;
    }

    ESP_RETURN_ON_ERROR(speaker_client_preload_silence(ctx, prime_bytes),
                        SPEAKER_CLIENT_TAG,
                        "preload silence before enabling TX");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(ctx->tx_channel),
                        SPEAKER_CLIENT_TAG,
                        "enable I2S TX channel");
    ctx->tx_enabled = true;

    ESP_LOGI(SPEAKER_CLIENT_TAG,
             "speaker TX enabled prime_bytes=%u dma_bytes=%u",
             (unsigned int)prime_bytes,
             (unsigned int)dma_bytes);
    return ESP_OK;
}

static esp_err_t speaker_client_disable_tx(speaker_client_context_t *ctx)
{
    if (ctx == NULL || ctx->tx_channel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ctx->tx_enabled) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(i2s_channel_disable(ctx->tx_channel),
                        SPEAKER_CLIENT_TAG,
                        "disable I2S TX channel");
    ctx->tx_enabled = false;
    ESP_LOGI(SPEAKER_CLIENT_TAG, "speaker TX disabled");
    return ESP_OK;
}

static int16_t speaker_client_apply_ramp_in(speaker_client_context_t *ctx, int16_t sample)
{
    if (ctx == NULL || ctx->ramp_in_remaining == 0 || ctx->ramp_total_samples == 0) {
        return sample;
    }

    uint32_t progress = ctx->ramp_total_samples - ctx->ramp_in_remaining;
    sample = speaker_client_scale_sample(sample, progress + 1U, ctx->ramp_total_samples);
    ctx->ramp_in_remaining--;
    return sample;
}

static void speaker_client_log_progress(speaker_client_context_t *ctx)
{
    if (ctx == NULL || !ctx->streaming) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    if (now_us - ctx->last_stats_us < SPEAKER_CLIENT_STATS_PERIOD_US) {
        return;
    }

    ctx->last_stats_us = now_us;
    ESP_LOGI(SPEAKER_CLIENT_TAG,
             "audio progress chunks=%" PRIu32 " received=%" PRIu64 " written=%" PRIu64
             " declick=%" PRIu64 " frames=%" PRIu64 " elapsed_ms=%" PRIu64,
             ctx->received_chunks,
             ctx->received_bytes,
             ctx->written_bytes,
             ctx->declick_written_bytes,
             ctx->written_bytes / SPEAKER_CLIENT_SAMPLE_WIDTH_BYTES,
             (uint64_t)((now_us - ctx->stream_started_us) / 1000));
}

static esp_err_t speaker_client_write_processed_pcm_i2s(speaker_client_context_t *ctx,
                                                        const uint8_t *data,
                                                        size_t len)
{
    if (ctx == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((len % SPEAKER_CLIENT_SAMPLE_WIDTH_BYTES) != 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t output[SPEAKER_CLIENT_AUDIO_WORK_SAMPLES * SPEAKER_CLIENT_SAMPLE_WIDTH_BYTES];
    const uint8_t *cursor = data;
    size_t remaining = len;

    while (remaining > 0) {
        size_t sample_count = remaining / SPEAKER_CLIENT_SAMPLE_WIDTH_BYTES;
        if (sample_count > SPEAKER_CLIENT_AUDIO_WORK_SAMPLES) {
            sample_count = SPEAKER_CLIENT_AUDIO_WORK_SAMPLES;
        }

        for (size_t i = 0; i < sample_count; i++) {
            int16_t sample = speaker_client_load_s16le(cursor + i * SPEAKER_CLIENT_SAMPLE_WIDTH_BYTES);
            sample = speaker_client_apply_ramp_in(ctx, sample);
            speaker_client_store_s16le(output + i * SPEAKER_CLIENT_SAMPLE_WIDTH_BYTES, sample);
            ctx->last_output_sample = sample;
            ctx->has_last_output_sample = true;
        }

        size_t bytes_to_write = sample_count * SPEAKER_CLIENT_SAMPLE_WIDTH_BYTES;
        ESP_RETURN_ON_ERROR(speaker_client_write_raw_i2s(ctx, output, bytes_to_write, true),
                            SPEAKER_CLIENT_TAG,
                            "write PCM to I2S");
        cursor += bytes_to_write;
        remaining -= bytes_to_write;
    }

    return ESP_OK;
}

static esp_err_t speaker_client_write_ramp_down(speaker_client_context_t *ctx)
{
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ctx->has_last_output_sample || ctx->last_output_sample == 0) {
        return ESP_OK;
    }

    uint32_t ramp_samples = ctx->ramp_total_samples;
    if (ramp_samples < 2U) {
        ramp_samples = 2U;
    }

    uint8_t output[SPEAKER_CLIENT_AUDIO_WORK_SAMPLES * SPEAKER_CLIENT_SAMPLE_WIDTH_BYTES];
    uint32_t produced = 0;

    while (produced < ramp_samples) {
        uint32_t sample_count = ramp_samples - produced;
        if (sample_count > SPEAKER_CLIENT_AUDIO_WORK_SAMPLES) {
            sample_count = SPEAKER_CLIENT_AUDIO_WORK_SAMPLES;
        }

        for (uint32_t i = 0; i < sample_count; i++) {
            uint32_t ramp_index = produced + i;
            int16_t sample = speaker_client_scale_sample(ctx->last_output_sample,
                                                         (ramp_samples - 1U) - ramp_index,
                                                         ramp_samples - 1U);
            speaker_client_store_s16le(output + i * SPEAKER_CLIENT_SAMPLE_WIDTH_BYTES, sample);
        }

        size_t bytes_to_write = (size_t)sample_count * SPEAKER_CLIENT_SAMPLE_WIDTH_BYTES;
        ESP_RETURN_ON_ERROR(speaker_client_write_raw_i2s(ctx, output, bytes_to_write, false),
                            SPEAKER_CLIENT_TAG,
                            "write tail declick ramp");
        produced += sample_count;
    }

    ctx->last_output_sample = 0;
    return ESP_OK;
}

esp_err_t speaker_client_create_i2s_channel(i2s_chan_handle_t *tx_channel)
{
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, tx_channel, NULL),
                        SPEAKER_CLIENT_TAG,
                        "create I2S TX channel");

    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_ESPESP_SPK_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_ESPESP_SPK_BCLK_GPIO,
            .ws = CONFIG_ESPESP_SPK_WS_GPIO,
            .dout = CONFIG_ESPESP_SPK_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    esp_err_t ret = i2s_channel_init_std_mode(*tx_channel, &std_config);
    if (ret != ESP_OK) {
        i2s_del_channel(*tx_channel);
        *tx_channel = NULL;
    }

    return ret;
}

esp_err_t speaker_client_prepare_stream(speaker_client_context_t *ctx,
                                        uint32_t sample_rate_hz,
                                        uint64_t expected_frames)
{
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int64_t now_us = esp_timer_get_time();
    ctx->streaming = true;
    ctx->binary_payload_active = false;
    ctx->warned_drop_without_stream = false;
    ctx->has_pending_byte = false;
    ctx->pending_byte = 0;
    ctx->sample_rate_hz = sample_rate_hz;
    ctx->ramp_total_samples = speaker_client_calculate_ramp_samples(sample_rate_hz);
    ctx->ramp_in_remaining = ctx->ramp_total_samples;
    ctx->expected_frames = expected_frames;
    ctx->received_bytes = 0;
    ctx->written_bytes = 0;
    ctx->declick_written_bytes = 0;
    ctx->received_chunks = 0;
    ctx->last_output_sample = 0;
    ctx->has_last_output_sample = false;
    ctx->stream_started_us = now_us;
    ctx->last_stats_us = now_us;
    ESP_RETURN_ON_ERROR(speaker_client_enable_tx(ctx, sample_rate_hz),
                        SPEAKER_CLIENT_TAG,
                        "prepare speaker TX");
    return ESP_OK;
}

esp_err_t speaker_client_write_audio_chunk(speaker_client_context_t *ctx,
                                           const uint8_t *data,
                                           int data_len,
                                           bool message_done)
{
    if (ctx == NULL || data == NULL || data_len <= 0) {
        return ESP_OK;
    }

    if (!ctx->streaming) {
        if (!ctx->warned_drop_without_stream) {
            ESP_LOGW(SPEAKER_CLIENT_TAG, "dropping binary audio because no valid audio_start metadata was received");
            ctx->warned_drop_without_stream = true;
        }
        return ESP_OK;
    }

    ctx->received_chunks++;
    ctx->received_bytes += (uint64_t)data_len;

    const uint8_t *cursor = data;
    size_t remaining = (size_t)data_len;

    if (ctx->has_pending_byte && remaining > 0) {
        uint8_t sample[SPEAKER_CLIENT_SAMPLE_WIDTH_BYTES] = {ctx->pending_byte, cursor[0]};
        ctx->has_pending_byte = false;
        cursor++;
        remaining--;
        ESP_RETURN_ON_ERROR(speaker_client_write_processed_pcm_i2s(ctx, sample, sizeof(sample)),
                            SPEAKER_CLIENT_TAG,
                            "write pending sample");
    }

    if ((remaining % SPEAKER_CLIENT_SAMPLE_WIDTH_BYTES) != 0) {
        ctx->pending_byte = cursor[remaining - 1];
        ctx->has_pending_byte = true;
        remaining--;
    }

    if (remaining > 0) {
        ESP_RETURN_ON_ERROR(speaker_client_write_processed_pcm_i2s(ctx, cursor, remaining),
                            SPEAKER_CLIENT_TAG,
                            "write audio chunk");
    }

    if (message_done && ctx->has_pending_byte) {
        ESP_LOGW(SPEAKER_CLIENT_TAG, "dropping odd trailing byte at end of binary audio frame");
        ctx->has_pending_byte = false;
    }

    speaker_client_log_progress(ctx);
    return ESP_OK;
}

esp_err_t speaker_client_finish_stream(speaker_client_context_t *ctx, const char *reason)
{
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (ctx->has_pending_byte) {
        ESP_LOGW(SPEAKER_CLIENT_TAG, "dropping pending odd byte before stream stop");
        ctx->has_pending_byte = false;
    }

    uint32_t ramp_samples_written = 0;
    uint64_t declick_before = ctx->declick_written_bytes;
    esp_err_t ret = speaker_client_write_ramp_down(ctx);
    if (ret == ESP_OK && ctx->tx_enabled && ctx->ramp_total_samples > 0) {
        ramp_samples_written = ctx->ramp_total_samples < 2U ? 2U : ctx->ramp_total_samples;
    }
    if (ret == ESP_OK && ctx->declick_written_bytes > declick_before) {
        ESP_LOGI(SPEAKER_CLIENT_TAG,
                 "tail declick reason=%s samples=%" PRIu64,
                 reason != NULL ? reason : "(unknown)",
                 (ctx->declick_written_bytes - declick_before) / SPEAKER_CLIENT_SAMPLE_WIDTH_BYTES);
    }

    if (ctx->tx_enabled) {
        uint32_t guard_samples = speaker_client_calculate_duration_samples(ctx->sample_rate_hz,
                                                                           SPEAKER_CLIENT_STOP_GUARD_MS);
        esp_err_t silence_ret = speaker_client_write_silence(ctx, guard_samples, false);
        if (silence_ret == ESP_OK) {
            uint32_t dma_samples = speaker_client_get_dma_bytes(ctx) / SPEAKER_CLIENT_SAMPLE_WIDTH_BYTES;
            uint32_t settle_samples = ramp_samples_written + guard_samples + dma_samples;
            uint32_t settle_ms = 1;
            if (ctx->sample_rate_hz > 0 && settle_samples > 0) {
                settle_ms = (uint32_t)(((uint64_t)settle_samples * 1000U + ctx->sample_rate_hz - 1U) /
                                       ctx->sample_rate_hz);
                if (settle_ms == 0) {
                    settle_ms = 1;
                }
            }

            ESP_LOGI(SPEAKER_CLIENT_TAG,
                     "speaker TX settle reason=%s ramp_samples=%u guard_samples=%u dma_samples=%u wait_ms=%u",
                     reason != NULL ? reason : "(unknown)",
                     (unsigned int)ramp_samples_written,
                     (unsigned int)guard_samples,
                     (unsigned int)dma_samples,
                     (unsigned int)settle_ms);
            vTaskDelay(pdMS_TO_TICKS(settle_ms));
        } else if (ret == ESP_OK) {
            ret = silence_ret;
        }

        esp_err_t disable_ret = speaker_client_disable_tx(ctx);
        if (ret == ESP_OK) {
            ret = disable_ret;
        }
    }

    ctx->streaming = false;
    ctx->binary_payload_active = false;
    ctx->warned_drop_without_stream = false;
    ctx->pending_byte = 0;
    ctx->has_last_output_sample = false;
    ctx->last_output_sample = 0;
    ctx->ramp_in_remaining = 0;

    return ret;
}
