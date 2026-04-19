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
static volatile int enableScanLines = 0; // Enable scanlines
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

uint16_t *__not_in_flash_func(hstx_getlineFromFramebuffer)(int scanline)
{
    return (uint16_t *)(WriteBuf + (scanline * HRes * 2));
}
void __not_in_flash_func(hstx_vsync_callbackfunc)(void)
{
   HSTX_vblank = true;
}
void __not_in_flash_func(scanline_callbackfunc)(uint32_t v_scanline, uint32_t active_line, uint32_t *buff)
{
    int load_line = v_scanline - (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES);
    if (load_line < 0 || load_line >= MODE_V_ACTIVE_LINES)
        return;

    HSTX_vblank = false;
    __dmb();

    // Source row (320 pixels RGB555 = 640 bytes = 160 uint32_t) with vertical
    // line doubling (two output scanlines share one framebuffer row).
    int Line_dup = load_line >> 1;
    const uint32_t *src = (const uint32_t *)&DisplayBuf[Line_dup * MODE_H_ACTIVE_PIXELS];
    uint32_t *dst = buff;
    const uint32_t iters = MODE_H_ACTIVE_PIXELS / 4; // 2 src pixels in, 4 dst pixels out

    // Darken-both-halves mask for a 32-bit word holding two RGB555 pixels.
    // Per 5-bit component, divide-by-two is (c & 0x1E) >> 1, i.e. mask off
    // the LSB and shift. Applied to both pixels in parallel with one AND+shift.
    //   r: bits 10..14   mask 0x7800 -> 0x7800 with LSB cleared = 0x7800
    //   g: bits  5.. 9   mask 0x03E0 -> LSB cleared             = 0x03C0
    //   b: bits  0.. 4   mask 0x001F -> LSB cleared             = 0x001E
    // Combined per pixel = 0x7BDE; packed twice = 0x7BDE7BDE.
    // (Faster than a 32-entry lookup table: no memory access, single cycle.)
    const uint32_t DARKEN_MASK = 0x7BDE7BDEu;

    if ((load_line & 1) && enableScanLines) {
        for (uint32_t i = 0; i < iters; i++) {
            uint32_t pair = (src[i] & DARKEN_MASK) >> 1; // darken both pixels
            uint32_t px0 = pair & 0xFFFFu;
            uint32_t px1 = pair >> 16;
            *dst++ = px0 | (px0 << 16); // duplicate pixel 0
            *dst++ = px1 | (px1 << 16); // duplicate pixel 1
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