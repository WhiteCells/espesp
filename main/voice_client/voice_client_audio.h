#ifndef VOICE_CLIENT_AUDIO_H
#define VOICE_CLIENT_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2s_std.h"
#include "esp_err.h"
#include "sdkconfig.h"
#include "voice_client/voice_client_context.h"

#define VOICE_CLIENT_SAMPLE_WIDTH_BYTES 2U
#define VOICE_CLIENT_PLAYBACK_WORK_SAMPLES 256U

#ifndef CONFIG_ESPESP_VOICE_CLIENT_TTS_VOLUME_PERCENT
#define CONFIG_ESPESP_VOICE_CLIENT_TTS_VOLUME_PERCENT 60
#endif

#ifndef CONFIG_ESPESP_VOICE_CLIENT_TTS_LIMIT_PERCENT
#define CONFIG_ESPESP_VOICE_CLIENT_TTS_LIMIT_PERCENT 90
#endif

#if CONFIG_ESPESP_VOICE_CLIENT_MIC_SLOT_RIGHT
#define VOICE_CLIENT_MIC_SLOT_MASK I2S_STD_SLOT_RIGHT
#define VOICE_CLIENT_MIC_SLOT_NAME "right"
#else
#define VOICE_CLIENT_MIC_SLOT_MASK I2S_STD_SLOT_LEFT
#define VOICE_CLIENT_MIC_SLOT_NAME "left"
#endif

esp_err_t voice_client_create_rx_channel(i2s_chan_handle_t *rx_channel);
esp_err_t voice_client_create_tx_channel(i2s_chan_handle_t *tx_channel);
int16_t voice_client_convert_sample(int32_t sample);
esp_err_t voice_client_set_output_sample_rate(voice_client_context_t *ctx, uint32_t sample_rate_hz);
void voice_client_reset_playback_stats(voice_client_context_t *ctx);
esp_err_t voice_client_write_audio_chunk(voice_client_context_t *ctx,
                                         const uint8_t *data,
                                         int data_len,
                                         bool message_done);

#endif
