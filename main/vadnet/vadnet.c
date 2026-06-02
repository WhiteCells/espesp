#include "vadnet/vadnet.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vad.h"
#include "esp_vadn_models.h"
#include "model_path.h"
#include "sdkconfig.h"

static const char *TAG = "vadnet";

#define VADNET_I2S_READ_TIMEOUT_MS 1000
#define VADNET_STATS_PERIOD_US 1000000LL
#define VADNET_CHANNELS 1

#if CONFIG_ESPESP_VADNET_MODE_0
#define ESPESP_VADNET_MODE VAD_MODE_0
#define ESPESP_VADNET_MODE_NAME "0 normal"
#elif CONFIG_ESPESP_VADNET_MODE_1
#define ESPESP_VADNET_MODE VAD_MODE_1
#define ESPESP_VADNET_MODE_NAME "1 aggressive"
#elif CONFIG_ESPESP_VADNET_MODE_2
#define ESPESP_VADNET_MODE VAD_MODE_2
#define ESPESP_VADNET_MODE_NAME "2 very aggressive"
#elif CONFIG_ESPESP_VADNET_MODE_3
#define ESPESP_VADNET_MODE VAD_MODE_3
#define ESPESP_VADNET_MODE_NAME "3 very very aggressive"
#else
#define ESPESP_VADNET_MODE VAD_MODE_4
#define ESPESP_VADNET_MODE_NAME "4 maximum"
#endif

#if CONFIG_ESPESP_VADNET_MIC_SLOT_RIGHT
#define VADNET_I2S_SLOT_MASK I2S_STD_SLOT_RIGHT
#define VADNET_I2S_SLOT_NAME "right"
#else
#define VADNET_I2S_SLOT_MASK I2S_STD_SLOT_LEFT
#define VADNET_I2S_SLOT_NAME "left"
#endif

typedef struct {
    i2s_chan_handle_t rx_channel;
    srmodel_list_t *models;
    const esp_vadn_iface_t *vadnet_iface;
    model_iface_data_t *vadnet_model;
    const char *model_name;
    int sample_rate_hz;
    int frame_samples;
    int16_t *pcm_frame;
    int32_t *i2s_frame;
    size_t frames;
    size_t speech_frames;
    size_t speech_segments;
    int64_t speech_started_us;
    int64_t last_stats_us;
    vad_state_t last_state;
    bool has_state;
} vadnet_context_t;

static bool vadnet_string_contains(const char *haystack, const char *needle)
{
    return haystack != NULL && needle != NULL && needle[0] != '\0' && strstr(haystack, needle) != NULL;
}

static bool vadnet_is_model(const char *name)
{
    return name != NULL && strncmp(name, ESP_VADN_PREFIX, strlen(ESP_VADN_PREFIX)) == 0;
}

static const char *vadnet_select_model(srmodel_list_t *models)
{
    if (models == NULL) {
        return NULL;
    }

    const char *configured = CONFIG_ESPESP_VADNET_MODEL;
    const char *fallback = NULL;

    for (int i = 0; i < models->num; i++) {
        const char *name = models->model_name[i];
        if (!vadnet_is_model(name)) {
            continue;
        }

        ESP_LOGI(TAG, "available VADNet model[%d]: %s", i, name);

        if (fallback == NULL) {
            fallback = name;
        }

        if (vadnet_string_contains(name, configured)) {
            return name;
        }
    }

    return fallback;
}

static esp_err_t vadnet_create_rx_channel(vadnet_context_t *ctx)
{
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, NULL, &ctx->rx_channel), TAG, "create I2S RX channel");

    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(ctx->sample_rate_hz),
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
    std_config.slot_cfg.slot_mask = VADNET_I2S_SLOT_MASK;

    esp_err_t ret = i2s_channel_init_std_mode(ctx->rx_channel, &std_config);
    if (ret != ESP_OK) {
        i2s_del_channel(ctx->rx_channel);
        ctx->rx_channel = NULL;
    }

    return ret;
}

static int16_t vadnet_convert_sample(int32_t sample)
{
    int32_t pcm = sample >> CONFIG_ESPESP_VADNET_SAMPLE_SHIFT_BITS;
    if (pcm > INT16_MAX) {
        pcm = INT16_MAX;
    } else if (pcm < INT16_MIN) {
        pcm = INT16_MIN;
    }
    return (int16_t)pcm;
}

