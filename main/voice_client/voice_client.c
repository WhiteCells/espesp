#include "voice_client/voice_client.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "wifi_station/wifi_station.h"

#define VOICE_CLIENT_CONNECTED_BIT BIT0
#define VOICE_CLIENT_ERROR_BIT BIT1
#define VOICE_CLIENT_AUTH_HEADER_MAX 256
#define VOICE_CLIENT_START_PAYLOAD_MAX 768
#define VOICE_CLIENT_CONTROL_MAX 1024
#define VOICE_CLIENT_SAMPLE_WIDTH_BYTES 2U
#define VOICE_CLIENT_CHANNELS 1U
#define VOICE_CLIENT_OPCODE_CONTINUATION 0x0
#define VOICE_CLIENT_OPCODE_TEXT 0x1
#define VOICE_CLIENT_OPCODE_BINARY 0x2
#define VOICE_CLIENT_OPCODE_CLOSE 0x8
#define VOICE_CLIENT_OPCODE_PING 0x9
#define VOICE_CLIENT_OPCODE_PONG 0xA
#define VOICE_CLIENT_STATS_PERIOD_US 1000000LL
#define VOICE_CLIENT_PLAYBACK_WORK_SAMPLES 256U

#ifndef CONFIG_ESPESP_VOICE_CLIENT_TTS_VOLUME_PERCENT
#define CONFIG_ESPESP_VOICE_CLIENT_TTS_VOLUME_PERCENT 60
#endif

#ifndef CONFIG_ESPESP_VOICE_CLIENT_TTS_LIMIT_PERCENT
#define CONFIG_ESPESP_VOICE_CLIENT_TTS_LIMIT_PERCENT 90
#endif

#if CONFIG_ESPESP_VOICE_CLIENT_MIC_SLOT_RIGHT
#define VOICE_CLIENT_MIC_SLOT_MASK I2S_STD_SLOT_RIGHT
#define VOICE_CLIENT_MIC_SLOT_NAME "right"
#else
#define VOICE_CLIENT_MIC_SLOT_MASK I2S_STD_SLOT_LEFT
#define VOICE_CLIENT_MIC_SLOT_NAME "left"
#endif

typedef struct {
    EventGroupHandle_t event_group;
    i2s_chan_handle_t rx_channel;
    i2s_chan_handle_t tx_channel;
    volatile bool tx_enabled;
    volatile bool start_pending;
    volatile bool session_started;
    volatile bool playback_streaming;
    volatile bool playback_pcm;
    volatile bool binary_payload_active;
    volatile bool warned_drop_binary;
    volatile bool has_pending_byte;
    uint8_t pending_byte;
    uint32_t output_sample_rate_hz;
    uint64_t mic_sent_bytes;
    uint64_t mic_sent_chunks;
    uint64_t mic_dropped_chunks;
    uint64_t tts_received_bytes;
    uint64_t tts_written_bytes;
    uint64_t tts_samples;
    uint64_t tts_limited_samples;
    uint32_t tts_input_peak;
    uint32_t tts_output_peak;
    uint32_t tts_chunks;
    int64_t playback_started_us;
    int64_t last_mic_stats_us;
    int64_t last_playback_stats_us;
} voice_client_context_t;

static const char *TAG = "voice_client";

static bool voice_client_uri_is_valid(const char *uri)
{
    return uri != NULL &&
           (strncmp(uri, "ws://", strlen("ws://")) == 0 ||
            strncmp(uri, "wss://", strlen("wss://")) == 0);
}

