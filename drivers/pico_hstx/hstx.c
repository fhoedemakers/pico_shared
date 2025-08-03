#if PICO_RP2350
#include "hstx.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/structs/sio.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"
#include "pico/sem.h"
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include <string.h>
// This code is based on https://forums.raspberrypi.com/viewtopic.php?t=375277

#define ALIGNED __attribute__((aligned(4)))
// ----------------------------------------------------------------------------
// DVI constants

#define TMDS_CTRL_00 0x354u
#define TMDS_CTRL_01 0x0abu
#define TMDS_CTRL_10 0x154u
#define TMDS_CTRL_11 0x2abu

#define SYNC_V0_H0 (TMDS_CTRL_00 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V0_H1 (TMDS_CTRL_01 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H0 (TMDS_CTRL_10 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H1 (TMDS_CTRL_11 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))

#define MODE_H_SYNC_POLARITY 0
#define MODE_H_ACTIVE_PIXELS 640
#define MODE_H_FRONT_PORCH 16
#define MODE_H_SYNC_WIDTH 64
#define MODE_H_BACK_PORCH 120

#define MODE_V_SYNC_POLARITY 0
#define MODE_V_ACTIVE_LINES 480
#define MODE_V_FRONT_PORCH 1
#define MODE_V_SYNC_WIDTH 3
#define MODE_V_BACK_PORCH 16
// #define clockspeed 252000 // 315000
int clockspeed;
#define clockdivisor 2
int X_TILE = 80, Y_TILE = 40;
uint8_t FRAMEBUFFER[(MODE_H_ACTIVE_PIXELS / 2) * (MODE_V_ACTIVE_LINES / 2) * 2];
uint16_t ALIGNED HDMIlines[2][MODE_H_ACTIVE_PIXELS] = {0};
uint8_t *WriteBuf = FRAMEBUFFER;
uint8_t *DisplayBuf = FRAMEBUFFER;
uint8_t *LayerBuf = FRAMEBUFFER;
uint16_t *tilefcols;
uint16_t *tilebcols;
volatile int enableScanLines = 0; // Enable scanlines
volatile int scanlineMode = 0; 
// volatile int HRes;      // 320
// volatile int VRes;       // 240
//  Fix to 320x240
#define HRes (MODE_H_ACTIVE_PIXELS / 2) // 320
#define VRes (MODE_V_ACTIVE_LINES / 2)  // 240
#define MODE_H_TOTAL_PIXELS (                \
    MODE_H_FRONT_PORCH + MODE_H_SYNC_WIDTH + \
    MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS)
#define MODE_V_TOTAL_LINES (                 \
    MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + \
    MODE_V_BACK_PORCH + MODE_V_ACTIVE_LINES)
volatile int HDMImode = 0;
volatile uint32_t frame_counter = 0; // Frame counter
#define HSTX_CMD_RAW (0x0u << 12)
#define HSTX_CMD_RAW_REPEAT (0x1u << 12)
#define HSTX_CMD_TMDS (0x2u << 12)
#define HSTX_CMD_TMDS_REPEAT (0x3u << 12)
#define HSTX_CMD_NOP (0xfu << 12)
#define SCREENMODE1 26
#define SCREENMODE2 27
#define SCREENMODE3 28
#define SCREENMODE4 29
#define SCREENMODE5 30
#define SCREENMODE6 31

// --- TMDS lane to GPIO mapping (configurable) ---
#ifndef GPIOHSTXD0
#define GPIOHSTXD0 18 // D0+ (default: GPIO18)
#endif
#ifndef GPIOHSTXD1
#define GPIOHSTXD1 16 // D1+ (default: GPIO16)
#endif
#ifndef GPIOHSTXD2
#define GPIOHSTXD2 12 // D2+ (default: GPIO12)
#endif
// Calculate HSTX output bit from GPIO number (GPIO12-19 => bit 0-7)
#define HSTX_BIT_FROM_GPIO(gpio) ((gpio) - 12)

// ----------------------------------------------------------------------------
// HSTX command lists

// Lists are padded with NOPs to be >= HSTX FIFO size, to avoid DMA rapidly
// pingponging and tripping up the IRQs.

