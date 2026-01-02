#include "FlashParams.h"
#include <cstring>

// Helper functions to manage FlashParams in flash memory.
namespace Frens
{

    /// @brief Validate the given FlashParams structure.
    /// @param params
    /// @return true if valid, false otherwise.
    bool validateFlashParams(const FlashParams &params)
    {
        // Check magic string
        if (strncmp(params.magic, FLASHPARAM_MAGIC, sizeof(FLASHPARAM_MAGIC)) != 0)
        {
            // printf("Magic string mismatch in FlashParams\n");
            return false;
        }

        // Check CPU frequency & voltage
        if (params.cpuFreqKHz == FLASHPARAM_MIN_FREQ_KHZ && params.voltage == FLASHPARAM_MIN_VOLTAGE)
        {
            // printf("Valid FlashParams: min freq/voltage\n");
            return true;
        }
        if (params.cpuFreqKHz == FLASHPARAM_MAX_FREQ_KHZ && params.voltage == FLASHPARAM_MAX_VOLTAGE)
        {
            // printf("Valid FlashParams: max freq/voltage\n");
            return true;
        }

        return true;
    }

    /// @brief Get a pointer to the FlashParams stored in flash memory.
    /// @return Pointer to FlashParams in flash memory.
    FlashParams *getFlashParams()
    {
        return (FlashParams *)FLASHPARAM_ADDRESS;
    }

    /// @brief Write new FlashParams to flash memory and reboot.
    /// @param cpuFreqKHz The CPU frequency in KHz.
    /// @param voltage The voltage setting.
    /// @return true on success, false when invalid params are provided.
    bool writeFlashParamsToFlash(uint32_t cpuFreqKHz, vreg_voltage voltage)
    {
        FlashParams params;
        params.cpuFreqKHz = cpuFreqKHz;
        params.voltage = voltage;
        strncpy(params.magic, FLASHPARAM_MAGIC, sizeof(FLASHPARAM_MAGIC));
        auto ofs = FLASHPARAM_ADDRESS - XIP_BASE;
        printf("Erasing and programming flash at offset: 0x%08X\n", ofs);
        if (!validateFlashParams(params))
        {
            printf("Invalid FlashParams provided. Aborting flash operation.\n");
            return false; // Invalid params
        }

        printf("New FlashParams: cpuFreqKHz=%u, voltage=%u\n", params.cpuFreqKHz, params.voltage);
        printf("System will reboot after programming flash...\n");
        // Program the hardware watchdog timer to reboot after 100 ms and do this before writing to flash,
        // system will likely hang after flash write.
        // Must be time enough to complete flash write.
        // This ensures the reboot even if the system crashes after flash write.
        // We will also reset core 1 to avoid it possibly interfering with the flash write.
        printf("Resetting core 1...\n");
        multicore_reset_core1();
        printf("Setting watchdog timer to reboot in 100 ms\n");
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
        return true;
    }

} // namespace Frens