static esp_err_t voice_client_make_headers(char *headers, size_t headers_len)
{
    if (headers == NULL || headers_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    headers[0] = '\0';
    if (CONFIG_ESPESP_VOICE_CLIENT_AUTH_TOKEN[0] == '\0') {
        return ESP_OK;
    }

    int written = snprintf(headers,
                           headers_len,
                           "Authorization: Bearer %s\r\n",
                           CONFIG_ESPESP_VOICE_CLIENT_AUTH_TOKEN);
    if (written < 0 || written >= (int)headers_len) {
        ESP_LOGE(TAG, "voice client auth token is too long");
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static esp_err_t voice_client_create_rx_channel(i2s_chan_handle_t *rx_channel)
{
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, NULL, rx_channel), TAG, "create I2S RX channel");

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

static esp_err_t voice_client_create_tx_channel(i2s_chan_handle_t *tx_channel)
{
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, tx_channel, NULL), TAG, "create I2S TX channel");

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

static int16_t voice_client_convert_sample(int32_t sample)
{
    int32_t pcm = sample >> CONFIG_ESPESP_VOICE_CLIENT_MIC_SAMPLE_SHIFT_BITS;
    if (pcm > INT16_MAX) {
        pcm = INT16_MAX;
    } else if (pcm < INT16_MIN) {
        pcm = INT16_MIN;
    }
    return (int16_t)pcm;
}

static const char *voice_client_find_json_value(const char *json, const char *key)
{
    if (json == NULL || key == NULL) {
        return NULL;
    }

    char pattern[64];
    int written = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (written < 0 || written >= (int)sizeof(pattern)) {
        return NULL;
    }

    const char *key_pos = strstr(json, pattern);
    if (key_pos == NULL) {
        return NULL;
    }

    const char *colon = strchr(key_pos + written, ':');
    if (colon == NULL) {
        return NULL;
    }

    const char *value = colon + 1;
    while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n') {
        value++;
    }
    return value;
}

static bool voice_client_json_get_string(const char *json, const char *key, char *out, size_t out_len)
{
    const char *value = voice_client_find_json_value(json, key);
    if (value == NULL || *value != '"' || out == NULL || out_len == 0) {
        return false;
    }

    value++;
    size_t pos = 0;
    while (*value != '\0' && *value != '"') {
        char ch = *value++;
        if (ch == '\\') {
            ch = *value++;
            if (ch == '\0') {
                return false;
            }
        }
        if (pos + 1 >= out_len) {
            return false;
        }
        out[pos++] = ch;
    }

    if (*value != '"') {
        return false;
    }
    out[pos] = '\0';
    return true;
}

static bool voice_client_json_get_u64(const char *json, const char *key, uint64_t *out)
{
    const char *value = voice_client_find_json_value(json, key);
    if (value == NULL || out == NULL) {
        return false;
    }

    char *end = NULL;
    unsigned long long parsed = strtoull(value, &end, 10);
    if (end == value) {
        return false;
    }

    *out = (uint64_t)parsed;
    return true;
}

static bool voice_client_json_get_u32(const char *json, const char *key, uint32_t *out)
{
    uint64_t value = 0;
    if (!voice_client_json_get_u64(json, key, &value) || value > UINT32_MAX) {
        return false;
    }

    *out = (uint32_t)value;
    return true;
}

static esp_err_t voice_client_appendf(char *out,
                                      size_t out_len,
                                      size_t *pos,
                                      const char *fmt,
                                      ...)
{
    if (out == NULL || pos == NULL || *pos >= out_len) {
        return ESP_ERR_INVALID_ARG;
    }

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(out + *pos, out_len - *pos, fmt, args);
    va_end(args);

    if (written < 0 || written >= (int)(out_len - *pos)) {
        return ESP_ERR_INVALID_SIZE;
    }

    *pos += (size_t)written;
    return ESP_OK;
}

static esp_err_t voice_client_append_json_string(char *out,
                                                 size_t out_len,
                                                 size_t *pos,
                                                 const char *value)
{
    ESP_RETURN_ON_ERROR(voice_client_appendf(out, out_len, pos, "\""), TAG, "append JSON quote");

    for (const unsigned char *cursor = (const unsigned char *)value; cursor != NULL && *cursor != '\0'; cursor++) {
        unsigned char ch = *cursor;
        if (ch == '"' || ch == '\\') {
            ESP_RETURN_ON_ERROR(voice_client_appendf(out, out_len, pos, "\\%c", ch), TAG, "append JSON escape");
        } else if (ch == '\n') {
            ESP_RETURN_ON_ERROR(voice_client_appendf(out, out_len, pos, "\\n"), TAG, "append JSON newline");
        } else if (ch == '\r') {
            ESP_RETURN_ON_ERROR(voice_client_appendf(out, out_len, pos, "\\r"), TAG, "append JSON carriage return");
        } else if (ch == '\t') {
            ESP_RETURN_ON_ERROR(voice_client_appendf(out, out_len, pos, "\\t"), TAG, "append JSON tab");
        } else if (ch < 0x20) {
            ESP_RETURN_ON_ERROR(voice_client_appendf(out, out_len, pos, "\\u%04x", ch), TAG, "append JSON control");
        } else {
            ESP_RETURN_ON_ERROR(voice_client_appendf(out, out_len, pos, "%c", ch), TAG, "append JSON char");
        }
    }

    return voice_client_appendf(out, out_len, pos, "\"");
}

