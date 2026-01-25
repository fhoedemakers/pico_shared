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
static uint32_t *audio_ring = NULL;
static volatile size_t write_index = 0;
static volatile size_t read_index = 0;
static int _driver = 0;
static volatile bool speakerIsMuted = false;
bool dacError = false;
// Audio I2S hardware structure that holds the state machine, PIO instance, and DMA channel.
static audio_i2s_hw_t audio_i2s = {
	.sm = -1,		  // State machine index, initialized to -1 (not set)
	.pio = AUDIO_PIO, // PIO instance for audio I2S
	.dma_chan = -1	  // DMA channel for audio transfer, initialized to -1 (not set)
};

// Sample frequency for the audio I2S interface, initialized to the default PICO_AUDIO_I2S_FREQ.
static int samplefreq = PICO_AUDIO_I2S_FREQ;
#define TLV320_ADDR 0x18 // I2C address for the TLV320AIC3204 codec

#define I2C_PORT WIIPAD_I2C // I2C port for the TLV320 codec is tied to WIIPAD_I2C, must be configurable in the future
#define PIN_SDA WII_PIN_SDA // SDA pin tied to WII_PIN_SDA, must be configurable in the future
#define PIN_SCL WII_PIN_SCL // SCL pin tied to WII_PIN_SCL, must be configurable in the future
#define I2C_ADDR 0x18
#define DAC_I2C_ADDR I2C_ADDR
#if 0
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
	int result = i2c_write_blocking(I2C_PORT, I2C_ADDR, &reg, 1, true); // true = no stop
	if (result < 0)
	{
		printf("I2C write failed during read (reg=0x%02X)\n", reg);
		return false;
	}

	// Read data from device
	result = i2c_read_blocking(I2C_PORT, I2C_ADDR, data, len, false); // false = stop
	if (result < 0)
	{
		printf("I2C read failed (reg=0x%02X)\n", reg);
		return false;
	}

	return true;
}
#endif
/// @brief Perform a hardware reset of the TLV320AIC3204
/// This function toggles the reset pin to reset the codec hardware.
static void tlv320_hardware_reset()
{
	// assert that the reset pin is defined
	assert(PICO_AUDIO_I2S_RESET_PIN >= 0 && PICO_AUDIO_I2S_RESET_PIN < NUM_BANK0_GPIOS);
	printf("Performing TLV320 hardware reset...\n");
#if 1
	gpio_put(PICO_AUDIO_I2S_RESET_PIN, 0);
	gpio_set_dir(PICO_AUDIO_I2S_RESET_PIN, GPIO_OUT);
	gpio_set_function(PICO_AUDIO_I2S_RESET_PIN, GPIO_FUNC_SIO);
	sleep_us(20); // Hold low for >10us
	gpio_put(PICO_AUDIO_I2S_RESET_PIN, 1);
	gpio_set_dir(PICO_AUDIO_I2S_RESET_PIN, GPIO_OUT);
	gpio_set_function(PICO_AUDIO_I2S_RESET_PIN, GPIO_FUNC_SIO);
	sleep_ms(10); // Wait for the chip to reset
#else
	gpio_init(PICO_AUDIO_I2S_RESET_PIN);
	gpio_set_dir(PICO_AUDIO_I2S_RESET_PIN, true);
	gpio_put(PICO_AUDIO_I2S_RESET_PIN, true); // allow i2s to come out of reset
#endif
	printf("TLV320 hardware reset complete\n");
}
// Helper functions for reading/writing and modifying registers over I2C
// From https://github.com/jepler/fruitjam-doom/blob/adafruit-fruitjam/src/i_main.c
static void writeRegister(uint8_t reg, uint8_t value)
{
	uint8_t buf[2];
	buf[0] = reg;
	buf[1] = value;
	int res = i2c_write_timeout_us(I2C_PORT, DAC_I2C_ADDR, buf, sizeof(buf), /* nostop */ false, 1000);
	if (res != 2)
	{
		printf("!!!WARNING!!!: i2s_audio i2c_write_timeout failed: res=%d\n", res);
		dacError = true;
	}
#if PICO_AUDIO_I2S_DEBUG
	printf("Write Reg: %d = 0x%x\n", reg, value);
#endif
}

