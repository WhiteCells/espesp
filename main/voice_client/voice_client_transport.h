#ifndef VOICE_CLIENT_TRANSPORT_H
#define VOICE_CLIENT_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_websocket_client.h"
#include "voice_client/voice_client_context.h"

#define VOICE_CLIENT_AUTH_HEADER_MAX 256
#define VOICE_CLIENT_START_PAYLOAD_MAX 768
#define VOICE_CLIENT_CONTROL_MAX 1024
#define VOICE_CLIENT_OPCODE_CONTINUATION 0x0
#define VOICE_CLIENT_OPCODE_TEXT 0x1
#define VOICE_CLIENT_OPCODE_BINARY 0x2
#define VOICE_CLIENT_OPCODE_CLOSE 0x8
#define VOICE_CLIENT_OPCODE_PING 0x9
#define VOICE_CLIENT_OPCODE_PONG 0xA

bool voice_client_uri_is_valid(const char *uri);
esp_err_t voice_client_make_headers(char *headers, size_t headers_len);
esp_err_t voice_client_send_start(esp_websocket_client_handle_t client, voice_client_context_t *ctx);
void voice_client_event_handler(void *handler_args,
                                esp_event_base_t base,
                                int32_t event_id,
                                void *event_data);
esp_err_t voice_client_send_audio_frame(esp_websocket_client_handle_t client,
                                        voice_client_context_t *ctx,
                                        const int16_t *pcm,
                                        size_t sample_count);
void voice_client_log_mic_progress(voice_client_context_t *ctx);

#endif
