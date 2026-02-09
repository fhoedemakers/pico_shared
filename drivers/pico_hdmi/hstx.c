#if PICO_RP2350
#include "hstx.h"
#include "pico/multicore.h" 
#include "stdio.h"
// Custom changes
volatile bool HSTX_vblank = false;
static uint8_t FRAMEBUFFER[(MODE_H_ACTIVE_PIXELS / 2) * (MODE_V_ACTIVE_LINES / 2) * 2];
volatile bool audioPlaying = false;
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
#if 1
    while (HSTX_vblank)
    {
        tight_loop_contents();
    }
    while (!HSTX_vblank)
    {
        tight_loop_contents();
    }
#endif
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
    uint16_t *p = (uint16_t *)buff;
    int last_line = 2, load_line, line_to_load, Line_dup;
    load_line = v_scanline - (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES);
    Line_dup = load_line >> 1;
    if (load_line >= 0 && load_line < MODE_V_ACTIVE_LINES)
    {
        HSTX_vblank = false;
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

extern volatile uint32_t video_frame_count;
uint32_t hstx_getframecounter(void)
{
    return video_frame_count;
}

static int g_hdmi_audio_frame_counter = 0;
static void generate_silence(void)
{
    while (!audioPlaying)
    {

        // Keep the audio queue fed
        while (hstx_di_queue_get_level() < 200)
        {
            audio_sample_t samples[4];
            for (int i = 0; i < 4; i++)
            {
                samples[i].left = 0;
                samples[i].right = 0;
            }

            hstx_packet_t packet;
            g_hdmi_audio_frame_counter = hstx_packet_set_audio_samples(&packet, samples, 4, g_hdmi_audio_frame_counter);

            hstx_data_island_t island;
            hstx_encode_data_island(&island, &packet, false, true);
            hstx_di_queue_push(&island);
        }
    }
}
void __not_in_flash_func(hstx_push_audio_sample)(const int left, const int right)
{
    static audio_sample_t acc_buf[4];
    static int acc_count = 0;
  
    acc_buf[acc_count].left = left;
    acc_buf[acc_count].right = right;
    acc_count++;
    audioPlaying = true;
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
void hstx_mute_audio(void)
{
    audioPlaying = false;
    printf("HSTX audio muted.\n");
}

void hstx_init(void)
{
    hstx_di_queue_init();
    hstx_mute_audio();
    video_output_set_vsync_callback(hstx_vsync_callbackfunc);
    video_output_set_background_task(generate_silence);
    //video_output_set_dvi_mode(true);
    video_output_init(640, 480);
    pico_hdmi_set_audio_sample_rate(44100);
    video_output_set_scanline_callback(scanline_callbackfunc);
    multicore_launch_core1(video_output_core1_run);
    printf("Pico HDMI initialized.\n");
}
#endif