#include "microphone/microphone.h"

#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "microphone";
static const size_t SAMPLE_COUNT = 256;

static esp_err_t microphone_create_channel(i2s_chan_handle_t *rx_channel)
{
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, NULL, rx_channel), TAG, "create I2S RX channel");

    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_ESPESP_MIC_SAMPLE_RATE_HZ),
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

    /* 标准 I2S MEMS 麦克风通常输出 24-bit 数据，放在 32-bit slot 中读取。 */
    esp_err_t ret = i2s_channel_init_std_mode(*rx_channel, &std_config);
    if (ret != ESP_OK) {
        i2s_del_channel(*rx_channel);
        *rx_channel = NULL;
    }

    return ret;
}

esp_err_t microphone_run(void)
{
    i2s_chan_handle_t rx_channel = NULL;
    ESP_RETURN_ON_ERROR(microphone_create_channel(&rx_channel), TAG, "init microphone");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(rx_channel), TAG, "enable microphone");

    ESP_LOGI(TAG,
             "I2S microphone: BCLK=GPIO%d, WS=GPIO%d, DIN=GPIO%d, sample_rate=%d Hz",
             CONFIG_ESPESP_MIC_BCLK_GPIO,
             CONFIG_ESPESP_MIC_WS_GPIO,
             CONFIG_ESPESP_MIC_DIN_GPIO,
             CONFIG_ESPESP_MIC_SAMPLE_RATE_HZ);

    int32_t samples[SAMPLE_COUNT];
    while (true) {
        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(rx_channel, samples, sizeof(samples), &bytes_read, 1000);
        if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "read timeout, check microphone wiring and clock pins");
            continue;
        }
        ESP_RETURN_ON_ERROR(ret, TAG, "read I2S microphone");

        int64_t sum_abs = 0;
        int32_t peak = 0;
        size_t sample_count = bytes_read / sizeof(samples[0]);

        for (size_t i = 0; i < sample_count; i++) {
            int32_t sample = samples[i] >> 8;
            int32_t magnitude = sample < 0 ? -sample : sample;
            sum_abs += magnitude;
            if (magnitude > peak) {
                peak = magnitude;
            }
        }

        uint32_t avg_abs = sample_count > 0 ? (uint32_t)(sum_abs / sample_count) : 0;
        ESP_LOGI(TAG,
                 "audio frame: samples=%u, avg_abs=%" PRIu32 ", peak=%" PRIi32,
                 (unsigned int)sample_count,
                 avg_abs,
                 peak);
    }

    i2s_channel_disable(rx_channel);
    i2s_del_channel(rx_channel);
    return ESP_OK;
}
