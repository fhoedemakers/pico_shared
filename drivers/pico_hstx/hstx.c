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
#define MODE_H_FRONT_PORCH   16
#define MODE_H_SYNC_WIDTH    64
#define MODE_H_BACK_PORCH    120

#define MODE_V_SYNC_POLARITY 0
#define MODE_V_ACTIVE_LINES 480
#define MODE_V_FRONT_PORCH   1
#define MODE_V_SYNC_WIDTH    3
#define MODE_V_BACK_PORCH    16
//#define clockspeed 252000 // 315000
int clockspeed;
#define clockdivisor 2
int X_TILE=80, Y_TILE=40;
uint8_t FRAMEBUFFER[(MODE_H_ACTIVE_PIXELS/2)*(MODE_V_ACTIVE_LINES/2)*2];
uint16_t ALIGNED HDMIlines[2][MODE_H_ACTIVE_PIXELS]={0};
uint8_t *WriteBuf=FRAMEBUFFER;
uint8_t *DisplayBuf=FRAMEBUFFER;
uint8_t *LayerBuf=FRAMEBUFFER;
uint16_t *tilefcols; 
uint16_t *tilebcols;
volatile int HRes;
volatile int VRes;
#define MODE_H_TOTAL_PIXELS ( \
    MODE_H_FRONT_PORCH + MODE_H_SYNC_WIDTH + \
    MODE_H_BACK_PORCH  + MODE_H_ACTIVE_PIXELS \
)
#define MODE_V_TOTAL_LINES  ( \
    MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + \
    MODE_V_BACK_PORCH  + MODE_V_ACTIVE_LINES \
)
volatile int HDMImode = 0;
#define HSTX_CMD_RAW         (0x0u << 12)
#define HSTX_CMD_RAW_REPEAT  (0x1u << 12)
#define HSTX_CMD_TMDS        (0x2u << 12)
#define HSTX_CMD_TMDS_REPEAT (0x3u << 12)
#define HSTX_CMD_NOP         (0xfu << 12)
#define SCREENMODE1         26
#define SCREENMODE2       27
#define SCREENMODE3       28
#define SCREENMODE4       29
#define SCREENMODE5       30
#define SCREENMODE6       31
void hstx_init(void)
{
    clockspeed = clock_get_hz(clk_sys) / 1000; // Get current clock speed in kHz

    set_sys_clock_khz(clockspeed, false);
    clock_configure(
        clk_peri,
        0,                                                // No glitchless mux
        CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, // System PLL on AUX mux
        clockspeed * 1000,                               // Input frequency
        clockspeed * 1000                                // Output (must be same as no divider)
    );
    clock_configure(
        clk_hstx,
        0,                                                // No glitchless mux
        CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, // System PLL on AUX mux
        clockspeed * 1000,                               // Input frequency
        clockspeed/clockdivisor * 1000                                // Output (must be same as no divider)
    );
}