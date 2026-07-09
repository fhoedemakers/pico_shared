#include "hstx_data_island_queue.h"

#include "video_output.h"
#include "hstx_packet.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "pico.h"
#include "hardware/sync.h"  // __dmb
#ifndef DI_RING_BUFFER_SIZE
#define DI_RING_BUFFER_SIZE 256
#endif
// HSTX_DI_RING_ADDRESS lets the caller point the ring at a specific static
// SRAM address. fruitjam-doom uses this to place the ring above the top of
// its shortptr zone (SHORTPTR_BASE + 0x40000 = 0x20076000), reclaiming the
// 36 KB the malloc path would otherwise steal from Doom's heap. When unset,
// the driver falls back to its historical malloc-on-first-init behaviour.
#ifdef HSTX_DI_RING_ADDRESS
static hstx_data_island_t *const di_ring_buffer = (hstx_data_island_t *)(HSTX_DI_RING_ADDRESS);
#else
static hstx_data_island_t *di_ring_buffer = NULL; // [DI_RING_BUFFER_SIZE]
#endif
static volatile uint32_t di_ring_head = 0;
static volatile uint32_t di_ring_tail = 0;

// Single pre-encoded silent audio packet (fixed B-frame flags).
static hstx_data_island_t silence_packet;

// Audio packet scheduler — exact rational arithmetic, zero long-term
// drift. Accumulate `sample_rate` per scanline; a 4-sample packet is due
// each time the accumulator reaches 4 x line_rate. The previous
// fixed-point form truncated sample_rate/60 to whole samples per frame
// (32000/60 -> 533, i.e. 31980 samples/s), a -20 sample/s deficit
// against the ACR-derived sink clock that drained the sink's audio FIFO
// roughly every 6 s — an audible dropout while the receiver re-locked.
// Line rate follows from the 25.2 MHz pixel clock both video modes use.
#define DI_LINE_RATE_HZ (25200000u / MODE_H_TOTAL_PIXELS)
static uint32_t audio_sample_accum = 0;      // unit: samples x line-rate
static uint32_t audio_samples_per_sec = 48000;
extern void * frens_f_malloc(size_t size);
void hstx_di_queue_init(void)
{
    di_ring_head = 0;
    di_ring_tail = 0;
    audio_sample_accum = 0;
    // Allocate memory for the ring buffer (skipped when the address was
    // pinned at compile time via HSTX_DI_RING_ADDRESS).
#ifdef HSTX_DI_RING_ADDRESS
    printf("HSTX DI ring buffer at fixed %p (%u bytes)\n",
           (void *)di_ring_buffer,
           (unsigned)(DI_RING_BUFFER_SIZE * sizeof(hstx_data_island_t)));
#else
    if (di_ring_buffer == NULL) {
        printf("Allocating memory for HSTX DI ring buffer: %d bytes\n", DI_RING_BUFFER_SIZE * sizeof(hstx_data_island_t));
        di_ring_buffer = (hstx_data_island_t *)malloc(DI_RING_BUFFER_SIZE * sizeof(hstx_data_island_t));
    }
#endif
    // Build a single silent audio packet. frame_count=4 (NOT 0): frame 0
    // would set the IEC 60958 block-start B flag, so every underrun
    // insertion would reset the sink's channel-status block sync mid-stream.
    hstx_packet_t packet;
    audio_sample_t samples[4] = {0};
    (void)hstx_packet_set_audio_samples(&packet, samples, 4, 4);
    hstx_encode_data_island(&silence_packet, &packet, false, true);
}

void hstx_di_queue_set_sample_rate(uint32_t sample_rate)
{
    audio_samples_per_sec = sample_rate;
    audio_sample_accum = 0;
}

bool __not_in_flash_func(hstx_di_queue_push)(const hstx_data_island_t *island)
{
    uint32_t next_head = (di_ring_head + 1) % DI_RING_BUFFER_SIZE;
    if (next_head == di_ring_tail)
        return false;

    // Volatile word copy instead of struct assignment: keeps the copy inline
    // so this SRAM function never calls the flash-resident libc memcpy.
    volatile uint32_t *dst = di_ring_buffer[di_ring_head].words;
    const uint32_t *src = island->words;
    for (size_t i = 0; i < count_of(island->words); i++)
        dst[i] = src[i];
    // Publish payload before head: the consumer (DMA IRQ on the other core)
    // must never observe the head advance ahead of the payload words.
    __dmb();
    di_ring_head = next_head;
    return true;
}

uint32_t __not_in_flash_func(hstx_di_queue_get_level)(void)
{
    uint32_t head = di_ring_head;
    uint32_t tail = di_ring_tail;
    if (head >= tail)
        return head - tail;
    return DI_RING_BUFFER_SIZE + head - tail;
}

void __not_in_flash_func(hstx_di_queue_tick)(void)
{
    audio_sample_accum += audio_samples_per_sec;
}

// Counts silence-packet fallbacks (queue empty when a packet was due).
// Diagnostic: lets producers distinguish real queue underruns from
// silence that was genuinely mixed into the stream.
static volatile uint32_t di_underrun_count = 0;

const uint32_t *__not_in_flash_func(hstx_di_queue_get_audio_packet)(void)
{
    // Check if it's time to send a 4-sample audio packet (every ~3.9 lines
    // at 32 kHz / 31.5 kHz line rate)
    if (audio_sample_accum >= 4u * DI_LINE_RATE_HZ) {
        audio_sample_accum -= 4u * DI_LINE_RATE_HZ;
        if (di_ring_tail != di_ring_head) {
            const uint32_t *words = di_ring_buffer[di_ring_tail].words;
            di_ring_tail = (di_ring_tail + 1) % DI_RING_BUFFER_SIZE;
            return words;
        }
        // Queue is empty: return a pre-encoded silent packet to keep HDMI audio active.
        di_underrun_count++;
        return silence_packet.words;
    }
    return NULL;
}

uint32_t hstx_di_queue_get_underrun_count(void)
{
    return di_underrun_count;
}
