#ifndef __EXTERNAL_AUDIO_H__
#define __EXTERNAL_AUDIO_H__

#include "audio_i2s.h"
#ifndef USE_I2S_AUDIO
#define USE_I2S_AUDIO PICO_AUDIO_I2S_DRIVER_NONE
#endif

#ifndef USE_SPI_AUDIO
#define USE_SPI_AUDIO 0
#endif

// generete a compiler error if both USE_I2S_AUDIO and USE_SPI_AUDIO are defined
#if USE_I2S_AUDIO > 0 && USE_SPI_AUDIO == 1
#error "Both USE_I2S_AUDIO and USE_SPI_AUDIO cannot be defined at the same time. Please define only one."
#endif

#define EXT_AUDIO_IS_ENABLED (USE_I2S_AUDIO || USE_SPI_AUDIO)

#if USE_I2S_AUDIO
#include "audio_i2s.h"
#define EXT_AUDIO_ENQUEUE_SAMPLE(l, r) audio_i2s_enqueue_sample((uint32_t) ((l << 16) | (r & 0xFFFF)))
// Define a macro for setting up the I2S audio hardware
// The macro takes the driver, frequency, and a DMA channel as parameters. -1 means find the first unused DMA channel
// If the driver is PICO_AUDIO_I2S_DRIVER_NONE, it will skip the setup
#define EXT_AUDIO_SETUP(driver, freq, dmachannelstart) audio_i2s_setup(driver, freq, dmachannelstart)
#define EXT_AUDIO_POLL_HEADPHONE_STATUS() audio_i2s_poll_headphone_status()
#endif

// SPI audio is not supported in the current version of the code, but we keep the definition for future use.
#if USE_SPI_AUDIO
#include "audio_spi.h"
extern audio_spi_hw_t *spi_audio_hw;
#define EXT_AUDIO_ENQUEUE_SAMPLE(l, r) audio_spi_enqueue_sample(l, r)   
#define EXT_AUDIO_SETUP(driver, freq, dmachan) audio_spi_setup(driver, freq, dmachan)
#define EXT_AUDIO_POLL_HEADPHONE_STATUS() audio_spi_poll_headphone_status()
#endif
// If neither I2S nor SPI audio is enabled, define the functions as no-ops
#if !EXT_AUDIO_IS_ENABLED
#ifndef EXT_AUDIO_ENQUEUE_SAMPLE
#define EXT_AUDIO_ENQUEUE_SAMPLE(l, r)  (0)
#endif
#ifndef EXT_AUDIO_SETUP
#define EXT_AUDIO_SETUP(driver, freq, dmachan) (driver, freq, dmachan)
#endif
#ifndef EXT_AUDIO_POLL_HEADPHONE_STATUS
#define EXT_AUDIO_POLL_HEADPHONE_STATUS() (0)
#endif
#endif
#endif // __EXTERNAL_AUDIO_H__