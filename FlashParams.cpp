#include "FlashParams.h"
#include <cstring>

// Helper functions to manage FlashParams in flash memory.
namespace Frens {
  
    /// @brief Validate the given FlashParams structure.
    /// @param params 
    /// @return true if valid, false otherwise.
    bool validateFlashParams(const FlashParams &params) {
        // Check magic string
        for (size_t i = 0; i < 8; ++i) {
            if (params.magic[i] != FLASHPARAM_MAGIC[i]) {
                //printf("Invalid magic in FlashParams: expected '%c', got '%c' at index %d\n", FLASHPARAM_MAGIC[i], params.magic[i], (int)i);
                return false;
            }
        }

        // Check CPU frequency & voltage
        if (params.cpuFreqKHz == FLASHPARAM_MIN_FREQ_KHZ  && params.voltage == FLASHPARAM_MIN_VOLTAGE) {
            //printf("Valid FlashParams: min freq/voltage\n");
            return true;
        }
        if (params.cpuFreqKHz == FLASHPARAM_MAX_FREQ_KHZ  && params.voltage == FLASHPARAM_MAX_VOLTAGE) {
            // printf("Valid FlashParams: max freq/voltage\n");
            return true;
        }

        return true;
    }

    /// @brief Get a pointer to the FlashParams stored in flash memory. 
    /// @return Pointer to FlashParams in flash memory.
    FlashParams *getFlashParams() {
        return (FlashParams *)FLASHPARAM_ADDRESS;
    }

    /// @brief Write new FlashParams to flash memory and reboot.
    /// @param cpuFreqKHz The CPU frequency in KHz.
    /// @param voltage The voltage setting.
    /// @return true on success, false when invalid params are provided.
    bool writeFlashParamsToFlash(uint32_t cpuFreqKHz, vreg_voltage voltage) {
        FlashParams params;
        params.cpuFreqKHz = cpuFreqKHz;
        params.voltage = voltage;
        strncpy(params.magic, FLASHPARAM_MAGIC, sizeof(FLASHPARAM_MAGIC));
        printf("Flashing new FlashParams to flash memory... address: 0x%08X\n", FLASHPARAM_ADDRESS);
        if (!validateFlashParams(params)) {
            printf("Invalid FlashParams provided. Aborting flash operation.\n");
            return false; // Invalid params
        }
      
       
#if 1
        auto ofs = FLASHPARAM_ADDRESS - XIP_BASE;
        printf("Erasing and programming flash at offset: 0x%08X\n", ofs);
        printf("New FlashParams: cpuFreqKHz=%u, voltage=%u\n", params.cpuFreqKHz, params.voltage);
        // Program the hardware watchdog timer to reboot after 100 ms and do this before writing to flash, 
        // system will likely hang after flash write.
        // Must be time enough to complete flash write.
        // This ensures the reboot even if the system crashes after flash write.
        watchdog_enable(100, 1);
    
        uint32_t ints = save_and_disable_interrupts();
        flash_range_erase(ofs, 4096);
        flash_range_program(ofs, (const uint8_t *)&params, sizeof(FlashParams));
        restore_interrupts(ints);
        // Will likely to crash here.  
        while (1)
        {
            tight_loop_contents();
        };
        __unreachable();
#endif
        return true; 
    }

} // namespace Frens