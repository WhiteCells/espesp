#include "voice_callback/voice_callback_aec.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#define AEC_EPSILON_POWER 1ULL
#define AEC_MIN_REF_RMS 8U
#define AEC_DOUBLE_TALK_RATIO_SQ 16ULL
#define AEC_DOUBLE_TALK_OFFSET_SQ 262144ULL
#define AEC_COEFF_LIMIT_Q15 (32768 * 2)
#define AEC_GRADIENT_Q_SHIFT 20

static const char *TAG = "voice_callback_aec";

struct voice_callback_aec {
    int32_t *filter_w_q15;
    int16_t *ref_buf;
    uint32_t filter_len;
    uint32_t ref_size;
    uint32_t ref_write_pos;
    uint32_t ref_count;
    uint32_t reference_delay_samples;
    uint32_t step_size_x256;
    bool active;
    uint32_t total_processed;
    uint32_t total_adapted;
    uint32_t total_fed;
};

static uint32_t ring_sub(uint32_t index, uint32_t distance, uint32_t size)
{
    if (size == 0) {
        return 0;
    }
    if (distance >= size) {
        distance %= size;
    }
    return (index + size - distance) % size;
}

static uint32_t ring_next(uint32_t index, uint32_t size)
{
    index++;
    return index >= size ? 0 : index;
}

static uint32_t ring_prev(uint32_t index, uint32_t size)
{
    return index == 0 ? size - 1 : index - 1;
}

static int16_t saturate_i16(int32_t sample)
{
    if (sample > INT16_MAX) {
        return INT16_MAX;
    }
    if (sample < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)sample;
}

static int32_t clamp_i32(int64_t value, int32_t min_value, int32_t max_value)
{
    if (value > max_value) {
        return max_value;
    }
    if (value < min_value) {
        return min_value;
    }
    return (int32_t)value;
}

voice_callback_aec_t *voice_callback_aec_create(uint32_t filter_len,
                                                 uint32_t step_size_x256,
                                                 uint32_t sample_rate_hz,
                                                 uint32_t max_delay_ms,
                                                 uint32_t reference_delay_ms)
{
    if (filter_len == 0 || step_size_x256 == 0 || sample_rate_hz == 0) {
        return NULL;
    }

    voice_callback_aec_t *aec = calloc(1, sizeof(voice_callback_aec_t));
    if (aec == NULL) {
        return NULL;
    }

    uint32_t max_delay_samples = (sample_rate_hz * max_delay_ms) / 1000;
    uint32_t reference_delay_samples = (sample_rate_hz * reference_delay_ms) / 1000;
    if (reference_delay_samples > max_delay_samples) {
        reference_delay_samples = max_delay_samples;
    }
    uint32_t ref_size = filter_len + max_delay_samples + 512;

    aec->filter_w_q15 = calloc(filter_len, sizeof(aec->filter_w_q15[0]));
    aec->ref_buf = calloc(ref_size, sizeof(aec->ref_buf[0]));
    if (aec->filter_w_q15 == NULL || aec->ref_buf == NULL) {
        free(aec->filter_w_q15);
        free(aec->ref_buf);
        free(aec);
        return NULL;
    }

    aec->filter_len = filter_len;
    aec->ref_size = ref_size;
    aec->reference_delay_samples = reference_delay_samples;
    aec->step_size_x256 = step_size_x256;

    ESP_LOGI(TAG,
             "created: filter_len=%" PRIu32 " step=%" PRIu32 "/256 sample_rate=%" PRIu32
             " ref_delay=%" PRIu32 " samples ref_buf=%" PRIu32,
             filter_len,
             step_size_x256,
             sample_rate_hz,
             reference_delay_samples,
             ref_size);

    return aec;
}

void voice_callback_aec_destroy(voice_callback_aec_t *aec)
{
    if (aec == NULL) {
        return;
    }

    ESP_LOGI(TAG,
             "destroy: processed=%" PRIu32 " adapted=%" PRIu32 " fed=%" PRIu32,
             aec->total_processed,
             aec->total_adapted,
             aec->total_fed);

    free(aec->filter_w_q15);
    free(aec->ref_buf);
    free(aec);
}

static void voice_callback_aec_playback_start(voice_callback_aec_t *aec)
{
    if (aec == NULL) {
        return;
    }

    aec->active = true;
    aec->ref_write_pos = 0;
    aec->ref_count = 0;
    memset(aec->filter_w_q15, 0, aec->filter_len * sizeof(aec->filter_w_q15[0]));
    memset(aec->ref_buf, 0, aec->ref_size * sizeof(aec->ref_buf[0]));
    ESP_LOGD(TAG, "playback reference reset");
}

