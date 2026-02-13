#pragma once
#include "FrensHelpers.h"
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "pico/multicore.h"
#define FLASHPARAM_ADDRESS (((uintptr_t)&__flash_binary_end + 0xFFF) & ~0xFFF)
#define FLASHPARAM_MAGIC "FRENS01"
#define FLASHPARAM_MIN_FREQ_KHZ 252000 // NES, GB, SMS
#define FLASHPARAM_MIN_VOLTAGE vreg_voltage::VREG_VOLTAGE_1_20

// Genesis max settings
#if !HSTX
#define FLASHPARAM_MAX_FREQ_KHZ 324000 
// Because of high overclock, RP2450-Pizero needs high voltage for stable image. 
// THIS MAY OVERHEAT AND DAMAGE THE CPU, USE HEATSINK!!!
#if HW_CONFIG == 7   
// 1_90 2_00 : Unstable image during gameplay
// 2_35 : Stable image during gameplay, but random reboots.
#define FLASHPARAM_MAX_VOLTAGE vreg_voltage::VREG_VOLTAGE_2_50
#else
#define FLASHPARAM_MAX_VOLTAGE vreg_voltage::VREG_VOLTAGE_1_30
#endif
#else
#define FLASHPARAM_MAX_FREQ_KHZ 378000 // May cause artifacts on some screens, 336000 seems stable 
                                       // https://github.com/fhoedemakers/retroJam/issues/7
#define FLASHPARAM_MAX_VOLTAGE vreg_voltage::VREG_VOLTAGE_1_60
#endif
namespace Frens {
    
    typedef struct 
    {
        char magic[sizeof(FLASHPARAM_MAGIC)];    // "FRENS001"
        uint32_t cpuFreqKHz;
        vreg_voltage voltage;
        // pad to 256 bytes
        char padding[256 - sizeof(magic) - sizeof(cpuFreqKHz) - sizeof(voltage)];
    } FlashParams;

    bool validateFlashParams(const FlashParams &params);
    bool writeFlashParamsToFlash(uint32_t cpuFreqKHz, vreg_voltage voltage);
}