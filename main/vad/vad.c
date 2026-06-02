#include "vad/vad.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vad.h"
#include "sdkconfig.h"

static const char *TAG = "vad";

#define VAD_SAMPLE_RATE_HZ 16000
#define VAD_I2S_READ_TIMEOUT_MS 1000
#define VAD_STATS_PERIOD_US 1000000LL
#define VAD_FRAME_SAMPLES ((VAD_SAMPLE_RATE_HZ * CONFIG_ESPESP_VAD_FRAME_MS) / 1000)

#if CONFIG_ESPESP_VAD_MODE_0
#define ESPESP_VAD_MODE VAD_MODE_0
#define ESPESP_VAD_MODE_NAME "0 normal"
#elif CONFIG_ESPESP_VAD_MODE_1
#define ESPESP_VAD_MODE VAD_MODE_1
#define ESPESP_VAD_MODE_NAME "1 aggressive"
#elif CONFIG_ESPESP_VAD_MODE_2
#define ESPESP_VAD_MODE VAD_MODE_2
#define ESPESP_VAD_MODE_NAME "2 very aggressive"
#elif CONFIG_ESPESP_VAD_MODE_3
#define ESPESP_VAD_MODE VAD_MODE_3
#define ESPESP_VAD_MODE_NAME "3 very very aggressive"
#else
#define ESPESP_VAD_MODE VAD_MODE_4
#define ESPESP_VAD_MODE_NAME "4 maximum"
#endif

#if CONFIG_ESPESP_VAD_MIC_SLOT_RIGHT
#define VAD_I2S_SLOT_MASK I2S_STD_SLOT_RIGHT
#define VAD_I2S_SLOT_NAME "right"
#else
#define VAD_I2S_SLOT_MASK I2S_STD_SLOT_LEFT
#define VAD_I2S_SLOT_NAME "left"
#endif

typedef struct {
    i2s_chan_handle_t rx_channel;
    vad_handle_t vad_handle;
    int16_t *pcm_frame;
    int32_t *i2s_frame;
    size_t frames;
    size_t speech_frames;
    size_t speech_segments;
    int64_t speech_started_us;
    int64_t last_stats_us;
    vad_state_t last_state;
    bool has_state;
} vad_context_t;

static esp_err_t vad_create_rx_channel(i2s_chan_handle_t *rx_channel)
{
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, NULL, rx_channel), TAG, "create I2S RX channel");

    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(VAD_SAMPLE_RATE_HZ),
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
    std_config.slot_cfg.slot_mask = VAD_I2S_SLOT_MASK;

    esp_err_t ret = i2s_channel_init_std_mode(*rx_channel, &std_config);
    if (ret != ESP_OK) {
        i2s_del_channel(*rx_channel);
        *rx_channel = NULL;
    }

    return ret;
}

static int16_t vad_convert_sample(int32_t sample)
{
    int32_t pcm = sample >> CONFIG_ESPESP_VAD_SAMPLE_SHIFT_BITS;
    if (pcm > INT16_MAX) {
        pcm = INT16_MAX;
    } else if (pcm < INT16_MIN) {
        pcm = INT16_MIN;
    }
    return (int16_t)pcm;
}

static const char *vad_state_name(vad_state_t state)
{
    return state == VAD_SPEECH ? "speech" : "silence";
}

static void vad_cleanup(vad_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->vad_handle != NULL) {
        vad_destroy(ctx->vad_handle);
        ctx->vad_handle = NULL;
    }

    if (ctx->rx_channel != NULL) {
        (void)i2s_channel_disable(ctx->rx_channel);
        (void)i2s_del_channel(ctx->rx_channel);
        ctx->rx_channel = NULL;
    }

    free(ctx->pcm_frame);
    ctx->pcm_frame = NULL;
    free(ctx->i2s_frame);
    ctx->i2s_frame = NULL;
}

static void vad_log_state_change(vad_context_t *ctx, vad_state_t state, uint32_t avg_abs, int32_t peak)
{
    int64_t now_us = esp_timer_get_time();

    if (state == VAD_SPEECH) {
        ctx->speech_segments++;
        ctx->speech_started_us = now_us;
        ESP_LOGI(TAG,
                 "voice activity started: segment=%u, avg_abs=%" PRIu32 ", peak=%" PRIi32,
                 (unsigned int)ctx->speech_segments,
                 avg_abs,
                 peak);
        return;
    }

    int64_t duration_ms = ctx->speech_started_us > 0 ? (now_us - ctx->speech_started_us) / 1000 : 0;
    ESP_LOGI(TAG,
             "voice activity ended: duration_ms=%" PRId64 ", avg_abs=%" PRIu32 ", peak=%" PRIi32,
             duration_ms,
             avg_abs,
             peak);
    ctx->speech_started_us = 0;
}

