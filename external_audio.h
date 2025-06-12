#ifndef __EXTERNAL_AUDIO_H__
#define __EXTERNAL_AUDIO_H__
#ifndef USE_I2S_AUDIO
#define USE_I2S_AUDIO 0
#endif

#ifndef USE_SPI_AUDIO
#define USE_SPI_AUDIO 0
#endif

// generete a compiler error if both USE_I2S_AUDIO and USE_SPI_AUDIO are defined
#if USE_I2S_AUDIO + USE_SPI_AUDIO > 1
#error "Both USE_I2S_AUDIO and USE_SPI_AUDIO cannot be defined at the same time. Please define only one."
#endif

#define EXT_AUDIO_IS_ENABLED (USE_I2S_AUDIO || USE_SPI_AUDIO)

#if USE_I2S_AUDIO
#include "audio_i2s.h"
#define EXT_AUDIO_ENQUEUE_SAMPLE(l, r) audio_i2s_enqueue_sample((uint32_t) ((l << 16) | (r & 0xFFFF)))
#define EXT_AUDIO_SETUP(freq) audio_i2s_setup(freq)
#endif

// SPI audio is not supported in the current version of the code, but we keep the definition for future use.
#if USE_SPI_AUDIO
#include "audio_spi.h"
extern audio_spi_hw_t *spi_audio_hw;
#define EXT_AUDIO_ENQUEUE_SAMPLE(l, r) audio_spi_enqueue_sample(l, r)   
#define EXT_AUDIO_SETUP(freq) audio_spi_setup(freq)
#endif
// If neither I2S nor SPI audio is enabled, define the functions as no-ops
#if !EXT_AUDIO_IS_ENABLED
#ifndef EXT_AUDIO_ENQUEUE_SAMPLE
#define EXT_AUDIO_ENQUEUE_SAMPLE(l, r)  (0)
#endif
#ifndef EXT_AUDIO_SETUP
#define EXT_AUDIO_SETUP(freq) (freq)
#endif
#endif
#endif // __EXTERNAL_AUDIO_H__