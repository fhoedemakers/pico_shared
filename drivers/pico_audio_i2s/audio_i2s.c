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
 *  TLV320DAC3100 codec control lives in the separate tlv320dac3100 module so it
 *  can be shared with the pico-extras based driver.
 *
 *  Intended for use in embedded audio applications, emulators, and projects requiring high-quality digital audio output.
 *  Based on: https://github.com/raspberrypi/pico-extras/tree/master/src/rp2_common/pico_audio_i2s
 *            and the example code provided in https://www.waveshare.com/wiki/Pico-Audio
 */

#include "audio_i2s.h"
#include "tlv320dac3100.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include <stdlib.h>
#include "audio_i2s.pio.h"
#include "string.h"
#include "stdio.h"

#define AUDIO_PIO __CONCAT(pio, PICO_AUDIO_I2S_PIO)
#define GPIO_FUNC_PIOx __CONCAT(GPIO_FUNC_PIO, PICO_AUDIO_I2S_PIO)

// Ring buffer for audio samples. It holds AUDIO_RING_SIZE samples, which are 32-bit integers.
static uint32_t *audio_ring = NULL;
static volatile size_t write_index = 0;
static volatile size_t read_index = 0;
static int _driver = 0;

// Audio I2S hardware structure that holds the state machine, PIO instance, and DMA channel.
static audio_i2s_hw_t audio_i2s = {
	.sm = -1,		  // State machine index, initialized to -1 (not set)
	.pio = AUDIO_PIO, // PIO instance for audio I2S
	.dma_chan = -1	  // DMA channel for audio transfer, initialized to -1 (not set)
};

// Sample frequency for the audio I2S interface, initialized to the default PICO_AUDIO_I2S_FREQ.
static int samplefreq = PICO_AUDIO_I2S_FREQ;

/// Poll for pending headphone detection events. Delegates to the shared codec
/// module so the same logic is reused by the pico-extras based driver.
enum headphone_toggle_t audio_i2s_poll_headphone_status()
{
	return tlv320_poll_headphone();
}

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
	read_index = (read_index + DMA_BLOCK_SIZE) % I2S_AUDIO_RING_SIZE;
	// Check if we have enough data to continue DMA transfer
	size_t available = (write_index >= read_index)
						   ? (write_index - read_index)
						   : (I2S_AUDIO_RING_SIZE - read_index + write_index);
	if (available >= DMA_BLOCK_SIZE)
	{
		dma_channel_set_read_addr(audio_i2s.dma_chan, &audio_ring[read_index], false);
		dma_channel_set_trans_count(audio_i2s.dma_chan, DMA_BLOCK_SIZE, true); // true = start immediately
	}
}

/**
 * @brief Initializes and sets up the I2S audio hardware with the specified sample frequency.
 *
 * @param driver The I2S driver to use (e.g., PICO_AUDIO_I2S_DRIVER_TLV320).
 * @param freqHZ The sample frequency in Hertz. If set to 0, the default PICO_AUDIO_I2S_FREQ is used.
 * @param dmachan The DMA channel to use for audio transfer. If set to -1, a free DMA channel will be claimed.
 */
audio_i2s_hw_t *audio_i2s_setup(int driver, int freqHZ, int dmachan)
{
	printf("Setting up legacy I2S audio with driver %d, frequency %d Hz, DMA channel %d\n", driver, freqHZ, dmachan);
	_driver = driver; // Store the selected driver globally
	if (_driver == PICO_AUDIO_I2S_DRIVER_NONE)
	{
		printf("No I2S driver selected, skipping audio setup.\n");
		return NULL;
	}
	if (_driver == PICO_AUDIO_I2S_DRIVER_TLV320)
	{
		tlv320_init();
	}
	audio_i2s.dma_chan = dmachan;
	if (freqHZ > 0)
	{
		samplefreq = freqHZ;
	}
	printf("Allocating %d bytes for audio ring buffer\n", I2S_AUDIO_RING_SIZE * sizeof(uint32_t));
	audio_ring = malloc(I2S_AUDIO_RING_SIZE * sizeof(uint32_t));
	memset(audio_ring, 0, I2S_AUDIO_RING_SIZE * sizeof(uint32_t));
	write_index = 0;
	read_index = 0;
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
	printf("Using DMA channel %d for audio I2S\n", audio_i2s.dma_chan);
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
	// chan 0-3 use IRQ_0, chan 4+ use IRQ_1
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
	if (_driver == PICO_AUDIO_I2S_DRIVER_TLV320)
	{
		// Pre-fill one DMA block of silence and start DMA so that BCLK begins
		// toggling immediately, giving the TLV320's PLL time to lock before
		// real audio arrives.
		write_index = DMA_BLOCK_SIZE;
		dma_channel_set_read_addr(audio_i2s.dma_chan, &audio_ring[read_index], false);
		dma_channel_set_trans_count(audio_i2s.dma_chan, DMA_BLOCK_SIZE, true);
	}
	// For other DACs (e.g. PCM5000A), don't start DMA yet — enqueue_sample
	// will kick it once enough real data accumulates.
	return &audio_i2s;
}

