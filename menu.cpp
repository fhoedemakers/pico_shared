#include <stdio.h>
#include <string.h>
#include <memory>
#include "pico.h"
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "hardware/divider.h"
#include "tusb.h"

#include "dvi/dvi.h"
#include "util/exclusive_proc.h"
#include "FrensFonts.h"
#include "FrensHelpers.h"
#include "gamepad.h"
#include "RomLister.h"
#include "menu.h"
#include "nespad.h"
#include "wiipad.h"

#include "font_8x8.h"
#include "settings.h"
#include "ffwrappers.h"
#define CC(x) (((x >> 1) & 15) | (((x >> 6) & 15) << 4) | (((x >> 11) & 15) << 8))
const __UINT16_TYPE__ NesMenuPalette[64] = {
    CC(0x39ce), CC(0x1071), CC(0x0015), CC(0x2013), CC(0x440e), CC(0x5402), CC(0x5000), CC(0x3c20),
    CC(0x20a0), CC(0x0100), CC(0x0140), CC(0x00e2), CC(0x0ceb), CC(0x0000), CC(0x0000), CC(0x0000),
    CC(0x5ef7), CC(0x01dd), CC(0x10fd), CC(0x401e), CC(0x5c17), CC(0x700b), CC(0x6ca0), CC(0x6521),
    CC(0x45c0), CC(0x0240), CC(0x02a0), CC(0x0247), CC(0x0211), CC(0x0000), CC(0x0000), CC(0x0000),
    CC(0x7fff), CC(0x1eff), CC(0x2e5f), CC(0x223f), CC(0x79ff), CC(0x7dd6), CC(0x7dcc), CC(0x7e67),
    CC(0x7ae7), CC(0x4342), CC(0x2769), CC(0x2ff3), CC(0x03bb), CC(0x0000), CC(0x0000), CC(0x0000),
    CC(0x7fff), CC(0x579f), CC(0x635f), CC(0x6b3f), CC(0x7f1f), CC(0x7f1b), CC(0x7ef6), CC(0x7f75),
    CC(0x7f94), CC(0x73f4), CC(0x57d7), CC(0x5bf9), CC(0x4ffe), CC(0x0000), CC(0x0000), CC(0x0000)};
int NesMenuPaletteItems = sizeof(NesMenuPalette) / sizeof(NesMenuPalette[0]);

static char connectedGamePadName[sizeof(io::GamePadState::GamePadName)];
static bool useFrameBuffer = false;
#define SCREENBUFCELLS SCREEN_ROWS *SCREEN_COLS
charCell *screenBuffer;

#define LONG_PRESS_TRESHOLD (500)
#define REPEAT_DELAY (40)

static WORD *WorkLineRom = nullptr;
static BYTE *WorkLineRom8 = nullptr;

void RomSelect_SetLineBuffer(WORD *p, WORD size)
{
    WorkLineRom = p;
}

static constexpr int LEFT = 1 << 6;
static constexpr int RIGHT = 1 << 7;
static constexpr int UP = 1 << 4;
static constexpr int DOWN = 1 << 5;
static constexpr int SELECT = 1 << 2;
static constexpr int START = 1 << 3;
static constexpr int A = 1 << 0;
static constexpr int B = 1 << 1;
static constexpr int X = 1 << 8;
static constexpr int Y = 1 << 9;

void resetColors(int prevfgColor, int prevbgColor)
{
    for (auto i = 0; i < SCREENBUFCELLS; i++)
    {
        if (screenBuffer[i].fgcolor == prevfgColor)
        {
            screenBuffer[i].fgcolor = settings.fgcolor;
        }
        if (screenBuffer[i].bgcolor == prevbgColor)
        {
            screenBuffer[i].bgcolor = settings.bgcolor;
        }
    }
}

int Menu_LoadFrame()
{
#if NES_PIN_CLK != -1
    nespad_read_start();
#endif
    auto count = dvi_->getFrameCounter();
    auto onOff = hw_divider_s32_quotient_inlined(count, 60) & 1;
    Frens::blinkLed(onOff);
#if NES_PIN_CLK != -1
    nespad_read_finish(); // Sets global nespad_state var
#endif
    tuh_task();
    if (Frens::isFrameBufferUsed())
    {
        Frens::markFrameReadyForReendering(true);
    }
    return count;
}

bool resetScreenSaver = false;

