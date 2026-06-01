#include "voice_callback/voice_callback.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "voice_callback/voice_callback_aec.h"

#define VOICE_CALLBACK_SAMPLE_BYTES 2U
#define VOICE_CALLBACK_STATS_PERIOD_US 1000000LL
#define VOICE_CALLBACK_MAX_PENDING_PLAYBACK_FRAMES 4U
#define VOICE_CALLBACK_FRAME_DURATION_MS \
    (((CONFIG_ESPESP_VOICE_CALLBACK_FRAME_SAMPLES * 1000U) + \
      CONFIG_ESPESP_VOICE_CALLBACK_SAMPLE_RATE_HZ - 1U) / \
     CONFIG_ESPESP_VOICE_CALLBACK_SAMPLE_RATE_HZ)
#define VOICE_CALLBACK_CAPTURE_PRIORITY 4
#define VOICE_CALLBACK_PLAYBACK_PRIORITY 5

#ifndef CONFIG_FREERTOS_NUMBER_OF_CORES
#define CONFIG_FREERTOS_NUMBER_OF_CORES 1
#endif

#if CONFIG_FREERTOS_NUMBER_OF_CORES > 1
#define VOICE_CALLBACK_TASK_CORE 1
#else
#define VOICE_CALLBACK_TASK_CORE 0
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_SAMPLE_RATE_HZ
#define CONFIG_ESPESP_VOICE_CALLBACK_SAMPLE_RATE_HZ 16000
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_FRAME_SAMPLES
#define CONFIG_ESPESP_VOICE_CALLBACK_FRAME_SAMPLES 160
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_QUEUE_LENGTH
#define CONFIG_ESPESP_VOICE_CALLBACK_QUEUE_LENGTH 6
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_MIC_SAMPLE_SHIFT_BITS
#define CONFIG_ESPESP_VOICE_CALLBACK_MIC_SAMPLE_SHIFT_BITS 12
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_MIC_INPUT_LIMIT_PERCENT
#define CONFIG_ESPESP_VOICE_CALLBACK_MIC_INPUT_LIMIT_PERCENT 85
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_PLAYBACK_VOLUME_PERCENT
#define CONFIG_ESPESP_VOICE_CALLBACK_PLAYBACK_VOLUME_PERCENT 15
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_PLAYBACK_LIMIT_PERCENT
#define CONFIG_ESPESP_VOICE_CALLBACK_PLAYBACK_LIMIT_PERCENT 80
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_I2S_READ_TIMEOUT_MS
#define CONFIG_ESPESP_VOICE_CALLBACK_I2S_READ_TIMEOUT_MS 1000
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_I2S_WRITE_TIMEOUT_MS
#define CONFIG_ESPESP_VOICE_CALLBACK_I2S_WRITE_TIMEOUT_MS 1000
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_AEC_ENABLED
#define CONFIG_ESPESP_VOICE_CALLBACK_AEC_ENABLED 0
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_AEC_FILTER_LEN
#define CONFIG_ESPESP_VOICE_CALLBACK_AEC_FILTER_LEN 64
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_AEC_STEP_SIZE_X256
#define CONFIG_ESPESP_VOICE_CALLBACK_AEC_STEP_SIZE_X256 64
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_AEC_MAX_DELAY_MS
#define CONFIG_ESPESP_VOICE_CALLBACK_AEC_MAX_DELAY_MS 80
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_AEC_REFERENCE_DELAY_MS
#define CONFIG_ESPESP_VOICE_CALLBACK_AEC_REFERENCE_DELAY_MS 40
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_NOISE_GATE_AVG_ABS
#define CONFIG_ESPESP_VOICE_CALLBACK_NOISE_GATE_AVG_ABS 120
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_ECHO_GATE_PERCENT
#define CONFIG_ESPESP_VOICE_CALLBACK_ECHO_GATE_PERCENT 500
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_SPEAKER_ACTIVE_TIMEOUT_MS
#define CONFIG_ESPESP_VOICE_CALLBACK_SPEAKER_ACTIVE_TIMEOUT_MS 700
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_GATE_HOLD_MS
#define CONFIG_ESPESP_VOICE_CALLBACK_GATE_HOLD_MS 900
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_GATE_RELEASE_PERCENT
#define CONFIG_ESPESP_VOICE_CALLBACK_GATE_RELEASE_PERCENT 25
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_GATE_RELEASE_MIN_AVG_ABS
#define CONFIG_ESPESP_VOICE_CALLBACK_GATE_RELEASE_MIN_AVG_ABS 80
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_GATE_HOLD_GAIN_PERCENT
#define CONFIG_ESPESP_VOICE_CALLBACK_GATE_HOLD_GAIN_PERCENT 80
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_GATE_TAIL_MS
#define CONFIG_ESPESP_VOICE_CALLBACK_GATE_TAIL_MS 350
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_GATE_TAIL_GAIN_PERCENT
#define CONFIG_ESPESP_VOICE_CALLBACK_GATE_TAIL_GAIN_PERCENT 60
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_HIGHPASS_FILTER_ENABLED
#define CONFIG_ESPESP_VOICE_CALLBACK_HIGHPASS_FILTER_ENABLED 0
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_HIGHPASS_ALPHA_Q15
#define CONFIG_ESPESP_VOICE_CALLBACK_HIGHPASS_ALPHA_Q15 31800
#endif

