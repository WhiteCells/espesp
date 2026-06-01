#ifndef SPEAKER_CLIENT_PROTOCOL_H
#define SPEAKER_CLIENT_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#include "speaker_client/speaker_client_context.h"

bool speaker_client_json_get_string(const char *json, const char *key, char *out, size_t out_len);
bool speaker_client_json_get_u64(const char *json, const char *key, uint64_t *out);
bool speaker_client_json_get_u32(const char *json, const char *key, uint32_t *out);
void speaker_client_handle_control_text(speaker_client_context_t *ctx, const char *json);

#endif