void RomSelect_PadState(DWORD *pdwPad1, bool ignorepushed = false)
{
    static uint32_t longpressTreshold = 0;
    static uint32_t previousTime = Frens::time_ms();
    uint32_t currentTime = Frens::time_ms();
    uint32_t delta;
    int prevBgColor = settings.bgcolor;
    int prevFgColor = settings.fgcolor;
    static DWORD prevButtons{};
    auto &gp = io::getCurrentGamePadState(0);
    strcpy(connectedGamePadName, gp.GamePadName);

    int v = (gp.buttons & io::GamePadState::Button::LEFT ? LEFT : 0) |
            (gp.buttons & io::GamePadState::Button::RIGHT ? RIGHT : 0) |
            (gp.buttons & io::GamePadState::Button::UP ? UP : 0) |
            (gp.buttons & io::GamePadState::Button::DOWN ? DOWN : 0) |
            (gp.buttons & io::GamePadState::Button::A ? A : 0) |
            (gp.buttons & io::GamePadState::Button::B ? B : 0) |
            (gp.buttons & io::GamePadState::Button::SELECT ? SELECT : 0) |
            (gp.buttons & io::GamePadState::Button::START ? START : 0) |
            (gp.buttons & io::GamePadState::Button::X ? X : 0) |
            (gp.buttons & io::GamePadState::Button::Y ? Y : 0) |
            0;

#if NES_PIN_CLK != -1
    v |= nespad_states[0];
#endif
#if NES_PIN_CLK_1 != -1
    v |= nespad_states[1];
#endif
#if WII_PIN_SDA >= 0 and WII_PIN_SCL >= 0
    v |= wiipad_read();
#endif
    delta = currentTime - previousTime;
    previousTime = currentTime;
    if (v & (UP | DOWN | LEFT | RIGHT))
    {
        longpressTreshold += delta;
    }
    else
    {
        longpressTreshold = 0;
    }

    *pdwPad1 = 0;

    unsigned long pushed;
    auto p1 = v;
    if (ignorepushed == false)
    {
        pushed = v & ~prevButtons;
    }
    else
    {
        pushed = v;
    }
    if (p1 & SELECT)
    {
        resetScreenSaver = true;
        if (pushed & UP)
        {
            settings.fgcolor++;
            if (settings.fgcolor >= NesMenuPaletteItems)
            {
                settings.fgcolor = 0;
            }
            printf("fgcolor: %d\n", settings.fgcolor);
            resetColors(prevFgColor, prevBgColor);
        }
        else if (pushed & DOWN)
        {
            settings.fgcolor--;
            if (settings.fgcolor < 0)
            {
                settings.fgcolor = NesMenuPaletteItems - 1;
            }
            printf("fgcolor: %d\n", settings.fgcolor);
            resetColors(prevFgColor, prevBgColor);
        }
        else if (pushed & LEFT)
        {
            settings.bgcolor++;
            if (settings.bgcolor >= NesMenuPaletteItems)
            {
                settings.bgcolor = 0;
            }
            printf("bgcolor: %d\n", settings.bgcolor);
            resetColors(prevFgColor, prevBgColor);
        }
        else if (pushed & RIGHT)
        {
            settings.bgcolor--;
            if (settings.bgcolor < 0)
            {
                settings.bgcolor = NesMenuPaletteItems - 1;
            }
            printf("bgcolor: %d\n", settings.bgcolor);
            resetColors(prevFgColor, prevBgColor);
        }
        else if (pushed & A)
        {
            printf("Saving colors to settings file.\n");
            Frens::savesettings();
        }
        else if (pushed & B)
        {
            printf("Resetting colors to default.\n");
            // reset colors to default
            settings.fgcolor = DEFAULT_FGCOLOR;
            settings.bgcolor = DEFAULT_BGCOLOR;
            resetColors(prevFgColor, prevBgColor);
            Frens::savesettings();
        }

        v = 0;
    }

    if (pushed || longpressTreshold > LONG_PRESS_TRESHOLD)
    {
        if (!pushed)
        {
            if (longpressTreshold > LONG_PRESS_TRESHOLD)
            {
                longpressTreshold = LONG_PRESS_TRESHOLD - REPEAT_DELAY;
            }
        }
        *pdwPad1 = v;
        if (v != 0)
        {
            resetScreenSaver = true;
        }
    }
    prevButtons = p1;
}
void RomSelect_DrawLine(int line, int selectedRow)
{
    WORD fgcolor, bgcolor;
    bool useFrameBuffer = Frens::isFrameBufferUsed();
    if (useFrameBuffer)
    {
        memset(WorkLineRom8, 0, SCREENWIDTH);
    }
    else
    {
        memset(WorkLineRom, 0, SCREENWIDTH * sizeof(WORD));
    }

    for (auto i = 0; i < SCREEN_COLS; ++i)
    {
        int charIndex = i + line / FONT_CHAR_HEIGHT * SCREEN_COLS;
        int row = charIndex / SCREEN_COLS;
        uint c = screenBuffer[charIndex].charvalue;
        if (row == selectedRow)
        {
            if (useFrameBuffer)
            {
                fgcolor = screenBuffer[charIndex].bgcolor;
                bgcolor = screenBuffer[charIndex].fgcolor;
            }
            else
            {
                fgcolor = NesMenuPalette[screenBuffer[charIndex].bgcolor];
                bgcolor = NesMenuPalette[screenBuffer[charIndex].fgcolor];
            }
        }
        else
        {
            if (useFrameBuffer)
            {
                fgcolor = screenBuffer[charIndex].fgcolor;
                bgcolor = screenBuffer[charIndex].bgcolor;
            }
            else
            {
                fgcolor = NesMenuPalette[screenBuffer[charIndex].fgcolor];
                bgcolor = NesMenuPalette[screenBuffer[charIndex].bgcolor];
            }
        }

        int rowInChar = line % FONT_CHAR_HEIGHT;
        char fontSlice = getcharslicefrom8x8font(c, rowInChar); // font_8x8[(c - FONT_FIRST_ASCII) + (rowInChar)*FONT_N_CHARS];
        for (auto bit = 0; bit < 8; bit++)
        {
            if (fontSlice & 1)
            {
                if (useFrameBuffer)
                {
                    *WorkLineRom8 = fgcolor;
                }
                else
                {
                    *WorkLineRom = fgcolor;
                }
            }
            else
            {
                if (useFrameBuffer)
                {
                    *WorkLineRom8 = bgcolor;
                }
                else
                {
                    *WorkLineRom = bgcolor;
                }
            }
            fontSlice >>= 1;
            if (useFrameBuffer)
            {
                WorkLineRom8++;
            }
            else
            {
                WorkLineRom++;
            }
        }
    }
    return;
}

