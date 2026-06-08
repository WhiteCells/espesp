#include "voice_client/voice_client_aec.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AEC_TAG "voice_aec"
#define AEC_EPSILON 1.0f
#define AEC_YIELD_SAMPLES 64U
#define AEC_MIN_REF_RMS 8.0f
#define AEC_DOUBLE_TALK_RATIO_SQ 16.0f
#define AEC_DOUBLE_TALK_OFFSET_SQ 262144.0f

static uint32_t voice_client_aec_ring_sub(uint32_t index, uint32_t distance, uint32_t size)
{
    if (size == 0) {
        return 0;
    }
    if (distance >= size) {
        distance %= size;
    }
    return (index + size - distance) % size;
}

struct voice_client_aec {
    float *filter_w;
    float *ref_buf;
    uint32_t filter_len;
    uint32_t ref_size;
    uint32_t ref_write_pos;
    uint32_t ref_count;
    uint32_t step_size_x256;
    uint32_t mic_sample_rate;
    uint32_t spk_sample_rate;
    uint32_t reference_delay_samples;
    uint32_t max_delay_samples;
    float mic_per_spk;
    float ref_phase;
    uint32_t tail_remaining;
    bool playback_active;
    bool active;
    uint32_t total_processed;
    uint32_t total_fed;
};

voice_client_aec_t *voice_client_aec_create(uint32_t filter_len,
                                             uint32_t step_size_x256,
                                             uint32_t mic_sample_rate,
                                             uint32_t spk_sample_rate,
                                             uint32_t max_delay_ms)
{
    if (filter_len == 0 || step_size_x256 == 0 ||
        mic_sample_rate == 0 || spk_sample_rate == 0) {
        return NULL;
    }

    voice_client_aec_t *aec = calloc(1, sizeof(voice_client_aec_t));
    if (aec == NULL) {
        return NULL;
    }

    uint32_t max_delay_samples = (mic_sample_rate * max_delay_ms) / 1000;
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
    aec->ref_write_pos = 0;
    aec->ref_count = 0;
    aec->step_size_x256 = step_size_x256;
    aec->mic_sample_rate = mic_sample_rate;
    aec->spk_sample_rate = spk_sample_rate;
    aec->reference_delay_samples = 0;
    aec->max_delay_samples = max_delay_samples;
    aec->mic_per_spk = (float)mic_sample_rate / (float)spk_sample_rate;
    aec->ref_phase = 0.0f;
    aec->tail_remaining = 0;
    aec->playback_active = false;
    aec->active = false;
    aec->total_processed = 0;
    aec->total_fed = 0;

    ESP_LOGI(AEC_TAG,
             "created: filter_len=%" PRIu32 " step=%" PRIu32 "/256 "
             "mic_rate=%" PRIu32 " spk_rate=%" PRIu32 " mic_per_spk=%.3f"
             " max_delay=%" PRIu32 " samples ref_buf=%" PRIu32,
             filter_len, step_size_x256,
             mic_sample_rate, spk_sample_rate, (double)aec->mic_per_spk,
             max_delay_samples,
             ref_size);

    return aec;
}

void voice_client_aec_destroy(voice_client_aec_t *aec)
{
    if (aec == NULL) {
        return;
    }

    ESP_LOGI(AEC_TAG,
             "destroy: processed=%" PRIu32 " fed=%" PRIu32,
             aec->total_processed, aec->total_fed);

    free(aec->filter_w);
    free(aec->ref_buf);
    free(aec);
}

void voice_client_aec_set_reference_delay(voice_client_aec_t *aec, uint32_t reference_delay_ms)
{
    if (aec == NULL || aec->mic_sample_rate == 0) {
        return;
    }

    uint32_t delay_samples = (aec->mic_sample_rate * reference_delay_ms) / 1000U;
    if (delay_samples > aec->max_delay_samples) {
        delay_samples = aec->max_delay_samples;
    }
    aec->reference_delay_samples = delay_samples;

    ESP_LOGI(AEC_TAG,
             "reference delay=%" PRIu32 " ms (%" PRIu32 " samples)",
             reference_delay_ms,
             delay_samples);
}

bool voice_client_aec_has_reference(const voice_client_aec_t *aec, size_t sample_count)
{
    if (aec == NULL || !aec->active || sample_count == 0) {
        return false;
    }

    uint32_t required_ref_count = aec->filter_len +
                                  aec->reference_delay_samples +
                                  (uint32_t)sample_count;
    return aec->ref_count >= required_ref_count;
}

esp_err_t voice_client_aec_feed_reference(voice_client_aec_t *aec,
                                          const int16_t *samples,
                                          size_t count)
{
    if (aec == NULL || samples == NULL || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!aec->playback_active) {
        return ESP_OK;
    }

    for (size_t i = 0; i < count; i++) {
        float ref_sample = (float)samples[i];

        aec->ref_phase += aec->mic_per_spk;
        while (aec->ref_phase >= 1.0f) {
            aec->ref_buf[aec->ref_write_pos] = ref_sample;
            aec->ref_write_pos = (aec->ref_write_pos + 1) % aec->ref_size;
            if (aec->ref_count < aec->ref_size) {
                aec->ref_count++;
            }
            aec->ref_phase -= 1.0f;
        }
    }

    aec->total_fed += (uint32_t)count;
    return ESP_OK;
}

