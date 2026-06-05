#ifndef CHAT_PLAYBACK_H
#define CHAT_PLAYBACK_H

#include <stdbool.h>
#include <stdint.h>

#include "chat/chat_context.h"
#include "esp_err.h"

esp_err_t chat_playback_start_task(chat_context_t *ctx);
void chat_playback_stop_task(chat_context_t *ctx);
esp_err_t chat_playback_begin(chat_context_t *ctx, uint32_t sample_rate_hz);
void chat_playback_end(chat_context_t *ctx, const char *reason);
void chat_playback_interrupt(chat_context_t *ctx, const char *reason);
esp_err_t chat_playback_enqueue_audio(chat_context_t *ctx,
                                      const uint8_t *data,
                                      int data_len,
                                      bool message_done);

#endif