static const char *vadnet_state_name(vad_state_t state)
{
    return state == VAD_SPEECH ? "speech" : "silence";
}

static void vadnet_cleanup(vadnet_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->vadnet_iface != NULL && ctx->vadnet_model != NULL) {
        ctx->vadnet_iface->destroy(ctx->vadnet_model);
        ctx->vadnet_model = NULL;
    }

    if (ctx->rx_channel != NULL) {
        (void)i2s_channel_disable(ctx->rx_channel);
        (void)i2s_del_channel(ctx->rx_channel);
        ctx->rx_channel = NULL;
    }

    if (ctx->models != NULL) {
        esp_srmodel_deinit(ctx->models);
        ctx->models = NULL;
    }

    free(ctx->pcm_frame);
    ctx->pcm_frame = NULL;
    free(ctx->i2s_frame);
    ctx->i2s_frame = NULL;
}

static void vadnet_log_state_change(vadnet_context_t *ctx, vad_state_t state, uint32_t avg_abs, int32_t peak)
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

static void vadnet_log_periodic_stats(vadnet_context_t *ctx, uint32_t avg_abs, int32_t peak, vad_state_t state)
{
    int64_t now_us = esp_timer_get_time();
    if (ctx->last_stats_us != 0 && now_us - ctx->last_stats_us < VADNET_STATS_PERIOD_US) {
        return;
    }

    ctx->last_stats_us = now_us;
    ESP_LOGI(TAG,
             "listening: frames=%u, state=%s, speech_frames=%u, segments=%u, avg_abs=%" PRIu32 ", peak=%" PRIi32,
             (unsigned int)ctx->frames,
             vadnet_state_name(state),
             (unsigned int)ctx->speech_frames,
             (unsigned int)ctx->speech_segments,
             avg_abs,
             peak);
}

static esp_err_t vadnet_init_model(vadnet_context_t *ctx)
{
    ctx->models = esp_srmodel_init("model");
    if (ctx->models == NULL) {
        ESP_LOGE(TAG, "no speech models found in partition label 'model'");
        return ESP_ERR_NOT_FOUND;
    }

    ctx->model_name = vadnet_select_model(ctx->models);
    if (ctx->model_name == NULL) {
        ESP_LOGE(TAG, "no VADNet model found in partition label 'model'");
        ESP_LOGE(TAG, "enable ESP Speech Recognition -> Select voice activity detection -> vadnet1 medium");
        return ESP_ERR_NOT_FOUND;
    }

    ctx->vadnet_iface = esp_vadn_handle_from_name(ctx->model_name);
    if (ctx->vadnet_iface == NULL) {
        ESP_LOGE(TAG, "get VADNet handle failed: %s", ctx->model_name);
        return ESP_ERR_NOT_FOUND;
    }

    ctx->vadnet_model = ctx->vadnet_iface->create(ctx->model_name,
                                                  ESPESP_VADNET_MODE,
                                                  VADNET_CHANNELS,
                                                  CONFIG_ESPESP_VADNET_MIN_SPEECH_MS,
                                                  CONFIG_ESPESP_VADNET_MIN_SILENCE_MS);
    if (ctx->vadnet_model == NULL) {
        ESP_LOGE(TAG, "create VADNet model failed: %s", ctx->model_name);
        return ESP_ERR_NO_MEM;
    }

    ctx->frame_samples = ctx->vadnet_iface->get_samp_chunksize(ctx->vadnet_model);
    ctx->sample_rate_hz = ctx->vadnet_iface->get_samp_rate(ctx->vadnet_model);
    int channel_num = ctx->vadnet_iface->get_channel_num(ctx->vadnet_model);
    if (ctx->frame_samples <= 0 || ctx->sample_rate_hz <= 0 || channel_num != VADNET_CHANNELS) {
        ESP_LOGE(TAG,
                 "invalid VADNet model shape: frame_samples=%d sample_rate=%d channels=%d",
                 ctx->frame_samples,
                 ctx->sample_rate_hz,
                 channel_num);
        return ESP_ERR_INVALID_ARG;
    }

#if CONFIG_ESPESP_VADNET_SET_THRESHOLD
    if (ctx->vadnet_iface->set_det_threshold(ctx->vadnet_model, CONFIG_ESPESP_VADNET_THRESHOLD) != 1) {
        ESP_LOGW(TAG, "set VADNet threshold failed: %.3f", (double)CONFIG_ESPESP_VADNET_THRESHOLD);
    }
#endif

    ESP_LOGI(TAG,
             "selected VADNet model: %s, threshold=%.3f",
             ctx->model_name,
             (double)ctx->vadnet_iface->get_det_threshold(ctx->vadnet_model));
    return ESP_OK;
}

