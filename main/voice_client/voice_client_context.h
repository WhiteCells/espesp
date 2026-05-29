#ifndef VOICE_CLIENT_CONTEXT_H
#define VOICE_CLIENT_CONTEXT_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define VOICE_CLIENT_CONNECTED_BIT BIT0
#define VOICE_CLIENT_ERROR_BIT BIT1
#define VOICE_CLIENT_STATS_PERIOD_US 1000000LL

typedef struct {
    EventGroupHandle_t event_group;
    i2s_chan_handle_t rx_channel;
    i2s_chan_handle_t tx_channel;
    volatile bool tx_enabled;
    volatile bool start_pending;
    volatile bool session_started;
    volatile bool playback_streaming;
    volatile bool playback_pcm;
    volatile bool binary_payload_active;
    volatile bool warned_drop_binary;
    volatile bool has_pending_byte;
    uint8_t pending_byte;
    uint32_t output_sample_rate_hz;
    uint64_t mic_sent_bytes;
    uint64_t mic_sent_chunks;
    uint64_t mic_dropped_chunks;
    uint64_t tts_received_bytes;
    uint64_t tts_written_bytes;
    uint64_t tts_samples;
    uint64_t tts_limited_samples;
    uint32_t tts_input_peak;
    uint32_t tts_output_peak;
    uint32_t tts_chunks;
    int64_t playback_started_us;
    int64_t last_mic_stats_us;
    int64_t last_playback_stats_us;
} voice_client_context_t;

extern const char *VOICE_CLIENT_TAG;

#endif
