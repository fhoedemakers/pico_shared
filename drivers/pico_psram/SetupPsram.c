#if PICO_RP2350
#include "pico/stdlib.h"
#include "stdio.h"
#include "hardware/regs/qmi.h"
#include "hardware/regs/xip.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"

/// @brief Setup PSRAM
/// @param psramCS Chip select pin for the PSRAM
/// @return 0: no psram detected, > 0 psram size, -1 on failure
/// From: https://github.com/adafruit/circuitpython/blob/main/ports/raspberrypi/supervisor/port.c
int32_t __no_inline_not_in_flash_func(SetupPsram)(int psramCS)
{

    gpio_set_function(psramCS, GPIO_FUNC_XIP_CS1);
    int32_t _psram_size = 0;
    uint32_t intr_stash = save_and_disable_interrupts();
    // Try and read the PSRAM ID via direct_csr.
    qmi_hw->direct_csr = 30 << QMI_DIRECT_CSR_CLKDIV_LSB |
                         QMI_DIRECT_CSR_EN_BITS;
    // Need to poll for the cooldown on the last XIP transfer to expire
    // (via direct-mode BUSY flag) before it is safe to perform the first
    // direct-mode operation
    while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0)
    {
    }

    // Exit out of QMI in case we've inited already
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    // Transmit as quad.
    qmi_hw->direct_tx = QMI_DIRECT_TX_OE_BITS |
                        QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB |
                        0xf5;
    while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0)
    {
    }
    (void)qmi_hw->direct_rx;
    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS);

    // Read the id
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    uint8_t kgd = 0;
    uint8_t eid = 0;
    for (size_t i = 0; i < 7; i++)
    {
        if (i == 0)
        {
            qmi_hw->direct_tx = 0x9f;
        }
        else
        {
            qmi_hw->direct_tx = 0xff;
        }
        while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_TXEMPTY_BITS) == 0)
        {
        }
        while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0)
        {
        }
        if (i == 5)
        {
            kgd = qmi_hw->direct_rx;
        }
        else if (i == 6)
        {
            eid = qmi_hw->direct_rx;
        }
        else
        {
            (void)qmi_hw->direct_rx;
        }
    }
    // Disable direct csr.
    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS | QMI_DIRECT_CSR_EN_BITS);

    if (kgd != 0x5D)
    {
        restore_interrupts(intr_stash);
        printf("PSRAM not detected, kgd = 0x%02x\n", kgd);
        return 0;
    }
    // never_reset_pin_number(CIRCUITPY_PSRAM_CHIP_SELECT->number);

    // Enable quad mode.
    qmi_hw->direct_csr = 30 << QMI_DIRECT_CSR_CLKDIV_LSB |
                         QMI_DIRECT_CSR_EN_BITS;
    // Need to poll for the cooldown on the last XIP transfer to expire
    // (via direct-mode BUSY flag) before it is safe to perform the first
    // direct-mode operation
    while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0)
    {
    }

    // RESETEN, RESET and quad enable
    for (uint8_t i = 0; i < 3; i++)
    {
        qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
        if (i == 0)
        {
            qmi_hw->direct_tx = 0x66;
        }
        else if (i == 1)
        {
            qmi_hw->direct_tx = 0x99;
        }
        else
        {
            qmi_hw->direct_tx = 0x35;
        }
        while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0)
        {
        }
        qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
        for (size_t j = 0; j < 20; j++)
        {
            asm("nop");
        }
        (void)qmi_hw->direct_rx;
    }
    // Disable direct csr.
    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS | QMI_DIRECT_CSR_EN_BITS);

    qmi_hw->m[1].timing =
        QMI_M0_TIMING_PAGEBREAK_VALUE_1024 << QMI_M0_TIMING_PAGEBREAK_LSB | // Break between pages.
        3 << QMI_M0_TIMING_SELECT_HOLD_LSB |                                // Delay releasing CS for 3 extra system cycles.
        1 << QMI_M0_TIMING_COOLDOWN_LSB |
        1 << QMI_M0_TIMING_RXDELAY_LSB |
        16 << QMI_M0_TIMING_MAX_SELECT_LSB |  // In units of 64 system clock cycles. PSRAM says 8us max. 8 / 0.00752 / 64 = 16.62
        7 << QMI_M0_TIMING_MIN_DESELECT_LSB | // In units of system clock cycles. PSRAM says 50ns.50 / 7.52 = 6.64
        2 << QMI_M0_TIMING_CLKDIV_LSB;
    qmi_hw->m[1].rfmt = (QMI_M0_RFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_PREFIX_WIDTH_LSB |
                         QMI_M0_RFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_RFMT_ADDR_WIDTH_LSB |
                         QMI_M0_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_SUFFIX_WIDTH_LSB |
                         QMI_M0_RFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_RFMT_DUMMY_WIDTH_LSB |
                         QMI_M0_RFMT_DUMMY_LEN_VALUE_24 << QMI_M0_RFMT_DUMMY_LEN_LSB |
                         QMI_M0_RFMT_DATA_WIDTH_VALUE_Q << QMI_M0_RFMT_DATA_WIDTH_LSB |
                         QMI_M0_RFMT_PREFIX_LEN_VALUE_8 << QMI_M0_RFMT_PREFIX_LEN_LSB |
                         QMI_M0_RFMT_SUFFIX_LEN_VALUE_NONE << QMI_M0_RFMT_SUFFIX_LEN_LSB);
    qmi_hw->m[1].rcmd = 0xeb << QMI_M0_RCMD_PREFIX_LSB |
                        0 << QMI_M0_RCMD_SUFFIX_LSB;
    qmi_hw->m[1].wfmt = (QMI_M0_WFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_PREFIX_WIDTH_LSB |
                         QMI_M0_WFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_WFMT_ADDR_WIDTH_LSB |
                         QMI_M0_WFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_SUFFIX_WIDTH_LSB |
                         QMI_M0_WFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_WFMT_DUMMY_WIDTH_LSB |
                         QMI_M0_WFMT_DUMMY_LEN_VALUE_NONE << QMI_M0_WFMT_DUMMY_LEN_LSB |
                         QMI_M0_WFMT_DATA_WIDTH_VALUE_Q << QMI_M0_WFMT_DATA_WIDTH_LSB |
                         QMI_M0_WFMT_PREFIX_LEN_VALUE_8 << QMI_M0_WFMT_PREFIX_LEN_LSB |
                         QMI_M0_WFMT_SUFFIX_LEN_VALUE_NONE << QMI_M0_WFMT_SUFFIX_LEN_LSB);
    qmi_hw->m[1].wcmd = 0x38 << QMI_M0_WCMD_PREFIX_LSB |
                        0 << QMI_M0_WCMD_SUFFIX_LSB;

    restore_interrupts(intr_stash);

    _psram_size = 1024 * 1024; // 1 MiB
    uint8_t size_id = eid >> 5;
    if (eid == 0x26 || size_id == 2)
    {
        _psram_size *= 8;
    }
    else if (size_id == 0)
    {
        _psram_size *= 2;
    }
    else if (size_id == 1)
    {
        _psram_size *= 4;
    }

    // Mark that we can write to PSRAM.
    xip_ctrl_hw->ctrl |= XIP_CTRL_WRITABLE_M1_BITS;

    // Test write to the PSRAM.
    volatile uint32_t *psram_nocache = (volatile uint32_t *)0x15000000;
    psram_nocache[0] = 0x12345678;
    volatile uint32_t readback = psram_nocache[0];
    if (readback != 0x12345678)
    {
        _psram_size = 0;
        printf("PSRAM test failed, readback = 0x%08x\n", readback);
        return -1;
    }
    return _psram_size;
}
#endif // PICO_RP2350