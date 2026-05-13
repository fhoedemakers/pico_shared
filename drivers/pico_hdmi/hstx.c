#if PICO_RP2350
#include "hstx.h"
#include "pico/multicore.h" 
#include "stdio.h"
// Custom changes
volatile bool HSTX_vblank = false;
static uint8_t FRAMEBUFFER[(MODE_H_ACTIVE_PIXELS / 2) * (MODE_V_ACTIVE_LINES / 2) * 2] __attribute__((aligned(4)));
// uint16_t ALIGNED HDMIlines[2][MODE_H_ACTIVE_PIXELS] = {0};
static uint8_t *WriteBuf = FRAMEBUFFER;
static uint8_t *DisplayBuf = FRAMEBUFFER;
static uint8_t *LayerBuf = FRAMEBUFFER;
static uint16_t *tilefcols;
static uint16_t *tilebcols;
static volatile int enableScanLines = 0;
static volatile int enableAspectRatio87 = 0;
static volatile int scanlineType = 0;
static volatile int scanlineMode = 0;
#define HRes (MODE_H_ACTIVE_PIXELS / 2) // 320
#define VRes (MODE_V_ACTIVE_LINES / 2)  // 240
void hstx_waitForVSync(void)
{
    // Wait until the frame counter advances, indicating a new vsync edge.
    // video_frame_count is incremented exactly once per frame in the DMA ISR
    // on core1 (video_output.c), so this guarantees one-frame synchronization
    // without the race conditions of a bool flag approach.
    uint32_t current = video_frame_count;
    while (video_frame_count == current)
    {
        tight_loop_contents();
    }
}

void hstx_paceFrame(bool init)
{
    // Slack-aware 60fps pacing. Waits for the next vsync only if we haven't
    // already passed our target frame. If the caller overran a frame, resync
    // to the current counter instead of stalling another ~16.6ms, which would
    // otherwise harmonic-lock the loop to 30fps.
    static uint32_t target_frame = 0;
    if (init)
    {
        target_frame = video_frame_count;
    }
    target_frame++;
    uint32_t current = video_frame_count;
    if ((int32_t)(current - target_frame) < 0)
    {
        while (video_frame_count != target_frame)
        {
            tight_loop_contents();
        }
    }
    else
    {
        target_frame = current;
    }
}
uint8_t *hstx_getframebuffer(void)
{
    // Return the pointer to the framebuffer
    return WriteBuf;
}

void hstx_setScanLines(int enable)
{
    // Set the scanlines effect flag
    enableScanLines = enable ? 1 : 0;
}

void hstx_setAspectRatio87(int enable)
{
    enableAspectRatio87 = enable ? 1 : 0;
}

void hstx_setScanLineType(int type)
{
    scanlineType = type;
}

uint16_t *__not_in_flash_func(hstx_getlineFromFramebuffer)(int scanline)
{
    return (uint16_t *)(WriteBuf + (scanline * HRes * 2));
}
void __not_in_flash_func(hstx_vsync_callbackfunc)(void)
{
   HSTX_vblank = true;
}

