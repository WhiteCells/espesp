#include "wake_word/wake_word.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#if CONFIG_SPIRAM
#include "esp_psram.h"
#endif
#include "model_path.h"
#include "sdkconfig.h"

static const char *TAG = "wake_word";

#define WAKE_WORD_SAMPLE_RATE_HZ 16000
#define WAKE_WORD_CHANNELS 1
#define WAKE_WORD_I2S_READ_TIMEOUT_MS 1000

#if CONFIG_ESPESP_WAKE_WORD_MIC_SLOT_RIGHT
#define WAKE_WORD_I2S_SLOT_MASK I2S_STD_SLOT_RIGHT
#define WAKE_WORD_I2S_SLOT_NAME "right"
#else
#define WAKE_WORD_I2S_SLOT_MASK I2S_STD_SLOT_LEFT
#define WAKE_WORD_I2S_SLOT_NAME "left"
#endif

typedef struct {
    i2s_chan_handle_t rx_channel;
    const esp_afe_sr_iface_t *afe_handle;
    esp_afe_sr_data_t *afe_data;
    afe_config_t *afe_config;
    srmodel_list_t *models;
    int16_t *pcm_frame;
    int32_t *i2s_frame;
    size_t feed_chunks;
    size_t detections;
} wake_word_context_t;

static bool wake_word_string_contains(const char *haystack, const char *needle)
{
    return haystack != NULL && needle != NULL && needle[0] != '\0' && strstr(haystack, needle) != NULL;
}

static bool wake_word_is_wakenet_model(const char *name)
{
    return name != NULL && strncmp(name, ESP_WN_PREFIX, strlen(ESP_WN_PREFIX)) == 0;
}

static const char *wake_word_select_model(srmodel_list_t *models)
{
    if (models == NULL) {
        return NULL;
    }

    const char *configured = CONFIG_ESPESP_WAKE_WORD_MODEL;
    const char *keyword_hint = CONFIG_ESPESP_WAKE_WORD_KEYWORD_HINT;
    const char *fallback = NULL;

    for (int i = 0; i < models->num; i++) {
        const char *name = models->model_name[i];
        if (!wake_word_is_wakenet_model(name)) {
            continue;
        }

        ESP_LOGI(TAG, "available WakeNet model[%d]: %s", i, name);

        if (fallback == NULL) {
            fallback = name;
        }

        if (wake_word_string_contains(name, configured)) {
            return name;
        }

        if (wake_word_string_contains(name, keyword_hint)) {
            return name;
        }
    }

    return fallback;
}

static esp_err_t wake_word_create_rx_channel(i2s_chan_handle_t *rx_channel)
{
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, NULL, rx_channel), TAG, "create I2S RX channel");

    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(WAKE_WORD_SAMPLE_RATE_HZ),
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
    std_config.slot_cfg.slot_mask = WAKE_WORD_I2S_SLOT_MASK;

    esp_err_t ret = i2s_channel_init_std_mode(*rx_channel, &std_config);
    if (ret != ESP_OK) {
        i2s_del_channel(*rx_channel);
        *rx_channel = NULL;
    }

    return ret;
}

static int16_t wake_word_convert_sample(int32_t sample)
{
    int32_t pcm = sample >> CONFIG_ESPESP_WAKE_WORD_SAMPLE_SHIFT_BITS;
    if (pcm > INT16_MAX) {
        pcm = INT16_MAX;
    } else if (pcm < INT16_MIN) {
        pcm = INT16_MIN;
    }
    return (int16_t)pcm;
}

