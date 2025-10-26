#pragma once
#include <cstdint>
#ifndef WIIPAD_I2C
#define WIIPAD_I2C i2c1
#endif
#define WII_I2C WIIPAD_I2C
#define WII_ADDR 0x52

extern void wiipad_begin(void);
extern uint16_t wiipad_read(void);
extern void wiipad_end(void);
extern bool wiipad_is_connected();
