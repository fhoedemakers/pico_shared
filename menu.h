#ifndef ROMSELECT
#define ROMSELECT
#include <stdint.h>
#include <stddef.h>
#define SWVERSION "VX.X"

#if PICO_RP2350
#if __riscv
#define PICOHWNAME_ "rp2350-riscv"
#else
#define PICOHWNAME_ "rp2350-arm"
#endif
#else
#define PICOHWNAME_ "rp2040"
#endif

#define SCREEN_COLS 40
#define SCREEN_ROWS 30

#define STARTROW 3
#define ENDROW (SCREEN_ROWS - 5)
#define PAGESIZE (ENDROW - STARTROW + 1)

#define VISIBLEPATHSIZE (SCREEN_COLS - 3)   
struct charCell
{
    uint8_t fgcolor;
    uint8_t bgcolor;
    char charvalue;
};
extern charCell *screenBuffer;
#define screenbufferSize  (sizeof(charCell) * SCREEN_COLS * SCREEN_ROWS)
void menu(const char *title, char *errorMessage, bool isFatalError, bool showSplash, const char *allowedExtensions, char *rompath, const char *emulatorType = nullptr);
void ClearScreen(int color);
void putText(int x, int y, const char *text, int fgcolor, int bgcolor, bool wraplines = false, int offset = 0);
void splash();  // is emulator specific
int showSettingsMenu(bool allocateScreenBuffer = false);

#endif