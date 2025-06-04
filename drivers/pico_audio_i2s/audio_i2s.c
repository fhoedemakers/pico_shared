/*
 *  pico_audio_i2s.c
 *
 *  Purpose:
 *  This library provides an I2S audio output driver for the Raspberry Pi Pico (RP2040) and Pico 2 (RP2350).
 *  It uses the PIO (Programmable I/O) subsystem and DMA (Direct Memory Access) to stream 32-bit audio samples
 *  from a ring buffer to an external I2S audio device with minimal CPU intervention.
 *
 *  Features:
 *    - Configurable sample rate and PIO state machine setup for I2S protocol
 *    - Ring buffer management for continuous audio streaming
 *    - DMA-based transfer for low-latency, high-throughput audio output
 *    - Interrupt-driven buffer refill and underrun handling
 *    - Simple API for initializing, updating, and outputting audio samples
 *
 *  Intended for use in embedded audio applications, emulators, and projects requiring high-quality digital audio output.
 *  Based on: https://github.com/raspberrypi/pico-extras/tree/master/src/rp2_common/pico_audio_i2s
 *            and the xample code provided in https://www.waveshare.com/wiki/Pico-Audio
 */

#include "audio_i2s.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "pico/stdlib.h"
#include <stdlib.h>
#include "audio_i2s.pio.h"
#include "string.h"
#include "stdio.h"

#define AUDIO_PIO __CONCAT(pio, PICO_AUDIO_I2S_PIO)
#define GPIO_FUNC_PIOx __CONCAT(GPIO_FUNC_PIO, PICO_AUDIO_I2S_PIO)

// Ring buffer for audio samples. It holds AUDIO_RING_SIZE samples, which are 32-bit integers.
uint32_t *audio_ring = NULL;
volatile size_t write_index = 0;
volatile size_t read_index = 0;

// Audio I2S hardware structure that holds the state machine, PIO instance, and DMA channel.
static audio_i2s_hw_t audio_i2s = {
	.sm = -1,		  // State machine index, initialized to -1 (not set)
	.pio = AUDIO_PIO, // PIO instance for audio I2S
	.dma_chan = -1	  // DMA channel for audio transfer, initialized to -1 (not set)
};

// Sample frequency for the audio I2S interface, initialized to the default PICO_AUDIO_I2S_FREQ.
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
 * @brief Outputs a 32-bit audio sample to the I2S interface.
 *
 * This function sends a single 32-bit unsigned integer audio sample to the I2S output.
 * It is intended to be used for streaming audio data to an external I2S device withou using DMA.
 *
 * @param sample The 32-bit unsigned audio sample to output.
 */
void audio_i2s_out_32(uint32_t sample32)
{
	pio_sm_put_blocking(AUDIO_PIO, audio_i2s.sm, sample32);
}

/**
 * @brief DMA interrupt handler for audio I2S output.
 *
 * This function is called when the DMA transfer is complete. It clears the interrupt,
 * updates the read index to point to the next block of audio data, and checks if there
 * is enough data available in the ring buffer to continue the DMA transfer.
 */
void __isr dma_handler()
{
	// Clear the interrupt
	dma_hw->ints0 = 1u << audio_i2s.dma_chan;
	// Advance read_index by block size
	read_index = (read_index + DMA_BLOCK_SIZE) % AUDIO_RING_SIZE;
	// Check if we have enough data to continue DMA transfer
	// If write_index is ahead of read_index, we have data to send
	size_t available = (write_index >= read_index)
						   ? (write_index - read_index)
						   : (AUDIO_RING_SIZE - read_index + write_index);

	if (available >= DMA_BLOCK_SIZE)
	{
		dma_channel_set_read_addr(audio_i2s.dma_chan, &audio_ring[read_index], false);
		dma_channel_set_trans_count(audio_i2s.dma_chan, DMA_BLOCK_SIZE, true); // true = start immediately
	}
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
	audio_ring = malloc(AUDIO_RING_SIZE * sizeof(uint32_t)); // Allocate memory for the audio ring buffer
	memset(audio_ring, 0, AUDIO_RING_SIZE * sizeof(uint32_t)); // Initialize the audio ring buffer to zero
	write_index = DMA_BLOCK_SIZE;			   // Start writing after the first block
	read_index = 0;							   // Reset read index
	// Set up the PIO and GPIO pins for I2S audio output
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
	channel_config_set_transfer_data_size(&c, DMA_SIZE_32);					  // 32-bit samples
	channel_config_set_read_increment(&c, true);							  // Read address will increment
	channel_config_set_write_increment(&c, false);							  // Write address (PIO FIFO) will not increment
	channel_config_set_dreq(&c, pio_get_dreq(AUDIO_PIO, audio_i2s.sm, true)); // true = TX

	// Set up the DMA channel configuration
	dma_channel_configure(
		audio_i2s.dma_chan,
		&c,
		&AUDIO_PIO->txf[audio_i2s.sm], // Write address (PIO FIFO)
		&audio_ring[read_index],	   // Read address (ring buffer)
		DMA_BLOCK_SIZE,				   // Number of transfers per block
		false						   // Don't start yet
	);

	// Enable the DMA channel interrupt
	// This will trigger when the DMA transfer is complete.
	// chan 1-3 are used for PIO0, chan 4-7 for PIO1
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
	dma_channel_set_read_addr(audio_i2s.dma_chan, &audio_ring[read_index], false);
	dma_channel_set_trans_count(audio_i2s.dma_chan, DMA_BLOCK_SIZE, true); // true = start immediately
	return &audio_i2s;													   // Return the audio_i2s hardware structure for further use
}

/**
 * @brief Enqueues a 32-bit audio sample into the ring buffer for I2S output.
 *
 * This function adds a 32-bit audio sample to the ring buffer. If the buffer is full, it drops the sample
 * and prints a warning message every 100 dropped samples. It also checks if the DMA channel is busy and
 * starts a new DMA transfer if there is enough data available in the ring buffer.
 *
 * @param sample32 The 32-bit audio sample to enqueue.
 */
void __not_in_flash_func(audio_i2s_enqueue_sample)(uint32_t sample32)
{
#if 0
	static int droppedsamples = 0;
#endif
	// Ensure we don't write past the end of the buffer

	size_t next_write = (write_index + 1) % AUDIO_RING_SIZE;
	if (next_write != read_index)
	{
		audio_ring[write_index] = sample32;
		write_index = next_write;
		// If the DMA channel is not busy, check if we have enough data to continue the transfer
	    // If write_index is ahead of read_index, we have data to send.
		if (!dma_channel_is_busy(audio_i2s.dma_chan))
		{
			size_t available = (write_index >= read_index)
								   ? (write_index - read_index)
								   : (AUDIO_RING_SIZE - read_index + write_index);
			if (available >= DMA_BLOCK_SIZE)
			{
				dma_channel_set_read_addr(audio_i2s.dma_chan, &audio_ring[read_index], false);
				dma_channel_set_trans_count(audio_i2s.dma_chan, DMA_BLOCK_SIZE, true);
			}
		}
	}
#if 0
	else
	{
		// Buffer is full, drop sample
		droppedsamples++;
		if (droppedsamples % 1000 == 0)
		{
			printf("Dropped samples: %d\n", droppedsamples);
		}
	}
#endif
	
}