

#ifndef _PICO_AUDIO_I2S_PIO_H
#define _PICO_AUDIO_I2S_PIO_H

#include "audio_i2s.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#define PICO_AUDIO_I2S_DRIVER_NONE 0         // No I2S driver
#define PICO_AUDIO_I2S_DRIVER_TLV320 1       // TLV320 DAC - Adafruit FruitJam
#define PICO_AUDIO_I2S_DRIVER_PCM5000A 2     // PCM5000A DAC - Pimoroni Pico Dv Demo base
#ifndef PICO_AUDIO_I2S_FREQ
#define PICO_AUDIO_I2S_FREQ 44100
#endif
#ifndef PICO_AUDIO_I2S_COUNT
#define PICO_AUDIO_I2S_COUNT 2
#endif


// Reset pin, when using TLV320 codec
#ifndef PICO_AUDIO_I2S_RESET_PIN
#define PICO_AUDIO_I2S_RESET_PIN 7
#endif
// Data pin, DIN or SDATA
#ifndef PICO_AUDIO_I2S_DATA_PIN
#define PICO_AUDIO_I2S_DATA_PIN 26
#endif

#ifndef PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED
#define PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED 0
#endif
// Note:  BCK is also known as BCLK
//        LRCLK is also known as WS (Word Select), WSEL, WCLK
// PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED == 0: BASE = BCK, BASE + 1 = LRCLK 
// PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED == 1: BASE = LRCLK, BASE + 1 = BCK
#ifndef PICO_AUDIO_I2S_CLOCK_PIN_BASE
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE 27
#endif
#ifndef PICO_AUDIO_I2S_PIO
#define PICO_AUDIO_I2S_PIO 0
#endif

// headphone detect pin, when using TLV320 codec
// #ifndef PICO_AUDIO_I2S_HP_DETECT_PIN
// #define PICO_AUDIO_I2S_HP_DETECT_PIN -1
// #endif



#ifdef __cplusplus
extern "C" {
#endif


#define AUDIO_RING_SIZE 1024 // Size of the audio ring buffer (must be a multiple of DMA_BLOCK_SIZE)
#define DMA_BLOCK_SIZE 256 // Size of each DMA block transfer

#define TLV320_HEADPHONE_NOTCONNECTED 0b00 // Headphone not connected
#define TLV320_HEADPHONE_CONNECTED 0b01     // Headphone connected
#define TLV320_HEADPHONE_CONNECTED_WITH_MIC 0b11 // Headphone connected with microphone

typedef struct {
    int sm;      // State machine index
    PIO pio;     // PIO instance (e.g., pio0 or pio1)
    int dma_chan; // DMA channel for audio transfer
} audio_i2s_hw_t;

audio_i2s_hw_t *audio_i2s_setup(int driver, int freqHZ, int dmachan);
void audio_i2s_update_pio_frequency(uint32_t sample_freq);
void audio_i2s_out_32(uint32_t sample32);
void audio_i2s_enqueue_sample(uint32_t sample32);
void audio_i2s_poll_headphone_status();
#ifdef __cplusplus
}
#endif

#endif //_PICO_AUDIO_I2S_PIO_H