esp_err_t voice_callback_aec_feed_reference(voice_callback_aec_t *aec,
                                            const int16_t *samples,
                                            size_t count)
{
    if (aec == NULL || samples == NULL || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!aec->active) {
        voice_callback_aec_playback_start(aec);
    }

    for (size_t i = 0; i < count; i++) {
        aec->ref_buf[aec->ref_write_pos] = samples[i];
        aec->ref_write_pos = (aec->ref_write_pos + 1) % aec->ref_size;
        if (aec->ref_count < aec->ref_size) {
            aec->ref_count++;
        }
    }

    aec->total_fed += (uint32_t)count;
    return ESP_OK;
}

esp_err_t voice_callback_aec_process(voice_callback_aec_t *aec,
                                     const int16_t *pcm_in,
                                     int16_t *pcm_out,
                                     size_t sample_count)
{
    if (aec == NULL || pcm_in == NULL || pcm_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sample_count == 0) {
        return ESP_OK;
    }

    if (!aec->active) {
        if (pcm_in != pcm_out) {
            memcpy(pcm_out, pcm_in, sample_count * sizeof(pcm_out[0]));
        }
        return ESP_OK;
    }

    uint32_t required_ref_count = aec->filter_len + aec->reference_delay_samples + (uint32_t)sample_count;
    if (aec->ref_count < required_ref_count) {
        if (pcm_in != pcm_out) {
            memcpy(pcm_out, pcm_in, sample_count * sizeof(pcm_out[0]));
        }
        return ESP_OK;
    }

    const uint64_t min_ref_power = (uint64_t)AEC_MIN_REF_RMS *
                                   (uint64_t)AEC_MIN_REF_RMS *
                                   (uint64_t)aec->filter_len;
    const uint32_t filter_len = aec->filter_len;
    const uint32_t ref_size = aec->ref_size;
    const uint32_t reference_delay = aec->reference_delay_samples;
    const uint32_t newest = (aec->ref_write_pos + ref_size - 1) % ref_size;
    uint32_t current = ring_sub(newest,
                                reference_delay + (uint32_t)sample_count - 1,
                                ref_size);

    for (size_t n = 0; n < sample_count; n++) {
        int32_t d = pcm_in[n];
        int64_t y_acc_q15 = 0;
        uint64_t power = AEC_EPSILON_POWER;
        uint32_t idx = current;
        for (uint32_t k = 0; k < filter_len; k++) {
            int32_t x = aec->ref_buf[idx];
            y_acc_q15 += (int64_t)aec->filter_w_q15[k] * (int64_t)x;
            power += (uint64_t)((int64_t)x * (int64_t)x);
            idx = ring_prev(idx, ref_size);
        }

        int32_t y_hat = clamp_i32(y_acc_q15 >> 15, INT32_MIN, INT32_MAX);
        int32_t e = clamp_i32((int64_t)d - (int64_t)y_hat, INT16_MIN, INT16_MAX);

        uint64_t input_power = (uint64_t)((int64_t)d * (int64_t)d);
        uint64_t double_talk_limit = AEC_DOUBLE_TALK_RATIO_SQ * (power / filter_len) +
                                     AEC_DOUBLE_TALK_OFFSET_SQ;
        bool can_adapt = power >= min_ref_power && input_power <= double_talk_limit;
        if (can_adapt) {
            int64_t gradient_q = ((((int64_t)aec->step_size_x256 * (int64_t)e * 128LL)
                                   << AEC_GRADIENT_Q_SHIFT) /
                                  (int64_t)power);
            idx = current;
            for (uint32_t k = 0; k < filter_len; k++) {
                int32_t x = aec->ref_buf[idx];
                int64_t delta = (gradient_q * (int64_t)x) >> AEC_GRADIENT_Q_SHIFT;
                int64_t updated = (int64_t)aec->filter_w_q15[k] + delta;
                aec->filter_w_q15[k] = clamp_i32(updated,
                                                 -AEC_COEFF_LIMIT_Q15,
                                                 AEC_COEFF_LIMIT_Q15);
                idx = ring_prev(idx, ref_size);
            }
            aec->total_adapted++;
        }

        pcm_out[n] = saturate_i16(e);
        current = ring_next(current, ref_size);
    }

    aec->total_processed += (uint32_t)sample_count;
    return ESP_OK;
}
