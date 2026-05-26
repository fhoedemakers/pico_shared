/*
 *  pico_audio_i2s_pe/audio_i2s.h
 *
 *  Legacy-shaped header for the pico-extras based driver. The function
 *  signatures here match the custom driver one-for-one so that callers
 *  (FrensHelpers, main.cpp, wavplayer.cpp, ...) compile unchanged.
 *
 *  The implementation lives in audio_i2s_adapter.c and internally drives a
 *  pico-extras audio_buffer_pool.
 */

#ifndef _PICO_AUDIO_I2S_PE_H
#define _PICO_AUDIO_I2S_PE_H

#include "tlv320dac3100.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PICO_AUDIO_I2S_DRIVER_NONE      0
#define PICO_AUDIO_I2S_DRIVER_TLV320    1
#define PICO_AUDIO_I2S_DRIVER_PCM5000A  2

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

#ifndef PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED
#define PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED 0
#endif

#ifndef PICO_AUDIO_I2S_PIO
#define PICO_AUDIO_I2S_PIO 0
#endif

// DC blocking filter: removes DC offset so DACs without built-in DC blocking
// (e.g. PCM5102A) output a properly centered AC waveform.
// Uses a leaky integrator to track and subtract the running average.
// Shift 10 ≈ 7 Hz cutoff at 44100 Hz. Higher = more bass preserved, slower response.
#ifndef I2S_AUDIO_COMPENSATE_DC_OFFSET
#define I2S_AUDIO_COMPENSATE_DC_OFFSET 0
#endif
#ifndef I2S_DC_FILTER_SHIFT
#define I2S_DC_FILTER_SHIFT 10
#endif

/* Buffer-pool sizing for the producer side. Total RAM = COUNT * SAMPLES * 4
 * bytes (stereo S16). Defaults (4 * 256 = 1024 samples) match the legacy
 * 1024-sample ring buffer exactly. */
#ifndef PICO_AUDIO_I2S_PE_BUFFER_COUNT
#define PICO_AUDIO_I2S_PE_BUFFER_COUNT 4
#endif

#ifndef PICO_AUDIO_I2S_PE_BUFFER_SAMPLES
#define PICO_AUDIO_I2S_PE_BUFFER_SAMPLES 256
#endif

/* Backwards-compatible aliases used by callers of the legacy driver. */
#define I2S_AUDIO_RING_SIZE (PICO_AUDIO_I2S_PE_BUFFER_COUNT * PICO_AUDIO_I2S_PE_BUFFER_SAMPLES)
#define AUDIO_RING_MASK (I2S_AUDIO_RING_SIZE - 1)
#define DMA_BLOCK_SIZE PICO_AUDIO_I2S_PE_BUFFER_SAMPLES

typedef struct {
    int sm;
    PIO pio;
    int dma_chan;
} audio_i2s_hw_t;

/* enum headphone_toggle_t is declared in tlv320dac3100.h */

audio_i2s_hw_t *audio_i2s_setup(int driver, int freqHZ, int dmachan);
void audio_i2s_update_pio_frequency(uint32_t sample_freq);
void audio_i2s_out_32(uint32_t sample32);
void audio_i2s_enqueue_sample(uint32_t sample32);
enum headphone_toggle_t audio_i2s_poll_headphone_status(void);
int audio_i2s_get_freebuffer_size(void);
void audio_i2s_disable(void);
bool audio_i2s_dacError(void);
void audio_i2s_muteInternalSpeaker(bool mute);
void audio_i2s_setVolume(int8_t level);

#ifdef __cplusplus
}
#endif

#endif /* _PICO_AUDIO_I2S_PE_H */
