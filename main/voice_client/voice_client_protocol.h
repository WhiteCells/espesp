#ifndef VOICE_CLIENT_PROTOCOL_H
#define VOICE_CLIENT_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "voice_client/voice_client_context.h"

#define VOICE_CLIENT_CHANNELS 1U

bool voice_client_json_get_string(const char *json, const char *key, char *out, size_t out_len);
bool voice_client_json_get_u64(const char *json, const char *key, uint64_t *out);
bool voice_client_json_get_u32(const char *json, const char *key, uint32_t *out);
esp_err_t voice_client_build_start_event(char *payload, size_t payload_len);
void voice_client_handle_control_text(voice_client_context_t *ctx, const char *json);

#endif
