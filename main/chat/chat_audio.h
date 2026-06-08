#ifndef CHAT_AUDIO_H
#define CHAT_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#include "chat/chat_context.h"
#include "esp_err.h"

/* 初始化 VADNet 模型，并把模型要求的采样率、帧长写入 ctx。 */
esp_err_t chat_audio_init_vadnet(chat_context_t *ctx);

/* 创建麦克风 I2S RX 通道；采样率来自已初始化的 VADNet 模型。 */
esp_err_t chat_audio_create_rx_channel(chat_context_t *ctx);

/* 创建扬声器 I2S TX 通道；默认使用 Chat 配置里的扬声器采样率。 */
esp_err_t chat_audio_create_tx_channel(chat_context_t *ctx);

/* 将 32-bit I2S 麦克风样本按配置缩放并限幅为 pcm_s16le 样本。 */
int16_t chat_audio_convert_sample(int32_t sample);

/* 切换扬声器输出采样率；TTS 服务端采样率变化时调用。 */
esp_err_t chat_audio_set_output_sample_rate(chat_context_t *ctx, uint32_t sample_rate_hz);

/* 启用扬声器 TX 前预填静音，减少 I2S/DMA 启动瞬态。 */
esp_err_t chat_audio_prime_tx_channel(chat_context_t *ctx, uint32_t sample_rate_hz);

/* 需要播放时启用扬声器 TX；空闲时保持关闭以降低数字串扰底噪。 */
esp_err_t chat_audio_enable_tx(chat_context_t *ctx, uint32_t sample_rate_hz);

/* 播放结束或打断后关闭扬声器 TX。 */
esp_err_t chat_audio_disable_tx(chat_context_t *ctx);

/* 释放 VADNet/模型列表等音频模块资源。 */
void chat_audio_cleanup(chat_context_t *ctx);

#endif
