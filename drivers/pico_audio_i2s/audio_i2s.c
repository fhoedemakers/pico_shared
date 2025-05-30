/*****************************************************************************
 * |  Pico Audio I2S Library
 * |
 * |  Description:
 * |    This library provides functions for audio output using the I2S protocol
 * |    on the Raspberry Pi Pico (RP2040) and Pico 2 (RP2350) via the PIO (Programmable I/O) hardware.
 * |    It supports initialization, frequency configuration, sample output, and
 * |    volume adjustment for streaming audio to external I2S DACs.
 * |
 * |  Features:
 * |    - I2S hardware setup and configuration
 * |    - Output of audio samples (single or block)
 * |    - Adjustable sample rate (frequency)
 * |    - Volume scaling for 16-bit and 32-bit samples
 * |    - Memory management helpers for audio buffers
 * |
 * |  Usage:
 * |    1. Call audio_i2s_setup() to initialize the I2S hardware.
 * |    2. Use audio_i2s_update_pio_frequency() to set the sample rate.
 * |    3. Output audio samples using audio_i2s_out() or audio_i2s_out_32().
 * |    4. Use volume adjustment and buffer management functions as needed.
 * |
 * |  Author: Frank Hoedemakers. Based on the Pico Audio I2S PIO example in the pico_extras
 * |  repository (https://github.com/raspberrypi/pico-extras/tree/master/src/rp2_common/pico_audio_i2s)
 * |  and Waveshare Pico Audio Demo code. (https://www.waveshare.com/wiki/Pico-Audio)
 * |  License: MIT
 ******************************************************************************/

#include "audio_i2s.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "pico/stdlib.h"
#include <stdlib.h>
#include "audio_i2s.pio.h"

#define AUDIO_PIO __CONCAT(pio, PICO_AUDIO_I2S_PIO)
#define GPIO_FUNC_PIOx __CONCAT(GPIO_FUNC_PIO, PICO_AUDIO_I2S_PIO)

int32_t audio_buffer[3][AUDIO_BUFFER_SIZE]; // Two buffers
volatile int current_buffer = 0;			// Index of buffer being filled
volatile int dma_buffer = 0;				// Index of buffer being sent by DMA
volatile int audio_buffer_index = 0;

static audio_i2s_hw_t audio_i2s = {
	.sm = -1,		  // State machine index, initialized to -1 (not set)
	.pio = AUDIO_PIO, // PIO instance for audio I2S
	.dma_chan = -1	  // DMA channel for audio transfer, initialized to -1 (not set)
};

static int samplefreq = PICO_AUDIO_I2S_FREQ;

/**
 * @brief Updates the PIO frequency for the audio I2S interface.
 *
 * This function sets the clock divider for the PIO state machine based on the provided sample frequency.
 * It calculates the appropriate divider value to ensure that the PIO operates at the desired sample rate.
 *
 * @param sample_freq The desired sample frequency in Hertz. This should be a multiple of 256 for proper I2S operation.
 */
void audio_i2s_update_pio_frequency(uint32_t sample_freq)
{
	samplefreq = sample_freq;
	uint32_t system_clock_frequency = clock_get_hz(clk_sys);
	uint32_t divider = system_clock_frequency * 4 / samplefreq; // avoid arithmetic overflow
	pio_sm_set_clkdiv_int_frac(AUDIO_PIO, audio_i2s.sm, divider >> 8u, divider & 0xffu);
}

/**
 * @brief Outputs a block of 256 audio samples to the I2S interface.
 *
 * This function sends an array of 32-bit signed integer audio samples to the I2S output.
 * It is intended for streaming audio data to an external I2S device.
 *
 * @param samples Pointer to an array of 32-bit signed audio samples to output.
 */
void audio_i2s_out(int32_t *samples)
{
	for (uint16_t i = 0; i < 256; i++)
		pio_sm_put_blocking(AUDIO_PIO, audio_i2s.sm, samples[i]);
}

/**
 * @brief Outputs a 32-bit audio sample to the I2S interface.
 *
 * This function sends a single 32-bit signed integer audio sample to the I2S output.
 * It is intended to be used for streaming audio data to an external I2S device.
 *
 * @param sample The 32-bit signed audio sample to output.
 */
void audio_i2s_out_32(int32_t sample)
{
	pio_sm_put_blocking(AUDIO_PIO, audio_i2s.sm, sample);
}

