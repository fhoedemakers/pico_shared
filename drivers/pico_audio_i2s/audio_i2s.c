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
#include "hardware/i2c.h"

#define AUDIO_PIO __CONCAT(pio, PICO_AUDIO_I2S_PIO)
#define GPIO_FUNC_PIOx __CONCAT(GPIO_FUNC_PIO, PICO_AUDIO_I2S_PIO)

// Ring buffer for audio samples. It holds AUDIO_RING_SIZE samples, which are 32-bit integers.
uint32_t *audio_ring = NULL;
volatile size_t write_index = 0;
volatile size_t read_index = 0;
static int _driver = 0;
// Audio I2S hardware structure that holds the state machine, PIO instance, and DMA channel.
static audio_i2s_hw_t audio_i2s = {
	.sm = -1,		  // State machine index, initialized to -1 (not set)
	.pio = AUDIO_PIO, // PIO instance for audio I2S
	.dma_chan = -1	  // DMA channel for audio transfer, initialized to -1 (not set)
};

// Sample frequency for the audio I2S interface, initialized to the default PICO_AUDIO_I2S_FREQ.
static int samplefreq = PICO_AUDIO_I2S_FREQ;
#define TLV320_ADDR 0x18 // I2C address for the TLV320AIC3204 codec

#define I2C_PORT i2c0 // hardcoded I2C port for the TLV320 codec, must be configurable in the future
#define I2C_ADDR 0x18

/// @brief Write data to the TLV320AIC3204 over I2C
/// @param data Pointer to the data buffer
/// @param len Length of the data buffer
static void write_tlv320(uint8_t *data, size_t len)
{
	int ret = i2c_write_blocking(I2C_PORT, I2C_ADDR, data, len, false);
	if (ret < 0)
	{
		printf("I2C write failed: error %d\n", ret);
	}
	else if (ret != (int)len)
	{
		printf("I2C write incomplete: wrote %d of %d bytes\n", ret, len);
	}
#if 0
	printf("I2C write %d bytes: ", len);
	for (size_t i = 0; i < len; i++)
	{
		printf("%02x ", data[i]);
	}
	printf("\n");
#endif
}

bool read_tlv320(uint8_t reg, uint8_t *data, size_t len)
{
	// Write register address first
	int result = i2c_write_blocking(i2c0, I2C_ADDR, &reg, 1, true); // true = no stop
	if (result < 0)
	{
		printf("I2C write failed during read (reg=0x%02X)\n", reg);
		return false;
	}

	// Read data from device
	result = i2c_read_blocking(i2c0, I2C_ADDR, data, len, false); // false = stop
	if (result < 0)
	{
		printf("I2C read failed (reg=0x%02X)\n", reg);
		return false;
	}

	return true;
}

/// @brief Perform a hardware reset of the TLV320AIC3204
/// This function toggles the reset pin to reset the codec hardware.
static void tlv320_hardware_reset()
{
	// assert that the reset pin is defined
	assert(PICO_AUDIO_I2S_RESET_PIN >= 0 && PICO_AUDIO_I2S_RESET_PIN < NUM_BANK0_GPIOS);
	printf("Performing TLV320 hardware reset...\n");
	gpio_put(PICO_AUDIO_I2S_RESET_PIN, 0);
	gpio_set_dir(PICO_AUDIO_I2S_RESET_PIN, GPIO_OUT);
	gpio_set_function(PICO_AUDIO_I2S_RESET_PIN, GPIO_FUNC_SIO);
	sleep_us(20); // Hold low for >10us
	gpio_put(PICO_AUDIO_I2S_RESET_PIN, 1);
	gpio_set_dir(PICO_AUDIO_I2S_RESET_PIN, GPIO_OUT);
	gpio_set_function(PICO_AUDIO_I2S_RESET_PIN, GPIO_FUNC_SIO);
	sleep_ms(10); // Wait for the chip to reset
	printf("TLV320 hardware reset complete\n");
}

