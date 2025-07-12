# Release notes
## 12/7/2025

- Make PIO USB only available for RP2350, because of memory limitations on RP2040.
- Move PIO USB to Pio2, this fixes the NES controller not working on controller port 2.

## 6/7/2025

- If PSRAM is present (default pin 47), ROMs load from the SD card into PSRAM instead of flash (RP2350 boards only). This speeds up loading because the board no longer has to reboot to copy the ROM from the SD card to flash. Based on https://github.com/AndrewCapon/PicoPlusPsram
- Added -s option to bld.sh to allow an alternative GPIO pin for PSRAM chip select.
- Added support for Pimoroni Pico Plus 2. (Use hardware configuration 2, which is also used for breadboard and PCB). No extra binary needed.

## 5/7/2025

- Enabled PIO-USB for certain board configurations.
- Refactored bld.sh

## 7/6/2025

- Enable I2S audio on the Pimoroni Pico DV Demo Base. This allows audio output through external speakers connected to the line-out jack of the Pimoroni Pico DV Demo Base. 
- improved error handling in build scripts.

## 20/5/2025
- Added Custom PCB design for use with Waveshare [RP2040-Zero](https://www.waveshare.com/rp2040-zero.htm) or [RP2350-Zero](https://www.waveshare.com/rp2350-zero.htm) mini development board. The PCB is designed to fit in a 3D-printed case. PCB and Case design by [@DynaMight1124](https://github.com/DynaMight1124)
- Added new configuration to BoardConfigs.cmake and bld.sh to support the new configuration for this PCB. 

## 26/4/2025

- Releases now built with SDK 2.1.1
- Support added for Adafruit Metro RP2350 board. See README for more info. No RISCV support yet.
- Switched to SD card driver pico_fatfs from https://github.com/elehobica/pico_fatfs. This is required for the Adafruit Metro RP2350. Thanks to [elehobica](https://github.com/elehobica/pico_fatfs) for helping making it work for the Pimoroni Pico DV Demo board.
- Besides FAT32, SD cards can now also be formatted as exFAT.
- Nes controller PIO code updated by [@ManCloud](https://github.com/ManCloud). This fixes the NES controller issues on the Waveshare RP2040 - PiZero board. [#8](https://github.com/fhoedemakers/pico_shared/issues/8)
- Board configs are moved to pico_shared.

## Fixes
- Fixed Pico 2 W: Led blinking causes screen flicker and ioctl timeouts [#2](https://github.com/fhoedemakers/pico_shared/issues/2). Solved with in SDK 2.1.1
- WII classic controller: i2c bus instance (i2c0 / i2c1) not hardcoded anymore but configurable via CMakeLists.txt. 

## 19/01/2025

- To properly use the AliExpress SNES controller you need to press Y to enable the X-button. This is now documented in the README.md.
- Added support for additional controllers. See README for details.

## 01/01/2025

- Enabe fastscrolling in the menu, by holding up/down/left/right for 500 milliseconds, repeat delay is 40 milliseconds.
- bld.sh mow uses the amount of cores available on the system to speed up the build process.
- Temporary Rollback NesPad code for the WaveShare RP2040-PiZero only. Other configurations are not affected.
- Update time functions to return milliseconds and use uint64_t to return microseconds.

## 22/12/2024

- The menu now uses the entire screen resolution of 320x240 pixels. This makes a 40x30 char screen with 8x8 font possible instead of 32x29. This also fixes the menu not displaying correctly on Risc-v builds because of a not implemented assembly rendering routine in Risc-v.
- Updated NESPAD to have CLK idle HIGH instead of idle LOW. Thanks to [ManCloud](https://github.com/ManCloud). 
- Other minor changes.
