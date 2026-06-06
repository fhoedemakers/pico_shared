#pragma once
#if PICO_RP2350
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

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
void hstx_paceFrame(bool init);
uint8_t *hstx_getframebuffer(void);
void hstx_setScanLines(int enable);
void hstx_setAspectRatio87(int enable);
void hstx_setScanLineType(int type);
uint16_t *hstx_getlineFromFramebuffer(int scanline);
void hstx_init(bool dviOnly);
void video_output_core1_run(void);
void hstx_push_audio_sample(const int left, const int right);

// Tear HSTX + core1 down and re-launch core1 with the supplied stack
// buffer. Used by pico-pcePlus to grow core1's stack at runtime when a
// CHD CD game is mounted (libchdr decompression needs ~8 KB). The caller
// must keep `new_stack` allocated until the next restart.
void hstx_restart_core1(uint32_t *new_stack, size_t new_stack_bytes);

// Return the boot-time static core1 stack pointer + size (in bytes via
// *out_bytes). Pass these back to hstx_restart_core1 to restore the
// default stack after the CHD game exits.
void *hstx_default_core1_stack(size_t *out_bytes);
#ifdef __cplusplus
}
#endif
#endif