/// @brief Initialize the TLV320AIC3204 codec
/// This function sets up the codec with default settings for audio playback.
/// From tlv320dac3100 datasheet, section 6.3.10.15
/// "Typical EVM I2C register control script"
/// https://www.ti.com/lit/ds/symlink/tlv320dac3100.pdf?ts=1754043773385
/// A typical EVM I2C register control script follows to show how to set up the TLV320DAC3100 in playback
/// mode with fS = 44.1 kHz and MCLK = 11.2896 MHz.
static void tlv320_init()
{
	// Initialize the DAC over I2C
	printf("Initializing TLV320AIC3204 audio DAC...\n");
	i2c_init(I2C_PORT, 400 * 1000); // Initialize I2C at 400kHz
	// 1. Define starting point:
	// 		(a) Power up applicable external hardware power supplies
	//     	(b) Set register to Page 0
	// ### SET REGISTER PAGE 0 ###
	write_tlv320((uint8_t[]){0x00, 0x00}, 2);
	// 		(c) Initiate SW reset (PLL is powered off as part of reset)
	write_tlv320((uint8_t[]){0x01, 0x01}, 2);
	// 2. Program clock settings
	// 		(a) Program PLL clock dividers P, J, D, R (if PLL is used)
	//     		PLL_clkin = MCLK,codec_clkin = PLL_CLK
	write_tlv320((uint8_t[]){0x04, 0x03}, 2);
	//     		J = 8
	write_tlv320((uint8_t[]){0x06, 0x08}, 2);
	//    		D = 0000, D(13:8) = 0, D(7:0) = 0
	write_tlv320((uint8_t[]){0x07, 0x00, 0x00}, 3);
	// 		(b) Power up PLL (if PLL is used)
	//     		PLL Power up, P = 1, R = 1
	write_tlv320((uint8_t[]){0x05, 0x91}, 2);
	// 		(c) Program and power up NDAC
	//     		NDAC is powered up and set to 8
	write_tlv320((uint8_t[]){0x0B, 0x88}, 2);
	// 		(d) Program and power up MDAC
	//     		MDAC is powered up and set to 2
	write_tlv320((uint8_t[]){0x0C, 0x82}, 2);
	// 		(e) Program OSR value
	//	   		DOSR = 128, DOSR(9:8) = 0, DOSR(7:0) = 128
	write_tlv320((uint8_t[]){0x0D, 0x00, 0x80}, 3);
	// 		(f) Program I2S word length if required (16, 20, 24, 32 bits)
	//     		and master mode (BCLK and WCLK are outputs)
	//      	mode is i2s, wordlength is 16, slave mode
	write_tlv320((uint8_t[]){0x1B, 0x00}, 2);
	// 		(g) Program the processing block to be used
	//     		Select Processing Block PRB_P11
	write_tlv320((uint8_t[]){0x3C, 0x0B}, 2);
	// ### SET REGISTER PAGE 8 ###
	write_tlv320((uint8_t[]){0x00, 0x08}, 2);
	write_tlv320((uint8_t[]){0x01, 0x04}, 2);
	// ### SET REGISTER PAGE 0 ###
	write_tlv320((uint8_t[]){0x00, 0x00}, 2);
	// 		(h) Miscellaneous page 0 controls
	// 			DAC => volume control thru pin disable
	write_tlv320((uint8_t[]){0x74, 0x00}, 2);
	// 3. Program analog blocks
	// ### SET REGISTER PAGE 1 ###
	//  	(a) Set register to Page 1
	write_tlv320((uint8_t[]){0x00, 0x01}, 2);
	// 		(b) Program common-mode voltage (defalut = 1.35 V)
	write_tlv320((uint8_t[]){0x1F, 0x04}, 2);
	//      (c) Program headphone-specific depop settings (in case headphone driver is used)
	//          De-pop, Power on = 800 ms, Step time = 4 ms
	write_tlv320((uint8_t[]){0x21, 0x4E}, 2);
	//      (d) Program routing of DAC output to the output amplifier (headphone/lineout or speaker)
	//          LDAC routed to HPL out, RDAC routed to HPR out
	write_tlv320((uint8_t[]){0x23, 0x44}, 2);
	// 	    (e) Unmute and set gain of output driver
	//          Unmute HPL, set gain = 0 db
	write_tlv320((uint8_t[]){0x28, 0x06}, 2);
	//          Unmute HPR, set gain = 0 dB
	write_tlv320((uint8_t[]){0x29, 0x06}, 2);
	//          Unmute Class-D, set gain = 18 dB
	write_tlv320((uint8_t[]){0x2A, 0x1C}, 2);
	// 		(f) Power up output drivers
	//			HPL and HPR powered up
	// NOTE write_tlv320((uint8_t[]){0x1F, 0xF2}, 2); below causes a pop sound on startup. How to solve?
	printf("POP!\n");
	write_tlv320((uint8_t[]){0x1F, 0xF2}, 2); // Changed from 0xC2 to 0xF2
	//  		Power-up Class-D driver
	write_tlv320((uint8_t[]){0x20, 0x86}, 2);
	// 			Enable HPL output analog volume, set = -9dB
	write_tlv320((uint8_t[]){0x24, 0x92}, 2);
	// 			Enable HPR output analog volume, set = -9dB
	write_tlv320((uint8_t[]){0x25, 0x92}, 2);
	// 			Enable Class-D output analog volume, set = -9dB
	write_tlv320((uint8_t[]){0x26, 0x92}, 2);
	// 4. Apply waiting time determined by the de-pop settings and the soft-stepping settings
	//    of the driver gain or poll page 1 / register 63
	sleep_ms(800); // Wait for 800 ms as per the de-pop settings
	//  5. Power up DAC
	//  ### SET REGISTER PAGE 0 ###
	//  	(a) Set register to Page 0
	write_tlv320((uint8_t[]){0x00, 0x00}, 2);
	// 		(b) Power up DAC channels and set digital gain
	// 			Powerup DAC left and right channels (soft step enabled)
	write_tlv320((uint8_t[]){0x3F, 0xD4}, 2);
	//			DAC Left gain = 0 dB	(was -22 dB)
	write_tlv320((uint8_t[]){0x41, 0x00}, 2); // Changed 0xD4 to 0x00
	//			DAC Right gain = 0 dB	(was -22 dB)
	write_tlv320((uint8_t[]){0x42, 0x00}, 2); // Changed 0xD4 to 0x00

	// Additional settings
	write_tlv320((uint8_t[]){0x16, 0x10}, 2); // Enable ramping
	// 		(c) Unmute digital volume control
	// 			Unmute DAC left and right channels
	write_tlv320((uint8_t[]){0x40, 0x00}, 2);


	// Setup Headphone detection
	// Enable headphone detection 1 (enabled) 00 (Detection values) 000 (16 ms debounce headset) 00 (0 ms Debounce button) 
	uint8_t data;
	data = read_tlv320(0x43, &data, 1);
	//printf("Headphone detection register before: %02X\n", data);
	data &= 0b01100000;
	//printf("Headphone detection register after: %02X\n", data);
	data |= 0b10010100;
	//printf("Setting headphone detection register to: %02X\n", data);
	write_tlv320((uint8_t[]){0x43, data}, 2);
	//printf("All I2C writes complete.\n");

	sleep_ms(100);
}