// DC-blocking high-pass filter.
// Removes DC offset so DACs without built-in DC blocking (e.g., PCM5102A)
// output a properly centered AC waveform.
// 1-pole IIR: y[n] = x[n] - x[n-1] + alpha * y[n-1]
// alpha = 255/256 ≈ 0.996, cutoff ≈ 28 Hz at 44100 Hz.
static int32_t dc_xL = 0, dc_yL = 0;
static int32_t dc_xR = 0, dc_yR = 0;

static inline int16_t dc_block_channel(int16_t x, int32_t *prev_x, int32_t *prev_y)
{
	int32_t y = (int32_t)x - *prev_x + ((*prev_y * 255) >> 8);
	*prev_x = (int32_t)x;
	*prev_y = y;
	if (y > 32767) y = 32767;
	else if (y < -32768) y = -32768;
	return (int16_t)y;
}

static inline uint32_t __not_in_flash_func(dc_block_sample)(uint32_t sample32)
{
	int16_t l = (int16_t)(sample32 >> 16);
	int16_t r = (int16_t)(sample32 & 0xFFFF);
	l = dc_block_channel(l, &dc_xL, &dc_yL);
	r = dc_block_channel(r, &dc_xR, &dc_yR);
	return ((uint32_t)(uint16_t)l << 16) | (uint16_t)r;
}

/**
 * @brief Enqueues a 32-bit audio sample into the ring buffer for I2S output.
 *
 * This function adds a 32-bit audio sample to the ring buffer. If the buffer is full, it drops the sample.
 * It also checks if the DMA channel is busy and starts a new DMA transfer if there is enough data available.
 */
void __not_in_flash_func(audio_i2s_enqueue_sample)(uint32_t sample32)
{
#if I2S_AUDIO_COMPENSATE_DC_OFFSET
	//if (_driver == PICO_AUDIO_I2S_DRIVER_PCM5000A)
		sample32 = dc_block_sample(sample32);
#endif
	size_t next_write = (write_index + 1) % I2S_AUDIO_RING_SIZE;
	if (next_write != read_index)
	{
		audio_ring[write_index] = sample32;
		write_index = next_write;
		// Disable IRQs to prevent dma_handler from racing with this DMA restart logic.
		uint32_t save = save_and_disable_interrupts();
		if (!dma_channel_is_busy(audio_i2s.dma_chan))
		{
			size_t available = (write_index >= read_index)
								   ? (write_index - read_index)
								   : (I2S_AUDIO_RING_SIZE - read_index + write_index);
			if (available >= DMA_BLOCK_SIZE)
			{
				dma_channel_set_read_addr(audio_i2s.dma_chan, &audio_ring[read_index], false);
				dma_channel_set_trans_count(audio_i2s.dma_chan, DMA_BLOCK_SIZE, true);
			}
		}
		restore_interrupts(save);
	}
}

int audio_i2s_get_freebuffer_size()
{
	return (read_index - write_index - 1) & AUDIO_RING_MASK;
}

void audio_i2s_disable()
{
	printf("Disabling I2S audio and release resources\n");
	if (audio_i2s.dma_chan >= 4)
	{
		irq_set_enabled(DMA_IRQ_1, false);
	}
	else
	{
		irq_set_enabled(DMA_IRQ_0, false);
	}
	dma_channel_abort(audio_i2s.dma_chan);
	free(audio_ring);
	audio_ring = NULL;
}

bool audio_i2s_dacError()
{
	return tlv320_dac_error();
}

void audio_i2s_setVolume(int8_t level)
{
	tlv320_set_volume(level);
}

void audio_i2s_muteInternalSpeaker(bool mute)
{
	tlv320_mute_speaker(mute);
}