/**
 * @brief Retrieves the PIO instance used for audio I2S operations.
 *
 * This function returns the PIO instance that is currently configured for audio I2S output.
 * It can be used to access the PIO hardware directly if needed.
 *
 * @return The PIO instance used for audio I2S operations.
 */
PIO audio_i2s_get_pio(void)
{
	return audio_i2s.pio;
}

void __isr dma_handler()
{
}
/**
 * @brief Initializes and sets up the I2S audio hardware with the specified sample frequency.
 *
 * This function configures the I2S hardware interface for audio output at the given frequency (in Hz).
 * It allocates and initializes an audio_i2s_hw_t structure, sets up the necessary hardware peripherals,
 * and prepares the system for audio data transmission.
 *
 * @param freqHZ The desired audio sample frequency in Hertz.
 * @return Pointer to an initialized audio_i2s_hw_t structure.
 */
audio_i2s_hw_t *audio_i2s_setup(int freqHZ)
{
	if (freqHZ > 0)
	{
		samplefreq = freqHZ;
	}
	gpio_set_function(PICO_AUDIO_I2S_DATA_PIN, GPIO_FUNC_PIOx);
	gpio_set_function(PICO_AUDIO_I2S_CLOCK_PIN_BASE, GPIO_FUNC_PIOx);
	gpio_set_function(PICO_AUDIO_I2S_CLOCK_PIN_BASE + 1, GPIO_FUNC_PIOx);

	audio_i2s.sm = pio_claim_unused_sm(AUDIO_PIO, false);

	const struct pio_program *program =
#if PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED
		&audio_i2s_swapped_program
#else
		&audio_i2s_program
#endif
		;
	uint offset = pio_add_program(AUDIO_PIO, program);

	audio_i2s_program_init(AUDIO_PIO, audio_i2s.sm, offset, PICO_AUDIO_I2S_DATA_PIN, PICO_AUDIO_I2S_CLOCK_PIN_BASE);

	uint32_t system_clock_frequency = clock_get_hz(clk_sys);
	uint32_t divider = system_clock_frequency * 4 / samplefreq; // avoid arithmetic overflow
	pio_sm_set_clkdiv_int_frac(AUDIO_PIO, audio_i2s.sm, divider >> 8u, divider & 0xffu);

	pio_sm_set_enabled(AUDIO_PIO, audio_i2s.sm, true);
	if (audio_i2s.dma_chan == -1)
	{
		audio_i2s.dma_chan = dma_claim_unused_channel(true);
	}
	dma_channel_config c = dma_channel_get_default_config(audio_i2s.dma_chan);
	channel_config_set_transfer_data_size(&c, DMA_SIZE_32); // 32-bit samples
	channel_config_set_read_increment(&c, true);
	channel_config_set_write_increment(&c, false);
	channel_config_set_dreq(&c, pio_get_dreq(AUDIO_PIO, audio_i2s.sm, true)); // true = TX

	dma_channel_configure(
		audio_i2s.dma_chan,
		&c,
		&AUDIO_PIO->txf[audio_i2s.sm], // Write address (PIO FIFO)
		audio_buffer,				   // Read address (your buffer)
		AUDIO_BUFFER_SIZE,			   // Number of transfers
		false						   // Don't start yet
	);
	if (audio_i2s.dma_chan >= 4)
	{
		dma_channel_set_irq1_enabled(audio_i2s.dma_chan, true);
		irq_set_exclusive_handler(DMA_IRQ_1, dma_handler);
		irq_set_enabled(DMA_IRQ_1, true);
	}
	else
	{
		dma_channel_set_irq0_enabled(audio_i2s.dma_chan, true);
		irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
		irq_set_enabled(DMA_IRQ_0, true);
	}
	return &audio_i2s; // Return the audio_i2s hardware structure for further use
}

void start_dma_transfer(int32_t *buffer, size_t count)
{
	// Reconfigure the DMA read address and transfer count if needed
	dma_channel_set_read_addr(audio_i2s.dma_chan, buffer, false);
	dma_channel_set_trans_count(audio_i2s.dma_chan, count, true); // true = start immediately
}
/**
 * @brief Audio output over time
 *
 * This functin repeats the audio output for a specified duration in seconds.
 *
 * @param samples Pointer to an array of 32-bit audio samples.
 * @param time In seconds, the duration for which the audio samples should be processed.
 */
void audio_i2s_Time_out(int32_t *samples, uint32_t time)
{
	for (uint32_t i = 0; i < (samplefreq * time) / 256; i++)
		audio_i2s_out(samples);
}

