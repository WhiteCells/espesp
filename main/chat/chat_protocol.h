#ifndef CHAT_PROTOCOL_H
#define CHAT_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "chat/chat_context.h"
#include "esp_err.h"

/* 校验 vchat WebSocket URI，仅接受 ws:// 或 wss://。 */
bool chat_uri_is_valid(const char *uri);

/* 根据配置生成握手 Header；未配置 token 时返回空字符串。 */
esp_err_t chat_make_headers(char *headers, size_t headers_len);

/* 发送 audio_start，声明后续上行音频格式并开启当前语音会话。 */
esp_err_t chat_send_audio_start(chat_context_t *ctx);

/* 发送 audio_end，结束当前语音会话并请求服务端输出最终 ASR。 */
esp_err_t chat_send_audio_end(chat_context_t *ctx);

/* 请求服务端取消正在生成或发送的回复，通常用于本地 VAD 打断。 */
esp_err_t chat_send_cancel_response(chat_context_t *ctx, const char *reason);

/* 发送一帧 mono raw pcm_s16le 麦克风音频；仅在会话激活时实际发送。 */
esp_err_t chat_send_audio_frame(chat_context_t *ctx, const int16_t *pcm, size_t sample_count);

/* 周期性输出麦克风/VAD/播放状态日志。 */
void chat_log_mic_progress(chat_context_t *ctx, bool speech_active, uint32_t avg_abs, uint32_t peak);

/* ESP WebSocket 事件入口，解析 vchat 控制事件并分发 TTS 二进制音频。 */
void chat_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

#endif