void drawline(int scanline, int selectedRow)
{
    if (Frens::isFrameBufferUsed())
    {
        WorkLineRom8 = &Frens::framebufferCore0[scanline * SCREENWIDTH];
        RomSelect_DrawLine(scanline, selectedRow);
    }
    else
    {
        auto b = dvi_->getLineBuffer();
        WorkLineRom = b->data();
        RomSelect_DrawLine(scanline, selectedRow);
        dvi_->setLineBuffer(scanline, b);
    }
}

void putText(int x, int y, const char *text, int fgcolor, int bgcolor)
{

    if (text != nullptr)
    {
        auto index = y * SCREEN_COLS + x;
        auto maxLen = strlen(text);
        if (strlen(text) > SCREEN_COLS - x)
        {
            maxLen = SCREEN_COLS - x;
        }
        while (index < SCREENBUFCELLS && *text && maxLen > 0)
        {
            screenBuffer[index].charvalue = *text++;
            screenBuffer[index].fgcolor = fgcolor;
            screenBuffer[index].bgcolor = bgcolor;
            index++;
            maxLen--;
        }
    }
}

void DrawScreen(int selectedRow)
{
    const char *spaces = "                   ";
    char tmpstr[sizeof(connectedGamePadName) + 4];
    char s[SCREEN_COLS + 1];
    if (selectedRow != -1)
    {
        putText(SCREEN_COLS / 2 - strlen(spaces) / 2, SCREEN_ROWS - 1, spaces, settings.bgcolor, settings.bgcolor);
        snprintf(tmpstr, sizeof(tmpstr), "- %s -", connectedGamePadName[0] != 0 ? connectedGamePadName : "No USB GamePad");
        putText(SCREEN_COLS / 2 - strlen(tmpstr) / 2, SCREEN_ROWS - 1, tmpstr, CBLUE, CWHITE);
        if (strcmp(connectedGamePadName, "Dual Shock 4") == 0 || strcmp(connectedGamePadName, "Dual Sense") == 0 || strcmp(connectedGamePadName, "PSClassic") == 0)
        {
            strcpy(s, "O Select, X Back");
        }
        else if (strcmp(connectedGamePadName, "XInput") == 0 || strncmp(connectedGamePadName, "Genesis", 7) == 0)
        {
            strcpy(s, "B Select, A Back");
        }
        else if (strcmp(connectedGamePadName, "Keyboard") == 0)
        {
            strcpy(s, "X, Select, Z Back");
        }
        else
        {
            strcpy(s, "A Select, B Back");
        }
        putText(1, ENDROW + 2, s, settings.fgcolor, settings.bgcolor);
    }

    for (auto line = 0; line < 240; line++)
    {
        drawline(line, selectedRow);
    }
}