#ifndef CONFIG_ESPESP_VOICE_CALLBACK_NOISE_SUPPRESS_FLOOR_ABS
#define CONFIG_ESPESP_VOICE_CALLBACK_NOISE_SUPPRESS_FLOOR_ABS 0
#endif

#if CONFIG_ESPESP_VOICE_CALLBACK_MIC_SLOT_RIGHT
#define VOICE_CALLBACK_MIC_SLOT_MASK I2S_STD_SLOT_RIGHT
#define VOICE_CALLBACK_MIC_SLOT_NAME "right"
#else
#define VOICE_CALLBACK_MIC_SLOT_MASK I2S_STD_SLOT_LEFT
#define VOICE_CALLBACK_MIC_SLOT_NAME "left"
#endif

typedef struct {
    uint32_t sequence;
    size_t sample_count;
    bool muted;
    uint32_t mic_avg_abs;
    uint32_t clean_avg_abs;
    int16_t samples[CONFIG_ESPESP_VOICE_CALLBACK_FRAME_SAMPLES];
} voice_callback_frame_t;

typedef enum {
    VOICE_CALLBACK_GATE_PASS = 0,
    VOICE_CALLBACK_GATE_HOLD,
    VOICE_CALLBACK_GATE_TAIL,
    VOICE_CALLBACK_GATE_NOISE,
    VOICE_CALLBACK_GATE_SPEAKER,
} voice_callback_gate_result_t;

typedef struct {
    i2s_chan_handle_t rx_channel;
    i2s_chan_handle_t tx_channel;
    QueueHandle_t free_queue;
    QueueHandle_t playback_queue;
    SemaphoreHandle_t aec_lock;
    voice_callback_aec_t *aec;
    uint64_t captured_frames;
    uint64_t played_frames;
    uint64_t dropped_frames;
    uint64_t muted_frames;
    uint64_t speaker_guard_frames;
    uint64_t passed_frames;
    uint64_t hold_frames;
    uint64_t tail_frames;
    uint64_t playback_underflow_frames;
    uint64_t input_limited_frames;
    uint64_t playback_limited_samples;
    volatile uint32_t last_playback_avg_abs;
    volatile uint32_t last_playback_peak;
    volatile int64_t last_audible_playback_us;
    volatile int64_t last_gate_pass_us;
    uint32_t last_input_peak_abs;
    int32_t input_gain_q15;
    uint32_t gate_gain_q15;
    int32_t highpass_prev_input;
    int32_t highpass_prev_output;
    int64_t last_capture_stats_us;
    int64_t last_playback_stats_us;
} voice_callback_context_t;

static const char *TAG = "voice_callback";

static esp_err_t voice_callback_create_rx_channel(i2s_chan_handle_t *rx_channel)
{
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, NULL, rx_channel), TAG, "create I2S RX channel");

    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_ESPESP_VOICE_CALLBACK_SAMPLE_RATE_HZ),
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
    std_config.slot_cfg.slot_mask = VOICE_CALLBACK_MIC_SLOT_MASK;

    esp_err_t ret = i2s_channel_init_std_mode(*rx_channel, &std_config);
    if (ret != ESP_OK) {
        i2s_del_channel(*rx_channel);
        *rx_channel = NULL;
    }

    return ret;
}

static esp_err_t voice_callback_create_tx_channel(i2s_chan_handle_t *tx_channel)
{
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, tx_channel, NULL), TAG, "create I2S TX channel");

    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_ESPESP_VOICE_CALLBACK_SAMPLE_RATE_HZ),
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

static int16_t voice_callback_saturate_i16(int32_t sample);

static int32_t voice_callback_abs_i32(int32_t sample)
{
    return sample == INT32_MIN ? INT32_MAX : abs(sample);
}

static void voice_callback_convert_frame(voice_callback_context_t *ctx,
                                         const int32_t *raw_samples,
                                         int16_t *pcm_samples,
                                         size_t sample_count,
                                         uint32_t *peak_abs)
{
    int32_t input_peak = 0;
    for (size_t i = 0; i < sample_count; i++) {
        int32_t pcm = raw_samples[i] >> CONFIG_ESPESP_VOICE_CALLBACK_MIC_SAMPLE_SHIFT_BITS;
        int32_t magnitude = voice_callback_abs_i32(pcm);
        if (magnitude > input_peak) {
            input_peak = magnitude;
        }
    }

    int32_t limit = ((int32_t)INT16_MAX * CONFIG_ESPESP_VOICE_CALLBACK_MIC_INPUT_LIMIT_PERCENT) / 100;
    if (limit <= 0 || limit > INT16_MAX) {
        limit = INT16_MAX;
    }

    int32_t desired_gain_q15 = 32768;
    bool frame_limited = input_peak > limit;
    if (frame_limited && input_peak > 0) {
        desired_gain_q15 = (int32_t)(((int64_t)limit * 32768LL) / input_peak);
        if (desired_gain_q15 < 1) {
            desired_gain_q15 = 1;
        }
    }

    int32_t current_gain_q15 = ctx->input_gain_q15;
    if (current_gain_q15 <= 0) {
        current_gain_q15 = 32768;
    }

    if (desired_gain_q15 < current_gain_q15) {
        current_gain_q15 = desired_gain_q15;
    } else if (desired_gain_q15 > current_gain_q15) {
        int32_t delta = desired_gain_q15 - current_gain_q15;
        current_gain_q15 += delta > 8 ? delta / 8 : delta;
    }
    ctx->input_gain_q15 = current_gain_q15;
    ctx->last_input_peak_abs = (uint32_t)input_peak;

    for (size_t i = 0; i < sample_count; i++) {
        int32_t pcm = raw_samples[i] >> CONFIG_ESPESP_VOICE_CALLBACK_MIC_SAMPLE_SHIFT_BITS;
        int32_t scaled = (int32_t)(((int64_t)pcm * current_gain_q15) >> 15);
        pcm_samples[i] = voice_callback_saturate_i16(scaled);
    }

    if (frame_limited) {
        ctx->input_limited_frames++;
    }
    if (peak_abs != NULL) {
        *peak_abs = (uint32_t)input_peak;
    }
}

