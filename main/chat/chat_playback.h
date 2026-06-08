#ifndef CHAT_PLAYBACK_H
#define CHAT_PLAYBACK_H

#include <stdbool.h>
#include <stdint.h>

#include "chat/chat_context.h"
#include "esp_err.h"

/* 启动 TTS 播放任务；任务从播放队列取 PCM 并写入 I2S。 */
esp_err_t chat_playback_start_task(chat_context_t *ctx);

/* 停止 TTS 播放任务，并清空未播放的音频队列。 */
void chat_playback_stop_task(chat_context_t *ctx);

/* 收到 tts_start 时调用，准备播放后续 raw pcm_s16le 音频。 */
esp_err_t chat_playback_begin(chat_context_t *ctx, uint32_t sample_rate_hz);

/* 收到 tts_done 时调用，在已入队 PCM 播完后结束播放状态。 */
void chat_playback_end(chat_context_t *ctx, const char *reason);

/* 本地打断/断线/错误时调用，立即丢弃队列并重置扬声器 DMA。 */
void chat_playback_interrupt(chat_context_t *ctx, const char *reason);

/* 入队服务端二进制 TTS 音频；data 是 mono raw pcm_s16le。 */
esp_err_t chat_playback_enqueue_audio(chat_context_t *ctx,
                                      const uint8_t *data,
                                      int data_len,
                                      bool message_done);

#endif
