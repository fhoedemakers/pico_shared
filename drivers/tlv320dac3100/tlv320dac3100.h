/*
 *  tlv320dac3100.h
 *
 *  TLV320DAC3100 I2C codec control, factored out of the I2S driver so it can
 *  be shared between the legacy custom driver and the pico-extras based one.
 *
 *  Configuration is supplied as compile definitions (see board configs):
 *    PICO_AUDIO_I2S_RESET_PIN            GPIO connected to the codec /RST pin
 *    PICO_AUDIO_I2S_INTERRUPT_PIN        GPIO connected to headphone-detect
 *                                        signal (DAC INT1 or button); -1 = none
 *    PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON  1 = the GPIO is a push-button that
 *                                        toggles speaker mute; 0 = the GPIO is
 *                                        driven by the DAC's INT1 output.
 *
 *  The I2C bus shared with the Wii controller pads (WIIPAD_I2C / WII_PIN_SDA /
 *  WII_PIN_SCL) is used to talk to the codec at 0x18.
 */

#ifndef _TLV320DAC3100_H_
#define _TLV320DAC3100_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PICO_AUDIO_I2S_RESET_PIN
#define PICO_AUDIO_I2S_RESET_PIN 7
#endif

#ifndef PICO_AUDIO_I2S_INTERRUPT_PIN
#define PICO_AUDIO_I2S_INTERRUPT_PIN -1
#endif

#ifndef PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON
#define PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON 0
#endif

#ifndef PICO_AUDIO_I2S_DEBUG
#define PICO_AUDIO_I2S_DEBUG 0
#endif

enum headphone_toggle_t {
    HP_TOGGLE_NONE = 0,
    HP_TOGGLE_CONNECT = 1,
    HP_TOGGLE_DISCONNECT = 2
};

/// Hardware reset, I2C bus setup, codec register programming, and (if
/// PICO_AUDIO_I2S_INTERRUPT_PIN >= 0) headphone-detect interrupt installation.
/// Retries up to 5 times on I2C failure. Returns true on success.
bool tlv320_init(void);

/// Set DAC digital volume. level is in -63..+23 (units of 0.5 dB? — see driver).
void tlv320_set_volume(int8_t level);

/// Mute or unmute the speaker amplifier path. No-op if codec inactive.
void tlv320_mute_speaker(bool mute);

/// Returns a pending headphone-insertion / -removal event if one was latched
/// in the GPIO IRQ, otherwise HP_TOGGLE_NONE. Safe to call every frame.
enum headphone_toggle_t tlv320_poll_headphone(void);

/// True if the last I2C transaction (anywhere in this module) failed.
bool tlv320_dac_error(void);

/// True after tlv320_init() returned true.
bool tlv320_is_active(void);

#ifdef __cplusplus
}
#endif

#endif // _TLV320DAC3100_H_
