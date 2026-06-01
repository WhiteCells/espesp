#ifndef SPEAKER_CLIENT_CONTEXT_H
#define SPEAKER_CLIENT_CONTEXT_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define SPEAKER_CLIENT_CONNECTED_BIT BIT0
#define SPEAKER_CLIENT_ERROR_BIT BIT1
#define SPEAKER_CLIENT_AUTH_HEADER_MAX 256
#define SPEAKER_CLIENT_CONTROL_MAX 512
#define SPEAKER_CLIENT_SAMPLE_WIDTH_BYTES 2
#define SPEAKER_CLIENT_AUDIO_WORK_SAMPLES 128
#define SPEAKER_CLIENT_STATS_PERIOD_US 1000000LL
#define SPEAKER_CLIENT_DECLICK_MS 5U
#define SPEAKER_CLIENT_START_PRIME_MS 8U
#define SPEAKER_CLIENT_STOP_GUARD_MS 24U

typedef struct {
    EventGroupHandle_t event_group;
    i2s_chan_handle_t tx_channel;
    volatile bool tx_enabled;
    volatile bool streaming;
    volatile bool binary_payload_active;
    volatile bool warned_drop_without_stream;
    volatile bool has_pending_byte;
    uint8_t pending_byte;
    uint32_t sample_rate_hz;
    uint32_t ramp_total_samples;
    uint32_t ramp_in_remaining;
    uint64_t expected_frames;
    uint64_t received_bytes;
    uint64_t written_bytes;
    uint64_t declick_written_bytes;
    uint32_t received_chunks;
    int16_t last_output_sample;
    bool has_last_output_sample;
    int64_t stream_started_us;
    int64_t last_stats_us;
} speaker_client_context_t;

extern const char *SPEAKER_CLIENT_TAG;

#endif