/// Per-scanline callback invoked by the HSTX video output on core 1.
/// Converts one row of the 320x240 RGB555 framebuffer into a 640-pixel
/// output scanline in `buff`, applying vertical line-doubling, optional
/// scanline darkening, and either 1:1 or 8:7 horizontal scaling.
///
/// Framebuffer layout : 320 x 240 pixels, 16 bpp (RGB555), row stride =
///                      MODE_H_ACTIVE_PIXELS bytes (640).
/// Output             : 640 x 480, each framebuffer row used twice
///                      (load_line >> 1 maps two output lines to one row).
///
/// Darkening math     : halve every RGB555 channel in one step by masking
///                      out the per-channel LSB and shifting right:
///                        dark(px) = (px & 0x7BDE) >> 1
///                      For a pair of pixels packed in a uint32_t:
///                        dark(pair) = (pair & 0x7BDE7BDE) >> 1
///
/// Scanline effects (active only when enableScanLines != 0):
///   stype 0 (Simple) : darken the entire odd output line by 50 %.
///   stype 1 (LCD)    : darken every other *output column* (the high
///                       half of each uint32_t word) on all lines;
///                       odd lines are additionally darkened by 50 %
///                       before the column effect is applied.
///
/// Two horizontal scaling paths:
///
/// -- 8:7 PAR (enableAspectRatio87) --
///   Source  : 252 pixels starting at offset 34 (clips 2 px overscan each
///             side of the NES 256-pixel output centred at column 32).
///   Output  : 32 px black + 576 px scaled + 32 px black = 640.
///
///   Bresenham-style integer scaling stretches 7 source pixels into 16
///   output pixels (ratio 16/7).  Each pixel needs at least 2 copies
///   (7 x 2 = 14); the 2 remaining slots are distributed as evenly as
///   possible, giving the pattern [2, 2, 3, 2, 2, 3, 2]:
///
///     p0 p0 | p1 p1 | p2 p2 p2 | p3 p3 | p4 p4 | p5 p5 p5 | p6 p6
///      2       2        3          2        2        3          2   = 16
///
///   Each uint32_t word packs two output pixels (low + high half):
///     word 0: p0 | p0    word 1: p1 | p1    word 2: p2 | p2
///     word 3: p2 | p3    word 4: p3 | p4    word 5: p4 | p5
///     word 6: p5 | p5    word 7: p6 | p6
///
///   This repeats 36 times (36 x 7 = 252 src, 36 x 16 = 576 dst).
///
/// -- 1:1 pixel doubling --
///   Each source pixel is written twice (low and high half of a uint32_t).
///   320 source pixels -> 640 output pixels, 160 loop iterations (reading
///   two source pixels per uint32_t).
void __not_in_flash_func(scanline_callbackfunc)(uint32_t v_scanline, uint32_t active_line, uint32_t *buff)
{
    int load_line = v_scanline - (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES);
    if (load_line < 0 || load_line >= MODE_V_ACTIVE_LINES)
        return;

    HSTX_vblank = false;
    __dmb();

    int Line_dup = load_line >> 1;
    const uint32_t DARKEN_MASK = 0x7BDE7BDEu;
    const uint16_t DM = 0x7BDEu;
    int is_odd_line = load_line & 1;
    int do_scanline = is_odd_line && enableScanLines;
    int stype = enableScanLines ? scanlineType : 0;

    if (enableAspectRatio87) {
        const uint16_t *sp = (const uint16_t *)&DisplayBuf[Line_dup * MODE_H_ACTIVE_PIXELS] + 34;
        uint32_t *dp = buff;

        // 32 px left black border (16 uint32_t words)
        for (int i = 0; i < 16; i++)
            *dp++ = 0;

        if (stype == 1) {
            for (int g = 0; g < 36; g++) {
                uint32_t p0=*sp++,p1=*sp++,p2=*sp++,p3=*sp++,p4=*sp++,p5=*sp++,p6=*sp++;
                if (do_scanline) {
                    p0=(p0&DM)>>1; p1=(p1&DM)>>1; p2=(p2&DM)>>1; p3=(p3&DM)>>1;
                    p4=(p4&DM)>>1; p5=(p5&DM)>>1; p6=(p6&DM)>>1;
                }
                uint32_t d0=(p0&DM)>>1,d1=(p1&DM)>>1,d2=(p2&DM)>>1,d3=(p3&DM)>>1;
                uint32_t d4=(p4&DM)>>1,d5=(p5&DM)>>1,d6=(p6&DM)>>1;
                // [2,2,2,3,2,2,3] pattern: normal pixel in low half, darkened in high half
                *dp++=p0|(d0<<16); *dp++=p1|(d1<<16);
                *dp++=p2|(d2<<16); *dp++=p2|(d3<<16);
                *dp++=p3|(d4<<16); *dp++=p4|(d5<<16);
                *dp++=p5|(d5<<16); *dp++=p6|(d6<<16);
            }
        } else if (do_scanline) {
            for (int g = 0; g < 36; g++) {
                uint32_t p0=(*sp++&DM)>>1,p1=(*sp++&DM)>>1,p2=(*sp++&DM)>>1;
                uint32_t p3=(*sp++&DM)>>1,p4=(*sp++&DM)>>1,p5=(*sp++&DM)>>1;
                uint32_t p6=(*sp++&DM)>>1;
                *dp++=p0|(p0<<16); *dp++=p1|(p1<<16);
                *dp++=p2|(p2<<16); *dp++=p2|(p3<<16);
                *dp++=p3|(p4<<16); *dp++=p4|(p5<<16);
                *dp++=p5|(p5<<16); *dp++=p6|(p6<<16);
            }
        } else {
            for (int g = 0; g < 36; g++) {
                uint32_t p0=*sp++,p1=*sp++,p2=*sp++,p3=*sp++,p4=*sp++,p5=*sp++,p6=*sp++;
                *dp++=p0|(p0<<16); *dp++=p1|(p1<<16);
                *dp++=p2|(p2<<16); *dp++=p2|(p3<<16);
                *dp++=p3|(p4<<16); *dp++=p4|(p5<<16);
                *dp++=p5|(p5<<16); *dp++=p6|(p6<<16);
            }
        }

        // 32 px right black border
        for (int i = 0; i < 16; i++)
            *dp++ = 0;
    } else {
        const uint32_t *src = (const uint32_t *)&DisplayBuf[Line_dup * MODE_H_ACTIVE_PIXELS];
        uint32_t *dst = buff;
        const uint32_t iters = MODE_H_ACTIVE_PIXELS / 4; // 160

        if (stype == 1) {
            for (uint32_t i = 0; i < iters; i++) {
                uint32_t pair = src[i];
                if (do_scanline) pair = (pair & DARKEN_MASK) >> 1;
                uint32_t px0 = pair & 0xFFFFu;
                uint32_t px1 = pair >> 16;
                // normal pixel in low half, darkened copy in high half
                *dst++ = px0 | (((px0 & DM) >> 1) << 16);
                *dst++ = px1 | (((px1 & DM) >> 1) << 16);
            }
        } else if (do_scanline) {
            for (uint32_t i = 0; i < iters; i++) {
                uint32_t pair = (src[i] & DARKEN_MASK) >> 1;
                uint32_t px0 = pair & 0xFFFFu;
                uint32_t px1 = pair >> 16;
                *dst++ = px0 | (px0 << 16);
                *dst++ = px1 | (px1 << 16);
            }
        } else {
            for (uint32_t i = 0; i < iters; i++) {
                uint32_t pair = src[i];
                uint32_t px0 = pair & 0xFFFFu;
                uint32_t px1 = pair >> 16;
                *dst++ = px0 | (px0 << 16);
                *dst++ = px1 | (px1 << 16);
            }
        }
    }
}

