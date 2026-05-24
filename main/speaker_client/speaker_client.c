#include "speaker_client/speaker_client.h"

#include <inttypes.h>
#include <stdbool.h>
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
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "wifi_station/wifi_station.h"

#define SPEAKER_CLIENT_CONNECTED_BIT BIT0
#define SPEAKER_CLIENT_ERROR_BIT BIT1
#define SPEAKER_CLIENT_AUTH_HEADER_MAX 256
#define SPEAKER_CLIENT_CONTROL_MAX 512
#define SPEAKER_CLIENT_OPCODE_CONTINUATION 0x0
#define SPEAKER_CLIENT_OPCODE_TEXT 0x1
#define SPEAKER_CLIENT_OPCODE_BINARY 0x2
#define SPEAKER_CLIENT_OPCODE_CLOSE 0x8
#define SPEAKER_CLIENT_OPCODE_PING 0x9
#define SPEAKER_CLIENT_OPCODE_PONG 0xA

typedef struct {
    EventGroupHandle_t event_group;
    i2s_chan_handle_t tx_channel;
    bool streaming;
    bool binary_payload_active;
    bool warned_drop_without_stream;
    bool has_pending_byte;
    uint8_t pending_byte;
    uint32_t sample_rate_hz;
    uint64_t expected_frames;
    uint64_t received_bytes;
    uint64_t written_bytes;
    uint32_t received_chunks;
    int64_t stream_started_us;
    int64_t last_stats_us;
} speaker_client_context_t;

static const char *TAG = "speaker_client";

static bool speaker_client_uri_is_valid(const char *uri)
{
    return uri != NULL &&
           (strncmp(uri, "ws://", strlen("ws://")) == 0 ||
            strncmp(uri, "wss://", strlen("wss://")) == 0);
}