static esp_err_t voice_client_build_start_event(char *payload, size_t payload_len)
{
    if (payload == NULL || payload_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int segment_ms = CONFIG_ESPESP_VOICE_CLIENT_SEGMENT_MS;
    size_t pos = 0;

    ESP_RETURN_ON_ERROR(
        voice_client_appendf(payload,
                             payload_len,
                             &pos,
                             "{\"type\":\"start\","
                             "\"audio_format\":{"
                             "\"encoding\":\"pcm_s16le\","
                             "\"sample_rate\":%d,"
                             "\"channels\":%u,"
                             "\"container\":\"raw\"},"
                             "\"segment_seconds\":%d.%03d",
                             CONFIG_ESPESP_VOICE_CLIENT_INPUT_SAMPLE_RATE_HZ,
                             VOICE_CLIENT_CHANNELS,
                             segment_ms / 1000,
                             segment_ms % 1000),
        TAG,
        "build start event");

    if (CONFIG_ESPESP_VOICE_CLIENT_ASR_LANGUAGE[0] != '\0') {
        ESP_RETURN_ON_ERROR(voice_client_appendf(payload, payload_len, &pos, ",\"asr\":{\"language\":"),
                            TAG,
                            "append ASR");
        ESP_RETURN_ON_ERROR(voice_client_append_json_string(payload,
                                                           payload_len,
                                                           &pos,
                                                           CONFIG_ESPESP_VOICE_CLIENT_ASR_LANGUAGE),
                            TAG,
                            "append ASR language");
        ESP_RETURN_ON_ERROR(voice_client_appendf(payload, payload_len, &pos, "}"), TAG, "close ASR");
    }

    ESP_RETURN_ON_ERROR(voice_client_appendf(payload, payload_len, &pos, ",\"tts\":{"),
                        TAG,
                        "append TTS");
    if (CONFIG_ESPESP_VOICE_CLIENT_TTS_VOICE[0] != '\0') {
        ESP_RETURN_ON_ERROR(voice_client_appendf(payload, payload_len, &pos, "\"voice\":"),
                            TAG,
                            "append TTS voice key");
        ESP_RETURN_ON_ERROR(voice_client_append_json_string(payload,
                                                           payload_len,
                                                           &pos,
                                                           CONFIG_ESPESP_VOICE_CLIENT_TTS_VOICE),
                            TAG,
                            "append TTS voice");
        ESP_RETURN_ON_ERROR(voice_client_appendf(payload, payload_len, &pos, ","),
                            TAG,
                            "append TTS comma");
    }
    ESP_RETURN_ON_ERROR(voice_client_appendf(payload, payload_len, &pos, "\"response_format\":\"pcm\"}}"),
                        TAG,
                        "close start event");

    return ESP_OK;
}

static esp_err_t voice_client_send_start(esp_websocket_client_handle_t client,
                                         voice_client_context_t *ctx)
{
    char payload[VOICE_CLIENT_START_PAYLOAD_MAX];
    ESP_RETURN_ON_ERROR(voice_client_build_start_event(payload, sizeof(payload)), TAG, "build start payload");

    int len = strlen(payload);
    int sent = esp_websocket_client_send_text(client,
                                              payload,
                                              len,
                                              pdMS_TO_TICKS(CONFIG_ESPESP_VOICE_CLIENT_NETWORK_TIMEOUT_MS));
    if (sent != len) {
        ESP_LOGW(TAG, "send start failed: sent=%d expected=%d", sent, len);
        return ESP_FAIL;
    }

    ctx->start_pending = false;
    ctx->session_started = true;
    ctx->warned_drop_binary = false;
    ESP_LOGI(TAG,
             "sent start: input=pcm_s16le/%dHz/%uch raw, segment_ms=%d, tts=pcm",
             CONFIG_ESPESP_VOICE_CLIENT_INPUT_SAMPLE_RATE_HZ,
             VOICE_CLIENT_CHANNELS,
             CONFIG_ESPESP_VOICE_CLIENT_SEGMENT_MS);
    return ESP_OK;
}

static esp_err_t voice_client_set_output_sample_rate(voice_client_context_t *ctx, uint32_t sample_rate_hz)
{
    if (ctx == NULL || ctx->tx_channel == NULL || sample_rate_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (ctx->output_sample_rate_hz == sample_rate_hz) {
        return ESP_OK;
    }

    ESP_LOGI(TAG,
             "reconfigure speaker sample rate: %" PRIu32 " -> %" PRIu32 " Hz",
             ctx->output_sample_rate_hz,
             sample_rate_hz);

    esp_err_t ret = ESP_OK;
    if (ctx->tx_enabled) {
        ret = i2s_channel_disable(ctx->tx_channel);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "disable speaker channel failed: %s", esp_err_to_name(ret));
            return ret;
        }
        ctx->tx_enabled = false;
    }

    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz);
    ret = i2s_channel_reconfig_std_clock(ctx->tx_channel, &clk_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "reconfigure speaker clock failed: %s", esp_err_to_name(ret));
        (void)i2s_channel_enable(ctx->tx_channel);
        ctx->tx_enabled = true;
        return ret;
    }

    ret = i2s_channel_enable(ctx->tx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "re-enable speaker channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ctx->tx_enabled = true;
    ctx->output_sample_rate_hz = sample_rate_hz;
    return ESP_OK;
}