/**
 * @brief Audio output over time
 *
 * This function repeats the audio output for a specified duration in milliseconds.
 *
 * @param samples Pointer to an array of 32-bit audio samples.
 * @param time_ms In millieseconds, the duration for which the audio samples should be processed.
 */
void audio_i2s_Time_out_ms(int32_t *samples, uint32_t time_ms)
{
	for (uint32_t i = 0; i < (samplefreq * time_ms) / (1000 * 256); i++)
		audio_i2s_out(samples);
}

/**
 * @brief Adjusts the volume of a buffer of 16-bit audio samples and returns a new buffer of 32-bit samples.
 *
 * This function takes an array of 16-bit signed audio samples, applies a volume adjustment,
 * and returns a pointer to a newly allocated array of 32-bit signed samples with the volume applied.
 *
 * @param samples Pointer to the input buffer containing 16-bit signed audio samples.
 * @param len Number of samples in the input buffer.
 * @param volume Volume adjustment factor (typically 0-255, where 255 is 100% volume).
 * @return Pointer to a newly allocated buffer of 32-bit signed samples with volume applied.
 *         The caller is responsible for freeing this buffer.
 */
int32_t *audio_i2s_Volume_32(int16_t *samples, uint32_t len, uint8_t volume)
{
	uint32_t *samples1 = (uint32_t *)calloc(len, sizeof(uint32_t));
	for (uint32_t i = 0; i < len; i++)
		samples1[i] = (((samples[i] * volume) / 100) * 65536) + ((samples[i] * volume) / 100);
	return samples1;
}

/**
 * @brief Adjusts the volume of an array of 32-bit audio samples.
 *
 * This function applies a volume scaling factor to each sample in the provided array.
 *
 * @param samples Pointer to the array of 32-bit signed audio samples.
 * @param len Number of samples in the array.
 * @param volume Volume scaling factor (typically 0-255, where 255 is maximum volume).
 * @return Pointer to the processed array of samples.
 */
int32_t *audio_i2s_Volume_321(int32_t *samples, uint32_t len, uint8_t volume)
{
	uint32_t *samples1 = (uint32_t *)calloc(len, sizeof(uint32_t));
	int16_t a, b;
	for (uint32_t i = 0; i < len; i++)
	{

		a = samples[i] >> 16;
		b = samples[i];
		samples1[i] = (((a * volume) / 100) * 65536) + ((b * volume) / 100);
	}

	return samples1;
}

/**
 * @brief Adjusts the volume of a buffer of 16-bit audio samples.
 *
 * This function applies a volume scaling factor to an array of 16-bit signed audio samples.
 * The scaling is determined by the provided volume parameter.
 *
 * @param samples Pointer to the input buffer containing 16-bit signed audio samples.
 * @param len The number of samples in the buffer.
 * @param volume The volume scaling factor (typically 0-255, where 255 is maximum volume).
 * @return Pointer to a buffer containing the volume-adjusted 32-bit signed audio samples.
 */
int32_t *audio_i2s_Volume_16(int16_t *samples, uint32_t len, uint8_t volume)
{
	int32_t *samples1 = (int32_t *)calloc(len, sizeof(int32_t));
	for (uint32_t i = 0; i < len; i++)
		samples1[i] = (samples[i] * volume) / 100;
	return samples1;
}

/**
 * @brief Frees memory allocated for a 32-bit integer audio sample buffer.
 *
 * This function releases the memory previously allocated for an array of
 * 32-bit signed integer audio samples used in I2S audio processing.
 *
 * @param samples Pointer to the buffer of 32-bit audio samples to be freed.
 */
void audio_i2s_free_32(int32_t *samples)
{
	int32_t *samples1 = samples;
	samples = NULL;
	free(samples1);
}

/**
 * @brief Frees memory allocated for 16-bit audio samples.
 *
 * This function releases the memory previously allocated for an array of
 * 16-bit signed integer audio samples used in I2S audio processing.
 *
 * @param samples Pointer to the buffer of 16-bit audio samples to be freed.
 */
void audio_i2s_free_16(int16_t *samples)
{
	int16_t *samples1 = samples;
	samples = NULL;
	free(samples1);
}

void flush_audio_buffer()
{
	if (audio_buffer_index > 0)
	{
		// Send the remaining samples (audio_buffer, audio_buffer_index)
		start_dma_transfer(audio_buffer[dma_buffer], audio_buffer_index);
		audio_buffer_index = 0;
		current_buffer ^= 1;
	}
}