static esp_err_t speaker_client_make_headers(char *headers, size_t headers_len)
{
    if (headers == NULL || headers_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    headers[0] = '\0';
    if (CONFIG_ESPESP_SPEAKER_CLIENT_AUTH_TOKEN[0] == '\0') {
        return ESP_OK;
    }

    int written = snprintf(headers,
                           headers_len,
                           "Authorization: Bearer %s\r\n",
                           CONFIG_ESPESP_SPEAKER_CLIENT_AUTH_TOKEN);
    if (written < 0 || written >= (int)headers_len) {
        ESP_LOGE(TAG, "speaker client auth token is too long");
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static esp_err_t speaker_client_create_i2s_channel(i2s_chan_handle_t *tx_channel)
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

static const char *speaker_client_find_json_value(const char *json, const char *key)
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

static bool speaker_client_json_get_string(const char *json, const char *key, char *out, size_t out_len)
{
    const char *value = speaker_client_find_json_value(json, key);
    if (value == NULL || *value != '"' || out == NULL || out_len == 0) {
        return false;
    }

    value++;
    size_t pos = 0;
    while (*value != '\0' && *value != '"') {
        if (*value == '\\') {
            value++;
            if (*value == '\0') {
                return false;
            }
        }
        if (pos + 1 >= out_len) {
            return false;
        }
        out[pos++] = *value++;
    }

    if (*value != '"') {
        return false;
    }
    out[pos] = '\0';
    return true;
}

static bool speaker_client_json_get_u64(const char *json, const char *key, uint64_t *out)
{
    const char *value = speaker_client_find_json_value(json, key);
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

static bool speaker_client_json_get_u32(const char *json, const char *key, uint32_t *out)
{
    uint64_t value = 0;
    if (!speaker_client_json_get_u64(json, key, &value) || value > UINT32_MAX) {
        return false;
    }

    *out = (uint32_t)value;
    return true;
}

static void speaker_client_reset_stream_stats(speaker_client_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    ctx->received_bytes = 0;
    ctx->written_bytes = 0;
    ctx->received_chunks = 0;
    ctx->expected_frames = 0;
    ctx->has_pending_byte = false;
    ctx->pending_byte = 0;
    ctx->warned_drop_without_stream = false;
    ctx->stream_started_us = esp_timer_get_time();
    ctx->last_stats_us = ctx->stream_started_us;
}

static esp_err_t speaker_client_write_all_i2s(speaker_client_context_t *ctx, const uint8_t *data, size_t len)
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
                                          CONFIG_ESPESP_SPEAKER_CLIENT_I2S_WRITE_TIMEOUT_MS);
        if (ret != ESP_OK) {
            return ret;
        }
        if (bytes_written == 0) {
            return ESP_ERR_TIMEOUT;
        }
        total_written += bytes_written;
    }

    ctx->written_bytes += total_written;
    return ESP_OK;
}

static void speaker_client_log_progress(speaker_client_context_t *ctx)
{
    if (ctx == NULL || !ctx->streaming) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    if (now_us - ctx->last_stats_us < 1000000) {
        return;
    }

    ctx->last_stats_us = now_us;
    uint64_t frames = ctx->written_bytes / 2;
    uint64_t elapsed_ms = (uint64_t)((now_us - ctx->stream_started_us) / 1000);
    ESP_LOGI(TAG,
             "audio progress chunks=%" PRIu32 " received=%" PRIu64 " written=%" PRIu64 " frames=%" PRIu64 " elapsed_ms=%" PRIu64,
             ctx->received_chunks,
             ctx->received_bytes,
             ctx->written_bytes,
             frames,
             elapsed_ms);
}

static esp_err_t speaker_client_write_audio_chunk(speaker_client_context_t *ctx,
                                                  const uint8_t *data,
                                                  int data_len,
                                                  bool message_done)
{
    if (ctx == NULL || data == NULL || data_len <= 0) {
        return ESP_OK;
    }

    if (!ctx->streaming) {
        if (!ctx->warned_drop_without_stream) {
            ESP_LOGW(TAG, "dropping binary audio because no valid audio_start metadata was received");
            ctx->warned_drop_without_stream = true;
        }
        return ESP_OK;
    }

    ctx->received_chunks++;
    ctx->received_bytes += (uint64_t)data_len;

    const uint8_t *cursor = data;
    size_t remaining = (size_t)data_len;

    if (ctx->has_pending_byte && remaining > 0) {
        uint8_t sample[2] = {ctx->pending_byte, cursor[0]};
        ctx->has_pending_byte = false;
        cursor++;
        remaining--;
        ESP_RETURN_ON_ERROR(speaker_client_write_all_i2s(ctx, sample, sizeof(sample)), TAG, "write pending sample");
    }

    if ((remaining % 2) != 0) {
        ctx->pending_byte = cursor[remaining - 1];
        ctx->has_pending_byte = true;
        remaining--;
    }

    if (remaining > 0) {
        ESP_RETURN_ON_ERROR(speaker_client_write_all_i2s(ctx, cursor, remaining), TAG, "write audio chunk");
    }

    if (message_done && ctx->has_pending_byte) {
        ESP_LOGW(TAG, "dropping odd trailing byte at end of binary audio frame");
        ctx->has_pending_byte = false;
    }

    speaker_client_log_progress(ctx);
    return ESP_OK;
}

static void speaker_client_handle_audio_start(speaker_client_context_t *ctx, const char *json)
{
    char format[24];
    char source[96] = "";
    uint32_t sample_rate_hz = 0;
    uint32_t channels = 0;
    uint32_t sample_width_bits = 0;
    uint32_t chunk_bytes = 0;
    uint64_t frames = 0;

    if (!speaker_client_json_get_string(json, "format", format, sizeof(format)) ||
        !speaker_client_json_get_u32(json, "sample_rate_hz", &sample_rate_hz) ||
        !speaker_client_json_get_u32(json, "channels", &channels) ||
        !speaker_client_json_get_u32(json, "sample_width_bits", &sample_width_bits)) {
        ESP_LOGE(TAG, "audio_start metadata missing required fields: %s", json);
        ctx->streaming = false;
        return;
    }

    (void)speaker_client_json_get_u32(json, "chunk_bytes", &chunk_bytes);
    (void)speaker_client_json_get_u64(json, "frames", &frames);
    (void)speaker_client_json_get_string(json, "source", source, sizeof(source));

    if (strcmp(format, "pcm_s16le") != 0 || channels != 1 || sample_width_bits != 16) {
        ESP_LOGE(TAG,
                 "unsupported audio format: format=%s channels=%" PRIu32 " bits=%" PRIu32,
                 format,
                 channels,
                 sample_width_bits);
        ctx->streaming = false;
        return;
    }

    if (sample_rate_hz != CONFIG_ESPESP_SPK_SAMPLE_RATE_HZ) {
        ESP_LOGE(TAG,
                 "sample rate mismatch: server=%" PRIu32 " Hz local_i2s=%d Hz",
                 sample_rate_hz,
                 CONFIG_ESPESP_SPK_SAMPLE_RATE_HZ);
        ctx->streaming = false;
        return;
    }

    speaker_client_reset_stream_stats(ctx);
    ctx->sample_rate_hz = sample_rate_hz;
    ctx->expected_frames = frames;
    ctx->streaming = true;
    ctx->binary_payload_active = false;

    ESP_LOGI(TAG,
             "audio_start source=%s format=%s sample_rate=%" PRIu32 " channels=%" PRIu32 " bits=%" PRIu32 " frames=%" PRIu64 " chunk_bytes=%" PRIu32,
             source[0] != '\0' ? source : "(unknown)",
             format,
             sample_rate_hz,
             channels,
             sample_width_bits,
             frames,
             chunk_bytes);
}

static void speaker_client_handle_audio_end(speaker_client_context_t *ctx, const char *json)
{
    uint64_t frames = 0;
    uint64_t byte_count = 0;
    (void)speaker_client_json_get_u64(json, "frames", &frames);
    (void)speaker_client_json_get_u64(json, "bytes", &byte_count);

    int64_t now_us = esp_timer_get_time();
    uint64_t elapsed_ms = (ctx != NULL && ctx->stream_started_us > 0) ?
                          (uint64_t)((now_us - ctx->stream_started_us) / 1000) :
                          0;
    uint64_t local_frames = ctx != NULL ? ctx->written_bytes / 2 : 0;

    if (ctx != NULL && ctx->has_pending_byte) {
        ESP_LOGW(TAG, "dropping pending odd byte at audio_end");
        ctx->has_pending_byte = false;
    }

    if (ctx != NULL) {
        ctx->streaming = false;
        ctx->binary_payload_active = false;
    }

    ESP_LOGI(TAG,
             "audio_end server_frames=%" PRIu64 " server_bytes=%" PRIu64 " local_frames=%" PRIu64 " local_bytes=%" PRIu64 " elapsed_ms=%" PRIu64,
             frames,
             byte_count,
             local_frames,
             ctx != NULL ? ctx->written_bytes : 0,
             elapsed_ms);
}

static void speaker_client_handle_control_text(speaker_client_context_t *ctx, const char *json)
{
    char type[32];
    if (!speaker_client_json_get_string(json, "type", type, sizeof(type))) {
        ESP_LOGW(TAG, "control text without type: %s", json);
        return;
    }

    if (strcmp(type, "audio_start") == 0) {
        speaker_client_handle_audio_start(ctx, json);
    } else if (strcmp(type, "audio_end") == 0) {
        speaker_client_handle_audio_end(ctx, json);
    } else if (strcmp(type, "error") == 0) {
        char message[160] = "";
        (void)speaker_client_json_get_string(json, "message", message, sizeof(message));
        ESP_LOGE(TAG, "speaker_server error: %s", message[0] != '\0' ? message : json);
    } else {
        ESP_LOGI(TAG, "control text type=%s payload=%s", type, json);
    }
}

static void speaker_client_handle_text_event(speaker_client_context_t *ctx,
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

    if (data->data_len <= 0 || data->data_len >= SPEAKER_CLIENT_CONTROL_MAX) {
        ESP_LOGW(TAG, "control text too large len=%d", data->data_len);
        return;
    }

    char text[SPEAKER_CLIENT_CONTROL_MAX];
    memcpy(text, data->data_ptr, (size_t)data->data_len);
    text[data->data_len] = '\0';
    speaker_client_handle_control_text(ctx, text);
}

static void speaker_client_handle_data(speaker_client_context_t *ctx,
                                       const esp_websocket_event_data_t *data)
{
    if (ctx == NULL || data == NULL) {
        return;
    }

    bool message_done = data->payload_len <= 0 ||
                        data->payload_offset + data->data_len >= data->payload_len;

    switch (data->op_code) {
    case SPEAKER_CLIENT_OPCODE_TEXT:
        speaker_client_handle_text_event(ctx, data);
        break;
    case SPEAKER_CLIENT_OPCODE_BINARY:
        if (data->payload_offset == 0) {
            ctx->binary_payload_active = true;
        }
        if (speaker_client_write_audio_chunk(ctx,
                                             (const uint8_t *)data->data_ptr,
                                             data->data_len,
                                             message_done) != ESP_OK) {
            ESP_LOGE(TAG, "audio chunk write failed");
        }
        if (message_done) {
            ctx->binary_payload_active = false;
        }
        break;
    case SPEAKER_CLIENT_OPCODE_CONTINUATION:
        if (ctx->binary_payload_active) {
            if (speaker_client_write_audio_chunk(ctx,
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
    case SPEAKER_CLIENT_OPCODE_CLOSE:
        ESP_LOGI(TAG, "close frame received");
        break;
    case SPEAKER_CLIENT_OPCODE_PING:
        ESP_LOGD(TAG, "ping frame received");
        break;
    case SPEAKER_CLIENT_OPCODE_PONG:
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

static void speaker_client_event_handler(void *handler_args,
                                         esp_event_base_t base,
                                         int32_t event_id,
                                         void *event_data)
{
    speaker_client_context_t *ctx = (speaker_client_context_t *)handler_args;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    (void)base;

    switch ((esp_websocket_event_id_t)event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected to %s", CONFIG_ESPESP_SPEAKER_CLIENT_URI);
        if (ctx != NULL && ctx->event_group != NULL) {
            xEventGroupClearBits(ctx->event_group, SPEAKER_CLIENT_ERROR_BIT);
            xEventGroupSetBits(ctx->event_group, SPEAKER_CLIENT_CONNECTED_BIT);
        }
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected from %s", CONFIG_ESPESP_SPEAKER_CLIENT_URI);
        if (ctx != NULL) {
            ctx->streaming = false;
            ctx->binary_payload_active = false;
            ctx->has_pending_byte = false;
        }
        if (ctx != NULL && ctx->event_group != NULL) {
            xEventGroupClearBits(ctx->event_group, SPEAKER_CLIENT_CONNECTED_BIT);
        }
        break;
    case WEBSOCKET_EVENT_DATA:
        speaker_client_handle_data(ctx, data);
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "websocket client error");
        if (ctx != NULL && ctx->event_group != NULL) {
            xEventGroupSetBits(ctx->event_group, SPEAKER_CLIENT_ERROR_BIT);
        }
        break;
    case WEBSOCKET_EVENT_CLOSED:
        ESP_LOGI(TAG, "websocket closed by peer");
        if (ctx != NULL && ctx->event_group != NULL) {
            xEventGroupClearBits(ctx->event_group, SPEAKER_CLIENT_CONNECTED_BIT);
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

static esp_err_t speaker_client_send_status(esp_websocket_client_handle_t client,
                                            const speaker_client_context_t *ctx)
{
    if (client == NULL || ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char payload[256];
    int written = snprintf(payload,
                           sizeof(payload),
                           "{\"type\":\"status\",\"service\":\"speaker_client\",\"streaming\":%s,\"received_bytes\":%" PRIu64 ",\"written_bytes\":%" PRIu64 ",\"chunks\":%" PRIu32 ",\"free_heap\":%" PRIu32 ",\"uptime_ms\":%" PRId64 "}",
                           ctx->streaming ? "true" : "false",
                           ctx->received_bytes,
                           ctx->written_bytes,
                           ctx->received_chunks,
                           esp_get_free_heap_size(),
                           esp_timer_get_time() / 1000);
    if (written < 0 || written >= (int)sizeof(payload)) {
        ESP_LOGE(TAG, "status payload too large");
        return ESP_FAIL;
    }

    int sent = esp_websocket_client_send_text(client,
                                              payload,
                                              written,
                                              pdMS_TO_TICKS(CONFIG_ESPESP_SPEAKER_CLIENT_NETWORK_TIMEOUT_MS));
    if (sent < 0) {
        ESP_LOGW(TAG, "send status failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG,
             "status streaming=%s received=%" PRIu64 " written=%" PRIu64 " chunks=%" PRIu32,
             ctx->streaming ? "true" : "false",
             ctx->received_bytes,
             ctx->written_bytes,
             ctx->received_chunks);
    return ESP_OK;
}

esp_err_t speaker_client_run(void)
{
    if (!speaker_client_uri_is_valid(CONFIG_ESPESP_SPEAKER_CLIENT_URI)) {
        ESP_LOGE(TAG, "speaker client URI must start with ws:// or wss://");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(wifi_station_connect(), TAG, "connect Wi-Fi");

    speaker_client_context_t ctx = {
        .event_group = xEventGroupCreate(),
        .tx_channel = NULL,
        .streaming = false,
        .binary_payload_active = false,
        .warned_drop_without_stream = false,
        .has_pending_byte = false,
        .pending_byte = 0,
        .sample_rate_hz = CONFIG_ESPESP_SPK_SAMPLE_RATE_HZ,
        .expected_frames = 0,
        .received_bytes = 0,
        .written_bytes = 0,
        .received_chunks = 0,
        .stream_started_us = 0,
        .last_stats_us = 0,
    };
    if (ctx.event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = speaker_client_create_i2s_channel(&ctx.tx_channel);
    if (ret != ESP_OK) {
        vEventGroupDelete(ctx.event_group);
        return ret;
    }

    ret = i2s_channel_enable(ctx.tx_channel);
    if (ret != ESP_OK) {
        i2s_del_channel(ctx.tx_channel);
        vEventGroupDelete(ctx.event_group);
        return ret;
    }

    ESP_LOGI(TAG,
             "I2S speaker client: BCLK=GPIO%d, WS=GPIO%d, DOUT=GPIO%d, sample_rate=%d Hz",
             CONFIG_ESPESP_SPK_BCLK_GPIO,
             CONFIG_ESPESP_SPK_WS_GPIO,
             CONFIG_ESPESP_SPK_DOUT_GPIO,
             CONFIG_ESPESP_SPK_SAMPLE_RATE_HZ);

    char headers[SPEAKER_CLIENT_AUTH_HEADER_MAX];
    ret = speaker_client_make_headers(headers, sizeof(headers));
    if (ret != ESP_OK) {
        i2s_channel_disable(ctx.tx_channel);
        i2s_del_channel(ctx.tx_channel);
        vEventGroupDelete(ctx.event_group);
        return ret;
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

    esp_websocket_client_handle_t client = esp_websocket_client_init(&config);
    if (client == NULL) {
        i2s_channel_disable(ctx.tx_channel);
        i2s_del_channel(ctx.tx_channel);
        vEventGroupDelete(ctx.event_group);
        return ESP_ERR_NO_MEM;
    }

    ret = esp_websocket_register_events(client,
                                        WEBSOCKET_EVENT_ANY,
                                        speaker_client_event_handler,
                                        &ctx);
    if (ret != ESP_OK) {
        esp_websocket_client_destroy(client);
        i2s_channel_disable(ctx.tx_channel);
        i2s_del_channel(ctx.tx_channel);
        vEventGroupDelete(ctx.event_group);
        return ret;
    }

    ESP_LOGI(TAG, "connect uri=%s", CONFIG_ESPESP_SPEAKER_CLIENT_URI);
    ret = esp_websocket_client_start(client);
    if (ret != ESP_OK) {
        esp_websocket_client_destroy(client);
        i2s_channel_disable(ctx.tx_channel);
        i2s_del_channel(ctx.tx_channel);
        vEventGroupDelete(ctx.event_group);
        return ret;
    }

    EventBits_t bits = xEventGroupWaitBits(ctx.event_group,
                                           SPEAKER_CLIENT_CONNECTED_BIT | SPEAKER_CLIENT_ERROR_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(CONFIG_ESPESP_SPEAKER_CLIENT_CONNECT_TIMEOUT_MS));
    if ((bits & SPEAKER_CLIENT_CONNECTED_BIT) == 0) {
        ESP_LOGE(TAG, "speaker WebSocket connect timeout or error");
        esp_websocket_client_stop(client);
        esp_websocket_client_destroy(client);
        i2s_channel_disable(ctx.tx_channel);
        i2s_del_channel(ctx.tx_channel);
        vEventGroupDelete(ctx.event_group);
        return (bits & SPEAKER_CLIENT_ERROR_BIT) ? ESP_FAIL : ESP_ERR_TIMEOUT;
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_ESPESP_SPEAKER_CLIENT_STATUS_PERIOD_MS));

        if (!esp_websocket_client_is_connected(client)) {
            ESP_LOGW(TAG,
                     "waiting for speaker_server reconnect, received=%" PRIu64 " written=%" PRIu64,
                     ctx.received_bytes,
                     ctx.written_bytes);
            continue;
        }

        ret = speaker_client_send_status(client, &ctx);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "status publish failed: %s", esp_err_to_name(ret));
        }
    }

    return ESP_OK;
}
