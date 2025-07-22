#ifndef PICO_HSTX_H
#define PICO_HSTX_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

#define MODE_H_ACTIVE_PIXELS 640
#define MODE_V_ACTIVE_LINES 480

extern uint8_t FRAMEBUFFER[(MODE_H_ACTIVE_PIXELS/2)*(MODE_V_ACTIVE_LINES/2)*2];

void hstx_init(uint8_t *buf1, uint8_t *buf2);
uint16_t *hstx_getlineFromFramebuffer(int scanline);
uint32_t hstx_getframecounter(void);
void hstx_waitForVSync(void);
void hstx_clearScreen(uint16_t color);
#ifdef __cplusplus
}
#endif
#endif // PICO_HSTX_H