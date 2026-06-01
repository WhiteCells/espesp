#ifndef VOICE_CALLBACK_AEC_H
#define VOICE_CALLBACK_AEC_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct voice_callback_aec voice_callback_aec_t;

voice_callback_aec_t *voice_callback_aec_create(uint32_t filter_len,
                                                 uint32_t step_size_x256,
                                                 uint32_t sample_rate_hz,
                                                 uint32_t max_delay_ms,
                                                 uint32_t reference_delay_ms);

void voice_callback_aec_destroy(voice_callback_aec_t *aec);

esp_err_t voice_callback_aec_feed_reference(voice_callback_aec_t *aec,
                                            const int16_t *samples,
                                            size_t count);

esp_err_t voice_callback_aec_process(voice_callback_aec_t *aec,
                                     const int16_t *pcm_in,
                                     int16_t *pcm_out,
                                     size_t sample_count);

#endif