void ClearScreen(int color)
{
    for (auto i = 0; i < SCREENBUFCELLS; i++)
    {
        screenBuffer[i].bgcolor = color;
        screenBuffer[i].fgcolor = color;
        screenBuffer[i].charvalue = ' ';
    }
}

char *menutitle = nullptr;

void displayRoms(Frens::RomLister romlister, int startIndex)
{
    char buffer[ROMLISTER_MAXPATH + 4];
    char s[SCREEN_COLS + 1];
    auto y = STARTROW;
    auto entries = romlister.GetEntries();
    ClearScreen(settings.bgcolor);
    snprintf(s, sizeof(s), "- %s -", menutitle);
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 0, s, settings.fgcolor, settings.bgcolor);

    strcpy(s, "Choose a rom to play:");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 1, s, settings.fgcolor, settings.bgcolor);
    // strcpy(s, "---------------------");
    // putText(SCREEN_COLS / 2 - strlen(s) / 2, 1, s, fgcolor, bgcolor);

    for (int i = 1; i < SCREEN_COLS - 1; i++)
    {
        putText(i, STARTROW - 1, "-", settings.fgcolor, settings.bgcolor);
    }
    for (int i = 1; i < SCREEN_COLS - 1; i++)
    {
        putText(i, ENDROW + 1, "-", settings.fgcolor, settings.bgcolor);
    }

    // strcpy(s, "A Select, B Back");
    // putText(1, ENDROW + 2, s, settings.fgcolor, settings.bgcolor);
    putText(SCREEN_COLS - strlen(PICOHWNAME_) - 1, ENDROW + 2, PICOHWNAME_, settings.fgcolor, settings.bgcolor);
    putText(SCREEN_COLS - strlen(SWVERSION) - 1, SCREEN_ROWS - 1, SWVERSION, settings.fgcolor, settings.bgcolor);

    // putText(SCREEN_COLS / 2 - strlen(picoType()) / 2, SCREEN_ROWS - 2, picoType(), fgcolor, bgcolor);

    for (auto index = startIndex; index < romlister.Count(); index++)
    {
        if (y <= ENDROW)
        {
            auto info = entries[index];
            if (info.IsDirectory)
            {
                // snprintf(buffer, sizeof(buffer), "D %s", info.Path);
                snprintf(buffer, SCREEN_COLS - 1, "D %s", info.Path);
            }
            else
            {
                // snprintf(buffer, sizeof(buffer), "R %s", info.Path);
                snprintf(buffer, SCREEN_COLS - 1, "R %s", info.Path);
            }

            putText(1, y, buffer, settings.fgcolor, settings.bgcolor);
            y++;
        }
    }
}

void DisplayFatalError(char *error)
{
    ClearScreen(settings.bgcolor);
    putText(0, 0, "Fatal error:", settings.fgcolor, settings.bgcolor);
    putText(1, 3, error, settings.fgcolor, settings.bgcolor);
    while (true)
    {
        auto frameCount = Menu_LoadFrame();
        DrawScreen(-1);
    }
}

void DisplayEmulatorErrorMessage(char *error)
{
    DWORD PAD1_Latch;
    ClearScreen(settings.bgcolor);
    putText(0, 0, "Error occured:", settings.fgcolor, settings.bgcolor);
    putText(0, 3, error, settings.fgcolor, settings.bgcolor);
    putText(0, ENDROW, "Press a button to continue.", settings.fgcolor, settings.bgcolor);
    while (true)
    {
        auto frameCount = Menu_LoadFrame();
        DrawScreen(-1);
        RomSelect_PadState(&PAD1_Latch);
        if (PAD1_Latch > 0)
        {
            return;
        }
    }
}