static uint32_t voice_callback_abs_i16(int16_t sample)
{
    return sample == INT16_MIN ? 32768U : (uint32_t)abs(sample);
}

static uint32_t voice_callback_scale_percent(uint32_t value, uint32_t percent)
{
    uint64_t scaled = ((uint64_t)value * percent) / 100U;
    return scaled > UINT32_MAX ? UINT32_MAX : (uint32_t)scaled;
}

static uint32_t voice_callback_saturating_mul_u32(uint32_t value, uint32_t factor)
{
    uint64_t scaled = (uint64_t)value * factor;
    return scaled > UINT32_MAX ? UINT32_MAX : (uint32_t)scaled;
}

static uint32_t voice_callback_max_u32(uint32_t a, uint32_t b)
{
    return a > b ? a : b;
}

static int16_t voice_callback_saturate_i16(int32_t sample)
{
    if (sample > INT16_MAX) {
        return INT16_MAX;
    }
    if (sample < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)sample;
}

static void voice_callback_highpass_filter(voice_callback_context_t *ctx,
                                           int16_t *samples,
                                           size_t sample_count)
{
#if CONFIG_ESPESP_VOICE_CALLBACK_HIGHPASS_FILTER_ENABLED
    int32_t prev_input = ctx->highpass_prev_input;
    int32_t prev_output = ctx->highpass_prev_output;

    for (size_t i = 0; i < sample_count; i++) {
        int32_t input = samples[i];
        int32_t filtered = input - prev_input +
                           (int32_t)(((int64_t)prev_output *
                                      CONFIG_ESPESP_VOICE_CALLBACK_HIGHPASS_ALPHA_Q15) >> 15);
        prev_input = input;
        prev_output = filtered;
        samples[i] = voice_callback_saturate_i16(filtered);
    }

    ctx->highpass_prev_input = prev_input;
    ctx->highpass_prev_output = prev_output;
#else
    (void)ctx;
    (void)samples;
    (void)sample_count;
#endif
}

static void voice_callback_apply_noise_floor_suppression(int16_t *samples,
                                                         size_t sample_count,
                                                         uint32_t frame_avg_abs)
{
#if CONFIG_ESPESP_VOICE_CALLBACK_NOISE_SUPPRESS_FLOOR_ABS > 0
    const uint32_t floor = CONFIG_ESPESP_VOICE_CALLBACK_NOISE_SUPPRESS_FLOOR_ABS;
    if (frame_avg_abs >= floor) {
        return;
    }

    uint32_t gain_percent = floor > 0 ? (frame_avg_abs * 100U) / floor : 100U;
    if (gain_percent < 35U) {
        gain_percent = 35U;
    } else if (gain_percent > 100U) {
        gain_percent = 100U;
    }

    for (size_t i = 0; i < sample_count; i++) {
        int32_t scaled = ((int32_t)samples[i] * (int32_t)gain_percent) / 100;
        samples[i] = (int16_t)scaled;
    }
#else
    (void)samples;
    (void)sample_count;
    (void)frame_avg_abs;
#endif
}

static void voice_callback_apply_gate_gain(voice_callback_context_t *ctx,
                                           int16_t *samples,
                                           size_t sample_count,
                                           uint32_t target_gain_q15)
{
    uint32_t current_gain_q15 = ctx->gate_gain_q15;
    if (current_gain_q15 > 32768U) {
        current_gain_q15 = 32768U;
    }
    if (target_gain_q15 > 32768U) {
        target_gain_q15 = 32768U;
    }

    if (sample_count == 0) {
        ctx->gate_gain_q15 = target_gain_q15;
        return;
    }

    int32_t gain_delta = (int32_t)target_gain_q15 - (int32_t)current_gain_q15;
    for (size_t i = 0; i < sample_count; i++) {
        uint32_t gain_q15 = current_gain_q15;
        if (sample_count > 1) {
            gain_q15 = (uint32_t)((int32_t)current_gain_q15 +
                                  (gain_delta * (int32_t)i) / (int32_t)(sample_count - 1));
        }
        int32_t scaled = (int32_t)(((int64_t)samples[i] * gain_q15) >> 15);
        samples[i] = voice_callback_saturate_i16(scaled);
    }

    ctx->gate_gain_q15 = target_gain_q15;
}

