#ifndef CHAT_CONTEXT_H
#define CHAT_CONTEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2s_std.h"
#include "esp_vad.h"
#include "esp_vadn_models.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "model_path.h"
#include "sdkconfig.h"
#include "voice_client/voice_client_aec.h"

#define CHAT_CONNECTED_BIT BIT0
#define CHAT_ERROR_BIT BIT1

#define CHAT_AUTH_HEADER_MAX 256
#define CHAT_CONTROL_MAX 512
#define CHAT_SAMPLE_WIDTH_BYTES 2U
#define CHAT_CHANNELS 1U
#define CHAT_STATS_PERIOD_US 1000000LL
#define CHAT_WS_OPCODE_CONTINUATION 0x0
#define CHAT_WS_OPCODE_TEXT 0x1
#define CHAT_WS_OPCODE_BINARY 0x2
#define CHAT_WS_OPCODE_CLOSE 0x8
#define CHAT_WS_OPCODE_PING 0x9
#define CHAT_WS_OPCODE_PONG 0xa

#if CONFIG_ESPESP_CHAT_MIC_SLOT_RIGHT
#define CHAT_MIC_SLOT_MASK I2S_STD_SLOT_RIGHT
#define CHAT_MIC_SLOT_NAME "right"
#else
#define CHAT_MIC_SLOT_MASK I2S_STD_SLOT_LEFT
#define CHAT_MIC_SLOT_NAME "left"
#endif

#if CONFIG_ESPESP_CHAT_VADNET_MODE_0
#define CHAT_VADNET_MODE VAD_MODE_0
#define CHAT_VADNET_MODE_NAME "0 normal"
#elif CONFIG_ESPESP_CHAT_VADNET_MODE_1
#define CHAT_VADNET_MODE VAD_MODE_1
#define CHAT_VADNET_MODE_NAME "1 aggressive"
#elif CONFIG_ESPESP_CHAT_VADNET_MODE_2
#define CHAT_VADNET_MODE VAD_MODE_2
#define CHAT_VADNET_MODE_NAME "2 very aggressive"
#elif CONFIG_ESPESP_CHAT_VADNET_MODE_3
#define CHAT_VADNET_MODE VAD_MODE_3
#define CHAT_VADNET_MODE_NAME "3 very very aggressive"
#else
#define CHAT_VADNET_MODE VAD_MODE_4
#define CHAT_VADNET_MODE_NAME "4 maximum"
#endif

typedef struct {
    uint8_t *data;
    size_t len;
    bool message_done;
    /* generation changes on barge-in/reset; segment_id changes for each tts_start. */
    uint32_t generation;
    uint32_t segment_id;
} chat_playback_chunk_t;

typedef struct {
    EventGroupHandle_t event_group;
    SemaphoreHandle_t playback_lock;
    QueueHandle_t playback_queue;
    i2s_chan_handle_t rx_channel;
    i2s_chan_handle_t tx_channel;
    esp_websocket_client_handle_t client;
    TaskHandle_t playback_task;
    srmodel_list_t *models;
    const esp_vadn_iface_t *vadnet_iface;
    model_iface_data_t *vadnet_model;
    voice_client_aec_t *aec;
    const char *vadnet_model_name;
    volatile bool tx_enabled;
    volatile bool playback_streaming;
    volatile bool playback_pcm;
    volatile bool playback_finishing;
    volatile bool binary_payload_active;
    volatile bool warned_drop_binary;
    volatile bool session_active;
    volatile bool stop_playback_task;
    volatile uint32_t playback_generation;
    volatile uint32_t playback_segment_id;
    volatile uint32_t playback_finishing_segment_id;
    bool has_pending_byte;
    uint8_t pending_byte;
    uint32_t input_sample_rate_hz;
    uint32_t output_sample_rate_hz;
    uint32_t server_tts_sample_rate_hz;
    uint32_t vad_frame_samples;
    uint32_t turn_id;
    uint64_t mic_sent_bytes;
    uint64_t mic_sent_chunks;
    uint64_t mic_dropped_chunks;
    uint64_t speech_segments;
    uint64_t playback_received_bytes;
    uint64_t playback_written_bytes;
    uint64_t playback_dropped_chunks;
    uint64_t playback_samples;
    uint64_t playback_limited_samples;
    uint64_t playback_segment_received_bytes;
    uint32_t playback_chunks;
    uint32_t playback_input_peak;
    uint32_t playback_output_peak;
    int64_t speech_started_us;
    int64_t last_mic_stats_us;
    int64_t playback_started_us;
    int64_t last_playback_stats_us;
} chat_context_t;

extern const char *CHAT_TAG;

#endif
