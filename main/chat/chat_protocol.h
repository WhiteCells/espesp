#ifndef CHAT_PROTOCOL_H
#define CHAT_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "chat/chat_context.h"
#include "esp_err.h"

bool chat_uri_is_valid(const char *uri);
esp_err_t chat_make_headers(char *headers, size_t headers_len);
esp_err_t chat_send_audio_start(chat_context_t *ctx);
esp_err_t chat_send_audio_end(chat_context_t *ctx);
esp_err_t chat_send_cancel_response(chat_context_t *ctx, const char *reason);
esp_err_t chat_send_audio_frame(chat_context_t *ctx, const int16_t *pcm, size_t sample_count);
void chat_log_mic_progress(chat_context_t *ctx, bool speech_active, uint32_t avg_abs, uint32_t peak);
void chat_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

#endif