static void voice_client_reset_playback_stats(voice_client_context_t *ctx)
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
                            TAG,
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
    ESP_LOGI(TAG,
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

static esp_err_t voice_client_write_audio_chunk(voice_client_context_t *ctx,
                                                const uint8_t *data,
                                                int data_len,
                                                bool message_done)
{
    if (ctx == NULL || data == NULL || data_len <= 0) {
        return ESP_OK;
    }

    if (!ctx->playback_streaming || !ctx->playback_pcm) {
        if (!ctx->warned_drop_binary) {
            ESP_LOGW(TAG, "dropping binary audio because no active pcm tts_start was received");
            ctx->warned_drop_binary = true;
        }
        return ESP_OK;
    }

    if (ctx->tts_received_bytes == 0) {
        const char *encoded_format = NULL;
        if (voice_client_payload_looks_encoded(data, (size_t)data_len, &encoded_format)) {
            ESP_LOGE(TAG,
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
                            TAG,
                            "write pending sample");
    }

    if ((remaining % VOICE_CLIENT_SAMPLE_WIDTH_BYTES) != 0) {
        ctx->pending_byte = cursor[remaining - 1];
        ctx->has_pending_byte = true;
        remaining--;
    }

    if (remaining > 0) {
        ESP_RETURN_ON_ERROR(voice_client_write_processed_pcm_i2s(ctx, cursor, remaining),
                            TAG,
                            "write audio chunk");
    }

    if (message_done && ctx->has_pending_byte) {
        ESP_LOGW(TAG, "dropping odd trailing byte at end of binary audio frame");
        ctx->has_pending_byte = false;
    }

    voice_client_log_playback_progress(ctx);
    return ESP_OK;
}

static void voice_client_handle_ready(const char *json)
{
    uint32_t input_sample_rate = 0;
    uint32_t tts_sample_rate = 0;
    char tts_response_format[24] = "";

    (void)voice_client_json_get_u32(json, "input_sample_rate", &input_sample_rate);
    (void)voice_client_json_get_u32(json, "tts_sample_rate", &tts_sample_rate);
    (void)voice_client_json_get_string(json, "tts_response_format", tts_response_format, sizeof(tts_response_format));

    ESP_LOGI(TAG,
             "server ready defaults input_sample_rate=%" PRIu32 " tts_sample_rate=%" PRIu32
             " tts_format=%s",
             input_sample_rate,
             tts_sample_rate,
             tts_response_format[0] != '\0' ? tts_response_format : "(unknown)");
}

static void voice_client_handle_started(const char *json)
{
    uint32_t sample_rate = 0;
    uint32_t channels = 0;
    (void)voice_client_json_get_u32(json, "sample_rate", &sample_rate);
    (void)voice_client_json_get_u32(json, "channels", &channels);

    ESP_LOGI(TAG,
             "server started input sample_rate=%" PRIu32 " channels=%" PRIu32,
             sample_rate,
             channels);
}

static void voice_client_handle_tts_start(voice_client_context_t *ctx, const char *json)
{
    char response_format[24] = "pcm";
    char text[160] = "";
    uint32_t sample_rate = 0;

    (void)voice_client_json_get_string(json, "response_format", response_format, sizeof(response_format));
    (void)voice_client_json_get_string(json, "text", text, sizeof(text));
    (void)voice_client_json_get_u32(json, "sample_rate", &sample_rate);

    ctx->playback_streaming = false;
    ctx->playback_pcm = strcmp(response_format, "pcm") == 0;
    ctx->binary_payload_active = false;
    ctx->warned_drop_binary = false;

    if (!ctx->playback_pcm) {
        ESP_LOGE(TAG, "unsupported TTS response_format=%s; only pcm can be played directly", response_format);
        return;
    }

    if (sample_rate == 0) {
        sample_rate = ctx->output_sample_rate_hz;
    }

    esp_err_t ret = voice_client_set_output_sample_rate(ctx, sample_rate);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "speaker sample rate setup failed: %s", esp_err_to_name(ret));
        ctx->playback_pcm = false;
        return;
    }

    voice_client_reset_playback_stats(ctx);
    ctx->playback_streaming = true;
    ESP_LOGI(TAG,
             "tts_start format=%s sample_rate=%" PRIu32 " volume=%d%% limit=%d%% text=%s",
             response_format,
             sample_rate,
             CONFIG_ESPESP_VOICE_CLIENT_TTS_VOLUME_PERCENT,
             CONFIG_ESPESP_VOICE_CLIENT_TTS_LIMIT_PERCENT,
             text[0] != '\0' ? text : "(empty)");
}

