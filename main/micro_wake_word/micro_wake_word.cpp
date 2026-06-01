#include "micro_wake_word/micro_wake_word.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <inttypes.h>
#include <string>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if CONFIG_SPIRAM
#include "esp_psram.h"
#endif
#include "esphome/components/micro_wake_word/micro_wake_word.h"
#include "esphome/components/microphone/microphone.h"
#include "sdkconfig.h"

#include "micro_wake_word/hey_jarvis_model.h"

static const char *TAG = "micro_wake_word_app";

#define MICRO_WAKE_WORD_SAMPLE_RATE_HZ 16000
#define MICRO_WAKE_WORD_READ_TIMEOUT_MS 1000
#define MICRO_WAKE_WORD_MIN_TENSOR_ARENA_SIZE 40960
#define MICRO_WAKE_WORD_LOOP_DELAY_TICKS 1

#if CONFIG_ESPESP_MICRO_WAKE_WORD_MIC_SLOT_RIGHT
#define MICRO_WAKE_WORD_I2S_SLOT_MASK I2S_STD_SLOT_RIGHT
#define MICRO_WAKE_WORD_I2S_SLOT_NAME "right"
#else
#define MICRO_WAKE_WORD_I2S_SLOT_MASK I2S_STD_SLOT_LEFT
#define MICRO_WAKE_WORD_I2S_SLOT_NAME "left"
#endif

class EspespI2sMicrophone final : public esphome::microphone::Microphone {
public:
    ~EspespI2sMicrophone()
    {
        stop();
        if (rx_channel_ != nullptr) {
            (void)i2s_del_channel(rx_channel_);
            rx_channel_ = nullptr;
        }
        free(i2s_frame_);
    }

    esp_err_t setup()
    {
        i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
        ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, NULL, &rx_channel_), TAG, "create I2S RX channel");

        i2s_std_config_t std_config = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MICRO_WAKE_WORD_SAMPLE_RATE_HZ),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
            .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = static_cast<gpio_num_t>(CONFIG_ESPESP_MIC_BCLK_GPIO),
                .ws = static_cast<gpio_num_t>(CONFIG_ESPESP_MIC_WS_GPIO),
                .dout = I2S_GPIO_UNUSED,
                .din = static_cast<gpio_num_t>(CONFIG_ESPESP_MIC_DIN_GPIO),
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv = false,
                },
            },
        };
        std_config.slot_cfg.slot_mask = MICRO_WAKE_WORD_I2S_SLOT_MASK;

        esp_err_t ret = i2s_channel_init_std_mode(rx_channel_, &std_config);
        if (ret != ESP_OK) {
            (void)i2s_del_channel(rx_channel_);
            rx_channel_ = nullptr;
            return ret;
        }

        return ESP_OK;
    }

    void start() override
    {
        if (is_failed_ || state_ == esphome::microphone::STATE_RUNNING) {
            return;
        }

        if (rx_channel_ == nullptr) {
            ESP_LOGE(TAG, "I2S microphone is not configured");
            is_failed_ = true;
            return;
        }

        esp_err_t ret = i2s_channel_enable(rx_channel_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "enable I2S microphone failed: %s", esp_err_to_name(ret));
            is_failed_ = true;
            return;
        }

        state_ = esphome::microphone::STATE_RUNNING;
    }

    void stop() override
    {
        if (rx_channel_ == nullptr) {
            state_ = esphome::microphone::STATE_STOPPED;
            return;
        }

        if (state_ == esphome::microphone::STATE_RUNNING) {
            (void)i2s_channel_disable(rx_channel_);
        }

        state_ = esphome::microphone::STATE_STOPPED;
    }

    size_t read(int16_t *buf, size_t len) override
    {
        if (rx_channel_ == nullptr || state_ != esphome::microphone::STATE_RUNNING || buf == nullptr || len == 0) {
            return 0;
        }

        const size_t max_samples = len / sizeof(int16_t);
        if (i2s_frame_ == nullptr || i2s_frame_samples_ < max_samples) {
            free(i2s_frame_);
            i2s_frame_ = static_cast<int32_t *>(malloc(max_samples * sizeof(i2s_frame_[0])));
            if (i2s_frame_ == nullptr) {
                ESP_LOGE(TAG, "allocate I2S conversion buffer failed");
                is_failed_ = true;
                return 0;
            }
            i2s_frame_samples_ = max_samples;
        }

        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(rx_channel_,
                                         i2s_frame_,
                                         max_samples * sizeof(i2s_frame_[0]),
                                         &bytes_read,
                                         MICRO_WAKE_WORD_READ_TIMEOUT_MS);
        if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "I2S read timeout, check microphone wiring and slot");
            return 0;
        }
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "read I2S microphone failed: %s", esp_err_to_name(ret));
            is_failed_ = true;
            return 0;
        }

        size_t sample_count = bytes_read / sizeof(i2s_frame_[0]);
        if (sample_count > max_samples) {
            sample_count = max_samples;
        }

        int64_t sum_abs = 0;
        int32_t peak = 0;
        for (size_t i = 0; i < sample_count; i++) {
            int32_t pcm = i2s_frame_[i] >> CONFIG_ESPESP_MICRO_WAKE_WORD_SAMPLE_SHIFT_BITS;
            if (pcm > INT16_MAX) {
                pcm = INT16_MAX;
            } else if (pcm < INT16_MIN) {
                pcm = INT16_MIN;
            }

            int16_t sample = static_cast<int16_t>(pcm);
            int32_t magnitude = sample == INT16_MIN ? INT16_MAX : std::abs(sample);
            buf[i] = sample;
            sum_abs += magnitude;
            if (magnitude > peak) {
                peak = magnitude;
            }
        }

        chunks_++;
        if ((chunks_ % CONFIG_ESPESP_MICRO_WAKE_WORD_STATUS_EVERY_READS) == 0) {
            uint32_t avg_abs = sample_count > 0 ? static_cast<uint32_t>(sum_abs / static_cast<int64_t>(sample_count)) : 0;
            ESP_LOGI(TAG,
                     "listening: reads=%u, avg_abs=%" PRIu32 ", peak=%" PRIi32,
                     static_cast<unsigned int>(chunks_),
                     avg_abs,
                     peak);
        }

        return sample_count * sizeof(int16_t);
    }

