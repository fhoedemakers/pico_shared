#include <math.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "hardware/spi.h"
#include "stdio.h"

// SPI configurations
#define PIN_MISO 8
#define PIN_CS 9
#define PIN_SCK 10
#define PIN_MOSI 11
#define SPI_PORT spi1
#define LDAC_GPIO 7
// Low-level alarm infrastructure we'll be using
#define ALARM_NUM 0
#define ALARM_IRQ timer_hardware_alarm_get_irq_num(timer_hw, ALARM_NUM)

// DDS parameters
#define Fs 44100       // Sampling frequency
#define DELAY 26       // 1/FS in microseconds is 1/44100 * 1e6 = 22.6757 but gets rounded to 26 for 44.1kHz for improved sound.

// SPI data
uint16_t DAC_data; // output value

// DAC parameters
//  A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000
// B-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000

// ringbuffer functions
#define MYRINGBUFFER_SIZE 1024  // Must be power of 2
#define MYRINGBUFFER_MASK (MYRINGBUFFER_SIZE - 1)
static volatile uint16_t buffer[MYRINGBUFFER_SIZE];
static volatile uint32_t head = 0;
static volatile uint32_t tail = 0;

/// @brief Push a sample into the ring buffer
/// @param sample The sample to push
/// @return true if successful, false if the buffer is full
bool __not_in_flash_func(mcp4822_push_sample)(uint16_t sample) {
    uint32_t next = (head + 1) & MYRINGBUFFER_MASK;
    if (next == tail) {
        printf("Buffer full\n");
        return false;  // Buffer full
    }
    buffer[head] = sample;
    head = next;
    return true;
}

/// @brief Get a sample from the ring buffer
/// @param sample Pointer to the variable to store the sample
/// @return true if successful, false if the buffer is empty
bool __not_in_flash_func(mcp4822_get_sample)(uint16_t *sample) {
    if (head == tail) return false;  // Buffer empty
    *sample = buffer[tail];
    tail = (tail + 1) & MYRINGBUFFER_MASK;
    return true;
}

/// @brief Get the amount of free space in the ring buffer
/// @param mcp4822_get_free_buffer_space 
/// @return 
uint32_t __not_in_flash_func(mcp4822_get_free_buffer_space)(void) {
    //  uint32_t irq = save_and_disable_interrupts();
    // __mem_fence_acquire();
    uint32_t h = head;
    uint32_t t = tail;
    //  __mem_fence_acquire();  
    // restore_interrupts(irq);
    return (t - h - 1) & MYRINGBUFFER_MASK;
}

/// @brief Get the amount of used space in the ring buffer
/// @param  
/// @return 
uint32_t mcp4822_get_used_buffer_space(void) {
    uint32_t h = head;
    uint32_t t = tail;
    return (h - t) & MYRINGBUFFER_MASK;
}



/// @brief Alarm IRQ handler, sends the sample to the DAC
/// @param  
/// @return 
static void __not_in_flash_func(alarm_irq)(void)
{

    // Clear the alarm irq
    hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);

    // Reset the alarm register
    timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + DELAY;
    uint16_t sample =0;
    mcp4822_get_sample(&sample);
    //int dac_value = (sample * 4095) / 255;
    DAC_data = (DAC_config_chan_A | (sample & 0xffff));
    spi_write16_blocking(SPI_PORT, &DAC_data, 1);
    DAC_data = (DAC_config_chan_B | (sample & 0xffff));
    spi_write16_blocking(SPI_PORT, &DAC_data, 1);
}

/// @brief Initialize the MCP4822 DAC
/// @param  
/// @return
void mcp4822_init()
{
    spi_init(SPI_PORT, 20000000);
    // Format (channel, data bits per transfer, polarity, phase, order)

    spi_set_format(SPI_PORT, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_LSB_FIRST);

    gpio_init(LDAC_GPIO);
    gpio_set_dir(LDAC_GPIO, GPIO_OUT);
    gpio_put(LDAC_GPIO, 0);

    // Map SPI signals to GPIO ports
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS, GPIO_FUNC_SPI);

    // Enable the interrupt for the alarm (we're using Alarm 0)
    hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM);
    // Associate an interrupt handler with the ALARM_IRQ
    irq_set_exclusive_handler(ALARM_IRQ, alarm_irq);
    // Enable the alarm interrupt
    irq_set_enabled(ALARM_IRQ, true);
    // Write the lower 32 bits of the target time to the alarm register, arming it.
    timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + DELAY;
}