void showSplashScreen()
{
    DWORD PAD1_Latch;
    splash();
    int startFrame = -1;
    while (true)
    {
        DrawScreen(-1);
        auto frameCount = Menu_LoadFrame();
        if (startFrame == -1)
        {
            startFrame = frameCount;
        }
        RomSelect_PadState(&PAD1_Latch);
        if (PAD1_Latch > 0 || (frameCount - startFrame) > 1000)
        {
            return;
        }
        if ((frameCount % 30) == 0)
        {
            for (auto i = 0; i < SCREEN_COLS; i++)
            {
                auto col = rand() % 63;
                putText(i, 0, " ", col, col);
                col = rand() % 63;
                putText(i, SCREEN_ROWS - 1, " ", col, col);
            }
            for (auto i = 1; i < SCREEN_ROWS - 1; i++)
            {
                auto col = rand() % 63;
                putText(0, i, " ", col, col);
                col = rand() % 63;
                putText(SCREEN_COLS - 1, i, " ", col, col);
            }
        }
    }
}

void screenSaver()
{
    DWORD PAD1_Latch;
    WORD frameCount;
    while (true)
    {
        frameCount = Menu_LoadFrame();
        DrawScreen(-1);
        RomSelect_PadState(&PAD1_Latch);
        if (PAD1_Latch > 0)
        {
            return;
        }
        if ((frameCount % 3) == 0)
        {
            auto color = rand() % 63;
            auto row = rand() % SCREEN_ROWS;
            auto column = rand() % SCREEN_COLS;
            putText(column, row, " ", color, color);
        }
    }
}

void __not_in_flash_func(processMenuScanLine)(int line, uint8_t *framebuffer, uint16_t *dvibuffer)
{
    auto current_line = &framebuffer[line * SCREENWIDTH];
    for (int kol = 0; kol < SCREENWIDTH; kol += 4)
    {
        dvibuffer[kol] = NesMenuPalette[current_line[kol]];
        dvibuffer[kol + 1] = NesMenuPalette[current_line[kol + 1]];
        dvibuffer[kol + 2] = NesMenuPalette[current_line[kol + 2]];
        dvibuffer[kol + 3] = NesMenuPalette[current_line[kol + 3]];
    }
}

static void showLoadingScreen()
{
    if (Frens::isFrameBufferUsed())
    {
        ClearScreen(settings.bgcolor);
        putText(SCREEN_COLS / 2 - 5, SCREEN_ROWS / 2, "Loading...", settings.fgcolor, settings.bgcolor);
        DrawScreen(-1);
        Menu_LoadFrame();
        DrawScreen(-1);
        Menu_LoadFrame();
    }
}

// Global instances of local vars in romselect() some used in Lambda expression later on
static char *selectedRomOrFolder;
static bool errorInSavingRom = false;
static char *globalErrorMessage;