static void voice_callback_calc_level(const int16_t *samples,
                                      size_t sample_count,
                                      uint32_t *avg_abs,
                                      uint32_t *peak)
{
    uint64_t sum_abs = 0;
    uint32_t local_peak = 0;

    for (size_t i = 0; i < sample_count; i++) {
        uint32_t magnitude = voice_callback_abs_i16(samples[i]);
        sum_abs += magnitude;
        if (magnitude > local_peak) {
            local_peak = magnitude;
        }
    }

    if (avg_abs != NULL) {
        *avg_abs = sample_count > 0 ? (uint32_t)(sum_abs / sample_count) : 0;
    }
    if (peak != NULL) {
        *peak = local_peak;
    }
}

static voice_callback_gate_result_t voice_callback_gate_frame(voice_callback_context_t *ctx,
                                                              uint32_t mic_avg_abs,
                                                              uint32_t clean_avg_abs,
                                                              uint32_t clean_peak)
{
    uint32_t base_threshold = CONFIG_ESPESP_VOICE_CALLBACK_NOISE_GATE_AVG_ABS;
    uint32_t threshold = base_threshold;
    int64_t now_us = esp_timer_get_time();
    int64_t active_window_us = (int64_t)CONFIG_ESPESP_VOICE_CALLBACK_SPEAKER_ACTIVE_TIMEOUT_MS * 1000LL;
    int64_t hold_window_us = (int64_t)CONFIG_ESPESP_VOICE_CALLBACK_GATE_HOLD_MS * 1000LL;
    bool speaker_recent = false;
    bool gate_held = ctx->last_gate_pass_us > 0 &&
                     now_us - ctx->last_gate_pass_us < hold_window_us;

    int64_t playback_elapsed_us = now_us - ctx->last_audible_playback_us;
    if (ctx->last_audible_playback_us > 0 &&
        playback_elapsed_us >= 0 &&
        playback_elapsed_us < active_window_us) {
        speaker_recent = true;
        int64_t active_remaining_us = active_window_us - playback_elapsed_us;
        uint32_t playback_avg = (uint32_t)(((uint64_t)ctx->last_playback_avg_abs *
                                            (uint64_t)active_remaining_us) /
                                           (uint64_t)active_window_us);
        uint32_t echo_threshold = voice_callback_scale_percent(
            playback_avg,
            CONFIG_ESPESP_VOICE_CALLBACK_ECHO_GATE_PERCENT);
        if (echo_threshold > threshold) {
            threshold = echo_threshold;
        }
    }

    uint32_t strong_peak_threshold = voice_callback_saturating_mul_u32(threshold, 3U);
    bool strong_avg_pass = clean_avg_abs >= threshold && mic_avg_abs >= threshold;
    bool strong_peak_pass = clean_peak >= strong_peak_threshold && mic_avg_abs >= threshold / 2U;
    if (strong_avg_pass || strong_peak_pass) {
        ctx->last_gate_pass_us = now_us;
        return VOICE_CALLBACK_GATE_PASS;
    }

    if (gate_held) {
        uint32_t release_threshold = voice_callback_scale_percent(
            base_threshold,
            CONFIG_ESPESP_VOICE_CALLBACK_GATE_RELEASE_PERCENT);
        release_threshold = voice_callback_max_u32(
            release_threshold,
            CONFIG_ESPESP_VOICE_CALLBACK_GATE_RELEASE_MIN_AVG_ABS);
        if (release_threshold == 0) {
            release_threshold = 1;
        }

        uint32_t release_peak_threshold = voice_callback_saturating_mul_u32(release_threshold, 2U);
        uint32_t release_avg_floor = release_threshold / 2U;
        bool release_avg_pass = clean_avg_abs >= release_threshold && mic_avg_abs >= release_avg_floor;
        bool release_peak_pass = clean_peak >= release_peak_threshold && mic_avg_abs >= release_avg_floor;
        if (release_avg_pass || release_peak_pass) {
            ctx->last_gate_pass_us = now_us;
            return VOICE_CALLBACK_GATE_PASS;
        }

        uint32_t hold_floor = release_threshold / 2U;
        hold_floor = voice_callback_max_u32(
            hold_floor,
            CONFIG_ESPESP_VOICE_CALLBACK_GATE_RELEASE_MIN_AVG_ABS / 2U);
        if (hold_floor == 0) {
            hold_floor = 1;
        }
        uint32_t hold_peak_floor = voice_callback_saturating_mul_u32(hold_floor, 2U);
        bool hold_avg_present = clean_avg_abs >= hold_floor && mic_avg_abs >= hold_floor;
        bool hold_peak_present = clean_peak >= hold_peak_floor && mic_avg_abs >= hold_floor / 2U;
        if (hold_avg_present || hold_peak_present) {
            return VOICE_CALLBACK_GATE_HOLD;
        }
    }

    return speaker_recent ? VOICE_CALLBACK_GATE_SPEAKER : VOICE_CALLBACK_GATE_NOISE;
}

