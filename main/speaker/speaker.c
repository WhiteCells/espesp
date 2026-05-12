#include "speaker/speaker.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "speaker";
static const size_t TONE_FRAME_COUNT = 256;
static const int16_t TONE_AMPLITUDE = 8000;
static const float TWO_PI = 6.28318530717958647692f;

static esp_err_t speaker_create_channel(i2s_chan_handle_t *tx_channel)
{
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, tx_channel, NULL), TAG, "create I2S TX channel");

    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_CASE2_SPK_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_CASE2_SPK_BCLK_GPIO,
            .ws = CONFIG_CASE2_SPK_WS_GPIO,
            .dout = CONFIG_CASE2_SPK_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    /* 外接 I2S 功放芯片通常只需要 BCLK、WS、DOUT 三根信号线。 */
    esp_err_t ret = i2s_channel_init_std_mode(*tx_channel, &std_config);
    if (ret != ESP_OK) {
        i2s_del_channel(*tx_channel);
        *tx_channel = NULL;
    }

    return ret;
}

static void fill_sine_tone(int16_t *buffer, size_t sample_count, uint32_t *phase)
{
    for (size_t i = 0; i < sample_count; i++) {
        float cycles = (float)(*phase) * (float)CONFIG_CASE2_SPK_TONE_HZ /
                       (float)CONFIG_CASE2_SPK_SAMPLE_RATE_HZ;
        buffer[i] = (int16_t)(sinf(cycles * TWO_PI) * TONE_AMPLITUDE);
        (*phase)++;
        if (*phase >= CONFIG_CASE2_SPK_SAMPLE_RATE_HZ) {
            *phase = 0;
        }
    }
}

esp_err_t speaker_run(void)
{
    i2s_chan_handle_t tx_channel = NULL;
    ESP_RETURN_ON_ERROR(speaker_create_channel(&tx_channel), TAG, "init speaker");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx_channel), TAG, "enable speaker");

    ESP_LOGI(TAG,
             "I2S speaker: BCLK=GPIO%d, WS=GPIO%d, DOUT=GPIO%d, sample_rate=%d Hz, tone=%d Hz",
             CONFIG_CASE2_SPK_BCLK_GPIO,
             CONFIG_CASE2_SPK_WS_GPIO,
             CONFIG_CASE2_SPK_DOUT_GPIO,
             CONFIG_CASE2_SPK_SAMPLE_RATE_HZ,
             CONFIG_CASE2_SPK_TONE_HZ);

    int16_t samples[TONE_FRAME_COUNT];
    uint32_t phase = 0;
    while (true) {
        size_t bytes_written = 0;
        fill_sine_tone(samples, TONE_FRAME_COUNT, &phase);
        ESP_RETURN_ON_ERROR(i2s_channel_write(tx_channel,
                                              samples,
                                              sizeof(samples),
                                              &bytes_written,
                                              1000),
                            TAG,
                            "write I2S speaker");
        ESP_LOGD(TAG, "wrote %u bytes", (unsigned int)bytes_written);
    }

    i2s_channel_disable(tx_channel);
    i2s_del_channel(tx_channel);
    return ESP_OK;
}
