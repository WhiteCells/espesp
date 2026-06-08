#ifndef VOICE_CLIENT_AEC_H
#define VOICE_CLIENT_AEC_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct voice_client_aec voice_client_aec_t;

voice_client_aec_t *voice_client_aec_create(uint32_t filter_len,
                                             uint32_t step_size_x256,
                                             uint32_t mic_sample_rate,
                                             uint32_t spk_sample_rate,
                                             uint32_t max_delay_ms);

void voice_client_aec_destroy(voice_client_aec_t *aec);

void voice_client_aec_set_reference_delay(voice_client_aec_t *aec, uint32_t reference_delay_ms);

bool voice_client_aec_has_reference(const voice_client_aec_t *aec, size_t sample_count);

esp_err_t voice_client_aec_feed_reference(voice_client_aec_t *aec,
                                          const int16_t *samples,
                                          size_t count);

esp_err_t voice_client_aec_process(voice_client_aec_t *aec,
                                   const int16_t *pcm_in,
                                   int16_t *pcm_out,
                                   size_t sample_count);

esp_err_t voice_client_aec_process_with_adaptation(voice_client_aec_t *aec,
                                                   const int16_t *pcm_in,
                                                   int16_t *pcm_out,
                                                   size_t sample_count,
                                                   bool adapt);

void voice_client_aec_playback_start(voice_client_aec_t *aec);

void voice_client_aec_playback_end(voice_client_aec_t *aec);

void voice_client_aec_set_speaker_rate(voice_client_aec_t *aec, uint32_t spk_sample_rate);

#endif
