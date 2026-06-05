#include "chat/chat_audio.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_vadn_models.h"
#include "model_path.h"
#include "sdkconfig.h"

static bool chat_string_contains(const char *haystack, const char *needle)
{
    return haystack != NULL && needle != NULL && needle[0] != '\0' && strstr(haystack, needle) != NULL;
}

static bool chat_is_vadnet_model(const char *name)
{
    return name != NULL && strncmp(name, ESP_VADN_PREFIX, strlen(ESP_VADN_PREFIX)) == 0;
}

static const char *chat_select_vadnet_model(srmodel_list_t *models)
{
    if (models == NULL) {
        return NULL;
    }

    const char *configured = CONFIG_ESPESP_CHAT_VADNET_MODEL;
    const char *fallback = NULL;

    for (int i = 0; i < models->num; i++) {
        const char *name = models->model_name[i];
        if (!chat_is_vadnet_model(name)) {
            continue;
        }

        ESP_LOGI(CHAT_TAG, "available VADNet model[%d]: %s", i, name);
        if (fallback == NULL) {
            fallback = name;
        }
        if (chat_string_contains(name, configured)) {
            return name;
        }
    }

    return fallback;
}

esp_err_t chat_audio_init_vadnet(chat_context_t *ctx)
{
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ctx->models = esp_srmodel_init("model");
    if (ctx->models == NULL) {
        ESP_LOGE(CHAT_TAG, "no speech models found in partition label 'model'");
        return ESP_ERR_NOT_FOUND;
    }

    ctx->vadnet_model_name = chat_select_vadnet_model(ctx->models);
    if (ctx->vadnet_model_name == NULL) {
        ESP_LOGE(CHAT_TAG, "no VADNet model found in partition label 'model'");
        ESP_LOGE(CHAT_TAG, "enable ESP Speech Recognition -> Select voice activity detection -> vadnet1 medium");
        return ESP_ERR_NOT_FOUND;
    }

    ctx->vadnet_iface = esp_vadn_handle_from_name(ctx->vadnet_model_name);
    if (ctx->vadnet_iface == NULL) {
        ESP_LOGE(CHAT_TAG, "get VADNet handle failed: %s", ctx->vadnet_model_name);
        return ESP_ERR_NOT_FOUND;
    }

    ctx->vadnet_model = ctx->vadnet_iface->create(ctx->vadnet_model_name,
                                                  CHAT_VADNET_MODE,
                                                  CHAT_CHANNELS,
                                                  CONFIG_ESPESP_CHAT_VADNET_MIN_SPEECH_MS,
                                                  CONFIG_ESPESP_CHAT_VADNET_MIN_SILENCE_MS);
    if (ctx->vadnet_model == NULL) {
        ESP_LOGE(CHAT_TAG, "create VADNet model failed: %s", ctx->vadnet_model_name);
        return ESP_ERR_NO_MEM;
    }

    int frame_samples = ctx->vadnet_iface->get_samp_chunksize(ctx->vadnet_model);
    int sample_rate_hz = ctx->vadnet_iface->get_samp_rate(ctx->vadnet_model);
    int channel_num = ctx->vadnet_iface->get_channel_num(ctx->vadnet_model);
    if (frame_samples <= 0 || sample_rate_hz <= 0 || channel_num != (int)CHAT_CHANNELS) {
        ESP_LOGE(CHAT_TAG,
                 "invalid VADNet model shape: frame_samples=%d sample_rate=%d channels=%d",
                 frame_samples,
                 sample_rate_hz,
                 channel_num);
        return ESP_ERR_INVALID_ARG;
    }

    ctx->vad_frame_samples = (uint32_t)frame_samples;
    ctx->input_sample_rate_hz = (uint32_t)sample_rate_hz;

#if CONFIG_ESPESP_CHAT_VADNET_SET_THRESHOLD
    if (ctx->vadnet_iface->set_det_threshold(ctx->vadnet_model, CONFIG_ESPESP_CHAT_VADNET_THRESHOLD) != 1) {
        ESP_LOGW(CHAT_TAG, "set VADNet threshold failed: %.3f", (double)CONFIG_ESPESP_CHAT_VADNET_THRESHOLD);
    }
#endif

    ESP_LOGI(CHAT_TAG,
             "selected VADNet model=%s sample_rate=%" PRIu32 " frame_samples=%" PRIu32
             " mode=%s threshold=%.3f",
             ctx->vadnet_model_name,
             ctx->input_sample_rate_hz,
             ctx->vad_frame_samples,
             CHAT_VADNET_MODE_NAME,
             (double)ctx->vadnet_iface->get_det_threshold(ctx->vadnet_model));
    return ESP_OK;
}

