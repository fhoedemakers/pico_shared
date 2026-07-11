#pragma once

#include "hardware/pio.h"

//extern uint8_t nespad_state;
// Legacy 8-bit view (bit0=A, 1=B, 2=Select, 3=Start, 4=Up, 5=Down, 6=Left,
// 7=Right for a NES pad; for a SNES pad the same bits carry B,Y,Select,
// Start,dpad — the first 8 bits it shifts out). Kept for existing consumers.
extern uint8_t nespad_states[2];
// Full 16-clock read in SNES serial order: bit0=B, 1=Y, 2=Select, 3=Start,
// 4=Up, 5=Down, 6=Left, 7=Right, 8=A, 9=X, 10=L, 11=R (1 = pressed).
// NES pads are auto-detected and only populate bits 0-7 (A,B,Select,Start,
// dpad), identical to nespad_states[].
extern uint16_t nespad_states_ext[2];
extern bool nespad_begin(uint8_t padnum, uint32_t cpu_khz, uint8_t clkPin, uint8_t dataPin,
                         uint8_t latPin, PIO _pio);
extern void nespad_read_start(void);
extern void nespad_read_finish(void);