static esp_err_t wake_word_check_memory_ready(void)
{
#if CONFIG_SPIRAM
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (!esp_psram_is_initialized() || free_psram == 0) {
        ESP_LOGE(TAG, "PSRAM is enabled in sdkconfig but not available at runtime");
        ESP_LOGE(TAG, "free_internal=%u, free_psram=%u",
                 (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned int)free_psram);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "PSRAM ready: size=%u, free=%u",
             (unsigned int)esp_psram_get_size(),
             (unsigned int)free_psram);
    return ESP_OK;
#else
    ESP_LOGE(TAG,
             "CONFIG_SPIRAM is disabled. Enable Component config -> ESP PSRAM -> Support for external, SPI-connected RAM.");
    ESP_LOGE(TAG, "free_internal=%u, free_psram=%u",
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return ESP_ERR_NO_MEM;
#endif
}

static void wake_word_cleanup(wake_word_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->afe_handle != NULL && ctx->afe_data != NULL) {
        ctx->afe_handle->destroy(ctx->afe_data);
        ctx->afe_data = NULL;
    }

    if (ctx->afe_config != NULL) {
        afe_config_free(ctx->afe_config);
        ctx->afe_config = NULL;
    }

    if (ctx->models != NULL) {
        esp_srmodel_deinit(ctx->models);
        ctx->models = NULL;
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

static esp_err_t wake_word_init_afe(wake_word_context_t *ctx)
{
    ctx->models = esp_srmodel_init("model");
    if (ctx->models == NULL) {
        ESP_LOGE(TAG, "no speech models found in partition label 'model'");
        return ESP_ERR_NOT_FOUND;
    }

    const char *wn_model_name = wake_word_select_model(ctx->models);
    if (wn_model_name == NULL) {
        ESP_LOGE(TAG, "no WakeNet model found in partition label 'model'");
        return ESP_ERR_NOT_FOUND;
    }

    ctx->afe_config = afe_config_init("M", ctx->models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    if (ctx->afe_config == NULL) {
        ESP_LOGE(TAG, "create AFE config failed");
        return ESP_ERR_NO_MEM;
    }

    ctx->afe_config->wakenet_init = true;
    ctx->afe_config->wakenet_model_name = (char *)wn_model_name;
    ctx->afe_config->aec_init = false;
    ctx->afe_config->se_init = false;
    ctx->afe_config->ns_init = false;
    ctx->afe_config->vad_init = false;
    ctx->afe_config->agc_init = false;
    ctx->afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    ctx->afe_config->afe_ringbuf_size = 4;
    ctx->afe_config->fixed_first_channel = true;
    ctx->afe_config->fixed_output_channel = true;
    ctx->afe_config->wakenet_mode = DET_MODE_90;

    ctx->afe_handle = esp_afe_handle_from_config(ctx->afe_config);
    if (ctx->afe_handle == NULL) {
        ESP_LOGE(TAG, "create AFE handle failed");
        return ESP_FAIL;
    }

    ctx->afe_data = ctx->afe_handle->create_from_config(ctx->afe_config);
    if (ctx->afe_data == NULL) {
        ESP_LOGE(TAG, "create AFE data failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "selected WakeNet model: %s", ctx->afe_config->wakenet_model_name);
    char *wake_words = esp_srmodel_get_wake_words(ctx->models, ctx->afe_config->wakenet_model_name);
    if (wake_words != NULL) {
        ESP_LOGI(TAG, "WakeNet wake words: %s", wake_words);
        free(wake_words);
    }
    return ESP_OK;
}

esp_err_t wake_word_run(void)
{
    esp_err_t ret = wake_word_check_memory_ready();
    if (ret != ESP_OK) {
        return ret;
    }

    wake_word_context_t ctx = {0};
    ret = wake_word_init_afe(&ctx);
    if (ret != ESP_OK) {
        wake_word_cleanup(&ctx);
        return ret;
    }

    int feed_chunksize = ctx.afe_handle->get_feed_chunksize(ctx.afe_data);
    if (feed_chunksize <= 0) {
        ESP_LOGE(TAG, "invalid AFE feed chunk size: %d", feed_chunksize);
        wake_word_cleanup(&ctx);
        return ESP_FAIL;
    }

    ctx.pcm_frame = calloc((size_t)feed_chunksize, sizeof(ctx.pcm_frame[0]));
    ctx.i2s_frame = calloc((size_t)feed_chunksize, sizeof(ctx.i2s_frame[0]));
    if (ctx.pcm_frame == NULL || ctx.i2s_frame == NULL) {
        ESP_LOGE(TAG, "allocate audio frames failed");
        wake_word_cleanup(&ctx);
        return ESP_ERR_NO_MEM;
    }

    ESP_GOTO_ON_ERROR(wake_word_create_rx_channel(&ctx.rx_channel), cleanup, TAG, "init I2S microphone");
    ESP_GOTO_ON_ERROR(i2s_channel_enable(ctx.rx_channel), cleanup, TAG, "enable I2S microphone");

    ESP_LOGI(TAG,
             "WakeNet listening: sample_rate=%d Hz, feed_chunk=%d samples, slot=%s, shift=%d, free_internal=%u, free_psram=%u",
             WAKE_WORD_SAMPLE_RATE_HZ,
             feed_chunksize,
             WAKE_WORD_I2S_SLOT_NAME,
             CONFIG_ESPESP_WAKE_WORD_SAMPLE_SHIFT_BITS,
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    while (true) {
        size_t bytes_read = 0;
        ret = i2s_channel_read(ctx.rx_channel,
                               ctx.i2s_frame,
                               (size_t)feed_chunksize * sizeof(ctx.i2s_frame[0]),
                               &bytes_read,
                               WAKE_WORD_I2S_READ_TIMEOUT_MS);
        if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "I2S read timeout, check microphone wiring and slot");
            continue;
        }
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "read I2S microphone failed: %s", esp_err_to_name(ret));
            break;
        }

        size_t sample_count = bytes_read / sizeof(ctx.i2s_frame[0]);
        if (sample_count != (size_t)feed_chunksize) {
            ESP_LOGW(TAG, "short I2S frame: expected=%d samples, got=%u", feed_chunksize, (unsigned int)sample_count);
            continue;
        }

        int64_t sum_abs = 0;
        int32_t peak = 0;
        for (size_t i = 0; i < sample_count; i++) {
            int16_t sample = wake_word_convert_sample(ctx.i2s_frame[i]);
            int32_t magnitude = sample == INT16_MIN ? INT16_MAX : abs(sample);
            ctx.pcm_frame[i] = sample;
            sum_abs += magnitude;
            if (magnitude > peak) {
                peak = magnitude;
            }
        }

        int feed_ret = ctx.afe_handle->feed(ctx.afe_data, ctx.pcm_frame);
        if (feed_ret < 0) {
            ESP_LOGE(TAG, "AFE feed failed: %d", feed_ret);
            ret = ESP_FAIL;
            break;
        }

        afe_fetch_result_t *result = ctx.afe_handle->fetch(ctx.afe_data);
        if (result == NULL || result->ret_value != ESP_OK) {
            ESP_LOGW(TAG, "AFE fetch failed");
            continue;
        }

        ctx.feed_chunks++;
        if ((ctx.feed_chunks % CONFIG_ESPESP_WAKE_WORD_STATUS_EVERY_CHUNKS) == 0) {
            uint32_t avg_abs = sample_count > 0 ? (uint32_t)(sum_abs / (int64_t)sample_count) : 0;
            ESP_LOGI(TAG,
                     "listening: chunks=%u, avg_abs=%" PRIu32 ", peak=%" PRIi32 ", detections=%u",
                     (unsigned int)ctx.feed_chunks,
                     avg_abs,
                     peak,
                     (unsigned int)ctx.detections);
        }

        if (result->wakeup_state == WAKENET_DETECTED) {
            ctx.detections++;
            ESP_LOGI(TAG,
                     "wake word detected: count=%u, model=%s, command_id=%d",
                     (unsigned int)ctx.detections,
                     ctx.afe_config->wakenet_model_name,
                     result->trigger_channel_id);
        }
    }

cleanup:
    wake_word_cleanup(&ctx);
    return ret;
}