esp_err_t chat_audio_create_rx_channel(chat_context_t *ctx)
{
    if (ctx == NULL || ctx->input_sample_rate_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, NULL, &ctx->rx_channel),
                        CHAT_TAG,
                        "create I2S RX channel");

    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(ctx->input_sample_rate_hz),
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
    std_config.slot_cfg.slot_mask = CHAT_MIC_SLOT_MASK;

    esp_err_t ret = i2s_channel_init_std_mode(ctx->rx_channel, &std_config);
    if (ret != ESP_OK) {
        i2s_del_channel(ctx->rx_channel);
        ctx->rx_channel = NULL;
    }

    return ret;
}

esp_err_t chat_audio_create_tx_channel(chat_context_t *ctx)
{
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, &ctx->tx_channel, NULL),
                        CHAT_TAG,
                        "create I2S TX channel");

    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_ESPESP_CHAT_SPK_SAMPLE_RATE_HZ),
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

    esp_err_t ret = i2s_channel_init_std_mode(ctx->tx_channel, &std_config);
    if (ret != ESP_OK) {
        i2s_del_channel(ctx->tx_channel);
        ctx->tx_channel = NULL;
        return ret;
    }

    ctx->output_sample_rate_hz = CONFIG_ESPESP_CHAT_SPK_SAMPLE_RATE_HZ;
    return ESP_OK;
}

int16_t chat_audio_convert_sample(int32_t sample)
{
    int32_t pcm = sample >> CONFIG_ESPESP_CHAT_MIC_SAMPLE_SHIFT_BITS;
    if (pcm > INT16_MAX) {
        pcm = INT16_MAX;
    } else if (pcm < INT16_MIN) {
        pcm = INT16_MIN;
    }
    return (int16_t)pcm;
}

esp_err_t chat_audio_set_output_sample_rate(chat_context_t *ctx, uint32_t sample_rate_hz)
{
    if (ctx == NULL || ctx->tx_channel == NULL || sample_rate_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (ctx->output_sample_rate_hz == sample_rate_hz) {
        return ESP_OK;
    }

    ESP_LOGI(CHAT_TAG,
             "reconfigure speaker sample rate: %" PRIu32 " -> %" PRIu32 " Hz",
             ctx->output_sample_rate_hz,
             sample_rate_hz);

    esp_err_t ret = ESP_OK;
    if (ctx->tx_enabled) {
        ret = i2s_channel_disable(ctx->tx_channel);
        if (ret != ESP_OK) {
            ESP_LOGE(CHAT_TAG, "disable speaker channel failed: %s", esp_err_to_name(ret));
            return ret;
        }
        ctx->tx_enabled = false;
    }

    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz);
    ret = i2s_channel_reconfig_std_clock(ctx->tx_channel, &clk_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(CHAT_TAG, "reconfigure speaker clock failed: %s", esp_err_to_name(ret));
        (void)i2s_channel_enable(ctx->tx_channel);
        ctx->tx_enabled = true;
        return ret;
    }

    ret = i2s_channel_enable(ctx->tx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(CHAT_TAG, "re-enable speaker channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ctx->tx_enabled = true;
    ctx->output_sample_rate_hz = sample_rate_hz;
    if (ctx->aec != NULL) {
        voice_client_aec_set_speaker_rate(ctx->aec, sample_rate_hz);
    }
    return ESP_OK;
}

void chat_audio_cleanup(chat_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->vadnet_iface != NULL && ctx->vadnet_model != NULL) {
        ctx->vadnet_iface->destroy(ctx->vadnet_model);
        ctx->vadnet_model = NULL;
    }
    ctx->vadnet_iface = NULL;

    if (ctx->models != NULL) {
        esp_srmodel_deinit(ctx->models);
        ctx->models = NULL;
    }
}