static uint32_t vblank_line_vsync_off[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
    SYNC_V1_H1,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
    SYNC_V1_H0,
    HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS),
    SYNC_V1_H1,
    HSTX_CMD_NOP};

static uint32_t vblank_line_vsync_on[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
    SYNC_V0_H1,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
    SYNC_V0_H0,
    HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS),
    SYNC_V0_H1,
    HSTX_CMD_NOP};

static uint32_t vactive_line[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
    SYNC_V1_H1,
    HSTX_CMD_NOP,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
    SYNC_V1_H0,
    HSTX_CMD_NOP,
    HSTX_CMD_RAW_REPEAT | MODE_H_BACK_PORCH,
    SYNC_V1_H1,
    HSTX_CMD_TMDS | MODE_H_ACTIVE_PIXELS};

// ----------------------------------------------------------------------------
// DMA logic

#define DMACH_PING 0
#define DMACH_PONG 1

// First we ping. Then we pong. Then... we ping again.
static bool dma_pong = false;

// A ping and a pong are cued up initially, so the first time we enter this
// handler it is to cue up the second ping after the first ping has completed.
// This is the third scanline overall (-> =2 because zero-based).
volatile uint v_scanline = 2;

// During the vertical active period, we take two IRQs per scanline: one to
// post the command list, and another to post the pixels.
static bool vactive_cmdlist_posted = false;
volatile uint vblank = 0;

/// @brief DMA IRQ handler for HSTX
/// This function is called when a DMA transfer completes.
/// It handles the ping-pong mechanism for the DMA channels and updates the
/// read address and transfer count for the next transfer.
/// It also handles the vertical blanking and active periods by switching
/// between the vblank and vactive command lists.
/// It updates the scanline counter and frame counter as needed.
/// It is called from the DMA IRQ handler.
/// @param
/// @return
void __not_in_flash_func(dma_irq_handler)()
{
    // dma_pong indicates the channel that just finished, which is the one
    // we're about to reload.
    uint ch_num = dma_pong ? DMACH_PONG : DMACH_PING;
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    dma_hw->intr = 1u << ch_num;
    dma_pong = !dma_pong;

    if (v_scanline >= MODE_V_FRONT_PORCH && v_scanline < (MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH))
    {
        ch->read_addr = (uintptr_t)vblank_line_vsync_on;
        ch->transfer_count = count_of(vblank_line_vsync_on);
        vblank = 1;
    }
    else if (v_scanline < MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + MODE_V_BACK_PORCH)
    {
        ch->read_addr = (uintptr_t)vblank_line_vsync_off;
        ch->transfer_count = count_of(vblank_line_vsync_off);
        vblank = 1;
    }
    else if (!vactive_cmdlist_posted)
    {
        ch->read_addr = (uintptr_t)vactive_line;
        ch->transfer_count = count_of(vactive_line);
        vactive_cmdlist_posted = true;
        vblank = 0;
    }
    else
    {
        //        ch->read_addr = (uintptr_t)&FRAMEBUFFER[(v_scanline - (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES)) * MODE_H_ACTIVE_PIXELS];
        ch->read_addr = (uintptr_t)HDMIlines[v_scanline & 1];
        ch->transfer_count = MODE_H_ACTIVE_PIXELS / 2;
        vactive_cmdlist_posted = false;
    }

    if (!vactive_cmdlist_posted)
    {
        v_scanline = (v_scanline + 1) % MODE_V_TOTAL_LINES;
        if (v_scanline == 0)
        {
            frame_counter++; // Increment frame counter at end of frame
        }
    }
}

uint32_t core1stack[128];

