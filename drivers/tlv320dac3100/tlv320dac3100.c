/*
 *  tlv320dac3100.c
 *
 *  TLV320DAC3100 codec control over I2C. Extracted verbatim (with light
 *  renaming for module boundary) from the legacy custom pico_audio_i2s driver
 *  so the same code is reused by the pico-extras based driver.
 *
 *  Register-script reference:
 *    https://github.com/jepler/fruitjam-doom/blob/adafruit-fruitjam/src/i_main.c
 *  TLV320DAC3100 datasheet:
 *    https://www.ti.com/lit/ds/symlink/tlv320dac3100.pdf
 */

#include "tlv320dac3100.h"

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"

#include <stdio.h>

/* The I2C bus and SDA/SCL pins are shared with the Wii controller pads.
 * WIIPAD_I2C / WII_PIN_SDA / WII_PIN_SCL are project-wide macros supplied via
 * compile definitions (see BoardConfigs.cmake / wiipad.h). */
#define I2C_PORT       WIIPAD_I2C
#define PIN_SDA        WII_PIN_SDA
#define PIN_SCL        WII_PIN_SCL
#define DAC_I2C_ADDR   0x18

static volatile bool s_dac_error = false;
static volatile bool s_active = false;
static volatile bool s_speaker_muted = false;
static volatile bool s_hp_irq_pending = false;
static volatile uint32_t s_last_irq_time = 0;

/* ----- low-level register access -------------------------------------- */

static void write_register(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    int res = i2c_write_timeout_us(I2C_PORT, DAC_I2C_ADDR, buf, sizeof(buf),
                                   /* nostop */ false, 1000);
    if (res != 2) {
        printf("!!!WARNING!!!: tlv320 i2c_write_timeout failed: res=%d\n", res);
        s_dac_error = true;
    }
#if PICO_AUDIO_I2S_DEBUG
    printf("Write Reg: %d = 0x%x\n", reg, value);
#endif
}

static uint8_t read_register(uint8_t reg)
{
    uint8_t buf[1] = { reg };
    int res = i2c_write_timeout_us(I2C_PORT, DAC_I2C_ADDR, buf, sizeof(buf),
                                   /* nostop */ true, 1000);
    if (res != 1) {
        printf("!!!WARNING!!!: tlv320 i2c_write_timeout failed: res=%d\n", res);
        s_dac_error = true;
    }
    res = i2c_read_timeout_us(I2C_PORT, DAC_I2C_ADDR, buf, sizeof(buf),
                              /* nostop */ false, 1000);
    if (res != 1) {
        printf("!!!WARNING!!!: tlv320 i2c_read_timeout failed: res=%d\n", res);
        s_dac_error = true;
    }
#if PICO_AUDIO_I2S_DEBUG
    printf("Read Reg: %d = 0x%x\n", reg, buf[0]);
#endif
    return buf[0];
}

static void modify_register(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t current = read_register(reg);
    uint8_t new_value = (current & ~mask) | (value & mask);
    write_register(reg, new_value);
}

static void set_page(uint8_t page)
{
    write_register(0x00, page);
}

/* ----- hardware reset ------------------------------------------------- */

static void tlv320_hardware_reset(void)
{
    assert(PICO_AUDIO_I2S_RESET_PIN >= 0 && PICO_AUDIO_I2S_RESET_PIN < NUM_BANK0_GPIOS);
    printf("Performing TLV320 hardware reset...\n");
    gpio_put(PICO_AUDIO_I2S_RESET_PIN, 0);
    gpio_set_dir(PICO_AUDIO_I2S_RESET_PIN, GPIO_OUT);
    gpio_set_function(PICO_AUDIO_I2S_RESET_PIN, GPIO_FUNC_SIO);
    sleep_us(20);
    gpio_put(PICO_AUDIO_I2S_RESET_PIN, 1);
    gpio_set_dir(PICO_AUDIO_I2S_RESET_PIN, GPIO_OUT);
    gpio_set_function(PICO_AUDIO_I2S_RESET_PIN, GPIO_FUNC_SIO);
    sleep_ms(10);
    printf("TLV320 hardware reset complete\n");
}

/* ----- speaker mute helpers ------------------------------------------- */

#define MASK_SPK_UNMUTE     (1 << 2)

/* HP analog volume (Page 1 / Reg 0x24-0x25): each LSB = -0.5 dB. The speaker
 * is louder than the HP at the same dB; HP gets ~20 dB extra attenuation. */
