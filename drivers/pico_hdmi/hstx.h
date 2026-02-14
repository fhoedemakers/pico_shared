#pragma once
#if PICO_RP2350
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "video_output.h"
#include "hstx_packet.h"
#include "hstx_data_island_queue.h"
extern volatile bool HSTX_vblank;
// Calculate HSTX output bit from GPIO number (GPIO12-19 => bit 0-7)
#define HSTX_BIT_FROM_GPIO(gpio) ((gpio) - 12)
#ifndef HSTX_AUDIO_DI_HIGH_WATERMARK
#define HSTX_AUDIO_DI_HIGH_WATERMARK 200  // ~16–18 ms at 4 samples/packet
#endif
uint32_t hstx_getframecounter(void);
void hstx_waitForVSync(void);
uint8_t *hstx_getframebuffer(void);
void hstx_setScanLines(int enable);
uint16_t *hstx_getlineFromFramebuffer(int scanline);
void hstx_init(bool dviOnly);
void video_output_core1_run(void);
void hstx_push_audio_sample(const int left, const int right);
#ifdef __cplusplus
}
#endif
#endif