#pragma once
#include <cstdint>
#ifndef WIIPAD_I2C
#define WIIPAD_I2C i2c1
#endif
#define WII_I2C WIIPAD_I2C
#define WII_ADDR 0x52

extern void wiipad_begin(void);
// Returns a bitmask of pressed buttons: bit0=A, 1=B, 2=Select(-), 3=Start(+),
// 4=Up, 5=Down, 6=Left, 7=Right, 8=X, 9=Y, 10=L (LT full-press or ZL),
// 11=R (RT full-press or ZR).
extern uint16_t wiipad_read(void);
extern void wiipad_end(void);
extern bool wiipad_is_connected();
#if WII_PIN_SDA >= 0 and WII_PIN_SCL >= 0
#define WIIPAD_IS_CONNECTED() (wiipad_is_connected())
#else
#define WIIPAD_IS_CONNECTED() (false)
#endif