static void voice_client_handle_tts_end(voice_client_context_t *ctx, const char *json)
{
    uint64_t server_bytes = 0;
    (void)voice_client_json_get_u64(json, "bytes", &server_bytes);

    int64_t elapsed_ms = ctx->playback_started_us > 0 ?
                         (esp_timer_get_time() - ctx->playback_started_us) / 1000 :
                         0;
    if (ctx->has_pending_byte) {
        ESP_LOGW(TAG, "dropping pending odd byte at tts_end");
        ctx->has_pending_byte = false;
    }

    ctx->playback_streaming = false;
    ctx->binary_payload_active = false;
    ESP_LOGI(TAG,
             "tts_end server_bytes=%" PRIu64 " received=%" PRIu64 " written=%" PRIu64
             " chunks=%" PRIu32 " peak_in=%" PRIu32 " peak_out=%" PRIu32
             " limited=%" PRIu64 "/%" PRIu64 " elapsed_ms=%" PRId64,
             server_bytes,
             ctx->tts_received_bytes,
             ctx->tts_written_bytes,
             ctx->tts_chunks,
             ctx->tts_input_peak,
             ctx->tts_output_peak,
             ctx->tts_limited_samples,
             ctx->tts_samples,
             elapsed_ms);
    if (ctx->tts_input_peak > 30000 || ctx->tts_limited_samples > 0) {
        ESP_LOGW(TAG,
                 "TTS PCM has little headroom; reduce Voice client module -> TTS playback volume percent if playback is distorted");
    }
}