static uint8_t readRegister(uint8_t reg)
{
	uint8_t buf[1];
	buf[0] = reg;
	int res = i2c_write_timeout_us(I2C_PORT, DAC_I2C_ADDR, buf, sizeof(buf), /* nostop */ true, 1000);
	if (res != 1)
	{

		printf("res=%d\n", res);
		printf("!!!WARNING!!!: i2s_audio i2c_write_timeout failed: res=%d\n", res);
		dacError = true;
	}
	res = i2c_read_timeout_us(I2C_PORT, DAC_I2C_ADDR, buf, sizeof(buf), /* nostop */ false, 1000);
	if (res != 1)
	{

		printf("res=%d\n", res);
		printf("!!!WARNING!!!: i2s_audio i2c_read_timeout failed: res=%d\n", res);
		dacError = true;
	}
	uint8_t value = buf[0];
#if PICO_AUDIO_I2S_DEBUG
	printf("Read Reg: %d = 0x%x\n", reg, value);
#endif
	return value;
}

/// @brief Modify a register on the TLV320AIC3204
/// @param reg The register address
/// @param mask tells the function which bits you care about. keeps all other bits as they were.
/// Example: mask = 0x04 means “only bit 2 matters; ignore the rest.”
/// @param value contains the desired state for those masked bits.
/// Example: value = 0x04 means “bit 2 should be 1”
///          value = 0x00 means “bit 2 should be 0”.
static void modifyRegister(uint8_t reg, uint8_t mask, uint8_t value)
{
	uint8_t current = readRegister(reg);
#if PICO_AUDIO_I2S_DEBUG
	printf("Modify Reg: %d = [Before: 0x%x] with mask 0x%x and value 0x%x\n", reg, current, mask, value);
#endif
	uint8_t new_value = (current & ~mask) | (value & mask);
	writeRegister(reg, new_value);
}

static void setPage(uint8_t page)
{
#if PICO_AUDIO_I2S_DEBUG
	printf("Set page %d\n", page);
#endif
	writeRegister(0x00, page);
}

#define MASK_SPK_UNMUTE (1 << 2) // D2

void speakerMute(void)
{
	// Switch to page 1
	setPage(0x01);

	// Set D2 = 0 → mute
	modifyRegister(0x2A, MASK_SPK_UNMUTE, 0x00);
}

void speakerUnmute(void)
{
	// Switch to page 1
	setPage(0x01);

	// Set D2 = 1 → unmute
	modifyRegister(0x2A, MASK_SPK_UNMUTE, MASK_SPK_UNMUTE);
}
static volatile uint32_t last_irq_time;
// Handle the GPIO callback for mute/unmute
static void gpio_callback(uint gpio, uint32_t events)
{
	// No printfs in IRQs
	// printf("GPIO callback on GPIO %d, events: %u\n", gpio, events);
	uint32_t now = to_ms_since_boot(get_absolute_time());
	if (now - last_irq_time < 50)
	{
		return;
	}
	last_irq_time = now;
	if (events & GPIO_IRQ_EDGE_RISE)
	{
		// printf("GPIO %d went HIGH\n", gpio);
		speakerIsMuted = !speakerIsMuted; // Toggle mute state
		if (speakerIsMuted)
		{
			speakerMute();
		}
		else
		{
			speakerUnmute();
		}
	}
	if (events & GPIO_IRQ_EDGE_FALL)
	{
		// printf("GPIO %d went LOW\n", gpio);
	}
}