#define HP_ANALOG_VOL_SPEAKER 0x0A  /* -5 dB */
#define HP_ANALOG_VOL_HP      0x34  /* -25 dB */

static void speaker_mute_internal(void)
{
    set_page(0x01);
    modify_register(0x2A, MASK_SPK_UNMUTE, 0x00);
}

static void speaker_unmute_internal(void)
{
    set_page(0x01);
    modify_register(0x2A, MASK_SPK_UNMUTE, MASK_SPK_UNMUTE);
}

/* ----- headphone detection IRQ callbacks ------------------------------ */

static void gpio_callback(uint gpio, uint32_t events)
{
    (void)gpio;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - s_last_irq_time < 50) return;
    s_last_irq_time = now;
    if (events & GPIO_IRQ_EDGE_RISE) {
        s_speaker_muted = !s_speaker_muted;
        if (s_speaker_muted) speaker_mute_internal();
        else                 speaker_unmute_internal();
    }
}

static void hp_int1_callback(uint gpio, uint32_t events)
{
    (void)gpio;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - s_last_irq_time < 200) return;
    s_last_irq_time = now;
    if (events & GPIO_IRQ_EDGE_RISE) {
        s_hp_irq_pending = true;
    }
}

static enum headphone_toggle_t handle_headphone_event(void)
{
    set_page(0);
    uint8_t sticky = read_register(0x2C);     /* clear sticky flag */
    uint8_t flags  = read_register(0x2E);
    bool hp_inserted = (flags & 0x10) != 0;

    uint8_t hsdet  = read_register(0x43);
    uint8_t hstype = (hsdet >> 5) & 0x03;

    printf("Headphone event: %s (type=%d, sticky=0x%02X, flags=0x%02X)\n",
           hp_inserted ? "inserted" : "removed", hstype, sticky, flags);

    if (hp_inserted) {
        speaker_mute_internal();
        write_register(0x24, HP_ANALOG_VOL_HP);
        write_register(0x25, HP_ANALOG_VOL_HP);
    } else {
        speaker_unmute_internal();
        write_register(0x24, HP_ANALOG_VOL_SPEAKER);
        write_register(0x25, HP_ANALOG_VOL_SPEAKER);
    }
    set_page(0);
    s_speaker_muted = hp_inserted;
    return hp_inserted ? HP_TOGGLE_CONNECT : HP_TOGGLE_DISCONNECT;
}

static void setup_headphone_detection_interrupt(int gpio, bool gpio_is_button)
{
    s_speaker_muted = false;
    printf("Setting up headphone detection on GPIO %d, is_button=%d\n", gpio, gpio_is_button);
    if (gpio_is_button) {
        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_pull_down(gpio);
        gpio_set_irq_enabled_with_callback(gpio, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
                                           true, &gpio_callback);
    } else {
        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_pull_down(gpio);
        gpio_set_irq_enabled_with_callback(gpio, GPIO_IRQ_EDGE_RISE,
                                           true, &hp_int1_callback);
        /* Check initial state (outside ISR context) */
        handle_headphone_event();
    }
}

/* ----- codec register program ----------------------------------------- */

/* Programs the TLV320DAC3100 for I2S, 16-bit, fS = 44.1 kHz from BCLK via PLL.
 * Adapted from the fruitjam-doom register script. */
