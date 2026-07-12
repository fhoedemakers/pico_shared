#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard I2C bus-clear (I2C-bus specification 3.1.16).
 *
 * Takes SDA/SCL over as SIO GPIOs with emulated open-drain (never drives a
 * line high), logs the observed line levels (tag prefixes the log lines),
 * pulses SCL at ~100 kHz if a slave holds SDA low, issues a STOP, and hands
 * the pins back to GPIO_FUNC_I2C with pull-ups enabled.
 *
 * Returns true if both lines are high (bus idle) afterwards. A slave that
 * keeps SCL low (clock stretch) cannot be recovered from the master side;
 * that case is logged and returns false.
 *
 * Pure GPIO: safe to call before any i2c_init(). The caller must (re)run
 * i2c_init() afterwards to (re)configure the I2C peripheral itself. */
bool i2c_bus_clear(unsigned int sda_pin, unsigned int scl_pin, const char *tag);

#ifdef __cplusplus
}
#endif