static voice_callback_gate_result_t voice_callback_apply_gate_tail(voice_callback_context_t *ctx,
                                                                   voice_callback_gate_result_t gate)
{
    if ((gate != VOICE_CALLBACK_GATE_NOISE && gate != VOICE_CALLBACK_GATE_SPEAKER) ||
        CONFIG_ESPESP_VOICE_CALLBACK_GATE_TAIL_MS <= 0) {
        return gate;
    }

    int64_t now_us = esp_timer_get_time();
    int64_t elapsed_us = now_us - ctx->last_gate_pass_us;
    int64_t hold_window_us = (int64_t)CONFIG_ESPESP_VOICE_CALLBACK_GATE_HOLD_MS * 1000LL;
    int64_t tail_window_us = (int64_t)CONFIG_ESPESP_VOICE_CALLBACK_GATE_TAIL_MS * 1000LL;
    if (ctx->last_gate_pass_us > 0 &&
        elapsed_us >= hold_window_us &&
        elapsed_us < hold_window_us + tail_window_us) {
        return VOICE_CALLBACK_GATE_TAIL;
    }

    return gate;
}

static uint32_t voice_callback_gate_gain_q15(voice_callback_gate_result_t gate)
{
    if (gate == VOICE_CALLBACK_GATE_PASS) {
        return 32768U;
    }
    if (gate == VOICE_CALLBACK_GATE_HOLD) {
        uint32_t gain = (32768U * CONFIG_ESPESP_VOICE_CALLBACK_GATE_HOLD_GAIN_PERCENT) / 100U;
        return gain > 32768U ? 32768U : gain;
    }
    if (gate == VOICE_CALLBACK_GATE_TAIL) {
        uint32_t gain = (32768U * CONFIG_ESPESP_VOICE_CALLBACK_GATE_TAIL_GAIN_PERCENT) / 100U;
        return gain > 32768U ? 32768U : gain;
    }
    return 0;
}

static int16_t voice_callback_process_playback_sample(int16_t sample,
                                                      uint32_t *peak_out,
                                                      uint32_t *limited_out)
{
    int32_t scaled = ((int32_t)sample * CONFIG_ESPESP_VOICE_CALLBACK_PLAYBACK_VOLUME_PERCENT) / 100;
    int32_t limit = ((int32_t)INT16_MAX * CONFIG_ESPESP_VOICE_CALLBACK_PLAYBACK_LIMIT_PERCENT) / 100;
    if (limit <= 0 || limit > INT16_MAX) {
        limit = INT16_MAX;
    }

    if (scaled > limit) {
        scaled = limit;
        if (limited_out != NULL) {
            (*limited_out)++;
        }
    } else if (scaled < -limit) {
        scaled = -limit;
        if (limited_out != NULL) {
            (*limited_out)++;
        }
    }

    int16_t processed = (int16_t)scaled;
    uint32_t magnitude = voice_callback_abs_i16(processed);
    if (peak_out != NULL && magnitude > *peak_out) {
        *peak_out = magnitude;
    }
    return processed;
}

static esp_err_t voice_callback_write_all_i2s(i2s_chan_handle_t tx_channel,
                                              const int16_t *samples,
                                              size_t sample_count)
{
    const uint8_t *data = (const uint8_t *)samples;
    size_t len = sample_count * VOICE_CALLBACK_SAMPLE_BYTES;
    size_t total_written = 0;

    while (total_written < len) {
        size_t bytes_written = 0;
        esp_err_t ret = i2s_channel_write(tx_channel,
                                          data + total_written,
                                          len - total_written,
                                          &bytes_written,
                                          CONFIG_ESPESP_VOICE_CALLBACK_I2S_WRITE_TIMEOUT_MS);
        if (ret != ESP_OK) {
            return ret;
        }
        if (bytes_written == 0) {
            return ESP_ERR_TIMEOUT;
        }
        total_written += bytes_written;
    }

    return ESP_OK;
}

static void voice_callback_return_frame(voice_callback_context_t *ctx, voice_callback_frame_t *frame)
{
    if (frame == NULL) {
        return;
    }
    if (xQueueSend(ctx->free_queue, &frame, 0) != pdTRUE) {
        ESP_LOGW(TAG, "free frame queue full, dropping frame buffer");
        free(frame);
    }
}

static void voice_callback_drop_stale_playback_frames(voice_callback_context_t *ctx)
{
    while (uxQueueMessagesWaiting(ctx->playback_queue) >= VOICE_CALLBACK_MAX_PENDING_PLAYBACK_FRAMES) {
        voice_callback_frame_t *stale = NULL;
        if (xQueueReceive(ctx->playback_queue, &stale, 0) != pdTRUE) {
            break;
        }
        ctx->dropped_frames++;
        voice_callback_return_frame(ctx, stale);
    }
}

static bool voice_callback_enqueue_playback_frame(voice_callback_context_t *ctx,
                                                  voice_callback_frame_t *frame)
{
    voice_callback_drop_stale_playback_frames(ctx);

    if (xQueueSend(ctx->playback_queue, &frame, 0) == pdTRUE) {
        return true;
    }

    voice_callback_frame_t *stale = NULL;
    if (xQueueReceive(ctx->playback_queue, &stale, 0) == pdTRUE) {
        ctx->dropped_frames++;
        voice_callback_return_frame(ctx, stale);
        if (xQueueSend(ctx->playback_queue, &frame, 0) == pdTRUE) {
            return true;
        }
    }

    ctx->dropped_frames++;
    voice_callback_return_frame(ctx, frame);
    return false;
}