static void tlv320_program_registers(void)
{
    printf("Initializing TLV320AIC3204 audio DAC...\n");
    i2c_init(I2C_PORT, 400 * 1000);
    sleep_ms(10);
    gpio_set_function(PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_SDA);
    gpio_pull_up(PIN_SCL);

    /* Software reset */
    write_register(0x01, 0x01);
    sleep_ms(10);

    /* Interface Control 1: I²S, 16-bit, no offset */
    write_register(0x1B, 0x00);
    write_register(0x1C, 0x00);

    /* Clock MUX / PLL */
    modify_register(0x04, 0x03, 0x03);
    modify_register(0x04, 0x0C, 0x04);
    write_register(0x06, 0x20);
    write_register(0x08, 0x00);
    write_register(0x07, 0x00);
    modify_register(0x05, 0x0F, 0x02);
    modify_register(0x05, 0x70, 0x10);

    /* DAC/ADC dividers */
    modify_register(0x0B, 0x7F, 0x08); modify_register(0x0B, 0x80, 0x80); /* NDAC */
    modify_register(0x0C, 0x7F, 0x02); modify_register(0x0C, 0x80, 0x80); /* MDAC */
    modify_register(0x12, 0x7F, 0x08); modify_register(0x12, 0x80, 0x80); /* NADC */
    modify_register(0x13, 0x7F, 0x02); modify_register(0x13, 0x80, 0x80); /* MADC */

    /* PLL Power Up */
    modify_register(0x05, 0x80, 0x80);

    /* Headset / GPIO config (page 1) */
    set_page(1);
    modify_register(0x2e, 0xFF, 0x0b);
    set_page(0);

    /* Timer clock = internal oscillator (page 3 / reg 0x10 D7=0). MCLK isn't
     * connected on these boards (PLL_CLKIN = BCLK), so the debounce clock
     * needs a source for headset detection to work. */
    set_page(3);
    modify_register(0x10, 0x80, 0x00);
    set_page(0);

    /* Headset detect enable, 64ms debounce */
    modify_register(0x43, 0x9C, 0x88);
    modify_register(0x30, 0x81, 0x81); /* INT1 = headset detect, multi-pulse */
    modify_register(0x33, 0x3C, 0x14); /* GPIO1 = INT1 output */

    modify_register(0x3F, 0xC0, 0xC0); /* DAC setup */

    /* DAC routing (page 1) */
    set_page(1);
    modify_register(0x23, 0xC0, 0x40);
    modify_register(0x23, 0x0C, 0x04);

    /* DAC digital volume (page 0) */
    set_page(0);
    modify_register(0x40, 0x0C, 0x00);
    write_register(0x41, 0x0A); /* +5 dB L */
    write_register(0x42, 0x0A); /* +5 dB R */

    /* ADC setup */
    modify_register(0x51, 0x80, 0x80);
    modify_register(0x52, 0x80, 0x00);
    write_register(0x53, 0x68);

    /* HP & Speaker (page 1) */
    set_page(1);
    modify_register(0x1F, 0xC0, 0xC0);
    modify_register(0x28, 0x04, 0x04);
    modify_register(0x29, 0x04, 0x04);
    write_register(0x24, 0x0A);
    write_register(0x25, 0x0A);
    modify_register(0x28, 0x78, 0x40);
    modify_register(0x29, 0x78, 0x40);

    modify_register(0x20, 0x80, 0x80);
    modify_register(0x2A, 0x04, 0x04);
    modify_register(0x2A, 0x18, 0x08);
    write_register(0x26, 0x0A);

    set_page(0);

#if PICO_AUDIO_I2S_INTERRUPT_PIN != -1
    if (!s_dac_error) {
        printf("setup headphone detection interrupt.\n");
        setup_headphone_detection_interrupt(PICO_AUDIO_I2S_INTERRUPT_PIN,
                                            PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON);
    }
#endif
    printf("TLV320AIC3204 Initialization complete!\n");
    sleep_ms(100);
}

/* ----- public API ----------------------------------------------------- */

bool tlv320_init(void)
{
    s_active = false;
    int retries = 0;
    printf("Init TLV320 DAC, max 5 retries...\n");
    do {
        s_dac_error = false;
        tlv320_hardware_reset();
        tlv320_program_registers();
        if (!s_dac_error) {
            s_active = true;
            return true;
        }
        printf("TLV320 init failed, retrying (%d/5)...\n", retries + 1);
        sleep_ms(500);
    } while (s_dac_error && ++retries < 5);
    printf("TLV320 initialization failed after 5 attempts.\n");
    return false;
}

void tlv320_set_volume(int8_t level)
{
    if (!s_active) return;
    if (level < -63 || level > 23) {
        printf("Volume level %d out of range (-63 to 23)\n", level);
        return;
    }
    printf("Setting TLV320 volume to level %d\n", level);
    set_page(0);
    modify_register(0x40, 0x0C, 0x00);
    write_register(0x41, level << 1);
    write_register(0x42, level << 1);
}

void tlv320_mute_speaker(bool mute)
{
    if (!s_active) return;
    s_speaker_muted = mute;
    if (mute) speaker_mute_internal();
    else      speaker_unmute_internal();
}

enum headphone_toggle_t tlv320_poll_headphone(void)
{
    if (!s_active || !s_hp_irq_pending) return HP_TOGGLE_NONE;
    s_hp_irq_pending = false;
    return handle_headphone_event();
}

bool tlv320_dac_error(void)
{
    return s_dac_error;
}

bool tlv320_is_active(void)
{
    return s_active;
}