esp_err_t voice_client_aec_process(voice_client_aec_t *aec,
                                   const int16_t *pcm_in,
                                   int16_t *pcm_out,
                                   size_t sample_count)
{
    return voice_client_aec_process_with_adaptation(aec, pcm_in, pcm_out, sample_count, true);
}

esp_err_t voice_client_aec_process_with_adaptation(voice_client_aec_t *aec,
                                                   const int16_t *pcm_in,
                                                   int16_t *pcm_out,
                                                   size_t sample_count,
                                                   bool adapt)
{
    if (aec == NULL || pcm_in == NULL || pcm_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sample_count == 0) {
        return ESP_OK;
    }

    if (!aec->playback_active && aec->tail_remaining == 0) {
        aec->active = false;
    }

    if (!voice_client_aec_has_reference(aec, sample_count)) {
        if (pcm_in != pcm_out) {
            memcpy(pcm_out, pcm_in, sample_count * sizeof(int16_t));
        }
        return ESP_OK;
    }

    if (aec->tail_remaining > 0 && !aec->playback_active) {
        if (aec->tail_remaining >= sample_count) {
            aec->tail_remaining -= (uint32_t)sample_count;
        } else {
            aec->tail_remaining = 0;
        }
    }

    const float mu_scale = (float)aec->step_size_x256 / 256.0f;
    const uint32_t filter_len = aec->filter_len;
    const uint32_t ref_size = aec->ref_size;
    const uint32_t newest = (aec->ref_write_pos + ref_size - 1) % ref_size;
    const uint32_t reference_delay = aec->reference_delay_samples;

    for (size_t n = 0; n < sample_count; n++) {
        if (n > 0 && (n % AEC_YIELD_SAMPLES) == 0) {
            taskYIELD();
        }

        float d = (float)pcm_in[n];
        uint32_t age = reference_delay + (uint32_t)(sample_count - 1 - n);
        if (age >= aec->ref_count) {
            age = aec->ref_count - 1;
        }
        uint32_t current = voice_client_aec_ring_sub(newest, age, ref_size);

        float y_hat = 0.0f;
        for (uint32_t k = 0; k < filter_len; k++) {
            uint32_t idx = voice_client_aec_ring_sub(current, k, ref_size);
            y_hat += aec->filter_w[k] * aec->ref_buf[idx];
        }

        float e = d - y_hat;

        float power = AEC_EPSILON;
        for (uint32_t k = 0; k < filter_len; k++) {
            uint32_t idx = voice_client_aec_ring_sub(current, k, ref_size);
            float x = aec->ref_buf[idx];
            power += x * x;
        }

        float ref_rms_sq = power / (float)filter_len;
        float input_power = d * d;
        bool can_adapt = adapt &&
                         ref_rms_sq >= (AEC_MIN_REF_RMS * AEC_MIN_REF_RMS) &&
                         input_power <=
                         (AEC_DOUBLE_TALK_RATIO_SQ * ref_rms_sq + AEC_DOUBLE_TALK_OFFSET_SQ);
        if (can_adapt) {
            float mu_eff = mu_scale / power;
            for (uint32_t k = 0; k < filter_len; k++) {
                uint32_t idx = voice_client_aec_ring_sub(current, k, ref_size);
                aec->filter_w[k] += mu_eff * e * aec->ref_buf[idx];
            }
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

void voice_client_aec_playback_start(voice_client_aec_t *aec)
{
    if (aec == NULL) {
        return;
    }

    aec->playback_active = true;
    aec->active = true;
    aec->ref_write_pos = 0;
    aec->ref_count = 0;
    aec->ref_phase = 0.0f;
    aec->tail_remaining = 0;

    ESP_LOGD(AEC_TAG, "playback start, ref buffer cleared");
}

void voice_client_aec_playback_end(voice_client_aec_t *aec)
{
    if (aec == NULL) {
        return;
    }

    aec->playback_active = false;
    aec->tail_remaining = aec->ref_size;

    ESP_LOGD(AEC_TAG, "playback end, tail=%" PRIu32 " samples", aec->tail_remaining);
}

void voice_client_aec_set_speaker_rate(voice_client_aec_t *aec, uint32_t spk_sample_rate)
{
    if (aec == NULL || spk_sample_rate == 0) {
        return;
    }

    if (aec->spk_sample_rate == spk_sample_rate) {
        return;
    }

    ESP_LOGI(AEC_TAG,
             "speaker rate changed: %" PRIu32 " -> %" PRIu32 " Hz (mic_per_spk=%.3f)",
             aec->spk_sample_rate, spk_sample_rate,
             (double)((float)aec->mic_sample_rate / (float)spk_sample_rate));

    aec->spk_sample_rate = spk_sample_rate;
    aec->mic_per_spk = (float)aec->mic_sample_rate / (float)spk_sample_rate;

    aec->ref_write_pos = 0;
    aec->ref_count = 0;
    aec->ref_phase = 0.0f;
}