static void voice_client_handle_control_text(voice_client_context_t *ctx, const char *json)
{
    char type[32];
    if (!voice_client_json_get_string(json, "type", type, sizeof(type))) {
        ESP_LOGW(TAG, "control text without type: %s", json);
        return;
    }

    if (strcmp(type, "ready") == 0) {
        voice_client_handle_ready(json);
    } else if (strcmp(type, "started") == 0) {
        voice_client_handle_started(json);
    } else if (strcmp(type, "transcript") == 0) {
        char text[160] = "";
        (void)voice_client_json_get_string(json, "text", text, sizeof(text));
        ESP_LOGI(TAG, "transcript: %s", text[0] != '\0' ? text : "(empty)");
    } else if (strcmp(type, "tts_start") == 0) {
        voice_client_handle_tts_start(ctx, json);
    } else if (strcmp(type, "tts_end") == 0) {
        voice_client_handle_tts_end(ctx, json);
    } else if (strcmp(type, "committed") == 0) {
        uint32_t queued = 0;
        (void)voice_client_json_get_u32(json, "queued", &queued);
        ESP_LOGI(TAG, "committed queued=%" PRIu32, queued);
    } else if (strcmp(type, "pong") == 0) {
        ESP_LOGD(TAG, "pong");
    } else if (strcmp(type, "error") == 0) {
        char code[64] = "";
        char message[200] = "";
        (void)voice_client_json_get_string(json, "code", code, sizeof(code));
        (void)voice_client_json_get_string(json, "message", message, sizeof(message));
        ESP_LOGE(TAG,
                 "voice_server error code=%s message=%s",
                 code[0] != '\0' ? code : "(unknown)",
                 message[0] != '\0' ? message : json);
    } else {
        ESP_LOGI(TAG, "control type=%s payload=%s", type, json);
    }
}

static void voice_client_handle_text_event(voice_client_context_t *ctx,
                                           const esp_websocket_event_data_t *data)
{
    if (data == NULL || data->data_ptr == NULL) {
        return;
    }

    if (data->payload_offset != 0 || data->data_len != data->payload_len) {
        ESP_LOGW(TAG,
                 "fragmented control text is not supported offset=%d len=%d payload_len=%d",
                 data->payload_offset,
                 data->data_len,
                 data->payload_len);
        return;
    }

    if (data->data_len <= 0 || data->data_len >= VOICE_CLIENT_CONTROL_MAX) {
        ESP_LOGW(TAG, "control text too large len=%d", data->data_len);
        return;
    }

    char text[VOICE_CLIENT_CONTROL_MAX];
    memcpy(text, data->data_ptr, (size_t)data->data_len);
    text[data->data_len] = '\0';
    voice_client_handle_control_text(ctx, text);
}

static void voice_client_handle_data(voice_client_context_t *ctx,
                                     const esp_websocket_event_data_t *data)
{
    if (ctx == NULL || data == NULL) {
        return;
    }

    bool message_done = data->payload_len <= 0 ||
                        data->payload_offset + data->data_len >= data->payload_len;

    switch (data->op_code) {
    case VOICE_CLIENT_OPCODE_TEXT:
        voice_client_handle_text_event(ctx, data);
        break;
    case VOICE_CLIENT_OPCODE_BINARY:
        if (data->payload_offset == 0) {
            ctx->binary_payload_active = true;
        }
        if (voice_client_write_audio_chunk(ctx,
                                           (const uint8_t *)data->data_ptr,
                                           data->data_len,
                                           message_done) != ESP_OK) {
            ESP_LOGE(TAG, "audio chunk write failed");
        }
        if (message_done) {
            ctx->binary_payload_active = false;
        }
        break;
    case VOICE_CLIENT_OPCODE_CONTINUATION:
        if (ctx->binary_payload_active) {
            if (voice_client_write_audio_chunk(ctx,
                                               (const uint8_t *)data->data_ptr,
                                               data->data_len,
                                               message_done) != ESP_OK) {
                ESP_LOGE(TAG, "audio continuation write failed");
            }
            if (message_done) {
                ctx->binary_payload_active = false;
            }
        } else {
            ESP_LOGW(TAG, "ignoring continuation frame without active binary payload");
        }
        break;
    case VOICE_CLIENT_OPCODE_CLOSE:
        ESP_LOGI(TAG, "close frame received");
        break;
    case VOICE_CLIENT_OPCODE_PING:
        ESP_LOGD(TAG, "ping frame received");
        break;
    case VOICE_CLIENT_OPCODE_PONG:
        ESP_LOGD(TAG, "pong frame received");
        break;
    default:
        ESP_LOGW(TAG,
                 "unsupported websocket opcode=0x%x data_len=%d payload_len=%d",
                 data->op_code,
                 data->data_len,
                 data->payload_len);
        break;
    }
}

