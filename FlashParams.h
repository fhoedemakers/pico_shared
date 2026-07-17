#pragma once
#include "FrensHelpers.h"
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "pico/multicore.h"

#define FLASHPARAM_MAGIC "FRENS01"
#define FLASHPARAM_ADDRESS (((uintptr_t)&__flash_binary_end + 0xFFF) & ~0xFFF)



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
    //bool writeFlashParamsToFlash(uint32_t cpuFreqKHz, vreg_voltage voltage);

    void setOverclockLimits(uint32_t minFreqKHz, uint32_t maxFreqKHz, vreg_voltage minVoltage, vreg_voltage maxVoltage);
    bool WriteMaxValuesToFlash();
    bool WriteMinValuesToFlash();
    uint32_t getMinFreqKHz() ;
    uint32_t getMaxFreqKHz() ;
} // namespace Frens