extern volatile uint32_t video_frame_count;
uint32_t hstx_getframecounter(void)
{
    return video_frame_count;
}

void __not_in_flash_func(hstx_push_audio_sample)(const int left, const int right)
{
    static int g_hdmi_audio_frame_counter = 0;
    static audio_sample_t acc_buf[4];
    static int acc_count = 0;
    static  uint32_t video_frame_count = 0;
    acc_buf[acc_count].left = left;
    acc_buf[acc_count].right = right;
    acc_count++;
    if (acc_count == 4)
    {
        if (hstx_di_queue_get_level() >= HSTX_AUDIO_DI_HIGH_WATERMARK)
        {
            acc_count = 0;
            return;
        }
        hstx_packet_t packet;
        g_hdmi_audio_frame_counter = hstx_packet_set_audio_samples(&packet, acc_buf, 4, g_hdmi_audio_frame_counter);

        hstx_data_island_t island;
        hstx_encode_data_island(&island, &packet, false, true);
        (void)hstx_di_queue_push(&island);
        acc_count = 0;
    }
}

void hstx_init(bool dviOnly)
{
    video_output_set_dvi_mode(dviOnly);
    hstx_di_queue_init();
    video_output_set_vsync_callback(hstx_vsync_callbackfunc);   
    video_output_init(640, 480);
    pico_hdmi_set_audio_sample_rate(44100);
    video_output_set_scanline_callback(scanline_callbackfunc);
    multicore_launch_core1(video_output_core1_run);
    printf("Pico HDMI initialized.\n");
}
#endif