uint8_t last_status = -1;

void tlv320_poll_headphone_status() {
	// goto page 0
	write_tlv320((uint8_t[]){0x00, 0x00}, 2);
	uint8_t data;
	
	read_tlv320(0x2E, &data, 1); // Read the headphone detection register
	//printf("Headphone status: %02X\n", data);
	data &= 0b00110000; // Mask to get headphone status bits
	data >>= 4; // Shift to get the status bits in the lower bits
	//printf("Headphone after shift: %02X\n", data);
	read_tlv320(0x2C, &data, 1); // Read the headphone detection register
	//printf("Headphone status: %02X\n", data);
	data &= 0b00110000; // Mask to get headphone status bits
	data >>= 4; // Shift to get the status bits in the lower bits
	//printf("Headphone after shift: %02X\n", data);
	

#if 1

	if (last_status != data) {
		last_status = data;
		switch (data) {
			case TLV320_HEADPHONE_NOTCONNECTED:
				printf("Headphone not connected");
				break;
			case TLV320_HEADPHONE_CONNECTED:
				printf("Headphone connected");
			case TLV320_HEADPHONE_CONNECTED_WITH_MIC:
				printf(" with microphone");
				break;
			default:
				printf("Unknown headphone status: %02X", data);
				break;
		}
		last_status = data;
		printf("\n");
	}
		#endif
	return;
}

void audio_i2s_poll_headphone_status()
{
	if (_driver == PICO_AUDIO_I2S_DRIVER_TLV320)
	{
		tlv320_poll_headphone_status();
	}
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
	read_index = (read_index + DMA_BLOCK_SIZE) % AUDIO_RING_SIZE;
	// Check if we have enough data to continue DMA transfer
	// If write_index is ahead of read_index, we have data to send
	size_t available = (write_index >= read_index)
						   ? (write_index - read_index)
						   : (AUDIO_RING_SIZE - read_index + write_index);
	// printf("DMA handler: read_index=%zu, write_index=%zu, available=%zu\n", read_index, write_index, available);
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
 * @param driver The I2S driver to use (e.g., PICO_AUDIO_I2S_DRIVER_TLV320).
 * @param resetPin The GPIO pin used to reset the TLV320 codec hardware (if	 applicable).
 * @param freqHZ The sample frequency in Hertz. If set to 0, the default PICO_AUDIO_I2S_FREQ is used.
 * @param dmachan The DMA channel to use for audio transfer. If set to -1, a free DMA channel will be claimed.
 */
audio_i2s_hw_t *audio_i2s_setup(int driver, int freqHZ, int dmachan)
{
	_driver = driver; // Store the selected driver globally
	if (_driver == PICO_AUDIO_I2S_DRIVER_NONE)
	{
		printf("No I2S driver selected, skipping audio setup.\n");
		return NULL; // No I2S driver selected, return NULL
	}
	if (_driver == PICO_AUDIO_I2S_DRIVER_TLV320)
	{
		tlv320_hardware_reset(); // Reset the TLV320 codec hardware
		tlv320_init();			 // Initialize the TLV320 codec with default settings
	}
	audio_i2s.dma_chan = dmachan;
	if (freqHZ > 0)
	{
		samplefreq = freqHZ;
	}
	audio_ring = malloc(AUDIO_RING_SIZE * sizeof(uint32_t));   // Allocate memory for the audio ring buffer
	memset(audio_ring, 0, AUDIO_RING_SIZE * sizeof(uint32_t)); // Initialize the audio ring buffer to zero
	write_index = DMA_BLOCK_SIZE;							   // Start writing after the first block
	read_index = 0;											   // Reset read index
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
		//
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
	tlv320_poll_headphone_status();
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