private:
    i2s_chan_handle_t rx_channel_{nullptr};
    int32_t *i2s_frame_{nullptr};
    size_t i2s_frame_samples_{0};
    size_t chunks_{0};
    bool is_failed_{false};
};

static esp_err_t micro_wake_word_check_memory_ready(void)
{
#if CONFIG_SPIRAM
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (!esp_psram_is_initialized() || free_psram == 0) {
        ESP_LOGE(TAG, "PSRAM is enabled in sdkconfig but not available at runtime");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "PSRAM ready: size=%u, free=%u",
             static_cast<unsigned int>(esp_psram_get_size()),
             static_cast<unsigned int>(free_psram));
    return ESP_OK;
#else
    ESP_LOGE(TAG,
             "CONFIG_SPIRAM is disabled. Enable Component config -> ESP PSRAM -> Support for external, SPI-connected RAM.");
    return ESP_ERR_NO_MEM;
#endif
}

static size_t micro_wake_word_tensor_arena_size(void)
{
    size_t configured_size = CONFIG_ESPESP_MICRO_WAKE_WORD_TENSOR_ARENA_SIZE;
    if (configured_size < MICRO_WAKE_WORD_MIN_TENSOR_ARENA_SIZE) {
        ESP_LOGW(TAG,
                 "configured tensor arena size %u is too small for the bundled model; using %u bytes",
                 static_cast<unsigned int>(configured_size),
                 static_cast<unsigned int>(MICRO_WAKE_WORD_MIN_TENSOR_ARENA_SIZE));
        return MICRO_WAKE_WORD_MIN_TENSOR_ARENA_SIZE;
    }

    return configured_size;
}

esp_err_t micro_wake_word_run(void)
{
    ESP_RETURN_ON_ERROR(micro_wake_word_check_memory_ready(), TAG, "check PSRAM");

    EspespI2sMicrophone microphone;
    ESP_RETURN_ON_ERROR(microphone.setup(), TAG, "init I2S microphone");

    esphome::micro_wake_word::MicroWakeWord wake_word;
    std::atomic<bool> detected{false};
    std::string detected_name;
    size_t detections = 0;
    const size_t tensor_arena_size = micro_wake_word_tensor_arena_size();

    wake_word.set_microphone(&microphone);
    wake_word.set_features_step_size(CONFIG_ESPESP_MICRO_WAKE_WORD_FEATURE_STEP_MS);
    wake_word.add_wake_word_model(espesp_micro_wake_word_model,
                                  CONFIG_ESPESP_MICRO_WAKE_WORD_PROBABILITY_CUTOFF,
                                  CONFIG_ESPESP_MICRO_WAKE_WORD_SLIDING_WINDOW_SIZE,
                                  CONFIG_ESPESP_MICRO_WAKE_WORD_LABEL,
                                  tensor_arena_size);
    wake_word.add_detection_callback([&detected, &detected_name](std::string wake_word_name) {
        detected_name = wake_word_name;
        detected.store(true);
    });

    ESP_LOGI(TAG,
             "microWakeWord listening: label=%s, sample_rate=%d Hz, slot=%s, shift=%d, cutoff=%.2f, window=%d, arena=%u",
             CONFIG_ESPESP_MICRO_WAKE_WORD_LABEL,
             MICRO_WAKE_WORD_SAMPLE_RATE_HZ,
             MICRO_WAKE_WORD_I2S_SLOT_NAME,
             CONFIG_ESPESP_MICRO_WAKE_WORD_SAMPLE_SHIFT_BITS,
             static_cast<double>(CONFIG_ESPESP_MICRO_WAKE_WORD_PROBABILITY_CUTOFF),
             CONFIG_ESPESP_MICRO_WAKE_WORD_SLIDING_WINDOW_SIZE,
             static_cast<unsigned int>(tensor_arena_size));
    ESP_LOGI(TAG, "say the bundled English wake phrase: Hey Jarvis");

    wake_word.setup();
    wake_word.start();
    if (wake_word.status_has_error()) {
        ESP_LOGE(TAG, "microWakeWord failed to start; increase tensor arena size or check PSRAM");
        return ESP_ERR_NO_MEM;
    }

    while (true) {
        wake_word.loop();
        if (detected.exchange(false)) {
            detections++;
            ESP_LOGI(TAG,
                     "micro wake word detected: count=%u, label=%s",
                     static_cast<unsigned int>(detections),
                     detected_name.c_str());
            wake_word.start();
        }
        vTaskDelay(MICRO_WAKE_WORD_LOOP_DELAY_TICKS);
    }

    return ESP_OK;
}
