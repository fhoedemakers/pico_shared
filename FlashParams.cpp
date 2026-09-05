#include "FlashParams.h"
#include <cstring>

#define FLASHPARAM_MIN_FREQ_KHZ 252000 // NES, GB, SMS
#define FLASHPARAM_MIN_VOLTAGE vreg_voltage::VREG_VOLTAGE_1_30

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
#define FLASHPARAM_MAX_VOLTAGE vreg_voltage::VREG_VOLTAGE_1_50
#endif
// Helper functions to manage FlashParams in flash memory.
namespace Frens
{
    static vreg_voltage _minVoltage = FLASHPARAM_MIN_VOLTAGE;
    static vreg_voltage _maxVoltage = FLASHPARAM_MAX_VOLTAGE;
    static uint32_t _minFreq = FLASHPARAM_MIN_FREQ_KHZ;
    static uint32_t _maxFreq = FLASHPARAM_MAX_FREQ_KHZ;
    
    void setOverclockLimits(uint32_t minFreqKHz, uint32_t maxFreqKHz, vreg_voltage minVoltage, vreg_voltage maxVoltage)
    {
        _minFreq = minFreqKHz;
        _maxFreq = maxFreqKHz;
        _minVoltage = minVoltage;
        _maxVoltage = maxVoltage;
    }

    uint32_t getMinFreqKHz() { return _minFreq; }
    uint32_t getMaxFreqKHz() { return _maxFreq; }
    vreg_voltage getMinVoltage() { return _minVoltage; }
    vreg_voltage getMaxVoltage() { return _maxVoltage; }

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
        if (params.cpuFreqKHz == _minFreq && params.voltage == _minVoltage)
        {
            // printf("Valid FlashParams: min freq/voltage\n");
            return true;
        }
        if (params.cpuFreqKHz == _maxFreq && params.voltage == _maxVoltage)
        {
            // printf("Valid FlashParams: max freq/voltage\n");
            return true;
        }

        return false;
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
    bool __not_in_flash_func(writeFlashParamsToFlash)(uint32_t cpuFreqKHz, vreg_voltage voltage)
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
        // Program the hardware watchdog timer to reboot and do this before writing to flash,
        // system will likely hang after flash write.
        // Must be time enough to complete flash write: erasing a 4 KB sector is
        // typically ~50 ms but several hundred ms worst case, so do not cut this fine.
        // This ensures the reboot even if the system crashes after flash write.
        // We will also reset core 1 to avoid it possibly interfering with the flash write.
        printf("Resetting core 1...\n");
        multicore_reset_core1();
        printf("Setting watchdog timer to reboot in 1000 ms\n");
        watchdog_enable(1000, 0);

        // Interrupts stay off across BOTH operations, not just inside each one.
        //
        // On RP2350 flash_range_erase()/flash_range_program() finish in the bootrom's
        // flash_enter_cmd_xip(), which resets qmi_hw->m[0] to its conservative default:
        // the CLKDIV/RXDELAY setClocksAndStartStdio() programmed for the current
        // (possibly overclocked) clock is gone, and XIP is back to a plain 03h serial
        // read with RXDELAY=0. The SDK only saves and restores QMI CS1 (the PSRAM), so
        // nothing puts M0 back. Code fetched from flash in that window is unreliable.
        //
        // flashEraseSafe() restores interrupts on the way out, so calling the two
        // wrappers back to back leaves interrupts enabled while XIP is in that state,
        // and the next IRQ vectors into a flash-resident handler. It faulted or stalled
        // between the erase and the program, leaving the sector erased and never
        // written: validateFlashParams() then rejected it on the next boot and the box
        // came up at the default clock instead of the overclock the user had just
        // enabled. Holding interrupts off across both calls is what this function did
        // before the wrappers were introduced, and keeps every instruction from here to
        // the watchdog reboot running from RAM.
        uint32_t ints = save_and_disable_interrupts();
        flashEraseSafe(ofs, 4096);
        flashProgramSafe(ofs, (const uint8_t *)&params, sizeof(FlashParams));
        restore_interrupts(ints);
        // Will likely to crash here.
        while (1)
        {
            tight_loop_contents();
        };
        __unreachable();
        return true;
    }

    bool WriteMaxValuesToFlash()
    {
        return writeFlashParamsToFlash(_maxFreq, _maxVoltage);
    }

    bool WriteMinValuesToFlash()
    {
        return writeFlashParamsToFlash(_minFreq, _minVoltage);
    }
} // namespace Frens