void menu(const char *title, char *errorMessage, bool isFatal, bool showSplash, const char *allowedExtensions)
{
    int margintop = dvi_->getBlankSettings().top;
    int marginbottom = dvi_->getBlankSettings().bottom;
    // Use the entire screen resolution of 320x240 pixels. This makes a 40x30 screen with 8x8 font possible.
    scaleMode8_7_ = Frens::applyScreenMode(ScreenMode::NOSCANLINE_1_1);
    dvi_->getBlankSettings().top = 0;
    dvi_->getBlankSettings().bottom = 0;

    Frens::SetFrameBufferProcessScanLineFunction(processMenuScanLine);

    abSwapped = 1; // Swap A and B buttons, so menu is consistent accrross different emilators

    //
    menutitle = (char *)title;
    int totalFrames = -1;
    if (settings.selectedRow <= 0)
    {
        settings.selectedRow = STARTROW;
    }
    globalErrorMessage = errorMessage;
    FRESULT fr;
    DWORD PAD1_Latch;

    printf("Starting Menu\n");
    // allocate buffers
    size_t screenbufferSize = sizeof(charCell) * SCREEN_COLS * SCREEN_ROWS;
    printf("Allocating %d bytes for screenbuffer\n", screenbufferSize);
    screenBuffer = (charCell *)malloc(screenbufferSize); // (charCell *)InfoNes_GetRAM(&ramsize);
    size_t directoryContentsBufferSize = 32768;
    printf("Allocating %d bytes for directory contents.\n", directoryContentsBufferSize);
    void *buffer = (void *)malloc(directoryContentsBufferSize); // InfoNes_GetChrBuf(&chr_size);
    Frens::RomLister romlister(buffer, directoryContentsBufferSize, allowedExtensions);

    if (strlen(errorMessage) > 0)
    {
        if (isFatal) // SD card not working, show error
        {
            DisplayFatalError(errorMessage);
        }
        else
        {
            DisplayEmulatorErrorMessage(errorMessage); // Emulator cannot start, show error
        }
        showSplash = false;
    }
    if (showSplash)
    {
        showSplash = false;
        printf("Showing splash screen\n");
        showSplashScreen();
    }
    romlister.list(settings.currentDir);
    displayRoms(romlister, settings.firstVisibleRowINDEX);
    while (1)
    {

        auto frameCount = Menu_LoadFrame();
        auto index = settings.selectedRow - STARTROW + settings.firstVisibleRowINDEX;
        auto entries = romlister.GetEntries();
        selectedRomOrFolder = (romlister.Count() > 0) ? entries[index].Path : nullptr;
        errorInSavingRom = false;
        DrawScreen(settings.selectedRow);
        RomSelect_PadState(&PAD1_Latch);
        if (resetScreenSaver)
        {
            resetScreenSaver = false;
            totalFrames = frameCount;
        }
        if (PAD1_Latch > 0)
        {
            // reset horizontal scroll of highlighted row
            settings.horzontalScrollIndex = 0;
            putText(3, settings.selectedRow, selectedRomOrFolder, settings.fgcolor, settings.bgcolor);
            putText(SCREEN_COLS - 1, settings.selectedRow, " ", settings.bgcolor, settings.bgcolor);
            // if ((PAD1_Latch & Y) == Y)
            // {
            //     fgcolor++;
            //     if (fgcolor > 63)
            //     {
            //         fgcolor = 0;
            //     }
            //     printf("fgColor++ : %02d (%04x)\n", fgcolor, NesMenuPalette[fgcolor]);
            //     displayRoms(romlister, firstVisibleRowINDEX);
            // }
            // else if ((PAD1_Latch & X) == X)
            // {
            //     bgcolor++;
            //     if (bgcolor > 63)
            //     {
            //         bgcolor = 0;
            //     }
            //     printf("bgColor++ : %02d (%04x)\n", bgcolor, NesMenuPalette[bgcolor]);
            //     displayRoms(romlister, firstVisibleRowINDEX);
            // }
            // else
            if ((PAD1_Latch & UP) == UP && selectedRomOrFolder)
            {
                if (settings.selectedRow > STARTROW)
                {
                    settings.selectedRow--;
                }
                else
                {
                    if (settings.firstVisibleRowINDEX > 0)
                    {
                        settings.firstVisibleRowINDEX--;
                    }
                    else
                    {
                        settings.firstVisibleRowINDEX = romlister.Count() - PAGESIZE;
                        settings.selectedRow = ENDROW;
                        if (settings.firstVisibleRowINDEX < 0)
                        {
                            settings.firstVisibleRowINDEX = 0;
                            settings.selectedRow = romlister.Count() + STARTROW - 1;
                        }
                    }
                    displayRoms(romlister, settings.firstVisibleRowINDEX);
                }
            }
            else if ((PAD1_Latch & DOWN) == DOWN && selectedRomOrFolder)
            {
                if (settings.selectedRow < ENDROW && (index) < romlister.Count() - 1)
                {
                    settings.selectedRow++;
                }
                else
                {
                    if (index < romlister.Count() - 1)
                    {
                        settings.firstVisibleRowINDEX++;
                        displayRoms(romlister, settings.firstVisibleRowINDEX);
                    }
                    else
                    {

                        settings.firstVisibleRowINDEX = 0;
                        settings.selectedRow = STARTROW;
                        displayRoms(romlister, settings.firstVisibleRowINDEX);
                    }
                }
            }
            else if ((PAD1_Latch & LEFT) == LEFT && selectedRomOrFolder)
            {
                settings.firstVisibleRowINDEX -= PAGESIZE;
                settings.selectedRow = STARTROW;
                if (settings.firstVisibleRowINDEX < 0)
                {
                    settings.firstVisibleRowINDEX = romlister.Count() - PAGESIZE;
                    settings.selectedRow = ENDROW;
                    if (settings.firstVisibleRowINDEX < 0)
                    {
                        settings.firstVisibleRowINDEX = 0;
                        settings.selectedRow = romlister.Count() + STARTROW - 1;
                    }
                }
                displayRoms(romlister, settings.firstVisibleRowINDEX);
            }
            else if ((PAD1_Latch & RIGHT) == RIGHT && selectedRomOrFolder)
            {
                if (settings.firstVisibleRowINDEX + PAGESIZE < romlister.Count())
                {
                    settings.firstVisibleRowINDEX += PAGESIZE;
                }
                else
                {
                    settings.firstVisibleRowINDEX = 0;
                }
                settings.selectedRow = STARTROW;
                displayRoms(romlister, settings.firstVisibleRowINDEX);
            }
            else if ((PAD1_Latch & B) == B)
            {
                fr = my_getcwd(settings.currentDir, FF_MAX_LFN); // f_getcwd(settings.currentDir, FF_MAX_LFN);
                if (fr == FR_OK)
                {

                    if (strcmp(settings.currentDir, "/") != 0)
                    {
                        romlister.list("..");
                        settings.firstVisibleRowINDEX = 0;
                        settings.selectedRow = STARTROW;
                        displayRoms(romlister, settings.firstVisibleRowINDEX);
                        fr = my_getcwd(settings.currentDir, FF_MAX_LFN); // f_getcwd(settings.currentDir, FF_MAX_LFN);
                        if (fr == FR_OK)
                        {
                            printf("Current dir: %s\n", settings.currentDir);
                        }
                        else
                        {
                            printf("Cannot get current dir: %d\n", fr);
                        }
                    }
                }
                else
                {
                    printf("Cannot get current dir: %d\n", fr);
                }
            }
            else if ((PAD1_Latch & START) == START && ((PAD1_Latch & SELECT) != SELECT) && !Frens::isPsramEnabled())
            {
                showLoadingScreen();
                // reboot and start emulator with currently loaded game
                // Create a file /START indicating not to reflash the already flashed game
                // The emulator will delete this file after loading the game
                FRESULT fr;
                FIL fil;
                printf("Creating /START\n");
                fr = f_open(&fil, "/START", FA_CREATE_ALWAYS | FA_WRITE);
                if (fr == FR_OK)
                {
                    auto bytes = f_puts("START", &fil);
                    printf("Wrote %d bytes\n", bytes);
                    fr = f_close(&fil);
                    if (fr != FR_OK)
                    {
                        printf("Cannot close file /START:%d\n", fr);
                    }
                }
                else
                {
                    printf("Cannot create file /START:%d\n", fr);
                }
                break; // reboot
            }
            else if ((PAD1_Latch & A) == A && selectedRomOrFolder)
            {
                if (entries[index].IsDirectory)
                {
                    romlister.list(selectedRomOrFolder);
                    settings.firstVisibleRowINDEX = 0;
                    settings.selectedRow = STARTROW;
                    displayRoms(romlister, settings.firstVisibleRowINDEX);
                    // get full path name of folder
                    fr = my_getcwd(settings.currentDir, FF_MAX_LFN); //  f_getcwd(settings.currentDir, FF_MAX_LFN);
                    if (fr != FR_OK)
                    {
                        printf("Cannot get current dir: %d\n", fr);
                    }
                    printf("Current dir: %s\n", settings.currentDir);
                }
                else
                {
                    FRESULT fr;
                    FIL fil;
                    showLoadingScreen();
                    char curdir[FF_MAX_LFN];
                    fr = my_getcwd(curdir, sizeof(curdir)); // f_getcwd(curdir, sizeof(curdir));
                    printf("Current dir: %s\n", curdir);
                    if (Frens::isPsramEnabled())
                    {
                        char fullPath[FF_MAX_LFN];
                        // concatenate the current directory and the selected rom or folder
                        // and save it to the global variable selectedRomOrFolder
                        if (strlen(curdir) + strlen(selectedRomOrFolder) + 2 > FF_MAX_LFN)
                        {
                            snprintf(globalErrorMessage, 40, "Path too long: %s/%s", curdir, selectedRomOrFolder);
                            printf("%s\n", globalErrorMessage);
                            errorInSavingRom = true;
                            break;
                        }
                        else
                        {
                            snprintf(fullPath, FF_MAX_LFN, "%s/%s", curdir, selectedRomOrFolder);
                            printf("Full path: %s\n", fullPath);
                        }   
                        Frens::freePsram((void *)ROM_FILE_ADDR);
                        ROM_FILE_ADDR = (uintptr_t) Frens::flashromtoPsram(fullPath, false);
                    }
                    else
                    {
                        // If PSRAM is not enabled, we need to create a file with the full path name of the rom
                        // The emulator will read this file and flash the rom in main.cpp
                       
                        
                        // Create file containing full path name currently loaded rom
                        // The contents of this file will be used by the emulator to flash and start the correct rom in main.cpp
                        printf("Creating %s\n", ROMINFOFILE);
                        fr = f_open(&fil, ROMINFOFILE, FA_CREATE_ALWAYS | FA_WRITE);
                        if (fr == FR_OK)
                        {
                            for (auto i = 0; i < strlen(curdir); i++)
                            {

                                int x = f_putc(curdir[i], &fil);
                                printf("%c", curdir[i]);
                                if (x < 0)
                                {
                                    snprintf(globalErrorMessage, 40, "Error writing file %d", fr);
                                    printf("%s\n", globalErrorMessage);
                                    errorInSavingRom = true;
                                    break;
                                }
                            }
                            f_putc('/', &fil);
                            printf("%c", '/');
                            for (auto i = 0; i < strlen(selectedRomOrFolder); i++)
                            {

                                int x = f_putc(selectedRomOrFolder[i], &fil);
                                printf("%c", selectedRomOrFolder[i]);
                                if (x < 0)
                                {
                                    snprintf(globalErrorMessage, 40, "Error writing file %d", fr);
                                    printf("%s\n", globalErrorMessage);
                                    errorInSavingRom = true;
                                    break;
                                }
                            }
                            printf("\n");
                        }
                        else
                        {
                            printf("Cannot create %s:%d\n", ROMINFOFILE, fr);
                            snprintf(globalErrorMessage, 40, "Cannot create %s:%d", ROMINFOFILE, fr);
                            errorInSavingRom = true;
                        }
                        f_close(&fil);
                    }
                    // break out of loop and reboot
                    // rom will be flashed and started by main.cpp
                    // Cannot flash here because of lockups (when using wii controller) and sound issues
                    break;
                }
            }
        }
        // scroll selected row horizontally if textsize exceeds rowlength
        if (selectedRomOrFolder)
        {
            if ((frameCount % 30) == 0)
            {
                if (strlen(selectedRomOrFolder + settings.horzontalScrollIndex) >= VISIBLEPATHSIZE)
                {
                    settings.horzontalScrollIndex++;
                }
                else
                {
                    settings.horzontalScrollIndex = 0;
                }
                putText(3, settings.selectedRow, selectedRomOrFolder + settings.horzontalScrollIndex, settings.fgcolor, settings.bgcolor);
                putText(SCREEN_COLS - 1, settings.selectedRow, " ", settings.bgcolor, settings.bgcolor);
            }
        }
        if (totalFrames == -1)
        {
            totalFrames = frameCount;
        }
        if ((frameCount - totalFrames) > 800)
        {
            // printf("Starting screensaver\n");
            totalFrames = -1;
            screenSaver();
            displayRoms(romlister, settings.firstVisibleRowINDEX);
        }
    } // while 1
   
    ClearScreen(CBLACK); // Removes artifacts from previous screen
     // Wait until user has released all buttons
    while (1)
    {
        Menu_LoadFrame();
        DrawScreen(-1);
        RomSelect_PadState(&PAD1_Latch, true);
        if (PAD1_Latch == 0)
        {
            break;
        }
    }
    free(screenBuffer);
    free(buffer);

   
    Frens::savesettings();

    // Reset the screen mode to the original settings
    scaleMode8_7_ = Frens::applyScreenMode(settings.screenMode);
    dvi_->getBlankSettings().top = margintop;
    dvi_->getBlankSettings().bottom = marginbottom;
    if (!Frens::isPsramEnabled())
    {
#if WII_PIN_SDA >= 0 and WII_PIN_SCL >= 0
        wiipad_end();
#endif
        // Don't return from this function call, but reboot in order to get avoid several problems with sound and lockups (WII-pad)
        // After reboot the emulator will and flash start the selected game.
        Frens::resetWifi();
        printf("Rebooting...\n");
        watchdog_enable(100, 1);
        while (1)
        {
            tight_loop_contents();
            // printf("Waiting for reboot...\n");
        };
        // Never return
    }
}