// Set up the IRQ handler to mute/unmute the speaker on Fruit Jam
void setupHeadphoneDetectionInterrupt(int gpio, bool gpioisbutton)
{
	speakerIsMuted = false; // Reset mute state on headphone detection setup
	printf("Setting up headphone detection on GPIO %d, is_button=%d\n", gpio, gpioisbutton);
	// Initialize the GPIO pin for headphone detection
	if (gpioisbutton)
	{
		gpio_init(gpio);
		gpio_set_dir(gpio, GPIO_IN);
		gpio_pull_down(gpio);
		gpio_set_irq_enabled_with_callback(gpio, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
	}
	else
	{
		printf("TODO: Implement headphone via INT1/GPIO1 on DAC\n");
	}
}
void audio_i2s_setVolume(int8_t level) {
	if (_driver != PICO_AUDIO_I2S_DRIVER_TLV320) {
		return;
	}
	if ( level < -63 || level > 23) {
		printf("Volume level %d out of range (-63 to 23)\n", level);
		return;
	}
	printf("Setting TLV320 volume to level %d\n", level);
	setPage(0);
	modifyRegister(0x40, 0x0C, 0x00);
	writeRegister(0x41, level << 1); // Left DAC Vol  
	writeRegister(0x42, level << 1); // Right DAC Vol 
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
	sleep_ms(10);					// Wait for I2C to stabilize
	// Set up I2C pins
	gpio_set_function(PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_SDA);
    gpio_pull_up(PIN_SCL);
#if 0
    // Old setup, from DataSheet
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
	// Ensure Interface Control 2 defaults: normal polarity, no offset
	write_tlv320((uint8_t[]){0x1C, 0x00}, 2);
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
	// 			Set DAC digital volume to 0 dB (unity)
	write_tlv320((uint8_t[]){0x41, 0x00}, 2);
	write_tlv320((uint8_t[]){0x42, 0x00}, 2);
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
	// Power up HPL/HPR drivers with standard I2S polarity
	write_tlv320((uint8_t[]){0x1F, 0xC2}, 2);
	//  		Power-up Class-D driver
	write_tlv320((uint8_t[]){0x20, 0x86}, 2);
	// 			Enable output analog volumes, set to 0 dB
	write_tlv320((uint8_t[]){0x24, 0x80}, 2);
	write_tlv320((uint8_t[]){0x25, 0x80}, 2);
	write_tlv320((uint8_t[]){0x26, 0x80}, 2);
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

#endif
	// Better setup, from https://github.com/jepler/fruitjam-doom/blob/adafruit-fruitjam/src/i_main.c
	// Reset codec
	writeRegister(0x01, 0x01);
	sleep_ms(10);
#if 0
	// Interface Control
	modifyRegister(0x1B, 0xC0, 0x00);
	modifyRegister(0x1B, 0x30, 0x00);
#else
	// Set Interface Control 1 (0x1B)
	// Bits 7-6 = 00 → I²S mode
	// Bits 5-4 = 00 → 16-bit word length (matches PCM5000A path) (was 11 (0x30) for 32-bit)
	// Bits 3-0 = 0000 → no offset
	// PIO sends 16 bits samples in I2S format, so set accordingly
	writeRegister(0x1B, 0x00);

	// Optional: Interface Control 2 (0x1C)
	// Make sure data is left-aligned with I²S expectations
	// 0x00 = defaults (no invert, normal polarity)
	writeRegister(0x1C, 0x00);
#endif
	// Clock MUX and PLL settings
	modifyRegister(0x04, 0x03, 0x03);
	modifyRegister(0x04, 0x0C, 0x04);

	writeRegister(0x06, 0x20); // PLL J
	writeRegister(0x08, 0x00); // PLL D LSB
	writeRegister(0x07, 0x00); // PLL D MSB

	modifyRegister(0x05, 0x0F, 0x02); // PLL P/R
	modifyRegister(0x05, 0x70, 0x10);

	// DAC/ADC Config
	modifyRegister(0x0B, 0x7F, 0x08); // NDAC
	modifyRegister(0x0B, 0x80, 0x80);

	modifyRegister(0x0C, 0x7F, 0x02); // MDAC
	modifyRegister(0x0C, 0x80, 0x80);

	modifyRegister(0x12, 0x7F, 0x08); // NADC
	modifyRegister(0x12, 0x80, 0x80);

	modifyRegister(0x13, 0x7F, 0x02); // MADC
	modifyRegister(0x13, 0x80, 0x80);

	// PLL Power Up
	modifyRegister(0x05, 0x80, 0x80);

	// Headset and GPIO Config
	setPage(1);
	modifyRegister(0x2e, 0xFF, 0x0b);
	setPage(0);

	modifyRegister(0x43, 0x80, 0x80); // Headset Detect
	modifyRegister(0x30, 0x80, 0x80); // INT1 Control
	modifyRegister(0x33, 0x3C, 0x14); // GPIO1

	// DAC Setup
	modifyRegister(0x3F, 0xC0, 0xC0);

	// DAC Routing
	setPage(1);
	modifyRegister(0x23, 0xC0, 0x40);
	modifyRegister(0x23, 0x0C, 0x04);

	// DAC Volume Control
	setPage(0);
	modifyRegister(0x40, 0x0C, 0x00);
	writeRegister(0x41, 0x20); // Left DAC Vol  , was 0x28
	writeRegister(0x42, 0x20); // Right DAC Vol , was 0x28

	// ADC Setup
	modifyRegister(0x51, 0x80, 0x80);
	modifyRegister(0x52, 0x80, 0x00);
	writeRegister(0x53, 0x68); // ADC Volume

	// Headphone and Speaker Setup
	setPage(1);
	modifyRegister(0x1F, 0xC0, 0xC0); // HP Driver
	modifyRegister(0x28, 0x04, 0x04); // HP Left Gain
	modifyRegister(0x29, 0x04, 0x04); // HP Right Gain
	writeRegister(0x24, 0x0A);		  // Left Analog HP
	writeRegister(0x25, 0x0A);		  // Right Analog HP
	modifyRegister(0x28, 0x78, 0x40); // HP Left Gain
	modifyRegister(0x29, 0x78, 0x40); // HP Right Gain

	// Speaker Amp
	modifyRegister(0x20, 0x80, 0x80);
	modifyRegister(0x2A, 0x04, 0x04);
	modifyRegister(0x2A, 0x18, 0x08);
	writeRegister(0x26, 0x0A);

	// Return to page 0

	setPage(0);

#if PICO_AUDIO_I2S_INTERRUPT_PIN != -1
	if (dacError == false)
	{
		printf("setup headphone detection interrupt.\n");
		setupHeadphoneDetectionInterrupt(PICO_AUDIO_I2S_INTERRUPT_PIN, PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON);
	}
#endif
#if 0
	uint8_t v1b = readRegister(0x1B);
	uint8_t v1c = readRegister(0x1C);
	printf("IFACE1=0x%02X (expect 0x30), IFACE2=0x%02X (expect 0x00)\n", v1b, v1c);
#endif
	printf("TLV320AIC3204 Initialization complete!\n");

	// Read all registers for verification
#if 0
	printf("Reading all registers for verification:\n");

	setPage(0);
	readRegister(0x00); // AIC31XX_PAGECTL
	readRegister(0x01); // AIC31XX_RESET
	readRegister(0x03); // AIC31XX_OT_FLAG
	readRegister(0x04); // AIC31XX_CLKMUX
	readRegister(0x05); // AIC31XX_PLLPR
	readRegister(0x06); // AIC31XX_PLLJ
	readRegister(0x07); // AIC31XX_PLLDMSB
	readRegister(0x08); // AIC31XX_PLLDLSB
	readRegister(0x0B); // AIC31XX_NDAC
	readRegister(0x0C); // AIC31XX_MDAC
	readRegister(0x0D); // AIC31XX_DOSRMSB
	readRegister(0x0E); // AIC31XX_DOSRLSB
	readRegister(0x10); // AIC31XX_MINI_DSP_INPOL
	readRegister(0x12); // AIC31XX_NADC
	readRegister(0x13); // AIC31XX_MADC
	readRegister(0x14); // AIC31XX_AOSR
	readRegister(0x19); // AIC31XX_CLKOUTMUX
	readRegister(0x1A); // AIC31XX_CLKOUTMVAL
	readRegister(0x1B); // AIC31XX_IFACE1
	readRegister(0x1C); // AIC31XX_DATA_OFFSET
	readRegister(0x1D); // AIC31XX_IFACE2
	readRegister(0x1E); // AIC31XX_BCLKN
	readRegister(0x1F); // AIC31XX_IFACESEC1
	readRegister(0x20); // AIC31XX_IFACESEC2
	readRegister(0x21); // AIC31XX_IFACESEC3
	readRegister(0x22); // AIC31XX_I2C
	readRegister(0x24); // AIC31XX_ADCFLAG
	readRegister(0x25); // AIC31XX_DACFLAG1
	readRegister(0x26); // AIC31XX_DACFLAG2
	readRegister(0x27); // AIC31XX_OFFLAG
	readRegister(0x2C); // AIC31XX_INTRDACFLAG
	readRegister(0x2D); // AIC31XX_INTRADCFLAG
	readRegister(0x2E); // AIC31XX_INTRDACFLAG2
	readRegister(0x2F); // AIC31XX_INTRADCFLAG2
	readRegister(0x30); // AIC31XX_INT1CTRL
	readRegister(0x31); // AIC31XX_INT2CTRL
	readRegister(0x33); // AIC31XX_GPIO1
	readRegister(0x3C); // AIC31XX_DACPRB
	readRegister(0x3D); // AIC31XX_ADCPRB
	readRegister(0x3F); // AIC31XX_DACSETUP
	readRegister(0x40); // AIC31XX_DACMUTE
	readRegister(0x41); // AIC31XX_LDACVOL
	readRegister(0x42); // AIC31XX_RDACVOL
	readRegister(0x43); // AIC31XX_HSDETECT
	readRegister(0x51); // AIC31XX_ADCSETUP
	readRegister(0x52); // AIC31XX_ADCFGA
	readRegister(0x53); // AIC31XX_ADCVOL

	setPage(1);
	readRegister(0x1F); // AIC31XX_HPDRIVER
	readRegister(0x20); // AIC31XX_SPKAMP
	readRegister(0x21); // AIC31XX_HPPOP
	readRegister(0x22); // AIC31XX_SPPGARAMP
	readRegister(0x23); // AIC31XX_DACMIXERROUTE
	readRegister(0x24); // AIC31XX_LANALOGHPL
	readRegister(0x25); // AIC31XX_RANALOGHPR
	readRegister(0x26); // AIC31XX_LANALOGSPL
	readRegister(0x27); // AIC31XX_RANALOGSPR
	readRegister(0x28); // AIC31XX_HPLGAIN
	readRegister(0x29); // AIC31XX_HPRGAIN
	readRegister(0x2A); // AIC31XX_SPLGAIN
	readRegister(0x2B); // AIC31XX_SPRGAIN
	readRegister(0x2C); // AIC31XX_HPCONTROL
	readRegister(0x2E); // AIC31XX_MICBIAS
	readRegister(0x2F); // AIC31XX_MICPGA
	readRegister(0x30); // AIC31XX_MICPGAPI
	readRegister(0x31); // AIC31XX_MICPGAMI
	readRegister(0x32); // AIC31XX_MICPGACM

	setPage(3);
	readRegister(0x10); // AIC31XX_TIMERDIVIDER
	printf("All I2C writes complete.\n");
#endif
	sleep_ms(100);
}

// Headphone status check, not working for now
#if 0
uint8_t last_status = -1;
void tlv320_poll_headphone_status()
{

	setPage(0);
	// read_tlv320(0x2E, &data, 1); // Read the headphone detection register
	uint8_t data = readRegister(0x43); // Read the headphone detection register
	// printf("Headphone status: %02X\n", data);
	data &= 0b00110000; // Mask to get headphone status bits
	data >>= 4;			// Shift to get the status bits in the lower bits
	// printf("Headphone after shift: %02X\n", data);
	//  read_tlv320(0x2C, &data, 1); // Read the headphone detection register
	data = readRegister(0x2c);
	// printf("Headphone status: %02X\n", data);
	data &= 0b00110000; // Mask to get headphone status bits
	data >>= 4;			// Shift to get the status bits in the lower bits
						// printf("Headphone after shift: %02X\n", data);



	if (last_status != data)
	{
		last_status = data;
		switch (data)
		{
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

	return;
}

void audio_i2s_poll_headphone_status()
{
	if (_driver == PICO_AUDIO_I2S_DRIVER_TLV320)
	{
		 tlv320_poll_headphone_status();
	}
}
#endif

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
	// If write_index is ahead of read_index, we have data to send
	size_t available = (write_index >= read_index)
						   ? (write_index - read_index)
						   : (I2S_AUDIO_RING_SIZE - read_index + write_index);
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
	dacError = false;
	if (_driver == PICO_AUDIO_I2S_DRIVER_NONE)
	{
		printf("No I2S driver selected, skipping audio setup.\n");
		return NULL; // No I2S driver selected, return NULL
	}
	if (_driver == PICO_AUDIO_I2S_DRIVER_TLV320)
	{

		int retries = 0;
		printf("Init TLV320 DAC, max 5 retries...\n");
		do
		{
			dacError = false;
			tlv320_hardware_reset(); // Reset the TLV320 codec hardware
			tlv320_init();			 // Initialize the TLV320 codec with default settings
			if (!dacError)
			{
				break;
			}
			printf("TLV320 init failed, retrying (%d/5)...\n", retries + 1);
			sleep_ms(500);
		} while (dacError && ++retries < 5);
		if (dacError)
		{
			printf("TLV320 initialization failed after 5 attempts.\n");
		}
	}
	audio_i2s.dma_chan = dmachan;
	if (freqHZ > 0)
	{
		samplefreq = freqHZ;
	}
	printf("Allocating %d bytes for audio ring buffer\n", I2S_AUDIO_RING_SIZE * sizeof(uint32_t));
	audio_ring = malloc(I2S_AUDIO_RING_SIZE * sizeof(uint32_t));   // Allocate memory for the audio ring buffer
	memset(audio_ring, 0, I2S_AUDIO_RING_SIZE * sizeof(uint32_t)); // Initialize the audio ring buffer to zero
	write_index = 0; //DMA_BLOCK_SIZE;							   // Start writing after the first block
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

	size_t next_write = (write_index + 1) % I2S_AUDIO_RING_SIZE;
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
								   : (I2S_AUDIO_RING_SIZE - read_index + write_index);
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
	return dacError;
}
void audio_i2s_muteInternalSpeaker(bool mute)
{
#if  USE_I2S_AUDIO == PICO_AUDIO_I2S_DRIVER_TLV320
	speakerIsMuted = mute;
	if (speakerIsMuted) {
		speakerMute();
	} else {
		speakerUnmute();
	}
#endif
}