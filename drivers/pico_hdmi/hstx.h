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

#if USE80COLS
// 8bpp palette-indexed text mode, used by the 80-column menu.
//
// The scanline callback normally reads a 320-pixel RGB555 row and doubles each
// pixel to fill the 640-pixel output line. In text mode it instead reads a
// 640-*byte* row of palette indices and expands them 1:1 through `pal`, giving
// 640 distinct pixels — 80 columns of 8-pixel glyphs — out of the very same
// 153,600-byte framebuffer. No HSTX register, command list or DMA change is
// involved: the output stage already carries 640 distinct pixels.
//
// `state` is deliberately three-valued, not a bool: no single byte is black in
// both interpretations (16bpp 0x0000 is black, but 8bpp index 0 is whatever
// palette entry 0 holds), so switching needs a blanking state to avoid flashing
// the wrong colour for a frame.
#define HSTX_TEXTMODE_OFF   0  // 320-pixel RGB555 source, pixel-doubled
#define HSTX_TEXTMODE_640   1  // 640-byte palette-index source, LUT-expanded
#define HSTX_TEXTMODE_BLANK 2  // emit black regardless of framebuffer contents
//
// `pal` supplies up to 64 RGB555 entries and is copied into an SRAM table, so
// the caller's array may live in flash — the IRQ never touches it. Pass NULL to
// leave the existing table alone. Safe to call from either core.
void hstx_setTextMode640(int state, const uint16_t *pal, int palCount);

// Start of one 640-byte palette-index row. Same address arithmetic as
// hstx_getlineFromFramebuffer (which already steps 640 bytes per scanline),
// just typed for byte writes.
uint8_t *hstx_getTextLine640(int scanline);
#endif // USE80COLS

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