static void vad_log_periodic_stats(vad_context_t *ctx, uint32_t avg_abs, int32_t peak, vad_state_t state)
{
    int64_t now_us = esp_timer_get_time();
    if (ctx->last_stats_us != 0 && now_us - ctx->last_stats_us < VAD_STATS_PERIOD_US) {
        return;
    }

    ctx->last_stats_us = now_us;
    ESP_LOGI(TAG,
             "listening: frames=%u, state=%s, speech_frames=%u, segments=%u, avg_abs=%" PRIu32 ", peak=%" PRIi32,
             (unsigned int)ctx->frames,
             vad_state_name(state),
             (unsigned int)ctx->speech_frames,
             (unsigned int)ctx->speech_segments,
             avg_abs,
             peak);
}

esp_err_t vad_run(void)
{
    if (VAD_FRAME_SAMPLES <= 0) {
        ESP_LOGE(TAG, "invalid VAD frame size");
        return ESP_ERR_INVALID_ARG;
    }

    vad_context_t ctx = {
        .last_state = VAD_SILENCE,
    };
    esp_err_t ret = ESP_OK;

    ctx.vad_handle = vad_create_with_param(ESPESP_VAD_MODE,
                                           VAD_SAMPLE_RATE_HZ,
                                           CONFIG_ESPESP_VAD_FRAME_MS,
                                           CONFIG_ESPESP_VAD_MIN_SPEECH_MS,
                                           CONFIG_ESPESP_VAD_MIN_SILENCE_MS);
    if (ctx.vad_handle == NULL) {
        ESP_LOGE(TAG, "create VAD failed");
        return ESP_ERR_NO_MEM;
    }

    ctx.pcm_frame = calloc((size_t)VAD_FRAME_SAMPLES, sizeof(ctx.pcm_frame[0]));
    ctx.i2s_frame = calloc((size_t)VAD_FRAME_SAMPLES, sizeof(ctx.i2s_frame[0]));
    if (ctx.pcm_frame == NULL || ctx.i2s_frame == NULL) {
        ESP_LOGE(TAG, "allocate audio frames failed");
        vad_cleanup(&ctx);
        return ESP_ERR_NO_MEM;
    }

    ESP_GOTO_ON_ERROR(vad_create_rx_channel(&ctx.rx_channel), cleanup, TAG, "init I2S microphone");
    ESP_GOTO_ON_ERROR(i2s_channel_enable(ctx.rx_channel), cleanup, TAG, "enable I2S microphone");

    ESP_LOGI(TAG,
             "VAD listening: sample_rate=%d Hz, frame_ms=%d, frame_samples=%u, mode=%s, min_speech_ms=%d, "
             "min_silence_ms=%d, slot=%s, shift=%d",
             VAD_SAMPLE_RATE_HZ,
             CONFIG_ESPESP_VAD_FRAME_MS,
             (unsigned int)VAD_FRAME_SAMPLES,
             ESPESP_VAD_MODE_NAME,
             CONFIG_ESPESP_VAD_MIN_SPEECH_MS,
             CONFIG_ESPESP_VAD_MIN_SILENCE_MS,
             VAD_I2S_SLOT_NAME,
             CONFIG_ESPESP_VAD_SAMPLE_SHIFT_BITS);

    while (true) {
        size_t bytes_read = 0;
        ret = i2s_channel_read(ctx.rx_channel,
                               ctx.i2s_frame,
                               (size_t)VAD_FRAME_SAMPLES * sizeof(ctx.i2s_frame[0]),
                               &bytes_read,
                               VAD_I2S_READ_TIMEOUT_MS);
        if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "I2S read timeout, check microphone wiring and slot");
            continue;
        }
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "read I2S microphone failed: %s", esp_err_to_name(ret));
            break;
        }

        size_t sample_count = bytes_read / sizeof(ctx.i2s_frame[0]);
        if (sample_count != (size_t)VAD_FRAME_SAMPLES) {
            ESP_LOGW(TAG,
                     "short I2S frame: expected=%u samples, got=%u",
                     (unsigned int)VAD_FRAME_SAMPLES,
                     (unsigned int)sample_count);
            continue;
        }

        int64_t sum_abs = 0;
        int32_t peak = 0;
        for (size_t i = 0; i < sample_count; i++) {
            int16_t sample = vad_convert_sample(ctx.i2s_frame[i]);
            int32_t magnitude = sample == INT16_MIN ? INT16_MAX : abs(sample);
            ctx.pcm_frame[i] = sample;
            sum_abs += magnitude;
            if (magnitude > peak) {
                peak = magnitude;
            }
        }

        uint32_t avg_abs = sample_count > 0 ? (uint32_t)(sum_abs / (int64_t)sample_count) : 0;
        vad_state_t state = vad_process_with_trigger(ctx.vad_handle, ctx.pcm_frame);
        ctx.frames++;
        if (state == VAD_SPEECH) {
            ctx.speech_frames++;
        }

        if (!ctx.has_state || state != ctx.last_state) {
            vad_log_state_change(&ctx, state, avg_abs, peak);
            ctx.has_state = true;
            ctx.last_state = state;
        }
        vad_log_periodic_stats(&ctx, avg_abs, peak, state);
    }

cleanup:
    vad_cleanup(&ctx);
    return ret;
}
