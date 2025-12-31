#pragma once
#include "FrensHelpers.h"
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#define FLASHPARAM_ADDRESS (((uintptr_t)&__flash_binary_end + 0xFFF) & ~0xFFF)
#define FLASHPARAM_MAGIC "FRENS01"
#define FLASHPARAM_MIN_FREQ_KHZ 252000 // NES, GB, SMS
#define FLASHPARAM_MIN_VOLTAGE vreg_voltage::VREG_VOLTAGE_1_20

// Genesis max settings
#if !HSTX
#define FLASHPARAM_MAX_FREQ_KHZ 324000 
#define FLASHPARAM_MAX_VOLTAGE vreg_voltage::VREG_VOLTAGE_1_30
#else
#define FLASHPARAM_MAX_FREQ_KHZ 378000 // 
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