static void voice_client_event_handler(void *handler_args,
                                       esp_event_base_t base,
                                       int32_t event_id,
                                       void *event_data)
{
    voice_client_context_t *ctx = (voice_client_context_t *)handler_args;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    (void)base;

    switch ((esp_websocket_event_id_t)event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected to %s", CONFIG_ESPESP_VOICE_CLIENT_URI);
        if (ctx != NULL) {
            ctx->start_pending = true;
            ctx->session_started = false;
        }
        if (ctx != NULL && ctx->event_group != NULL) {
            xEventGroupClearBits(ctx->event_group, VOICE_CLIENT_ERROR_BIT);
            xEventGroupSetBits(ctx->event_group, VOICE_CLIENT_CONNECTED_BIT);
        }
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected from %s", CONFIG_ESPESP_VOICE_CLIENT_URI);
        if (ctx != NULL) {
            ctx->session_started = false;
            ctx->playback_streaming = false;
            ctx->binary_payload_active = false;
            ctx->has_pending_byte = false;
        }
        if (ctx != NULL && ctx->event_group != NULL) {
            xEventGroupClearBits(ctx->event_group, VOICE_CLIENT_CONNECTED_BIT);
        }
        break;
    case WEBSOCKET_EVENT_DATA:
        voice_client_handle_data(ctx, data);
        break;
    case WEBSOCKET_EVENT_ERROR:
        if (data != NULL) {
            ESP_LOGE(TAG,
                     "websocket error type=%d status=%d sock_errno=%d tls_err=0x%x",
                     data->error_handle.error_type,
                     data->error_handle.esp_ws_handshake_status_code,
                     data->error_handle.esp_transport_sock_errno,
                     data->error_handle.esp_tls_last_esp_err);
        } else {
            ESP_LOGE(TAG, "websocket client error");
        }
        if (ctx != NULL && ctx->event_group != NULL) {
            xEventGroupSetBits(ctx->event_group, VOICE_CLIENT_ERROR_BIT);
        }
        break;
    case WEBSOCKET_EVENT_CLOSED:
        ESP_LOGI(TAG, "websocket closed by peer");
        if (ctx != NULL && ctx->event_group != NULL) {
            xEventGroupClearBits(ctx->event_group, VOICE_CLIENT_CONNECTED_BIT);
        }
        break;
    case WEBSOCKET_EVENT_BEFORE_CONNECT:
        ESP_LOGD(TAG, "websocket before connect");
        break;
    case WEBSOCKET_EVENT_BEGIN:
        ESP_LOGD(TAG, "websocket transport begin");
        break;
    case WEBSOCKET_EVENT_FINISH:
        ESP_LOGD(TAG, "websocket transport finish");
        break;
    default:
        ESP_LOGD(TAG, "websocket event id=%" PRId32, event_id);
        break;
    }
}