static void voice_callback_capture_task(void *arg)
{
    voice_callback_context_t *ctx = (voice_callback_context_t *)arg;
    int32_t raw_samples[CONFIG_ESPESP_VOICE_CALLBACK_FRAME_SAMPLES];
    int16_t mic_samples[CONFIG_ESPESP_VOICE_CALLBACK_FRAME_SAMPLES];
    int16_t clean_samples[CONFIG_ESPESP_VOICE_CALLBACK_FRAME_SAMPLES];
    uint32_t sequence = 0;

    while (true) {
        voice_callback_frame_t *frame = NULL;
        if (xQueueReceive(ctx->free_queue, &frame, pdMS_TO_TICKS(100)) != pdTRUE) {
            ctx->dropped_frames++;
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(ctx->rx_channel,
                                         raw_samples,
                                         sizeof(raw_samples),
                                         &bytes_read,
                                         CONFIG_ESPESP_VOICE_CALLBACK_I2S_READ_TIMEOUT_MS);
        if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "I2S microphone read timeout");
            voice_callback_return_frame(ctx, frame);
            continue;
        }
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2S microphone read failed: %s", esp_err_to_name(ret));
            voice_callback_return_frame(ctx, frame);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        size_t sample_count = bytes_read / sizeof(raw_samples[0]);
        uint32_t input_peak_abs = 0;
        voice_callback_convert_frame(ctx,
                                     raw_samples,
                                     mic_samples,
                                     sample_count,
                                     &input_peak_abs);
        voice_callback_highpass_filter(ctx, mic_samples, sample_count);
        memcpy(clean_samples, mic_samples, sample_count * sizeof(clean_samples[0]));

        uint32_t mic_avg_abs = 0;
        voice_callback_calc_level(mic_samples, sample_count, &mic_avg_abs, NULL);

#if CONFIG_ESPESP_VOICE_CALLBACK_AEC_ENABLED
        if (ctx->aec != NULL && ctx->aec_lock != NULL &&
            xSemaphoreTake(ctx->aec_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
            (void)voice_callback_aec_process(ctx->aec, clean_samples, clean_samples, sample_count);
            xSemaphoreGive(ctx->aec_lock);
        }
#endif

        uint32_t clean_avg_abs = 0;
        uint32_t clean_peak = 0;
        voice_callback_calc_level(clean_samples, sample_count, &clean_avg_abs, &clean_peak);
        voice_callback_gate_result_t gate = voice_callback_gate_frame(ctx,
                                                                      mic_avg_abs,
                                                                      clean_avg_abs,
                                                                      clean_peak);
        gate = voice_callback_apply_gate_tail(ctx, gate);
        uint32_t target_gate_gain_q15 = voice_callback_gate_gain_q15(gate);
        bool should_play = target_gate_gain_q15 > 0;

        uint32_t frame_sequence = sequence++;
        frame->sequence = frame_sequence;
        frame->sample_count = sample_count;
        frame->muted = !should_play;
        frame->mic_avg_abs = mic_avg_abs;
        frame->clean_avg_abs = clean_avg_abs;
        if (should_play) {
            voice_callback_apply_noise_floor_suppression(clean_samples, sample_count, clean_avg_abs);
            voice_callback_apply_gate_gain(ctx, clean_samples, sample_count, target_gate_gain_q15);
            memcpy(frame->samples, clean_samples, sample_count * sizeof(frame->samples[0]));
            if (gate == VOICE_CALLBACK_GATE_TAIL) {
                ctx->tail_frames++;
            } else if (gate == VOICE_CALLBACK_GATE_HOLD) {
                ctx->hold_frames++;
            } else {
                ctx->passed_frames++;
            }
        } else {
            voice_callback_apply_gate_gain(ctx, clean_samples, sample_count, 0);
            memcpy(frame->samples, clean_samples, sample_count * sizeof(frame->samples[0]));
            ctx->muted_frames++;
            if (gate == VOICE_CALLBACK_GATE_SPEAKER) {
                ctx->speaker_guard_frames++;
            }
        }

        if (voice_callback_enqueue_playback_frame(ctx, frame)) {
            ctx->captured_frames++;
        }

        int64_t now_us = esp_timer_get_time();
        if (now_us - ctx->last_capture_stats_us >= VOICE_CALLBACK_STATS_PERIOD_US) {
            ctx->last_capture_stats_us = now_us;
            ESP_LOGI(TAG,
                     "capture frames=%" PRIu64 " muted=%" PRIu64 " passed=%" PRIu64
                     " hold=%" PRIu64 " tail=%" PRIu64 " guarded=%" PRIu64
                     " dropped=%" PRIu64 " mic_avg=%" PRIu32 " clean_avg=%" PRIu32
                     " mic_peak=%" PRIu32 " input_gain_q15=%" PRIi32
                     " gate_gain_q15=%" PRIu32 " input_limited=%" PRIu64
                     " spk_avg=%" PRIu32 " free_heap=%" PRIu32,
                     ctx->captured_frames,
                     ctx->muted_frames,
                     ctx->passed_frames,
                     ctx->hold_frames,
                     ctx->tail_frames,
                     ctx->speaker_guard_frames,
                     ctx->dropped_frames,
                     mic_avg_abs,
                     clean_avg_abs,
                     input_peak_abs,
                     ctx->input_gain_q15,
                     ctx->gate_gain_q15,
                     ctx->input_limited_frames,
                     ctx->last_playback_avg_abs,
                     esp_get_free_heap_size());
        }

        taskYIELD();
    }
}

