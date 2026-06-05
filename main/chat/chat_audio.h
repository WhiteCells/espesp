#ifndef CHAT_AUDIO_H
#define CHAT_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#include "chat/chat_context.h"
#include "esp_err.h"

esp_err_t chat_audio_init_vadnet(chat_context_t *ctx);
esp_err_t chat_audio_create_rx_channel(chat_context_t *ctx);
esp_err_t chat_audio_create_tx_channel(chat_context_t *ctx);
int16_t chat_audio_convert_sample(int32_t sample);
esp_err_t chat_audio_set_output_sample_rate(chat_context_t *ctx, uint32_t sample_rate_hz);
void chat_audio_cleanup(chat_context_t *ctx);

#endif
