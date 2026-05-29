#include "voice_client/voice_client_audio.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

esp_err_t voice_client_create_rx_channel(i2s_chan_handle_t *rx_channel)
{
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, NULL, rx_channel),
                        VOICE_CLIENT_TAG,
                        "create I2S RX channel");

    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_ESPESP_VOICE_CLIENT_INPUT_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_ESPESP_MIC_BCLK_GPIO,
            .ws = CONFIG_ESPESP_MIC_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = CONFIG_ESPESP_MIC_DIN_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_config.slot_cfg.slot_mask = VOICE_CLIENT_MIC_SLOT_MASK;

    esp_err_t ret = i2s_channel_init_std_mode(*rx_channel, &std_config);
    if (ret != ESP_OK) {
        i2s_del_channel(*rx_channel);
        *rx_channel = NULL;
    }

    return ret;
}

esp_err_t voice_client_create_tx_channel(i2s_chan_handle_t *tx_channel)
{
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, tx_channel, NULL),
                        VOICE_CLIENT_TAG,
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

int16_t voice_client_convert_sample(int32_t sample)
{
    int32_t pcm = sample >> CONFIG_ESPESP_VOICE_CLIENT_MIC_SAMPLE_SHIFT_BITS;
    if (pcm > INT16_MAX) {
        pcm = INT16_MAX;
    } else if (pcm < INT16_MIN) {
        pcm = INT16_MIN;
    }
    return (int16_t)pcm;
}

