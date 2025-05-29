/*****************************************************************************
* |	This version:   V1.0
* | Date        :   2021-04-20
* | Info        :   
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
******************************************************************************/

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

#define AUDIO_BUFFER_SIZE 768   // 737
extern int32_t audio_buffer[3][AUDIO_BUFFER_SIZE]; // Three buffers
extern volatile int current_buffer ;            // Index of buffer being filled
extern volatile int dma_buffer ;                // Index of buffer being sent by DMA
extern volatile int audio_buffer_index; 

typedef struct {
    int sm;      // State machine index
    PIO pio;     // PIO instance (e.g., pio0 or pio1)
    int dma_chan; // DMA channel for audio transfer
} audio_i2s_hw_t;

audio_i2s_hw_t *audio_i2s_setup(int freqHZ);
void audio_i2s_update_pio_frequency(uint32_t sample_freq);
void audio_i2s_out(int32_t *samples);
void audio_i2s_out_32(int32_t sample);
void audio_i2s_time_out(int32_t *samples, uint32_t time);
void audio_i2s_time_out_ms(int32_t *samples, uint32_t time_ms);
int32_t* audio_i2s_volume_16(int16_t *samples, uint32_t len, uint8_t volume);
int32_t* audio_i2s_volume_32(int16_t *samples, uint32_t len, uint8_t volume);
int32_t* audio_i2s_volume_321(int32_t *samples, uint32_t len, uint8_t volume);
void audio_i2s_free_32(int32_t *samples);
void audio_i2s_free_16(int16_t *samples);
PIO audio_i2s_get_pio(void); 
void start_dma_transfer(int32_t* buffer, size_t count) ;
void flush_audio_buffer(void);
#ifdef __cplusplus
}
#endif

#endif //_PICO_AUDIO_I2S_PIO_H