static esp_err_t voice_client_send_audio_frame(esp_websocket_client_handle_t client,
                                               voice_client_context_t *ctx,
                                               const int16_t *pcm,
                                               size_t sample_count)
{
    if (client == NULL || ctx == NULL || pcm == NULL || sample_count == 0) {
        return ESP_OK;
    }

    size_t byte_count = sample_count * sizeof(int16_t);
    if (byte_count > INT32_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    int sent = esp_websocket_client_send_bin(client,
                                             (const char *)pcm,
                                             (int)byte_count,
                                             pdMS_TO_TICKS(CONFIG_ESPESP_VOICE_CLIENT_NETWORK_TIMEOUT_MS));
    if (sent != (int)byte_count) {
        ctx->mic_dropped_chunks++;
        ESP_LOGW(TAG,
                 "send audio failed: sent=%d expected=%u dropped=%" PRIu64,
                 sent,
                 (unsigned int)byte_count,
                 ctx->mic_dropped_chunks);
        return ESP_FAIL;
    }

    ctx->mic_sent_chunks++;
    ctx->mic_sent_bytes += byte_count;
    return ESP_OK;
}

static void voice_client_log_mic_progress(voice_client_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    if (now_us - ctx->last_mic_stats_us < VOICE_CLIENT_STATS_PERIOD_US) {
        return;
    }

    ctx->last_mic_stats_us = now_us;
    ESP_LOGI(TAG,
             "mic stream chunks=%" PRIu64 " sent=%" PRIu64 " dropped=%" PRIu64
             " playback=%s free_heap=%" PRIu32,
             ctx->mic_sent_chunks,
             ctx->mic_sent_bytes,
             ctx->mic_dropped_chunks,
             ctx->playback_streaming ? "true" : "false",
             esp_get_free_heap_size());
}

esp_err_t voice_client_run(void)
{
    if (!voice_client_uri_is_valid(CONFIG_ESPESP_VOICE_CLIENT_URI)) {
        ESP_LOGE(TAG, "voice client URI must start with ws:// or wss://");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(wifi_station_connect(), TAG, "connect Wi-Fi");
    esp_err_t ps_ret = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ps_ret != ESP_OK) {
        ESP_LOGW(TAG, "disable Wi-Fi power save failed: %s", esp_err_to_name(ps_ret));
    }

    const size_t frame_samples =
        (CONFIG_ESPESP_VOICE_CLIENT_INPUT_SAMPLE_RATE_HZ * CONFIG_ESPESP_VOICE_CLIENT_FRAME_MS) / 1000;
    if (frame_samples == 0) {
        ESP_LOGE(TAG, "voice frame sample count is zero");
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
        free(raw_samples);
        free(pcm_samples);
        vEventGroupDelete(ctx.event_group);
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

    ESP_LOGI(TAG,
             "I2S microphone: BCLK=GPIO%d WS=GPIO%d DIN=GPIO%d sample_rate=%dHz slot=%s shift=%d frame=%ums/%u samples",
             CONFIG_ESPESP_MIC_BCLK_GPIO,
             CONFIG_ESPESP_MIC_WS_GPIO,
             CONFIG_ESPESP_MIC_DIN_GPIO,
             CONFIG_ESPESP_VOICE_CLIENT_INPUT_SAMPLE_RATE_HZ,
             VOICE_CLIENT_MIC_SLOT_NAME,
             CONFIG_ESPESP_VOICE_CLIENT_MIC_SAMPLE_SHIFT_BITS,
             CONFIG_ESPESP_VOICE_CLIENT_FRAME_MS,
             (unsigned int)frame_samples);
    ESP_LOGI(TAG,
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

    ESP_LOGI(TAG, "connect uri=%s", CONFIG_ESPESP_VOICE_CLIENT_URI);
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
        ESP_LOGE(TAG, "voice WebSocket connect timeout or error");
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
                ESP_LOGW(TAG, "start event failed: %s", esp_err_to_name(ret));
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
            ESP_LOGW(TAG, "I2S microphone read timeout");
            continue;
        }
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2S microphone read failed: %s", esp_err_to_name(ret));
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
    if (client != NULL) {
        esp_websocket_client_stop(client);
        esp_websocket_client_destroy(client);
    }
    if (ctx.tx_channel != NULL) {
        if (ctx.tx_enabled) {
            i2s_channel_disable(ctx.tx_channel);
        }
        i2s_del_channel(ctx.tx_channel);
    }
    if (ctx.rx_channel != NULL) {
        if (rx_enabled) {
            i2s_channel_disable(ctx.rx_channel);
        }
        i2s_del_channel(ctx.rx_channel);
    }
    free(raw_samples);
    free(pcm_samples);
    vEventGroupDelete(ctx.event_group);
    return ret;
}