esp_err_t voice_client_set_output_sample_rate(voice_client_context_t *ctx, uint32_t sample_rate_hz)
{
    if (ctx == NULL || ctx->tx_channel == NULL || sample_rate_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (ctx->output_sample_rate_hz == sample_rate_hz) {
        return ESP_OK;
    }

    ESP_LOGI(VOICE_CLIENT_TAG,
             "reconfigure speaker sample rate: %" PRIu32 " -> %" PRIu32 " Hz",
             ctx->output_sample_rate_hz,
             sample_rate_hz);

    esp_err_t ret = ESP_OK;
    if (ctx->tx_enabled) {
        ret = i2s_channel_disable(ctx->tx_channel);
        if (ret != ESP_OK) {
            ESP_LOGE(VOICE_CLIENT_TAG, "disable speaker channel failed: %s", esp_err_to_name(ret));
            return ret;
        }
        ctx->tx_enabled = false;
    }

    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz);
    ret = i2s_channel_reconfig_std_clock(ctx->tx_channel, &clk_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(VOICE_CLIENT_TAG, "reconfigure speaker clock failed: %s", esp_err_to_name(ret));
        (void)i2s_channel_enable(ctx->tx_channel);
        ctx->tx_enabled = true;
        return ret;
    }

    ret = i2s_channel_enable(ctx->tx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(VOICE_CLIENT_TAG, "re-enable speaker channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ctx->tx_enabled = true;
    ctx->output_sample_rate_hz = sample_rate_hz;
    return ESP_OK;
}

void voice_client_reset_playback_stats(voice_client_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    ctx->tts_received_bytes = 0;
    ctx->tts_written_bytes = 0;
    ctx->tts_samples = 0;
    ctx->tts_limited_samples = 0;
    ctx->tts_input_peak = 0;
    ctx->tts_output_peak = 0;
    ctx->tts_chunks = 0;
    ctx->has_pending_byte = false;
    ctx->pending_byte = 0;
    ctx->playback_started_us = esp_timer_get_time();
    ctx->last_playback_stats_us = ctx->playback_started_us;
}

static esp_err_t voice_client_write_all_i2s(voice_client_context_t *ctx, const uint8_t *data, size_t len)
{
    if (ctx == NULL || ctx->tx_channel == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t total_written = 0;
    while (total_written < len) {
        size_t bytes_written = 0;
        esp_err_t ret = i2s_channel_write(ctx->tx_channel,
                                          data + total_written,
                                          len - total_written,
                                          &bytes_written,
                                          CONFIG_ESPESP_VOICE_CLIENT_I2S_WRITE_TIMEOUT_MS);
        if (ret != ESP_OK) {
            return ret;
        }
        if (bytes_written == 0) {
            return ESP_ERR_TIMEOUT;
        }
        total_written += bytes_written;
    }

    ctx->tts_written_bytes += total_written;
    return ESP_OK;
}

static int16_t voice_client_load_s16le(const uint8_t *data)
{
    uint16_t raw = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    return (int16_t)raw;
}

static void voice_client_store_s16le(uint8_t *data, int16_t sample)
{
    uint16_t raw = (uint16_t)sample;
    data[0] = (uint8_t)(raw & 0xff);
    data[1] = (uint8_t)(raw >> 8);
}

static uint32_t voice_client_abs_i16(int16_t sample)
{
    return sample == INT16_MIN ? 32768U : (uint32_t)abs(sample);
}

static int16_t voice_client_process_tts_sample(voice_client_context_t *ctx, int16_t sample)
{
    int32_t scaled = ((int32_t)sample * CONFIG_ESPESP_VOICE_CLIENT_TTS_VOLUME_PERCENT) / 100;
    int32_t limit = ((int32_t)INT16_MAX * CONFIG_ESPESP_VOICE_CLIENT_TTS_LIMIT_PERCENT) / 100;
    uint32_t input_peak = voice_client_abs_i16(sample);

    if (limit <= 0 || limit > INT16_MAX) {
        limit = INT16_MAX;
    }

    if (scaled > limit) {
        scaled = limit;
        if (ctx != NULL) {
            ctx->tts_limited_samples++;
        }
    } else if (scaled < -limit) {
        scaled = -limit;
        if (ctx != NULL) {
            ctx->tts_limited_samples++;
        }
    }

    int16_t processed = (int16_t)scaled;
    if (ctx != NULL) {
        uint32_t output_peak = voice_client_abs_i16(processed);
        if (input_peak > ctx->tts_input_peak) {
            ctx->tts_input_peak = input_peak;
        }
        if (output_peak > ctx->tts_output_peak) {
            ctx->tts_output_peak = output_peak;
        }
        ctx->tts_samples++;
    }

    return processed;
}

static esp_err_t voice_client_write_processed_pcm_i2s(voice_client_context_t *ctx,
                                                      const uint8_t *data,
                                                      size_t len)
{
    if (ctx == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((len % VOICE_CLIENT_SAMPLE_WIDTH_BYTES) != 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t output[VOICE_CLIENT_PLAYBACK_WORK_SAMPLES * VOICE_CLIENT_SAMPLE_WIDTH_BYTES];
    const uint8_t *cursor = data;
    size_t remaining = len;

    while (remaining > 0) {
        size_t sample_count = remaining / VOICE_CLIENT_SAMPLE_WIDTH_BYTES;
        if (sample_count > VOICE_CLIENT_PLAYBACK_WORK_SAMPLES) {
            sample_count = VOICE_CLIENT_PLAYBACK_WORK_SAMPLES;
        }

        for (size_t i = 0; i < sample_count; i++) {
            int16_t sample = voice_client_load_s16le(cursor + i * VOICE_CLIENT_SAMPLE_WIDTH_BYTES);
            sample = voice_client_process_tts_sample(ctx, sample);
            voice_client_store_s16le(output + i * VOICE_CLIENT_SAMPLE_WIDTH_BYTES, sample);
        }

        size_t bytes_to_write = sample_count * VOICE_CLIENT_SAMPLE_WIDTH_BYTES;
        ESP_RETURN_ON_ERROR(voice_client_write_all_i2s(ctx, output, bytes_to_write),
                            VOICE_CLIENT_TAG,
                            "write processed TTS PCM");
        cursor += bytes_to_write;
        remaining -= bytes_to_write;
    }

    return ESP_OK;
}

static void voice_client_log_playback_progress(voice_client_context_t *ctx)
{
    if (ctx == NULL || !ctx->playback_streaming) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    if (now_us - ctx->last_playback_stats_us < VOICE_CLIENT_STATS_PERIOD_US) {
        return;
    }

    ctx->last_playback_stats_us = now_us;
    ESP_LOGI(VOICE_CLIENT_TAG,
             "tts playback chunks=%" PRIu32 " received=%" PRIu64 " written=%" PRIu64
             " sample_rate=%" PRIu32 " peak_in=%" PRIu32 " peak_out=%" PRIu32
             " limited=%" PRIu64 " elapsed_ms=%" PRId64,
             ctx->tts_chunks,
             ctx->tts_received_bytes,
             ctx->tts_written_bytes,
             ctx->output_sample_rate_hz,
             ctx->tts_input_peak,
             ctx->tts_output_peak,
             ctx->tts_limited_samples,
             (now_us - ctx->playback_started_us) / 1000);
}

static bool voice_client_payload_looks_encoded(const uint8_t *data, size_t len, const char **format)
{
    if (data == NULL || len < 4) {
        return false;
    }

    if (memcmp(data, "RIFF", 4) == 0) {
        *format = "wav";
        return true;
    }
    if (memcmp(data, "ID3", 3) == 0 || (data[0] == 0xff && (data[1] & 0xe0) == 0xe0)) {
        *format = "mp3";
        return true;
    }
    if (memcmp(data, "fLaC", 4) == 0) {
        *format = "flac";
        return true;
    }
    if (memcmp(data, "OggS", 4) == 0) {
        *format = "ogg/opus";
        return true;
    }

    return false;
}

esp_err_t voice_client_write_audio_chunk(voice_client_context_t *ctx,
                                         const uint8_t *data,
                                         int data_len,
                                         bool message_done)
{
    if (ctx == NULL || data == NULL || data_len <= 0) {
        return ESP_OK;
    }

    if (!ctx->playback_streaming || !ctx->playback_pcm) {
        if (!ctx->warned_drop_binary) {
            ESP_LOGW(VOICE_CLIENT_TAG, "dropping binary audio because no active pcm tts_start was received");
            ctx->warned_drop_binary = true;
        }
        return ESP_OK;
    }

    if (ctx->tts_received_bytes == 0) {
        const char *encoded_format = NULL;
        if (voice_client_payload_looks_encoded(data, (size_t)data_len, &encoded_format)) {
            ESP_LOGE(VOICE_CLIENT_TAG,
                     "TTS payload looks like %s, but voice_client expects raw pcm_s16le; dropping playback",
                     encoded_format);
            ctx->playback_streaming = false;
            ctx->playback_pcm = false;
            return ESP_ERR_INVALID_RESPONSE;
        }
    }

    ctx->tts_chunks++;
    ctx->tts_received_bytes += (uint64_t)data_len;

    const uint8_t *cursor = data;
    size_t remaining = (size_t)data_len;

    if (ctx->has_pending_byte && remaining > 0) {
        uint8_t sample[2] = { ctx->pending_byte, cursor[0] };
        ctx->has_pending_byte = false;
        cursor++;
        remaining--;
        ESP_RETURN_ON_ERROR(voice_client_write_processed_pcm_i2s(ctx, sample, sizeof(sample)),
                            VOICE_CLIENT_TAG,
                            "write pending sample");
    }

    if ((remaining % VOICE_CLIENT_SAMPLE_WIDTH_BYTES) != 0) {
        ctx->pending_byte = cursor[remaining - 1];
        ctx->has_pending_byte = true;
        remaining--;
    }

    if (remaining > 0) {
        ESP_RETURN_ON_ERROR(voice_client_write_processed_pcm_i2s(ctx, cursor, remaining),
                            VOICE_CLIENT_TAG,
                            "write audio chunk");
    }

    if (message_done && ctx->has_pending_byte) {
        ESP_LOGW(VOICE_CLIENT_TAG, "dropping odd trailing byte at end of binary audio frame");
        ctx->has_pending_byte = false;
    }

    voice_client_log_playback_progress(ctx);
    return ESP_OK;
}