esp_err_t vadnet_run(void)
{
    vadnet_context_t ctx = {
        .last_state = VAD_SILENCE,
    };
    esp_err_t ret = vadnet_init_model(&ctx);
    if (ret != ESP_OK) {
        vadnet_cleanup(&ctx);
        return ret;
    }

    ctx.pcm_frame = calloc((size_t)ctx.frame_samples, sizeof(ctx.pcm_frame[0]));
    ctx.i2s_frame = calloc((size_t)ctx.frame_samples, sizeof(ctx.i2s_frame[0]));
    if (ctx.pcm_frame == NULL || ctx.i2s_frame == NULL) {
        ESP_LOGE(TAG, "allocate audio frames failed");
        vadnet_cleanup(&ctx);
        return ESP_ERR_NO_MEM;
    }

    ESP_GOTO_ON_ERROR(vadnet_create_rx_channel(&ctx), cleanup, TAG, "init I2S microphone");
    ESP_GOTO_ON_ERROR(i2s_channel_enable(ctx.rx_channel), cleanup, TAG, "enable I2S microphone");

    ESP_LOGI(TAG,
             "VADNet listening: sample_rate=%d Hz, frame_samples=%u, mode=%s, min_speech_ms=%d, "
             "min_silence_ms=%d, slot=%s, shift=%d",
             ctx.sample_rate_hz,
             (unsigned int)ctx.frame_samples,
             ESPESP_VADNET_MODE_NAME,
             CONFIG_ESPESP_VADNET_MIN_SPEECH_MS,
             CONFIG_ESPESP_VADNET_MIN_SILENCE_MS,
             VADNET_I2S_SLOT_NAME,
             CONFIG_ESPESP_VADNET_SAMPLE_SHIFT_BITS);

    while (true) {
        size_t bytes_read = 0;
        ret = i2s_channel_read(ctx.rx_channel,
                               ctx.i2s_frame,
                               (size_t)ctx.frame_samples * sizeof(ctx.i2s_frame[0]),
                               &bytes_read,
                               VADNET_I2S_READ_TIMEOUT_MS);
        if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "I2S read timeout, check microphone wiring and slot");
            continue;
        }
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "read I2S microphone failed: %s", esp_err_to_name(ret));
            break;
        }

        size_t sample_count = bytes_read / sizeof(ctx.i2s_frame[0]);
        if (sample_count != (size_t)ctx.frame_samples) {
            ESP_LOGW(TAG,
                     "short I2S frame: expected=%u samples, got=%u",
                     (unsigned int)ctx.frame_samples,
                     (unsigned int)sample_count);
            continue;
        }

        int64_t sum_abs = 0;
        int32_t peak = 0;
        for (size_t i = 0; i < sample_count; i++) {
            int16_t sample = vadnet_convert_sample(ctx.i2s_frame[i]);
            int32_t magnitude = sample == INT16_MIN ? INT16_MAX : abs(sample);
            ctx.pcm_frame[i] = sample;
            sum_abs += magnitude;
            if (magnitude > peak) {
                peak = magnitude;
            }
        }

        uint32_t avg_abs = sample_count > 0 ? (uint32_t)(sum_abs / (int64_t)sample_count) : 0;
        vad_state_t state = ctx.vadnet_iface->detect(ctx.vadnet_model, ctx.pcm_frame);
        ctx.frames++;
        if (state == VAD_SPEECH) {
            ctx.speech_frames++;
        }

        if (!ctx.has_state || state != ctx.last_state) {
            vadnet_log_state_change(&ctx, state, avg_abs, peak);
            ctx.has_state = true;
            ctx.last_state = state;
        }
        vadnet_log_periodic_stats(&ctx, avg_abs, peak, state);
    }

cleanup:
    vadnet_cleanup(&ctx);
    return ret;
}