/// @brief HSTX  function that runs on core1
/// This function initializes the HSTX controller, sets up the DMA channels,
/// and starts the DMA transfers for the HSTX output.
/// It also sets up the IRQ handler for the DMA transfers.
/// @param
/// @return
void __not_in_flash_func(HSTXCore)(void)
{
    int last_line = 2, load_line, line_to_load, Line_dup;

    // Configure HSTX's TMDS encoder for RGB332
    hstx_ctrl_hw->expand_tmds =
        29 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB |
        4 << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB |
        2 << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB |
        4 << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB |
        7 << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB |
        4 << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB;
    // Pixels (TMDS) come in 4 8-bit chunks. Control symbols (RAW) are an
    // entire 32-bit word.
    hstx_ctrl_hw->expand_shift =
        2 << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
        16 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
        1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
        0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;

    // Serial output config: clock period of 5 cycles, pop from command
    // expander every 5 cycles, shift the output shiftreg by 2 every cycle.
    hstx_ctrl_hw->csr = 0;
    hstx_ctrl_hw->csr =
        HSTX_CTRL_CSR_EXPAND_EN_BITS |
        5u << HSTX_CTRL_CSR_CLKDIV_LSB |
        5u << HSTX_CTRL_CSR_N_SHIFTS_LSB |
        2u << HSTX_CTRL_CSR_SHIFT_LSB |
        HSTX_CTRL_CSR_EN_BITS;

    // Note we are leaving the HSTX clock at the SDK default of 125 MHz; since
    // we shift out two bits per HSTX clock cycle, this gives us an output of
    // 250 Mbps, which is very close to the bit clock for 480p 60Hz (252 MHz).
    // If we want the exact rate then we'll have to reconfigure PLLs.

    // HSTX outputs 0 through 7 appear on GPIO 12 through 19.
    // Pinout on Pico DVI sock:
    //
    //   GP12 D0+  GP13 D0-
    //   GP14 CK+  GP15 CK-
    //   GP16 D2+  GP17 D2-
    //   GP18 D1+  GP19 D1-

    // Assign clock pair to two neighbouring pins:
    hstx_ctrl_hw->bit[2] = HSTX_CTRL_BIT0_CLK_BITS;
    hstx_ctrl_hw->bit[3] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;
    for (uint lane = 0; lane < 3; ++lane)
    {
        // For each TMDS lane, assign it to the correct GPIO pair based on the
        // desired pinout:

        // HSTX Output Bit 0 → GPIO12
        // HSTX Output Bit 1 → GPIO13
        // HSTX Output Bit 2 → GPIO14
        // HSTX Output Bit 3 → GPIO15
        // HSTX Output Bit 4 → GPIO16
        // HSTX Output Bit 5 → GPIO17
        // HSTX Output Bit 6 → GPIO18
        // HSTX Output Bit 7 → GPIO19
        // lane_to_output_bit Array
        // The lane_to_output_bit array specifies which HSTX output bits are used for each TMDS lane:

        // Index 0: TMDS lane D0 (data lane 0)
        // Index 1: TMDS lane D1 (data lane 1)
        // Index 2: TMDS lane D2 (data lane 2)

        // Hardcoded mapping for now using Adafruit Metro RP2350
        // The mapping is fixed as follows:
        // D0+ = CPIO18, D0-=GPIO19, D1+=GPIO16, D1-=GPIO17, D2+-GPIO12, D2-=GPIO13
        // For the array {6, 4, 0}:

        // D0 (Index 0) is assigned to HSTX output bit 6 → GPIO18 (D0+) and GPIO19 (D0-).
        // D1 (Index 1) is assigned to HSTX output bit 4 → GPIO16 (D1+) and GPIO17 (D1-).
        // D2 (Index 2) is assigned to HSTX output bit 0 → GPIO12 (D2+) and GPIO13 (D2-).
        // https://learn.adafruit.com/adafruit-metro-rp2350/pinouts#hstx-connector-3193107
        // TODO make configurable
        static const int lane_to_output_bit[3] = {
            HSTX_BIT_FROM_GPIO(GPIOHSTXD0),
            HSTX_BIT_FROM_GPIO(GPIOHSTXD1),
            HSTX_BIT_FROM_GPIO(GPIOHSTXD2)};
        int bit = lane_to_output_bit[lane];
        // Output even bits during first half of each HSTX cycle, and odd bits
        // during second half. The shifter advances by two bits each cycle.
        uint32_t lane_data_sel_bits =
            (lane * 10) << HSTX_CTRL_BIT0_SEL_P_LSB |
            (lane * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB;
        // The two halves of each pair get identical data, but one pin is inverted.
        hstx_ctrl_hw->bit[bit] = lane_data_sel_bits;
        hstx_ctrl_hw->bit[bit + 1] = lane_data_sel_bits | HSTX_CTRL_BIT0_INV_BITS;
    }

    for (int i = 12; i <= 19; ++i)
    {
        gpio_set_function(i, 0); // HSTX
    }

    // Both channels are set up identically, to transfer a whole scanline and
    // then chain to the opposite channel. Each time a channel finishes, we
    // reconfigure the one that just finished, meanwhile the opposite channel
    // is already making progress.
    dma_channel_config c;
    c = dma_channel_get_default_config(DMACH_PING);
    channel_config_set_chain_to(&c, DMACH_PONG);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(
        DMACH_PING,
        &c,
        &hstx_fifo_hw->fifo,
        vblank_line_vsync_off,
        count_of(vblank_line_vsync_off),
        false);
    c = dma_channel_get_default_config(DMACH_PONG);
    channel_config_set_chain_to(&c, DMACH_PING);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(
        DMACH_PONG,
        &c,
        &hstx_fifo_hw->fifo,
        vblank_line_vsync_off,
        count_of(vblank_line_vsync_off),
        false);

    dma_hw->ints0 = (1u << DMACH_PING) | (1u << DMACH_PONG);
    dma_hw->inte0 = (1u << DMACH_PING) | (1u << DMACH_PONG);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    bus_ctrl_hw->priority = 1;
    dma_channel_start(DMACH_PING);

    while (1)
    {
        if (v_scanline != last_line)
        {
            last_line = v_scanline;
            load_line = v_scanline - (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES);
            Line_dup = load_line >> 1;
            line_to_load = last_line & 1;
            uint16_t *p = HDMIlines[line_to_load];
            if (load_line >= 0 && load_line < MODE_V_ACTIVE_LINES)
            {
                __dmb();

                for (int i = 0; i < MODE_H_ACTIVE_PIXELS / 2; i++)
                {
                    uint8_t *d = &DisplayBuf[(Line_dup)*MODE_H_ACTIVE_PIXELS + i * 2];
                    int c = *d++;
                    c |= ((*d++) << 8);
#if 1
                    // Apply CRT scanline effect for RGB555: darken every other line
                    if (load_line & 1 && enableScanLines)
                    { // Odd lines = scanlines
                        int r = (c >> 10) & 0x1F;
                        int g = (c >> 5) & 0x1F;
                        int b = c & 0x1F;
                        r = r >> 1;
                        g = g >> 1;
                        b = b >> 1;
                        c = (r << 10) | (g << 5) | b;
                    }
                    // tried vertical and pixel-grid scanlines
                    // but this is probably too much for the RP2350:
#else
                    // Horizontal scanlines: darken every other line
                    if (scanlineMode == 1 && (load_line & 1))
                    {
                        int r = (c >> 10) & 0x1F;
                        int g = (c >> 5) & 0x1F;
                        int b = c & 0x1F;
                        r = r >> 1;
                        g = g >> 1;
                        b = b >> 1;
                        c = (r << 10) | (g << 5) | b;
                    }
                    // Vertical scanlines: darken every other pixel column
                    else if (scanlineMode == 2 && (i & 1))
                    {
                        int r = (c >> 10) & 0x1F;
                        int g = (c >> 5) & 0x1F;
                        int b = c & 0x1F;
                        r = r >> 1;
                        g = g >> 1;
                        b = b >> 1;
                        c = (r << 10) | (g << 5) | b;
                    }
                    // Pixel-grid: darken every other line and every other pixel column
                    else if (scanlineMode == 3 && ((load_line & 1) || (i & 1)))
                    {
                        int r = (c >> 10) & 0x1F;
                        int g = (c >> 5) & 0x1F;
                        int b = c & 0x1F;
                        r = r >> 1;
                        g = g >> 1;
                        b = b >> 1;
                        c = (r << 10) | (g << 5) | b;
                    }

#endif
                    *p++ = c;
                    *p++ = c;
                }
            }
        }
    }
}

/// @brief Initialize the HSTX driver
/// This function sets up the HSTX controller, configures the DMA channels,
/// and launches the HSTX core on the second core of the RP2350.
/// It also initializes the GPIO pins used for HSTX output.
void hstx_init()
{
#if 0  
    // Messes up stdio, so we need to reinitialize it. Also breaks SDcard access. Not sure this is needed anyhow
    clockspeed = clock_get_hz(clk_sys) / 1000; // Get current clock speed in kHz
    clock_configure(
        clk_peri,
        0,                                                // No glitchless mux
        CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, // System PLL on AUX mux
        clockspeed * 1000,                                // Input frequency
        clockspeed * 1000                                 // Output (must be same as no divider)
    );

    // The above settings mess up the stdio, so we need to reinitialize it
    // This is a workaround to avoid stdio issues after changing the clock
    // settings.
    stdio_deinit_all();
    stdio_init_all();
#endif
    clock_configure(
        clk_hstx,
        0,                                                // No glitchless mux
        CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, // System PLL on AUX mux
        150000 * 1000,                                    // Input frequency
        150000 / clockdivisor * 1000                      // Output (must be same as no divider)
    );
    multicore_launch_core1_with_stack(HSTXCore, core1stack, 512);
    core1stack[0] = 0x12345678;
    printf("HSTX initialized\n");
    // is a second framebuffer possible?
 #if 0
    uint8_t *fb2 = malloc((MODE_H_ACTIVE_PIXELS / 2) * (MODE_V_ACTIVE_LINES / 2) * 2);
    printf("Address of fb2: %p\n", fb2);
    memset(fb2, 0, (MODE_H_ACTIVE_PIXELS / 2) * (MODE_V_ACTIVE_LINES / 2) * 2);
 
    // uint8_t *fb3 = malloc((MODE_H_ACTIVE_PIXELS / 2) * (MODE_V_ACTIVE_LINES / 2) * 2);
    // printf("Address of fb3: %p\n", fb3);
    // memset(fb3, 0, (MODE_H_ACTIVE_PIXELS / 2) * (MODE_V_ACTIVE_LINES / 2) * 2);
    //  uint8_t *fb4 = malloc((MODE_H_ACTIVE_PIXELS / 2) * (MODE_V_ACTIVE_LINES / 2) * 2);
    // printf("Address of fb4: %p\n", fb4);
    // memset(fb4, 0, (MODE_H_ACTIVE_PIXELS / 2) * (MODE_V_ACTIVE_LINES / 2) * 2);
#endif

}

/// @brief Get a pointer to the framebuffer line for a specific scanline
/// @param scanline The scanline number (0-based)
/// @return Pointer to the framebuffer line data
uint16_t *__not_in_flash_func(hstx_getlineFromFramebuffer)(int scanline)
{
#if 0
    if ( v_scanline & 1 ) {
        // Wait until the scanline is ready
        while(scanline == (v_scanline >> 1)) {
            tight_loop_contents(); 
        }
    }
#endif

    // was return (uint16_t *)((uint8_t *)(WriteBuf+((scanline*HRes)*2)));
    return (uint16_t *)(WriteBuf + (scanline * HRes * 2));
}

/// @brief Get the current frame counter
/// @return the current frame counter
uint32_t hstx_getframecounter(void)
{
    // Return the current frame counter
    return frame_counter;
}

/// @brief Wait for the vertical sync signal
/// @param
void hstx_waitForVSync(void)
{
    while (vblank)
    {
        tight_loop_contents();
    }
    while (!vblank)
    {
        tight_loop_contents();
    }
}

/// @brief Clear the screen with a specific color
/// @param color The color to fill the screen with
void hstx_clearScreen(uint16_t color)
{
    // Clear the framebuffer with a specific color
    for (int i = 0; i < (HRes * VRes); i++)
    {
        ((uint16_t *)WriteBuf)[i] = color;
    }
}
/// @brief Enable or disable scanlines effect
/// @param enable 1 to enable scanlines, 0 to disable
void hstx_setScanLines(int enable)
{
    // Set the scanlines effect flag
    enableScanLines = enable ? 1 : 0;
}
uint8_t *hstx_getframebuffer(void)
{
    // Return the pointer to the framebuffer
    return WriteBuf;
}
#endif // PICO_RP2350

// End of hstx.c