static void voice_callback_playback_task(void *arg)
{
    voice_callback_context_t *ctx = (voice_callback_context_t *)arg;
    int16_t playback_samples[CONFIG_ESPESP_VOICE_CALLBACK_FRAME_SAMPLES];
    TickType_t frame_wait_ticks = pdMS_TO_TICKS(VOICE_CALLBACK_FRAME_DURATION_MS);
    if (frame_wait_ticks == 0) {
        frame_wait_ticks = 1;
    }

    while (true) {
        voice_callback_frame_t *frame = NULL;
        bool have_frame = xQueueReceive(ctx->playback_queue, &frame, frame_wait_ticks) == pdTRUE;
        size_t sample_count = CONFIG_ESPESP_VOICE_CALLBACK_FRAME_SAMPLES;
        bool frame_muted = true;
        uint32_t peak_out = 0;
        uint32_t avg_out = 0;
        uint32_t limited_out = 0;

        if (have_frame && frame != NULL) {
            sample_count = frame->sample_count;
            if (sample_count > CONFIG_ESPESP_VOICE_CALLBACK_FRAME_SAMPLES) {
                sample_count = CONFIG_ESPESP_VOICE_CALLBACK_FRAME_SAMPLES;
            }
            frame_muted = frame->muted;
            for (size_t i = 0; i < sample_count; i++) {
                playback_samples[i] = voice_callback_process_playback_sample(frame->samples[i],
                                                                             &peak_out,
                                                                             &limited_out);
            }
            ctx->playback_limited_samples += limited_out;
            voice_callback_calc_level(playback_samples, sample_count, &avg_out, &peak_out);
        } else {
            memset(playback_samples, 0, sizeof(playback_samples));
            ctx->playback_underflow_frames++;
        }

#if CONFIG_ESPESP_VOICE_CALLBACK_AEC_ENABLED
        if (ctx->aec != NULL && ctx->aec_lock != NULL &&
            xSemaphoreTake(ctx->aec_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
            (void)voice_callback_aec_feed_reference(ctx->aec, playback_samples, sample_count);
            xSemaphoreGive(ctx->aec_lock);
        }
#endif

        esp_err_t ret = voice_callback_write_all_i2s(ctx->tx_channel,
                                                     playback_samples,
                                                     sample_count);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "I2S speaker write failed: %s", esp_err_to_name(ret));
        } else {
            ctx->played_frames++;
            if (peak_out > 0) {
                ctx->last_playback_avg_abs = avg_out;
                ctx->last_playback_peak = peak_out;
                ctx->last_audible_playback_us = esp_timer_get_time();
            }
        }

        int64_t now_us = esp_timer_get_time();
        if (now_us - ctx->last_playback_stats_us >= VOICE_CALLBACK_STATS_PERIOD_US) {
            ctx->last_playback_stats_us = now_us;
            ESP_LOGI(TAG,
                     "playback frames=%" PRIu64 " queue=%u muted=%d avg_out=%" PRIu32
                     " peak_out=%" PRIu32 " limited=%" PRIu64 " underflow=%" PRIu64,
                     ctx->played_frames,
                     (unsigned int)uxQueueMessagesWaiting(ctx->playback_queue),
                     frame_muted ? 1 : 0,
                     avg_out,
                     peak_out,
                     ctx->playback_limited_samples,
                     ctx->playback_underflow_frames);
        }

        if (frame != NULL) {
            voice_callback_return_frame(ctx, frame);
        }
    }
}

