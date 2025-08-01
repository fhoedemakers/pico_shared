

#ifndef _PICO_AUDIO_I2S_PIO_H
#define _PICO_AUDIO_I2S_PIO_H

#include "audio_i2s.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"

#ifndef PICO_AUDIO_I2S_FREQ
#define PICO_AUDIO_I2S_FREQ 44100
#endif
#ifndef PICO_AUDIO_I2S_COUNT
#define PICO_AUDIO_I2S_COUNT 2
#endif
#ifndef PICO_AUDIO_I2S_DATA_PIN
#define PICO_AUDIO_I2S_DATA_PIN 26
#endif
#ifndef PICO_AUDIO_I2S_CLOCK_PIN_BASE
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE 27
#endif
#ifndef PICO_AUDIO_I2S_PIO
#define PICO_AUDIO_I2S_PIO 0
#endif

#ifndef PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED
#define PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED 0
#endif


#ifdef __cplusplus
extern "C" {
#endif


#define AUDIO_RING_SIZE 1024 // Size of the audio ring buffer (must be a multiple of DMA_BLOCK_SIZE)
#define DMA_BLOCK_SIZE 256 // Size of each DMA block transfer

typedef struct {
    int sm;      // State machine index
    PIO pio;     // PIO instance (e.g., pio0 or pio1)
    int dma_chan; // DMA channel for audio transfer
} audio_i2s_hw_t;

audio_i2s_hw_t *audio_i2s_setup(int freqHZ, int dmachan);
void audio_i2s_update_pio_frequency(uint32_t sample_freq);
void audio_i2s_out_32(uint32_t sample32);
void audio_i2s_enqueue_sample(uint32_t sample32);
#ifdef __cplusplus
}
#endif

#endif //_PICO_AUDIO_I2S_PIO_H
