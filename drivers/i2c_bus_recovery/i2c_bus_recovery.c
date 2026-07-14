/* Generic I2C bus-clear / recovery (I2C-bus specification 3.1.16).
 *
 * A slave that was interrupted mid-transfer (or wedged by bus traffic it
 * could not follow) can hold SDA low indefinitely, blocking every device on
 * the bus. The standard remedy is to bit-bang clock pulses on SCL until the
 * slave finishes its byte and releases SDA, then issue a STOP condition.
 *
 * Open-drain is emulated by toggling the pin direction: output-low to drive
 * the line low, input (with pull-up) to release it. A line is never driven
 * high, so this is safe with other masters/slaves attached. */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "i2c_bus_recovery.h"

#define HALF_PERIOD_US 5       /* ~100 kHz recovery clock */
#define MAX_PULSES 16          /* spec says 9; allow some margin */
#define SCL_STRETCH_TIMEOUT_US 10000

/* Release the line and wait for the pull-up to raise it (stretch-tolerant). */
static bool release_and_wait_high(unsigned int pin, unsigned int timeout_us)
{
    gpio_set_dir(pin, GPIO_IN);
    unsigned int waited = 0;
    while (!gpio_get(pin) && waited < timeout_us) {
        busy_wait_us(10);
        waited += 10;
    }
    return gpio_get(pin);
}

bool i2c_bus_clear(unsigned int sda_pin, unsigned int scl_pin, const char *tag)
{
    /* Take the pins over as SIO with emulated open-drain. The output latch
     * is preset low BEFORE any direction/funcsel change so the lines are
     * only ever driven low. On RP2350 gpio_set_function() also enables the
     * pad input and clears pad isolation, which is required before
     * gpio_get() is trustworthy at cold boot. */
    gpio_pull_up(sda_pin);
    gpio_pull_up(scl_pin);
    gpio_put(sda_pin, 0);
    gpio_put(scl_pin, 0);
    gpio_set_dir(sda_pin, GPIO_IN);
    gpio_set_dir(scl_pin, GPIO_IN);
    gpio_set_function(sda_pin, GPIO_FUNC_SIO);
    gpio_set_function(scl_pin, GPIO_FUNC_SIO);
    busy_wait_us(HALF_PERIOD_US);

    bool sda0 = gpio_get(sda_pin);
    bool scl0 = gpio_get(scl_pin);
    printf("[%s] i2c bus check: SDA=%d SCL=%d%s\n", tag, sda0, scl0,
           (sda0 && scl0) ? " (idle)" : "");

    if (!(sda0 && scl0)) {
        /* A slave holding SCL low (clock stretch) cannot be clocked by the
         * master; all we can do is wait for it to let go. */
        if (!scl0 && !release_and_wait_high(scl_pin, SCL_STRETCH_TIMEOUT_US)) {
            printf("[%s] i2c bus clear: SCL held low by slave, unrecoverable\n", tag);
        } else {
            if (!gpio_get(sda_pin)) {
                /* SDA stuck low, SCL free: clock the wedged slave through
                 * the rest of its byte until it releases SDA. */
                int pulses = 0;
                while (!gpio_get(sda_pin) && pulses < MAX_PULSES) {
                    gpio_set_dir(scl_pin, GPIO_OUT); /* drive SCL low */
                    busy_wait_us(HALF_PERIOD_US);
                    release_and_wait_high(scl_pin, 1000);
                    busy_wait_us(HALF_PERIOD_US);
                    pulses++;
                }
                printf("[%s] i2c bus clear: %d SCL pulses, SDA now %d\n",
                       tag, pulses, gpio_get(sda_pin));
            }
            if (gpio_get(sda_pin) && gpio_get(scl_pin)) {
                /* START then STOP to reset slave state machines. */
                gpio_set_dir(sda_pin, GPIO_OUT); /* SDA low while SCL high */
                busy_wait_us(HALF_PERIOD_US);
                gpio_set_dir(sda_pin, GPIO_IN);  /* SDA released while SCL high */
                busy_wait_us(HALF_PERIOD_US);
            }
        }
    }

    bool ok = gpio_get(sda_pin) && gpio_get(scl_pin);
    if (!ok) {
        printf("[%s] i2c bus clear FAILED: SDA=%d SCL=%d\n", tag,
               gpio_get(sda_pin), gpio_get(scl_pin));
    }
    /* Hand the pins back to the I2C peripheral; callers run i2c_init()
     * afterwards, which also recovers a wedged I2C master block. */
    gpio_set_function(sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(scl_pin, GPIO_FUNC_I2C);
    return ok;
}