static esp_err_t voice_callback_create_queues(voice_callback_context_t *ctx)
{
    ctx->free_queue = xQueueCreate(CONFIG_ESPESP_VOICE_CALLBACK_QUEUE_LENGTH,
                                   sizeof(voice_callback_frame_t *));
    ctx->playback_queue = xQueueCreate(CONFIG_ESPESP_VOICE_CALLBACK_QUEUE_LENGTH,
                                       sizeof(voice_callback_frame_t *));
    if (ctx->free_queue == NULL || ctx->playback_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < CONFIG_ESPESP_VOICE_CALLBACK_QUEUE_LENGTH; i++) {
        voice_callback_frame_t *frame = calloc(1, sizeof(voice_callback_frame_t));
        if (frame == NULL) {
            return ESP_ERR_NO_MEM;
        }
        if (xQueueSend(ctx->free_queue, &frame, 0) != pdTRUE) {
            free(frame);
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

esp_err_t voice_callback_run(void)
{
    voice_callback_context_t *ctx = calloc(1, sizeof(voice_callback_context_t));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ctx->last_capture_stats_us = esp_timer_get_time();
    ctx->last_playback_stats_us = ctx->last_capture_stats_us;

    esp_err_t ret = voice_callback_create_queues(ctx);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = voice_callback_create_rx_channel(&ctx->rx_channel);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = voice_callback_create_tx_channel(&ctx->tx_channel);
    if (ret != ESP_OK) {
        return ret;
    }

#if CONFIG_ESPESP_VOICE_CALLBACK_AEC_ENABLED
    ctx->aec_lock = xSemaphoreCreateMutex();
    if (ctx->aec_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ctx->aec = voice_callback_aec_create(CONFIG_ESPESP_VOICE_CALLBACK_AEC_FILTER_LEN,
                                         CONFIG_ESPESP_VOICE_CALLBACK_AEC_STEP_SIZE_X256,
                                         CONFIG_ESPESP_VOICE_CALLBACK_SAMPLE_RATE_HZ,
                                         CONFIG_ESPESP_VOICE_CALLBACK_AEC_MAX_DELAY_MS,
                                         CONFIG_ESPESP_VOICE_CALLBACK_AEC_REFERENCE_DELAY_MS);
    if (ctx->aec == NULL) {
        ESP_LOGW(TAG, "AEC create failed, running direct monitor without echo cancellation");
    }
#endif

    ESP_RETURN_ON_ERROR(i2s_channel_enable(ctx->rx_channel), TAG, "enable microphone");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(ctx->tx_channel), TAG, "enable speaker");

    ESP_LOGI(TAG,
             "full-duplex voice callback: sample_rate=%dHz frame=%u samples queue=%d mic_slot=%s shift=%d input_limit=%d%% volume=%d%% limit=%d%% gate=%d echo_gate=%d%% speaker_window=%dms aec=%d",
             CONFIG_ESPESP_VOICE_CALLBACK_SAMPLE_RATE_HZ,
             (unsigned int)CONFIG_ESPESP_VOICE_CALLBACK_FRAME_SAMPLES,
             CONFIG_ESPESP_VOICE_CALLBACK_QUEUE_LENGTH,
             VOICE_CALLBACK_MIC_SLOT_NAME,
             CONFIG_ESPESP_VOICE_CALLBACK_MIC_SAMPLE_SHIFT_BITS,
             CONFIG_ESPESP_VOICE_CALLBACK_MIC_INPUT_LIMIT_PERCENT,
             CONFIG_ESPESP_VOICE_CALLBACK_PLAYBACK_VOLUME_PERCENT,
             CONFIG_ESPESP_VOICE_CALLBACK_PLAYBACK_LIMIT_PERCENT,
             CONFIG_ESPESP_VOICE_CALLBACK_NOISE_GATE_AVG_ABS,
             CONFIG_ESPESP_VOICE_CALLBACK_ECHO_GATE_PERCENT,
             CONFIG_ESPESP_VOICE_CALLBACK_SPEAKER_ACTIVE_TIMEOUT_MS,
             CONFIG_ESPESP_VOICE_CALLBACK_AEC_ENABLED);
    ESP_LOGI(TAG,
             "voice gate: max_pending=%u hold=%dms hold_gain=%d%% release=%d%% release_min=%d tail=%dms tail_gain=%d%% highpass=%d alpha_q15=%d suppress_floor=%d",
             (unsigned int)VOICE_CALLBACK_MAX_PENDING_PLAYBACK_FRAMES,
             CONFIG_ESPESP_VOICE_CALLBACK_GATE_HOLD_MS,
             CONFIG_ESPESP_VOICE_CALLBACK_GATE_HOLD_GAIN_PERCENT,
             CONFIG_ESPESP_VOICE_CALLBACK_GATE_RELEASE_PERCENT,
             CONFIG_ESPESP_VOICE_CALLBACK_GATE_RELEASE_MIN_AVG_ABS,
             CONFIG_ESPESP_VOICE_CALLBACK_GATE_TAIL_MS,
             CONFIG_ESPESP_VOICE_CALLBACK_GATE_TAIL_GAIN_PERCENT,
             CONFIG_ESPESP_VOICE_CALLBACK_HIGHPASS_FILTER_ENABLED,
             CONFIG_ESPESP_VOICE_CALLBACK_HIGHPASS_ALPHA_Q15,
             CONFIG_ESPESP_VOICE_CALLBACK_NOISE_SUPPRESS_FLOOR_ABS);
    ESP_LOGI(TAG,
             "I2S microphone: BCLK=GPIO%d WS=GPIO%d DIN=GPIO%d",
             CONFIG_ESPESP_MIC_BCLK_GPIO,
             CONFIG_ESPESP_MIC_WS_GPIO,
             CONFIG_ESPESP_MIC_DIN_GPIO);
    ESP_LOGI(TAG,
             "I2S speaker: BCLK=GPIO%d WS=GPIO%d DOUT=GPIO%d",
             CONFIG_ESPESP_SPK_BCLK_GPIO,
             CONFIG_ESPESP_SPK_WS_GPIO,
             CONFIG_ESPESP_SPK_DOUT_GPIO);

    if (xTaskCreatePinnedToCore(voice_callback_capture_task,
                                "vc_capture",
                                CONFIG_ESPESP_VOICE_CALLBACK_TASK_STACK_SIZE,
                                ctx,
                                VOICE_CALLBACK_CAPTURE_PRIORITY,
                                NULL,
                                VOICE_CALLBACK_TASK_CORE) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreatePinnedToCore(voice_callback_playback_task,
                                "vc_playback",
                                CONFIG_ESPESP_VOICE_CALLBACK_TASK_STACK_SIZE,
                                ctx,
                                VOICE_CALLBACK_PLAYBACK_PRIORITY,
                                NULL,
                                VOICE_CALLBACK_TASK_CORE) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
