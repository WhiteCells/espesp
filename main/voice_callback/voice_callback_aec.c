#include "voice_callback/voice_callback_aec.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#define AEC_EPSILON 1.0f

static const char *TAG = "voice_callback_aec";

struct voice_callback_aec {
    float *filter_w;
    float *ref_buf;
    uint32_t filter_len;
    uint32_t ref_size;
    uint32_t ref_write_pos;
    uint32_t ref_count;
    uint32_t reference_delay_samples;
    uint32_t step_size_x256;
    bool active;
    uint32_t total_processed;
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

    aec->filter_w = calloc(filter_len, sizeof(float));
    aec->ref_buf = calloc(ref_size, sizeof(float));
    if (aec->filter_w == NULL || aec->ref_buf == NULL) {
        free(aec->filter_w);
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
             "destroy: processed=%" PRIu32 " fed=%" PRIu32,
             aec->total_processed,
             aec->total_fed);

    free(aec->filter_w);
    free(aec->ref_buf);
    free(aec);
}

void voice_callback_aec_playback_start(voice_callback_aec_t *aec)
{
    if (aec == NULL) {
        return;
    }

    aec->active = true;
    aec->ref_write_pos = 0;
    aec->ref_count = 0;
    memset(aec->filter_w, 0, aec->filter_len * sizeof(aec->filter_w[0]));
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
        aec->ref_buf[aec->ref_write_pos] = (float)samples[i];
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

    uint32_t required_ref_count = aec->filter_len + aec->reference_delay_samples + (uint32_t)sample_count;
    if (!aec->active) {
        if (pcm_in != pcm_out) {
            memcpy(pcm_out, pcm_in, sample_count * sizeof(pcm_out[0]));
        }
        return ESP_OK;
    }
    if (aec->ref_count < required_ref_count) {
        memset(pcm_out, 0, sample_count * sizeof(pcm_out[0]));
        return ESP_OK;
    }

    const float mu_scale = (float)aec->step_size_x256 / 256.0f;
    const uint32_t filter_len = aec->filter_len;
    const uint32_t ref_size = aec->ref_size;
    const uint32_t reference_delay = aec->reference_delay_samples;
    const uint32_t newest = (aec->ref_write_pos + ref_size - 1) % ref_size;

    for (size_t n = 0; n < sample_count; n++) {
        float d = (float)pcm_in[n];
        uint32_t age = reference_delay + (uint32_t)(sample_count - 1 - n);
        if (age >= aec->ref_count) {
            age = aec->ref_count - 1;
        }
        uint32_t current = ring_sub(newest, age, ref_size);

        float y_hat = 0.0f;
        for (uint32_t k = 0; k < filter_len; k++) {
            uint32_t idx = ring_sub(current, k, ref_size);
            y_hat += aec->filter_w[k] * aec->ref_buf[idx];
        }

        float e = d - y_hat;
        float power = AEC_EPSILON;
        for (uint32_t k = 0; k < filter_len; k++) {
            uint32_t idx = ring_sub(current, k, ref_size);
            float x = aec->ref_buf[idx];
            power += x * x;
        }

        float mu_eff = mu_scale / power;
        for (uint32_t k = 0; k < filter_len; k++) {
            uint32_t idx = ring_sub(current, k, ref_size);
            aec->filter_w[k] += mu_eff * e * aec->ref_buf[idx];
        }

        int32_t out = (int32_t)e;
        if (out > INT16_MAX) {
            out = INT16_MAX;
        } else if (out < INT16_MIN) {
            out = INT16_MIN;
        }
        pcm_out[n] = (int16_t)out;
    }

    aec->total_processed += (uint32_t)sample_count;
    return ESP_OK;
}
