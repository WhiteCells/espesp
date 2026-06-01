#ifndef SPEAKER_CLIENT_AUDIO_H
#define SPEAKER_CLIENT_AUDIO_H

#include "esp_err.h"
#include "speaker_client/speaker_client_context.h"

esp_err_t speaker_client_create_i2s_channel(i2s_chan_handle_t *tx_channel);
esp_err_t speaker_client_prepare_stream(speaker_client_context_t *ctx,
                                        uint32_t sample_rate_hz,
                                        uint64_t expected_frames);
esp_err_t speaker_client_write_audio_chunk(speaker_client_context_t *ctx,
                                           const uint8_t *data,
                                           int data_len,
                                           bool message_done);
esp_err_t speaker_client_finish_stream(speaker_client_context_t *ctx, const char *reason);

#endif
