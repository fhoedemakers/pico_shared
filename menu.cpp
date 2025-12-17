#include <stdio.h>
#include <string.h>
#include <memory>
#include "pico.h"
#include "pico/stdlib.h"
#include "pico/rand.h"
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "hardware/divider.h"
#include "tusb.h"
#include "FrensHelpers.h"
#include "FrensFonts.h"
#include "gamepad.h"
#include "RomLister.h"
#include "menu.h"
#include "nespad.h"
#include "wiipad.h"
#include "menu_settings.h"

#include "font_8x8.h"
#include "settings.h"
#include "ffwrappers.h"
#include "vumeter.h"
#include "DefaultSS.h"
#include <stdint.h>
#if !HSTX
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

#else // TODO
#define CC(c) (((c & 0xf8) >> 3) | ((c & 0xf800) >> 6) | ((c & 0xf80000) >> 9))
const __UINT16_TYPE__ NesMenuPalette[64] = {
    CC(0x626262), CC(0x001C95), CC(0x1904AC), CC(0x42009D),
    CC(0x61006B), CC(0x6E0025), CC(0x650500), CC(0x491E00),
    CC(0x223700), CC(0x004900), CC(0x004F00), CC(0x004816),
    CC(0x00355E), CC(0x000000), CC(0x000000), CC(0x000000),

    CC(0xABABAB), CC(0x0C4EDB), CC(0x3D2EFF), CC(0x7115F3),
    CC(0x9B0BB9), CC(0xB01262), CC(0xA92704), CC(0x894600),
    CC(0x576600), CC(0x237F00), CC(0x008900), CC(0x008332),
    CC(0x006D90), CC(0x000000), CC(0x000000), CC(0x000000),

    CC(0xFFFFFF), CC(0x57A5FF), CC(0x8287FF), CC(0xB46DFF),
    CC(0xDF60FF), CC(0xF863C6), CC(0xF8746D), CC(0xDE9020),
    CC(0xB3AE00), CC(0x81C800), CC(0x56D522), CC(0x3DD36F),
    CC(0x3EC1C8), CC(0x4E4E4E), CC(0x000000), CC(0x000000),

    CC(0xFFFFFF), CC(0xBEE0FF), CC(0xCDD4FF), CC(0xE0CAFF),
    CC(0xF1C4FF), CC(0xFCC4EF), CC(0xFDCACE), CC(0xF5D4AF),
    CC(0xE6DF9C), CC(0xD3E99A), CC(0xC2EFA8), CC(0xB7EFC4),
    CC(0xB6EAE5), CC(0xB8B8B8), CC(0x000000), CC(0x000000)};
#endif
// Define the artwork directory and file formats
#if !HSTX
#define ARTWORKFILE "/metadata/%s/images/%d/%c/%s.444"
#else
#define ARTWORKFILE "/metadata/%s/images/%d/%c/%s.555"
#endif
#define METADDATAFILE "/metadata/%s/descr/%c/%s.txt"

int NesMenuPaletteItems = sizeof(NesMenuPalette) / sizeof(NesMenuPalette[0]);
static char connectedGamePadName[sizeof(io::GamePadState::GamePadName)];
static bool useFrameBuffer = false;
#define SCREENBUFCELLS SCREEN_ROWS *SCREEN_COLS
charCell *screenBuffer;

static char *selectedRomOrFolder;
static bool errorInSavingRom = false;
static char *globalErrorMessage;

static bool artworkEnabled = false;
static uint8_t crcOffset = 0; // Default offset for CRC calculation
#define LONG_PRESS_TRESHOLD (500)
#define REPEAT_DELAY (40)

static char buttonLabel1[2]; // e.g., "A", "B", "X", "O"
static char buttonLabel2[2]; // e.g., "A", "B", "
static char line[41];
static char valueBuf[16]; // separate buffer for numeric values

static WORD *WorkLineRom = nullptr;
#if !HSTX
// static BYTE *WorkLineRom8 = nullptr;

void RomSelect_SetLineBuffer(WORD *p, WORD size)
{
    WorkLineRom = p;
}
#endif

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

static void getButtonLabels(char *buttonLabel1, char *buttonLabel2)
{
    auto &gp = io::getCurrentGamePadState(0);
    strcpy(connectedGamePadName, gp.GamePadName);
    if (strcmp(connectedGamePadName, "Dual Shock 4") == 0 || strcmp(connectedGamePadName, "Dual Sense") == 0 || strcmp(connectedGamePadName, "PSClassic") == 0)
    {
        strcpy(buttonLabel1, "O");

    #if HW_CONFIG == 8
    // Simple WAV streamer for menu background audio using EXT_AUDIO_ENQUEUE_SAMPLE
    // Expects 16-bit stereo PCM @ 44100 Hz as produced by soundrecorder.cpp
        strcpy(buttonLabel2, "X");

    }
    else if (strcmp(connectedGamePadName, "XInput") == 0 || strncmp(connectedGamePadName, "Genesis", 7) == 0 || strcmp(connectedGamePadName, "MDArcade") == 0)
    {
        strcpy(buttonLabel1, "B");
        strcpy(buttonLabel2, "A");
    }
    else if (strcmp(connectedGamePadName, "Keyboard") == 0)
    {
        strcpy(buttonLabel1, "X");
        strcpy(buttonLabel2, "Z");
    }
    else
    {
        strcpy(buttonLabel1, "A");
        strcpy(buttonLabel2, "B");
    }
}

static bool isArtWorkEnabled()
{
    FILINFO fi;
    char PATH[FF_MAX_LFN];
    bool exists = false;
    PATH[0] = 0;
    const char *emulator = FrensSettings::getEmulatorTypeString();
    switch (FrensSettings::getEmulatorType())
    {
    case FrensSettings::emulators::NES:
        snprintf(PATH, sizeof(PATH), "/Metadata/%s/Images/320/D/D0E96F6B.444", emulator);
        break;
    case FrensSettings::emulators::SMS:
        snprintf(PATH, sizeof(PATH), "/Metadata/%s/Images/160/6/6A5A1E39.444", emulator);
        break;
    case FrensSettings::emulators::GENESIS:
        snprintf(PATH, sizeof(PATH), "/Metadata/%s/Images/160/5/56976261.444", emulator);
        break;
    case FrensSettings::emulators::GAMEBOY:
        snprintf(PATH, sizeof(PATH), "/Metadata/%s/Images/160/0/00A9001E.444", emulator);
        break;
    default:
        return false;
    }
    if (PATH[0])
    {
        FRESULT res = f_stat(PATH, &fi);
        bool exists = (res == FR_OK);
        printf("Artwork %s for %s\n", exists ? "enabled" : "not found", emulator);
        return exists;
    }
    return false;
}

int Menu_LoadFrame()
{
    Frens::waitForVSync();
#if NES_PIN_CLK != -1
    nespad_read_start();
#endif

    auto count =
#if !HSTX
        dvi_->getFrameCounter();
#else
        hstx_getframecounter();
#endif
    auto onOff = hw_divider_s32_quotient_inlined(count, 60) & 1;
    Frens::blinkLed(onOff);
#if NES_PIN_CLK != -1
    nespad_read_finish(); // Sets global nespad_state var
#endif
    tuh_task();
#if !HSTX && 0
    if (Frens::isFrameBufferUsed())
    {
        Frens::markFrameReadyForReendering(true);
    }
#endif
    // https://github.com/fhoedemakers/pico-genesisPlus/issues/10
    #endif // HW_CONFIG == 8
    // Initialize the Wii Pad here if delayed start is enabled, after the DAC has been initialized.
#if WIIPAD_DELAYED_START and WII_PIN_SDA >= 0 and WII_PIN_SCL >= 0
    // check only every 60 frames.
    if (!wiipad_is_connected() && onOff)
    {
        wiipad_begin();
    }
#endif
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
    // SELECT no longer changes colors directly; it opens the options menu in the main loop.

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
void RomSelect_DrawLine(int line, int selectedRow, int pixelsToSkip = 0)
{
    WORD fgcolor, bgcolor;

    auto pixelRow = WorkLineRom + pixelsToSkip;

    // calculate first char column index from pixelstoskip
    auto firstCharColumnIndex = (pixelsToSkip % SCREENWIDTH) / FONT_CHAR_WIDTH;
    for (auto i = 0; i < SCREEN_COLS; ++i)
    {
        if (i < firstCharColumnIndex)
        {
            continue; // skip out of bounds
        }
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
                *pixelRow = fgcolor;
            }
            else
            {
                *pixelRow = bgcolor;
            }
            fontSlice >>= 1;
            pixelRow++;
        }
    }
    return;
}

/// @brief Renders a single 320-pixel scanline into the active video line buffer.
///        Optionally blends (actually overwrites) an image row before drawing text.
///        Text glyphs are only drawn when not in screensaver (image moving) mode.
/// @param scanline Absolute scanline index (0..SCREENHEIGHT-1).
/// @param selectedRow Menu row index that is currently selected (for inverted colors); pass -1 for no selection.
/// @param w Image width in pixels (0 disables image drawing). Must be 1..SCREENWIDTH if imagebuffer != nullptr.
/// @param h Image height in pixels. Must be 1..SCREENHEIGHT if imagebuffer != nullptr.
/// @param imagebuffer Pointer to packed 16-bit pixel data (layout: row-major, RGB444/555 depending on build).
/// @param imagex Horizontal start position (column) where the image is placed (0-based).
/// @param imagey Vertical start position (scanline) where the top of the image is placed.
///               When imagex or imagey are non‑zero the function treats this as screensaver mode and
///               suppresses menu text drawing for lines overlapped or reserved by the image.
/// Algorithm:
///   1 Acquire destination line buffer (framebuffer or DVI line buffer).
///   2 If an image is active:
///        - Clear the line (only when image is moving: imagex || imagey) to prevent artifacts.
///        - If current scanline is within image vertical bounds, memcpy the corresponding image row.
///        - Reserve horizontal offset (offset = w) so text starts after image when image at top area (<120px).
///   3 If not in screensaver mode (imagex==0 && imagey==0) draw text glyphs via RomSelect_DrawLine(),
///        passing offset so text can start after embedded image when used for metadata screens.
///   4 Submit the populated line buffer back to the video subsystem when not using full framebuffer.
/// Notes:
///   - Safety checks ensure w/h are within screen bounds before treating imagebuffer as valid.
///   - Color mapping differs when useFrameBuffer is true (raw palette indices) versus false (lookup table).
///   - Clearing only moving-image lines reduces flicker on first static metadata image display.
///   - offset logic prevents garbled text when small images (<120px high) occupy left side.
/// Performance:
///   - memcpy used for image row copy (w * sizeof(uint16_t) bytes).
///   - Glyph rendering loops over SCREEN_COLS (character cells) * 8 pixels horizontally.
/// Edge cases:
///   - Invalid image dimensions: image ignored; only text drawn.
///   - scanline outside imagey..imagey+h: only text (unless reserved offset for early lines).
void drawline(int scanline, int selectedRow, int w = 0, int h = 0, uint16_t *imagebuffer = nullptr, int imagex = 0, int imagey = 0)
{
#if !HSTX
    dvi::DVI::LineBuffer *b = nullptr;
#if FRAMEBUFFERISPOSSIBLE
    if (Frens::isFrameBufferUsed())
    {
        WorkLineRom = &Frens::framebuffer[scanline * SCREENWIDTH];
    }
    else
    {
#endif
        b = dvi_->getLineBuffer();
        WorkLineRom = b->data();
#if FRAMEBUFFERISPOSSIBLE
    }
#endif
#else
    WorkLineRom = hstx_getlineFromFramebuffer(scanline);
#endif // !HSTX

    auto offset = 0;
    bool validImage = (imagebuffer != nullptr) && (w > 0 && w <= SCREENWIDTH && h > 0 && h <= SCREENHEIGHT);
    if (validImage)
    {
        // avoid flicker on first line in metadata screen
        // clear line only when image is moving (screensaver)
        if (imagex || imagey)
        {
            memset(WorkLineRom, 0, SCREENWIDTH * sizeof(WORD));
        }
        if (scanline >= imagey && scanline < imagey + h)
        {
            // printf("Drawing image at scanline %d, imagey %d, h %d imagey + h %d\n", scanline, imagey, h, imagey + h);
            //  copy image row into worklinerom
            auto rowOffset = (scanline - imagey) * w;
            memcpy(WorkLineRom + imagex, imagebuffer + rowOffset, w * sizeof(uint16_t));
            offset = w;
        }
        else
        {
            // avoid garbeled text when image is smaller than 120 pixels high
            if (scanline < 120)
            {
                offset = w;
            }
        }
    }
    // Only show text when not in screensaver mode (imagex and imagey are 0)
    if (imagex == 0 && imagey == 0)
    {
        RomSelect_DrawLine(scanline, selectedRow, offset);
    }

#if !HSTX
#if FRAMEBUFFERISPOSSIBLE
    if (!Frens::isFrameBufferUsed())
    {
#endif
        dvi_->setLineBuffer(scanline, b);
#if FRAMEBUFFERISPOSSIBLE
    }
#endif
#endif
}

void putText(int x, int y, const char *text, int fgcolor, int bgcolor, bool wraplines, int offset)
{

    if (text != nullptr)
    {
        int cur_x = x;
        int cur_y = y;
        auto index = cur_y * SCREEN_COLS + cur_x;
        auto maxLen = strlen(text);
        bool lastWasSpace = false;
        while (index < SCREENBUFCELLS && *text && maxLen > 0)
        {
            if (wraplines && !isspace(*text))
            {
                // Word wrapping: find length of next word
                const char *word_start = text;
                int word_len = 0;
                while (word_start[word_len] && !isspace(word_start[word_len]))
                {
                    word_len++;
                }
                // If word doesn't fit, move to next line
                if (cur_x + word_len > SCREEN_COLS && cur_x != 0)
                {
                    cur_x = offset;
                    cur_y++;
                    index = cur_y * SCREEN_COLS + cur_x;
                    if (index >= SCREENBUFCELLS)
                        break;
                }
                // Write the word
                for (int i = 0; i < word_len && index < SCREENBUFCELLS && maxLen > 0; i++)
                {
                    char ch = *text++;
                    if ((unsigned char)ch < 32 || (unsigned char)ch > 126)
                        ch = ' ';
                    screenBuffer[index].charvalue = (ch == '_' ? ' ' : ch);
                    screenBuffer[index].fgcolor = fgcolor;
                    screenBuffer[index].bgcolor = bgcolor;
                    cur_x++;
                    maxLen--;
                    lastWasSpace = false;
                    index = cur_y * SCREEN_COLS + cur_x;
                }
                // Write any following spaces (collapse consecutive)
                while (*text && isspace(*text) && index < SCREENBUFCELLS && maxLen > 0)
                {
                    if (!lastWasSpace)
                    {
                        char ch = *text;
                        if ((unsigned char)ch < 32 || (unsigned char)ch > 126)
                            ch = ' ';
                        screenBuffer[index].charvalue = (ch == '_' ? ' ' : ch);
                        screenBuffer[index].fgcolor = fgcolor;
                        screenBuffer[index].bgcolor = bgcolor;
                        cur_x++;
                        maxLen--;
                        lastWasSpace = true;
                        if (cur_x >= SCREEN_COLS)
                        {
                            cur_x = offset;
                            cur_y++;
                        }
                        index = cur_y * SCREEN_COLS + cur_x;
                    }
                    text++;
                }
            }
            else
            {
                char ch = *text++;
                if ((unsigned char)ch < 32 || (unsigned char)ch > 126)
                    ch = ' ';
                if (isspace(ch))
                {
                    if (lastWasSpace)
                        continue;
                    lastWasSpace = true;
                }
                else
                {
                    lastWasSpace = false;
                }
                screenBuffer[index].charvalue = (ch == '_' ? ' ' : ch);
                screenBuffer[index].fgcolor = fgcolor;
                screenBuffer[index].bgcolor = bgcolor;
                cur_x++;
                maxLen--;
                if (cur_x >= SCREEN_COLS)
                {
                    if (wraplines)
                    {
                        cur_x = offset;
                        cur_y++;
                    }
                    else
                    {
                        break; // Stop writing if wraplines is false
                    }
                }
                index = cur_y * SCREEN_COLS + cur_x;
            }
        }
    }
}

void DrawScreen(int selectedRow, int w = 0, int h = 0, uint16_t *imagebuffer = nullptr, int imagex = 0, int imagey = 0)
{
    const char *spaces = "                   ";
    char tmpstr[sizeof(connectedGamePadName) + 4];
    char s[SCREEN_COLS + 1];
    char buttonLabel1[2];
    char buttonLabel2[2];
    getButtonLabels(buttonLabel1, buttonLabel2);
    if (selectedRow != -1)
    {
        if (EXT_AUDIO_DACERROR())
        {
            putText(1, ENDROW + 3, "Dac Initialization Failed", CRED, CWHITE);
        }
        putText(SCREEN_COLS / 2 - strlen(spaces) / 2, SCREEN_ROWS - 1, spaces, settings.bgcolor, settings.bgcolor);
        snprintf(tmpstr, sizeof(tmpstr), "- %s -", connectedGamePadName[0] != 0 ? connectedGamePadName : "No USB GamePad");
        putText(SCREEN_COLS / 2 - strlen(tmpstr) / 2, SCREEN_ROWS - 1, tmpstr, CBLUE, CWHITE);
        snprintf(s, sizeof(s), "%c%dK %c", Frens::isPsramEnabled() ? 'P' : 'F', maxRomSize / 1024, WIIPAD_IS_CONNECTED() ? 'W' : ' ');
        putText(1, SCREEN_ROWS - 1, s, settings.fgcolor, settings.bgcolor);
        snprintf(s, sizeof(s), "%s:Open %s:Back", buttonLabel1, buttonLabel2);

        putText(1, ENDROW + 2, s, settings.fgcolor, settings.bgcolor);
        if (artworkEnabled)
        {
            strcpy(s, "START:Info");
            putText(17, ENDROW + 2, s, settings.fgcolor, settings.bgcolor);
        }
        int optionsRow = artworkEnabled ? ENDROW + 3 : ENDROW + 2;
        bool showSettingsbutton = true;
#if PICO_RP2350
#else
#if HW_CONFIG == 1
        if (FrensSettings::getEmulatorType() == FrensSettings::emulators::SMS)
        {
            showSettingsbutton = false;
        }
#endif
#endif
        if (strcmp(connectedGamePadName, "Genesis Mini 2") == 0 || strcmp(connectedGamePadName, "MDArcade") == 0)
        {
            strcpy(s, showSettingsbutton ? "Mode:Settings" : "Press MODE+START in-game for settings");
        }
        else
        {
            if (strncmp(connectedGamePadName, "Genesis", 7) == 0)
            {
                strcpy(s, showSettingsbutton ? "C:Settings" : "Press C+START in-game for settings");
            }
            else
            {
                strcpy(s, showSettingsbutton ? "SELECT:Settings" : "Press SELECT+START in-game for settings");
            }
        }
        putText(showSettingsbutton ? 17 : 1, optionsRow, s, settings.fgcolor, settings.bgcolor);
    }

    for (auto line = 0; line < 240; line++)
    {
        drawline(line, selectedRow, w, h, imagebuffer, imagex, imagey);
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

void displayRoms(Frens::RomLister &romlister, int startIndex)
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

static inline void drawAllLines(int selected)
{
    for (int lineNr = 0; lineNr < 240; ++lineNr)
    {
        drawline(lineNr, selected);
    }
}
void waitForNoButtonPress()
{
    DWORD PAD1_Latch;
    while (true)
    {
        DrawScreen(-1);
        Menu_LoadFrame();
        RomSelect_PadState(&PAD1_Latch);
        if (PAD1_Latch == 0)
        {
            return;
        }
    }
}
static inline int centerColClamped(int textLen)
{
    int col = (SCREEN_COLS - textLen) / 2;
    return col < 0 ? 0 : col;
}
static void showMessageBox(const char *message1, unsigned short fgcolor, const char *message2, const char *message3)
{

    ClearScreen(settings.bgcolor);
    waitForNoButtonPress();
    int row = SCREEN_ROWS / 2 - 1;
    putText(centerColClamped(strlen(message1)), row, message1, fgcolor, settings.bgcolor);
    if (message2)
    {
        row += 2;
        putText(centerColClamped(strlen(message2)), row, message2, settings.fgcolor, settings.bgcolor);
    }
    if (message3)
    {
        row += 2;
        putText(centerColClamped(strlen(message3)), row, message3, settings.fgcolor, settings.bgcolor);
    }
    DWORD waitPad;
    do
    {
        drawAllLines(-1);
        RomSelect_PadState(&waitPad);
        Menu_LoadFrame();
    } while (!waitPad);
}

static void showMessageBox(const char *message1, unsigned short fgcolor)
{
    const char *defaultMessage = "Press any button to continue.";
    showMessageBox(message1, fgcolor, defaultMessage, nullptr);
}

static void showMessageBox(const char *message1, int fgcolor, const char *message2)
{
    const char *defaultMessage = "Press any button to continue.";
    showMessageBox(message1, fgcolor, message2, defaultMessage);
}

static bool showDialogYesNo(const char *message)
{
    char tmpMsg[10];
    ClearScreen(settings.bgcolor);
    int row = SCREEN_ROWS / 2 - 1;
    putText(centerColClamped(strlen(message)), row, message, settings.fgcolor, settings.bgcolor);

    getButtonLabels(buttonLabel1, buttonLabel2);
    snprintf(tmpMsg, sizeof(tmpMsg), "%s:Yes", buttonLabel1);
    const char *optionNo = buttonLabel2;
    row += 2;
    putText(centerColClamped(strlen(tmpMsg)), row, tmpMsg, settings.fgcolor, settings.bgcolor);
    row += 1;
    snprintf(tmpMsg, sizeof(tmpMsg), "%s:No_", buttonLabel2);
    putText(centerColClamped(strlen(tmpMsg)), row, tmpMsg, settings.fgcolor, settings.bgcolor);
    waitForNoButtonPress();
    DWORD waitPad;
    while (true)
    {
        drawAllLines(-1);
        RomSelect_PadState(&waitPad);
        Menu_LoadFrame();
        if (waitPad & A)
        {
            return true;
        }
        else if (waitPad & B)
        {
            return false;
        }
    }
}
void DisplayFatalError(char *error)
{
    while (true)
    {
        showMessageBox("Fatal error:", CRED, error, "Please correct and restart.");
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

void screenSaverWithBlocks()
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

void screenSaverWithArt(bool showdefault = false)
{
    DWORD PAD1_Latch;
    WORD frameCount = 0;

    char fld;
    char *PATH = nullptr;
    char *CHOSEN = nullptr;
    FIL fil;
    FRESULT fr;
    uint8_t *buffer = nullptr;
    bool first = true;
    int16_t width = 0, height = 0;
    uint16_t *imagebuffer = nullptr;
    int imagex = 0;
    int imagey = 0;
    PATH = (char *)Frens::f_malloc(FF_MAX_LFN + 1);
    CHOSEN = (char *)Frens::f_malloc(FF_MAX_LFN + 1);
    // set speed
    int dx = 1; // (rand() % 2) + 1;
    int dy = 1; // (rand() % 2) + 1;
    // set direction
    if (rand() % 2)
        dx = -dx;
    if (rand() % 2)
        dy = -dy;

    while (true)
    {

        // choose new file every 60 * 20 frames
        if (first || (frameCount % (60 * 20)) == 0)
        {
            if (buffer)
            {
                Frens::f_free(buffer);
                buffer = nullptr;
            }
            ClearScreen(settings.bgcolor);
            if (showdefault == false)
            {
                fld = (char)(rand() % 15);
                snprintf(PATH, (FF_MAX_LFN + 1) * sizeof(char), "/metadata/%s/images/160/%X", FrensSettings::getEmulatorTypeString(), fld);
                printf("Scanning random folder: %s\n", PATH);
                fr = Frens::pick_random_file_fullpath(PATH, CHOSEN, (FF_MAX_LFN + 1) * sizeof(char));
            }
            else
            {
                fr = FR_DENIED;
            }
            if (fr == FR_OK)
            {
                fr = f_open(&fil, CHOSEN, FA_READ);
                FSIZE_t fsize;
                if (fr == FR_OK)
                {
                    fsize = f_size(&fil);
                    // printf("Reading %s, size: %d bytes\n", PATH, fsize);
                    buffer = (uint8_t *)Frens::f_malloc(fsize);
                    size_t r;
                    fr = f_read(&fil, buffer, fsize, &r);
                    if (fr != FR_OK || r != fsize)
                    {
                        printf("Error reading %s: %d, read %d bytes, expected %d bytes\n", PATH, fr, r, fsize);
                        Frens::f_free(buffer);
                        buffer = nullptr;
                    }

                    f_close(&fil);
                }
                else
                {
                    printf("Error opening %s: %d\n", CHOSEN, fr);
                    printf("Loading built-in screensaver image\n");
                }
            }
            if (fr != FR_OK || buffer == nullptr)
            {
                buffer = (uint8_t *)Frens::f_malloc(DEFAULT_SS_LEN);
                memcpy(buffer, DEFAULT_SS, DEFAULT_SS_LEN);
            }
            // first two bytes of buffer is width
            width = buffer ? *((uint16_t *)buffer) : 0;
            // next two bytes is height
            height = buffer ? *((uint16_t *)(buffer + 2)) : 0;
            if (width <= 0 || width > SCREENWIDTH || height <= 0 || height > SCREENHEIGHT)
            {
                printf("Invalid image size: %d x %d pixels\n", width, height);
                Frens::f_free(buffer);
                buffer = nullptr;
                Frens::f_free(PATH);
                PATH = nullptr;
                Frens::f_free(CHOSEN);
                CHOSEN = nullptr;
                return; // avoid endless loop of invalid images
            }
            imagebuffer = buffer ? (uint16_t *)(buffer + 4) : nullptr;
            if (first)
            {
                // if first time, set imagex and imagey to random position
                imagex = rand() % (SCREENWIDTH - width + 1);
                imagey = rand() % (SCREENHEIGHT - height + 1);
            }
            first = false;
        }
        Menu_LoadFrame();
        frameCount++;
        DrawScreen(-1, width, height, imagebuffer, imagex, imagey);
        RomSelect_PadState(&PAD1_Latch);
        if (PAD1_Latch > 0)
        {
            if (buffer)
            {
                Frens::f_free(buffer);
                buffer = nullptr;
                Frens::f_free(PATH);
                PATH = nullptr;
                Frens::f_free(CHOSEN);
                CHOSEN = nullptr;
            }
            srand(get_rand_32()); // Seed the random number generator for screensaver
            return;
        }
        if (frameCount % 2 == 0)
        {
            imagex += dx;
            imagey += dy;

            if (imagex <= 0)
            {
                imagex = 0;
                dx = -dx; // reverse direction
            }
            else if (imagex >= SCREENWIDTH - width)
            {
                imagex = SCREENWIDTH - width;
                dx = -dx; // reverse direction
            }

            if (imagey <= 0)
            {
                imagey = 0;
                dy = -dy; // reverse direction
            }
            else if (imagey >= SCREENHEIGHT - height)
            {
                imagey = SCREENHEIGHT - height;
                dy = -dy; // reverse direction
            }
        }
    }
}

void screenSaver()
{
#if 0
    if (artworkEnabled)
    {
        screenSaverWithArt();
    }
    else
    {
        screenSaverWithBlocks();
    }
#endif
    screenSaverWithArt(!artworkEnabled);
}

// #define ARTFILE "/ART/output_RGB555.raw"
// #define ARTFILERGB "/ART/output_RGB555.rgb"

#define DESC_SIZE 1024
/// @brief Show artwork for a given game
/// @param crc The CRC32 checksum of the game
/// @return 0: Do nothing, 1: start game, 2: start screensaver
int showartwork(uint32_t crc, FSIZE_t romsize)
{
    char info[SCREEN_COLS + 1];
    char gamename[64];
    char releaseDate[16]; // 19900212T000000
    char developer[64];   // Nintendo
    char genre[64];       // Platform-Platform / Run & Jump
    char rating[4];       // 0.0 0.1 0.2 - 1.0
    char players[4];      // 1-2 players
    char CRC[9];
    char *desc = (char *)Frens::f_malloc(DESC_SIZE); // preserve stack
    char *PATH = (char *)Frens::f_malloc(FF_MAX_LFN + 1);
    int stars = -1;
    int startGame = 0;
    char buttonLabel1[2];
    char buttonLabel2[2];

    getButtonLabels(buttonLabel1, buttonLabel2);

    // bool startscreensaver = false;
    FIL fil;
    FRESULT fr;
    uint8_t *buffer = nullptr;
    char *metadatabuffer = nullptr;
    snprintf(CRC, sizeof(CRC), "%08X", crc);
    snprintf(PATH, (FF_MAX_LFN + 1) * sizeof(char), ARTWORKFILE, FrensSettings::getEmulatorTypeString(), 160, CRC[0], CRC);
    // open the image
    fr = f_open(&fil, PATH, FA_READ);
    FSIZE_t fsize;
    if (fr == FR_OK)
    {
        fsize = f_size(&fil);
        // printf("Reading %s, size: %d bytes\n", PATH, fsize);
        buffer = (uint8_t *)Frens::f_malloc(fsize);
        size_t r;
        fr = f_read(&fil, buffer, fsize, &r);
        if (fr != FR_OK || r != fsize)
        {
            printf("Error reading %s: %d, read %d bytes\n", PATH, fr, r);
            Frens::f_free(buffer);
            buffer = nullptr;
        }

        f_close(&fil);
    }
    else
    {
        printf("Error opening %s: %d\n", PATH, fr);
    }
    // first two bytes of buffer is width
    int16_t width = buffer ? *((uint16_t *)buffer) : 0;
    // next two bytes is height
    int16_t height = buffer ? *((uint16_t *)(buffer + 2)) : 0;
    uint16_t *imagebuffer = buffer ? (uint16_t *)(buffer + 4) : nullptr;
    printf("Image size: %d x %d pixels\n", width, height);

    // open the file with metadata info
    snprintf(PATH, (FF_MAX_LFN + 1) * sizeof(char), METADDATAFILE, FrensSettings::getEmulatorTypeString(), CRC[0], CRC);
    fr = f_open(&fil, PATH, FA_READ);
    if (fr == FR_OK)
    {
        auto fsize = f_size(&fil);
        printf("Reading %s, size: %d bytes\n", PATH, fsize);
        metadatabuffer = (char *)Frens::f_malloc(fsize + 1);
        size_t r;
        fr = f_read(&fil, metadatabuffer, fsize, &r);
        if (fr != FR_OK || r != fsize)
        {
            printf("Error reading %s: %d, read %d bytes\n", PATH, fr, r);
            Frens::f_free(metadatabuffer);
            metadatabuffer = nullptr;
        }
        metadatabuffer[fsize] = '\0';
        f_close(&fil);
    }
    else
    {
        printf("Error opening %s: %d\n", PATH, fr);
        metadatabuffer = nullptr;
    }
    if (!metadatabuffer && !buffer)
    {
        // no metadata and no image, nothing to show
        printf("No metadata or image found for CRC: %s\n", CRC);
        Frens::f_free(PATH);
        Frens::f_free(desc);
        if (buffer)
        {
            Frens::f_free(buffer);
            buffer = nullptr;
        }
        return false;
    }
    gamename[0] = desc[0] = releaseDate[0] = developer[0] = genre[0] = rating[0] = players[0] = '\0';
    // extract the tags:
    if (metadatabuffer)
    {
        Frens::get_tag_text(metadatabuffer, "name", gamename, sizeof(gamename));
        Frens::get_tag_text(metadatabuffer, "releasedate", releaseDate, sizeof(releaseDate));
        Frens::get_tag_text(metadatabuffer, "developer", developer, sizeof(developer));
        Frens::get_tag_text(metadatabuffer, "genre", genre, sizeof(genre));
        Frens::get_tag_text(metadatabuffer, "desc", desc, DESC_SIZE * sizeof(char));
        Frens::get_tag_text(metadatabuffer, "rating", rating, sizeof(rating));
        Frens::get_tag_text(metadatabuffer, "players", players, sizeof(players));
#if 0
        printf("Game name: %s\n", gamename);
        printf("Release date: %s\n", releaseDate);
        printf("Developer: %s\n", developer);
        printf("Genre: %s\n", genre);
        printf("Rating: %s\n", rating);
        printf("Players: %s\n", players);
        printf("Description: %s\n", desc);
#endif
        stars = (int)(rating[0] - '0') * 10 + (int)(rating[2] - '0'); // convert first character to int
        if (stars < 0 || stars > 10)
        {
            stars = -1; // invalid rating
        }
    }
    DWORD PAD1_Latch;
    ClearScreen(settings.bgcolor);
    DrawScreen(-1);
    Menu_LoadFrame();
    // convert releasedate which is in the form of 19851117T000000
    // to a string MM-YYYY, If firstdigit of month is zero replace with space
    if (strlen(releaseDate) >= 8)
    {
        snprintf(info, sizeof(info), "%c%c-%c%c%c%c", releaseDate[4], releaseDate[5], releaseDate[0], releaseDate[1], releaseDate[2], releaseDate[3]);
    }
    // printf("Release date: %s\n", releaseDate);
    putText(0, height == 0 ? 9 : 20, desc, settings.fgcolor, settings.bgcolor, true);
    auto firstCharColumnIndex = ((width % SCREENWIDTH) / FONT_CHAR_WIDTH) + 1;
    putText(firstCharColumnIndex, 0, gamename, settings.fgcolor, settings.bgcolor, true, firstCharColumnIndex);
    putText(firstCharColumnIndex, 3, "Genre:", settings.fgcolor, settings.bgcolor, true, firstCharColumnIndex);
    putText(firstCharColumnIndex + 7, 3, genre, settings.fgcolor, settings.bgcolor, true, firstCharColumnIndex + 7);
    putText(firstCharColumnIndex, 6, "By:", settings.fgcolor, settings.bgcolor, true, firstCharColumnIndex);
    putText(firstCharColumnIndex + 4, 6, developer, settings.fgcolor, settings.bgcolor, true, firstCharColumnIndex + 4);
    putText(firstCharColumnIndex, 8, "Released:", settings.fgcolor, settings.bgcolor, true, firstCharColumnIndex);
    putText(firstCharColumnIndex + 10, 8, info, settings.fgcolor, settings.bgcolor, true, firstCharColumnIndex);
    putText(firstCharColumnIndex, 10, "Player(s):", settings.fgcolor, settings.bgcolor, true, firstCharColumnIndex);
    putText(firstCharColumnIndex + 11, 10, players, settings.fgcolor, settings.bgcolor, true, firstCharColumnIndex + 11);
    if (stars >= 0)
    {
        putText(firstCharColumnIndex, 12, "Rating:", settings.fgcolor, settings.bgcolor, true, firstCharColumnIndex);
        for (int i = 0; i < (stars >> 1); i++)
        {
            putText(firstCharColumnIndex + 8 + i, 12, "*", settings.fgcolor, settings.bgcolor, true, firstCharColumnIndex + 7 + i);
        }
    }
    int sizeInKB = (int)(romsize / 1024);
    snprintf(info, sizeof(info), "%d KB", sizeInKB);
    putText(firstCharColumnIndex, 14, "Size:", settings.fgcolor, settings.bgcolor, true, firstCharColumnIndex);
    putText(firstCharColumnIndex + 6, 14, info, settings.fgcolor, settings.bgcolor, true, firstCharColumnIndex + 6);
    putText(firstCharColumnIndex, 16, "SELECT: Full description", settings.fgcolor, settings.bgcolor, true, firstCharColumnIndex);
    snprintf(info, sizeof(info), "START or %s: Start game", buttonLabel1);
    putText(firstCharColumnIndex, 17, info, settings.fgcolor, settings.bgcolor, true, firstCharColumnIndex);
    snprintf(info, sizeof(info), "%s: Back to rom list", buttonLabel2);
    putText(firstCharColumnIndex, 18, info, settings.fgcolor, settings.bgcolor, true, firstCharColumnIndex);
    bool skipImage = false;
    int startFrames = -1;
    while (true)
    {
        auto frameCount = Menu_LoadFrame();
        if (startFrames == -1)
        {
            startFrames = frameCount;
        }
        DrawScreen(-1, width, height, (skipImage ? nullptr : imagebuffer));
        RomSelect_PadState(&PAD1_Latch);
        if (PAD1_Latch > 0)
        {
            if ((PAD1_Latch & SELECT) == SELECT)
            {
                // show entire description
                ClearScreen(settings.bgcolor);
                putText(0, 0, desc, settings.fgcolor, settings.bgcolor, true);
                skipImage = true;
                startFrames = frameCount; // reset totalframes to current frame count
                continue;
            }
            if ((PAD1_Latch & START) == START || (PAD1_Latch & A) == A)
            {
                printf("Starting game with CRC: %s\n", CRC);
                startGame = 1;
            }
            break;
        }
        if (frameCount - startFrames > 3600)
        {
            // if no input for 3600 frames, start screensaver
            startGame = 2;
            break;
        }
    }
    if (buffer)
    {
        Frens::f_free(buffer);
    }
    if (metadatabuffer)
    {
        Frens::f_free(metadatabuffer);
    }
    if (desc)
    {
        Frens::f_free(desc);
    }
    if (PATH)
    {
        Frens::f_free(PATH);
    }

    // occupies too much stack and crashes, return from call and let the caller
    // start the screensaver.
    // if (startscreensaver) {
    //     screenSaver();
    // }
    return startGame;
}
static void showLoadingScreen(const char *message = nullptr)
{
#if !HSTX
    if (Frens::isFrameBufferUsed())
    {
#else
#if 0
    // try to read .rgb file first
    // RGB colors are stored in 32 bit ARGB format, little endian.
    // If ARGB = 0xFF112233 (fully opaque 0xFF, red=0x11, green=0x22, blue=0x33):
    // Little endian storage (in memory): 33 22 11 FF
    // Note the A-byte is unused, so it is always 0xFF.
    // Resolution must be 320x240 pixels,
    // so the total size of the file must be 307200 bytes (320 * 240 * 4 bytes per pixel).
    FIL fil;
    FRESULT fr;
    int LINES2READ = Frens::isPsramEnabled() ? 240 : 4; // read the entire framebuffer in one go if PSRAM is enabled, otherwise read 4 lines at a time
    int bufferSize = 320 * LINES2READ * sizeof(uint32_t);
    fr = f_open(&fil, ARTFILERGB, FA_READ);
    if (fr == FR_OK)
    {
        if (f_size(&fil) == 307200)
        {
            printf("Reading %s, size: %d bytes\n", ARTFILERGB, f_size(&fil));
            uint32_t *buffer = (uint32_t *)Frens::f_malloc(bufferSize);
            uint16_t *line = hstx_getlineFromFramebuffer(0);
            size_t r;
            for (int j = 0; j < (240 / LINES2READ); j++)
            {
                fr = f_read(&fil, buffer, bufferSize, &r);
                for (int i = 0; i < (int)(bufferSize / sizeof(buffer[0])); i++)
                {
                    *line++ = CC(buffer[i]);
                }
            }
            Frens::f_free(buffer);
            f_close(&fil);
            // sleep_ms(1500);
            return;
        }
        else
        {
            printf("Error: %s is not 320x240 pixels, size: %d bytes\n", ARTFILERGB, f_size(&fil));
            f_close(&fil);
        }
    }
    else
    {
        printf("Error opening %s: %d\n", ARTFILERGB, fr);
    }
    // try to read .raw file which uses 16 bit RGB555 colors. Colrs are stored in little endian.
    // Resolution must be 320x240 pixels,
    // so the total size of the file must be 153600 bytes (320 * 240 * 2 bytes per pixel).
    // If the file is not found, it will  display a text - loading screen.
    fr = f_open(&fil, ARTFILE, FA_READ);
    if (fr == FR_OK)
    {
        if (f_size(&fil) == 153600)
        {
            size_t r;
            fr = f_read(&fil, hstx_getframebuffer(), 153600, &r);
            f_close(&fil);
            printf("Read %d bytes from %s\n", r, ARTFILE);
            // sleep_ms(1000);
        }
        else
        {
            printf("Error: %s is not 320x240 pixels, size: %d bytes\n", ARTFILE, f_size(&fil));
            f_close(&fil);
        }
        return;
    }
    else
    {
        printf("Error opening %s: %d\n", ARTFILE, fr);
    }
#endif // 0
#endif // HSTX
        ClearScreen(settings.bgcolor);
        if (message)
        {
            putText(SCREEN_COLS / 2 - strlen(message) / 2, SCREEN_ROWS / 2, message, settings.fgcolor, settings.bgcolor);
        }
        else
        {
            putText(SCREEN_COLS / 2 - 5, SCREEN_ROWS / 2, "Loading...", settings.fgcolor, settings.bgcolor);
        }
        DrawScreen(-1);
        Menu_LoadFrame();
        DrawScreen(-1);
        Menu_LoadFrame();
#if !HSTX
    } // Frens::isFrameBufferUsed()
#endif
}

uint32_t GetCRCOfRomFile(char *curdir, char *selectedRomOrFolder, char *rompath, FSIZE_t &romsize)
{
    char fullPath[FF_MAX_LFN];
    uint32_t crc = 0;
    // concatenate the current directory and the selected rom or folder
    // and save it to the global variable selectedRomOrFolder
    if (strlen(curdir) + strlen(selectedRomOrFolder) + 2 > FF_MAX_LFN)
    {
        printf("Path too long: %s/%s\n", curdir, selectedRomOrFolder);
        return 0;
    }
    else
    {
        snprintf(fullPath, FF_MAX_LFN, "%s/%s", curdir, selectedRomOrFolder);
        printf("Full path: %s\n", fullPath);
    }
    crc = compute_crc32(fullPath, crcOffset, romsize);
    if (crc != 0)
    {
        printf("CRC32 Checksum: 0x%08X\n", crc);
    }
    else
    {
        printf("Error computing CRC32 for %s\n", fullPath);
    }
    return crc;
}
uint32_t loadRomInPsRam(char *curdir, char *selectedRomOrFolder, char *rompath, bool &errorInSavingRom)
{
#if PICO_RP2350
    uint32_t crc = 0;
    errorInSavingRom = false;
    // If PSRAM is enabled, we need to copy the rom to PSRAM
    char fullPath[FF_MAX_LFN];
    // concatenate the current directory and the selected rom or folder
    // and save it to the global variable selectedRomOrFolder
    if (strlen(curdir) + strlen(selectedRomOrFolder) + 2 > FF_MAX_LFN)
    {
        snprintf(globalErrorMessage, 40, "Path too long: %s/%s", curdir, selectedRomOrFolder);
        printf("%s\n", globalErrorMessage);
        errorInSavingRom = true;
    }
    else
    {
        snprintf(fullPath, FF_MAX_LFN, "%s/%s", curdir, selectedRomOrFolder);
        printf("Full path: %s\n", fullPath);
        // If there is already a rom loaded in PSRAM, free it
        Frens::f_free((void *)ROM_FILE_ADDR);
        // and load the new rom to PSRAM
        printf("Loading rom to PSRAM: %s\n", fullPath);
        strcpy(rompath, fullPath);

        ROM_FILE_ADDR = (uintptr_t)Frens::flashromtoPsram(fullPath, Frens::romIsByteSwapped(), crc, crcOffset);
    }
    return crc;
#else
    return 0;
#endif
}

// On Fruit Jam: SNES classic/Pro controller can cause audio DAC initialization to fail
// Show instructions to the user on how to fix this.
void DisplayDacError()
{
    ClearScreen(settings.bgcolor);
    putText(0, 0, "Audio DAC Initialization Error", settings.fgcolor, settings.bgcolor);
    putText(0, 3, "Sound hardware failed to start.", settings.fgcolor, settings.bgcolor);
    putText(0, 4, "Probable cause: SNES Classic/Pro", settings.fgcolor, settings.bgcolor);
    putText(0, 6, "Fix steps:", settings.fgcolor, settings.bgcolor);
    putText(0, 7, "1 Unplug SNES Classic/Pro pad", settings.fgcolor, settings.bgcolor);
    putText(0, 8, "2 Press Reset; wait for menu", settings.fgcolor, settings.bgcolor);
    putText(0, 9, "3 Reconnect the controller", settings.fgcolor, settings.bgcolor);
    putText(0, ENDROW - 1, "To reset:", settings.fgcolor, settings.bgcolor);
    putText(0, ENDROW, "Press Reset on Fruit Jam board", settings.fgcolor, settings.bgcolor);
    while (true)
    {
        auto frameCount = Menu_LoadFrame();
        DrawScreen(-1);
    }
}

void getQuickSavePath(char *path, size_t pathsize)
{
    snprintf(path, pathsize, QUICKSAVEFILEFORMAT, FrensSettings::getEmulatorTypeString(), Frens::getCrcOfLoadedRom(), MAXSAVESTATESLOTS -1);
}

void getAutoSaveIsConfiguredPath(char *path, size_t pathsize)
{
    snprintf(path, pathsize, AUTOSAVEFILEISCONFIGUREDFORMAT, FrensSettings::getEmulatorTypeString(), Frens::getCrcOfLoadedRom());
}

void getAutoSaveStatePath(char *path, size_t pathsize)
{
    snprintf(path, pathsize, AUTOSAVEFILEFORMAT, FrensSettings::getEmulatorTypeString(), Frens::getCrcOfLoadedRom());
}

void getSaveStatePath(char *path, size_t pathsize, int slot)
{
    snprintf(path, pathsize, SLOTFORMAT, FrensSettings::getEmulatorTypeString(), Frens::getCrcOfLoadedRom(), slot);
}

bool isAutoSaveStateConfgured()
{
    char path[FF_MAX_LFN];
    getAutoSaveIsConfiguredPath(path, sizeof(path));
    return Frens::fileExists(path);
}

// Helper: ensure the directory structure for save states exists.
// Returns true on success, false on failure (and shows a message box).
static bool ensureSaveStateDirectories(uint32_t crc)
{
    FRESULT fr;
    char tmppath[40];

    // /SAVESTATES
    snprintf(tmppath, sizeof(tmppath), "%s", SAVESTATEDIR);
    fr = f_mkdir(tmppath);
    if (fr != FR_OK && fr != FR_EXIST)
    {
        printf("Error creating save state directory: %s (fr=%d)\n", tmppath, fr);
        showMessageBox("Save failed, cannot create folder.", CRED, tmppath);
        return false;
    }
    if ( fr == FR_OK ) {
        printf("Save state base directory created: %s\n", tmppath);
    }
    // /SAVESTATES/<emulator>
    snprintf(tmppath, sizeof(tmppath), "%s/%s", SAVESTATEDIR, FrensSettings::getEmulatorTypeString());
    
    fr = f_mkdir(tmppath);
    if (fr != FR_OK && fr != FR_EXIST)
    {
        printf("Error creating save state directory: %s (fr=%d)\n", tmppath, fr);
        showMessageBox("Save failed, cannot create folder.", CRED, tmppath);
        return false;
    }
    if ( fr == FR_OK ) {
        printf("Save state emulator directory created: %s\n", tmppath);
    }   
    // /SAVESTATES/<emulator>/<CRC>
    snprintf(tmppath, sizeof(tmppath), "%s/%s/%08X", SAVESTATEDIR, FrensSettings::getEmulatorTypeString(), crc);
    fr = f_mkdir(tmppath);
    if (fr != FR_OK && fr != FR_EXIST)
    {
        printf("Error creating save state directory: %s (fr=%d)\n", tmppath, fr);
        showMessageBox("Save failed, cannot create folder.", CRED, tmppath);
        return false;
    }
    if ( fr == FR_OK ) {
        printf("Save state CRC directory created: %s\n", tmppath);
    }   

    return true;
}

/// @brief Shows the save state menu
/// @param savestatefunc The function to call to save a state
/// @param loadstatefunc The function to call to load a state
/// @param extraMessage Extra message to display at the bottom of the menu
/// @return false when a save state failed to load. True otherwise.
bool showSaveStateMenu(int (*savestatefunc)(const char *path), int (*loadstatefunc)(const char *path), const char *extraMessage, SaveStateTypes quickSave)
{
    bool saveStateLoadedOK = true;
    uint8_t saveslots[MAXSAVESTATESLOTS]{};
    char tmppath[40]; // /SAVESTATES/NES/XXXXXXXX/slot1.sta
    int margintop = 0;
    int marginbottom = 0;
#if ENABLE_VU_METER
    turnOffAllLeds();
#endif

    screenBuffer = (charCell *)Frens::f_malloc(screenbufferSize);
    auto crc = Frens::getCrcOfLoadedRom();
#if !HSTX
    margintop = dvi_->getBlankSettings().top;
    marginbottom = dvi_->getBlankSettings().bottom;
    scaleMode8_7_ = Frens::applyScreenMode(ScreenMode::NOSCANLINE_1_1);
    dvi_->getBlankSettings().top = 0;
    dvi_->getBlankSettings().bottom = 0;
#else
    hstx_setScanLines(false);
#endif
    getAutoSaveStatePath(tmppath, sizeof(tmppath));
    bool autosaveFileExists = Frens::fileExists(tmppath);
    // Handle quicksave and quick load
    if (quickSave == SaveStateTypes::LOAD || quickSave == SaveStateTypes::SAVE || quickSave == SaveStateTypes::LOAD_AND_START || quickSave == SaveStateTypes::SAVE_AND_EXIT)
    {
        
        if (quickSave == SaveStateTypes::LOAD)
        {
            getQuickSavePath(tmppath, sizeof(tmppath));
            // do nothing if file does not exist
            if (Frens::fileExists(tmppath) )
            {
                bool ok = true;
                ok = showDialogYesNo("Load quick save state?");
                if (ok)
                {
                    printf("Loading quick save from %s\n", tmppath);
                    if (loadstatefunc(tmppath) == 0)
                    {
                        printf("Quick load successful\n");
                        //showMessageBox("State loaded successfully.", settings.fgcolor);
                    }
                    else
                    {
                        printf("Quick load failed\n");
                        showMessageBox("State load failed. Returning to menu.", CRED);
                        saveStateLoadedOK = false;
                    }
                }
            }
            else
            {
                showMessageBox("Quick save file does not exist.", CRED);
                printf("Quick save file does not exist\n");
            }
        }
        else if (quickSave == SaveStateTypes::SAVE)
        {
            getQuickSavePath(tmppath, sizeof(tmppath));
            if (ensureSaveStateDirectories(crc))
            {
                bool ok = true;
                if (Frens::fileExists(tmppath))
                {
                    ok = showDialogYesNo("Overwrite existing quick save?");
                }
                if (ok)
                {
                    printf("Saving quick save to %s\n", tmppath);
                    if (savestatefunc(tmppath) == 0)
                    {
                        printf("Quick save successful\n");
                        //showMessageBox("State saved successfully.", settings.fgcolor);
                    }
                    else
                    {
                        printf("Quick save failed\n");
                        showMessageBox("Failed to save state.", CRED);
                    }
                }
            }
        } else if ( quickSave == SaveStateTypes::LOAD_AND_START) {
            // do nothing if file does not exist
            if (autosaveFileExists)
            {
                bool ok = true;
                ok = showDialogYesNo("Load auto save state?");
                if (ok)
                {
                    printf("Loading auto save from %s\n", tmppath);
                    if (loadstatefunc(tmppath) == 0)
                    {
                        printf("Auto load successful\n");
                        //showMessageBox("State loaded successfully.", settings.fgcolor);
                    }
                    else
                    {
                        printf("Auto load failed\n");
                        showMessageBox("State load failed. Returning to menu.", CRED);
                        saveStateLoadedOK = false;
                    }
                }
            }
        } else if ( quickSave == SaveStateTypes::SAVE_AND_EXIT) {
            getAutoSaveStatePath(tmppath, sizeof(tmppath));
            if (ensureSaveStateDirectories(crc))
            {
                bool ok = true;
                if (autosaveFileExists)
                {
                    ok = showDialogYesNo("Overwrite existing auto save?");
                }
                if (ok)
                {
                    printf("Saving auto save to %s\n", tmppath);
                    if (savestatefunc(tmppath) == 0)
                    {
                        autosaveFileExists = true;
                        printf("Auto save successful\n");
                        //showMessageBox("State saved successfully.", settings.fgcolor);
                    }
                    else
                    {
                        printf("Auto save failed\n");
                        showMessageBox("Failed to save state.", CRED);
                    }
                }
            }
        }

    }
    else
    {
        
        for (int i = 0; i < MAXSAVESTATESLOTS; i++)
        {
            getSaveStatePath(tmppath, sizeof(tmppath), i);
            saveslots[i] = (Frens::fileExists(tmppath)) ? 1 : 0;
        }
        // check if auto save is enabled
        printf("Checking if auto save is configured...\n");
        getAutoSaveIsConfiguredPath(tmppath, sizeof(tmppath));
        printf("Auto save path: %s\n", tmppath);
        bool autosaveEnabled = (Frens::fileExists(tmppath));
        printf("Auto save configured: %s\n", autosaveEnabled ? "Yes" : "No");
        int selected = 0;
        bool exitMenu = false;
        bool saved = false;
        DWORD pad = 0;
        int idleStart = -1;

        // confirmType: 0 none, 1 overwrite, 2 delete
        auto redraw = [&](int confirmType = 0, int confirmSlot = -1)
        {
            char linebuf[48];
            ClearScreen(settings.bgcolor);
            getButtonLabels(buttonLabel1, buttonLabel2);
            putText(9, 0, "-- Save/Load State --", settings.fgcolor, settings.bgcolor);
            putText(0, 2, "Choose slot:", settings.fgcolor, settings.bgcolor);

            for (int i = 0; i < MAXSAVESTATESLOTS && (4 + i) < ENDROW - 2; i++)
            {
                const char *status = saveslots[i] ? "Used" : "Empty";
                // Last soft used for quick save
                if ( i == (MAXSAVESTATESLOTS -1) ){
                    snprintf(linebuf, sizeof(linebuf), "Quick Save: %s%s", status, (i == selected && saved) ? " Saved" : "");
                } else {
                    snprintf(linebuf, sizeof(linebuf), "Slot %d____: %s%s", i, status, (i == selected && saved) ? " Saved" : "");
                }
                int fg = settings.fgcolor;
                int bg = settings.bgcolor;
                if (confirmSlot == i)
                {
                    // Highlight slot being confirmed (overwrite/delete)
                    fg = CWHITE;
                    // bg = (confirmType == 2) ? CBLUE : CRED;
                    bg = CRED;
                }
                else if (i == selected && confirmSlot < 0)
                {
                    fg = settings.bgcolor;
                    bg = settings.fgcolor;
                }
                putText(2, 4 + i, linebuf, fg, bg);
            }
            // Add toggle option after the slots
            {
                int toggleRow = 4 + MAXSAVESTATESLOTS;
                const char *toggleStatus = autosaveEnabled ? "Enabled" : "Disabled";
                const char *autosaveUsed = autosaveFileExists ? "Used" : "Empty";
                snprintf(linebuf, sizeof(linebuf), "Auto Save : %s -  %s%s", autosaveUsed, toggleStatus, (selected == MAXSAVESTATESLOTS && saved) ? " Saved" : "");
                int fg = settings.fgcolor;
                int bg = settings.bgcolor;
                if (confirmSlot < 0 && selected == MAXSAVESTATESLOTS)
                {
                    fg = settings.bgcolor;
                    bg = settings.fgcolor;
                } else if (confirmSlot == MAXSAVESTATESLOTS) {
                    // Highlight slot being confirmed (overwrite/delete)
                    fg = CWHITE;
                    bg = CRED;
                }
                putText(2, toggleRow, linebuf, fg, bg);
            }
            putText(0, ENDROW - 9, extraMessage ? extraMessage : " ", settings.fgcolor, settings.bgcolor);
            if (confirmSlot >= 0)
            {
                if (confirmType == 1)
                {
                    putText(0, ENDROW - 4, "Overwrite existing state?", settings.fgcolor, settings.bgcolor);
                    snprintf(linebuf, sizeof(linebuf), "%s:Overwrite  %s:Cancel", buttonLabel1, buttonLabel2);
                }
                else
                {
                    putText(0, ENDROW - 4, "Delete this save state?", settings.fgcolor, settings.bgcolor);
                    snprintf(linebuf, sizeof(linebuf), "%s:Delete  %s:Cancel", buttonLabel1, buttonLabel2);
                }
                putText(0, ENDROW - 3, linebuf, settings.fgcolor, settings.bgcolor);
            }
            else
            {
                bool saveSlotHasData = false;
                if ( selected < MAXSAVESTATESLOTS )
                {
                    saveSlotHasData = saveslots[selected];
                }  else {
                    saveSlotHasData = autosaveFileExists; 
                }
                // General instructions (each action on its own line)
                if ( selected == MAXSAVESTATESLOTS ) {
                    putText(0, ENDROW - 7, "LEFT/RIGHT: Toggle autosave", settings.fgcolor, settings.bgcolor);
                } 
                //if ( selected < MAXSAVESTATESLOTS) {
                    snprintf(linebuf, sizeof(linebuf), "%s_____:Save state", buttonLabel1);
                    putText(0, ENDROW - 6, linebuf, settings.fgcolor, settings.bgcolor);
                //}
                if (saveSlotHasData)
                {                 
                    snprintf(linebuf, sizeof(linebuf), "SELECT:Delete state");
                    putText(0, ENDROW - 5, linebuf, settings.fgcolor, settings.bgcolor);
                    putText(0, ENDROW - 4, "START :Load state.", settings.fgcolor, settings.bgcolor);
                    // Back must be shown last when slot non-empty
                    snprintf(linebuf, sizeof(linebuf), "%s_____:Back", buttonLabel2);
                    putText(0, ENDROW - 3, linebuf, settings.fgcolor, settings.bgcolor);
                    // Toggle auto save now handled via menu line with A
                }
                else
                {
                    // When slot is empty, show Back immediately below Save
                    snprintf(linebuf, sizeof(linebuf), "%s_____:Back", buttonLabel2);
                    putText(0, ENDROW - 5, linebuf, settings.fgcolor, settings.bgcolor);
                }
                putText(0, SCREEN_ROWS - 4, "In-Game Quick Save/Load state: ", settings.fgcolor, settings.bgcolor);
                //snprintf(linebuf, sizeof(linebuf), "SELECT + %s : Quick Save", buttonLabel1);
                snprintf(linebuf, sizeof(linebuf), "START + DOWN : Quick Save");
                putText(1, SCREEN_ROWS - 3, linebuf, settings.fgcolor, settings.bgcolor);
                //snprintf(linebuf, sizeof(linebuf), "SELECT + %s : Quick Load", buttonLabel2);
                snprintf(linebuf, sizeof(linebuf), "START + UP___: Quick Load");
                putText(1, SCREEN_ROWS - 2, linebuf, settings.fgcolor, settings.bgcolor);
            }

            drawAllLines(-1);
        };

        waitForNoButtonPress();

        while (!exitMenu)
        {
            redraw();
            RomSelect_PadState(&pad);
            int frame = Menu_LoadFrame();
            if (idleStart < 0)
                idleStart = frame;
            if (pad)
                idleStart = frame;

            if ((frame - idleStart) > 3600)
            {
                exitMenu = true;
                idleStart = frame;
                continue;
            }

            if (pad & UP)
            {
                selected = (selected > 0) ? selected - 1 : (MAXSAVESTATESLOTS); // include toggle line
                saved = false;
            }
            else if (pad & DOWN)
            {
                selected = (selected < MAXSAVESTATESLOTS) ? selected + 1 : 0; // include toggle line
                saved = false;
            }
            else if (!(pad & SELECT) && (pad & START))
            {
                bool saveSlotHasData = false;
                if ( selected < MAXSAVESTATESLOTS )
                {
                    saveSlotHasData = saveslots[selected];
                    getSaveStatePath(tmppath, sizeof(tmppath), selected);
                } else {
                    saveSlotHasData = autosaveFileExists;
                    getAutoSaveStatePath(tmppath, sizeof(tmppath));
                }
                if (!saveSlotHasData)
                {
                    // No save state in this slot
                    continue;
                }
               
                printf("Loading state  %s from slot %d\n", tmppath, selected);
                if (loadstatefunc(tmppath) == 0)
                {
                    printf("Save state loaded from slot %d: %s\n", selected, tmppath);
                    //showMessageBox("Loaded state from", CBLUE, tmppath, "Press any button to resume game.");
                    exitMenu = true;
                    break;
                }
                else
                {
                    showMessageBox("State load failed. Returning to menu.", CRED);
                    saveStateLoadedOK = false;
                    exitMenu = true;
                    break;
                }
            }
            else if ((pad & SELECT) && !(pad & START))
            {
                bool saveSlotHasData = false;
                if ( selected < MAXSAVESTATESLOTS )
                {
                    saveSlotHasData = saveslots[selected];
                    getSaveStatePath(tmppath, sizeof(tmppath), selected);
                } else {
                    // Auto save slot
                    getAutoSaveStatePath(tmppath, sizeof(tmppath));
                    saveSlotHasData = autosaveFileExists;
                }
                // Delete confirmation only when slot used
                if (saveSlotHasData)
                {

                    while (true)
                    {
                        redraw(2, selected);
                        RomSelect_PadState(&pad);
                        Menu_LoadFrame();
                        if (pad & A) // Confirm delete
                        {
                            printf("Deleting save state file: %s\n", tmppath);
                            f_unlink(tmppath); // ignore result
                            saveslots[selected] = 0;
                            // showMessageBox("Save state deleted.", CBLUE);
                            if (selected == MAXSAVESTATESLOTS)
                            {
                                autosaveFileExists = false;
                            }
                            break;
                        }
                        if (pad & B) // Cancel
                            break;
                    }
                    continue;
                }
            }
            else if ((pad & LEFT || pad & RIGHT) && selected == MAXSAVESTATESLOTS)
            {
                if (ensureSaveStateDirectories(crc) == false)
                {
                    continue;
                }
                // Toggle auto save by creating/deleting AUTO file
                printf("Toggling auto save...\n");
                getAutoSaveIsConfiguredPath(tmppath, sizeof(tmppath));
                printf("Auto save config file path: %s\n", tmppath);
                FIL fil;
                FRESULT fr;
                fr = f_open(&fil, tmppath, FA_OPEN_EXISTING);
                if (fr == FR_OK)
                {
                    f_close(&fil);
                    fr = f_unlink(tmppath);
                    if (fr != FR_OK)
                    {
                        showMessageBox("Failed to disable auto save.", CRED);
                    }
                    else
                    {
                        autosaveEnabled = false;
                        //showMessageBox("Auto save disabled.", CBLUE);
                    }
                }
                else
                {
                    fr = f_open(&fil, tmppath, FA_WRITE | FA_CREATE_ALWAYS);
                    if (fr == FR_OK)
                    {
                        f_close(&fil);
                        autosaveEnabled = true;
                       // showMessageBox("Auto save enabled.", CBLUE);
                    }
                    else
                    {
                        showMessageBox("Failed to enable auto save.", CRED);
                    }
                }
                continue;
            }
            else if (pad & B)
            {
                exitMenu = true;
                saved = false;
            }
            else if (pad & A)
            {
                bool saveSlotHasData = false;
                if ( selected < MAXSAVESTATESLOTS )
                {
                    saveSlotHasData = saveslots[selected];
                    getSaveStatePath(tmppath, sizeof(tmppath), selected);
                } else {
                    // Auto save slot
                    getAutoSaveStatePath(tmppath, sizeof(tmppath));
                    saveSlotHasData = autosaveFileExists;
                }
                bool proceed = true;
                if (saveSlotHasData)
                {

                    while (true)
                    {
                        // Overwrite confirmation
                        redraw(1, selected);
                        RomSelect_PadState(&pad);
                        Menu_LoadFrame();
                        if (pad & A)
                        {
                            proceed = true;
                            break;
                        }
                        if (pad & B)
                        {
                            proceed = false;
                            break;
                        }
                    }
                    if (!proceed)
                    {
                        continue;
                    }
                }

                // Ensure save state directories exist
                if (ensureSaveStateDirectories(crc))
                {

                    // Save file
                    printf("Saving state to slot %d: %s\n", selected, tmppath);
                    if (savestatefunc(tmppath) == 0)
                    {
                        printf("Save state saved to slot %d: %s\n", selected + 1, tmppath);
                        if ( selected < MAXSAVESTATESLOTS )
                        {
                            saveslots[selected] = 1;
                        } else {
                            autosaveFileExists = true;
                        }
                        saved = true;
                    }
                    else
                    {
                        showMessageBox("Save failed.", CRED);
                    }
                }
            }
        }
    }
    ClearScreen(CBLACK);
    waitForNoButtonPress();

#if !HSTX
    scaleMode8_7_ = Frens::applyScreenMode(settings.screenMode);
    if (!Frens::isFrameBufferUsed())
    {
        dvi_->getBlankSettings().top = margintop;
        dvi_->getBlankSettings().bottom = marginbottom;
    }
#else
    hstx_setScanLines(settings.flags.scanlineOn);
#endif
    Frens::PaceFrames60fps(true);
    printf("Exiting save state menu.\n");
    Frens::f_free(screenBuffer);
    return saveStateLoadedOK;
}

#if HW_CONFIG == 8
#include "wavplayer.h"
#endif // HW_CONFIG == 8
// --- Settings Menu Implementation ---
// returns 0 if no changes, 1 if settings applied
//         2 start screensaver
//         3 exit to menu
int showSettingsMenu(bool calledFromGame)
{
    bool settingsChanged = false;
    int rval = 0;
    int margintop = 0;
    int marginbottom = 0;

    // Allocate screen buffer if called from game
    if (calledFromGame)
    {
#if 0
        assert(altscreenBufferSize >= screenbufferSize);
        FIL fil;
        FRESULT fr;
        size_t bw;
        fr = f_open(&fil, "/swapfile.DAT", FA_WRITE | FA_CREATE_ALWAYS);
        if (fr == FR_OK) {
            
            fr = f_write(&fil, altscreenBuffer, altscreenBufferSize, &bw);
            if (fr != FR_OK || bw != altscreenBufferSize) {
                printf("Error writing swapfile.DAT: %d, written %d bytes\n", fr, bw);
            } else {
                printf("Wrote %d bytes to swapfile.DAT\n", bw);
            }
            f_close(&fil);
            printf("%d bytes successfully written to swapfile.DAT.\n", altscreenBufferSize);
        } else {
            printf("Error opening swapfile.DAT for writing: %d\n", fr);
        }
        // exit if file operation failed
        if (fr != FR_OK || bw != altscreenBufferSize) {
            return 0;
        }
        screenBuffer = (charCell *)altscreenBuffer;
#else
        screenBuffer = (charCell *)Frens::f_malloc(screenbufferSize);
#if ENABLE_VU_METER
        turnOffAllLeds();
#endif
#endif
#if !HSTX
        margintop = dvi_->getBlankSettings().top;
        marginbottom = dvi_->getBlankSettings().bottom;
        printf("Top margin: %d, bottom margin: %d\n", margintop, marginbottom);
        // Use the entire screen resolution of 320x240 pixels. This makes a 40x30 screen with 8x8 font possible.
        scaleMode8_7_ = Frens::applyScreenMode(ScreenMode::NOSCANLINE_1_1);
        dvi_->getBlankSettings().top = 0;
        dvi_->getBlankSettings().bottom = 0;
#else
        hstx_setScanLines(false);
#endif
    }

    // Local working copy of settings.
    struct settings *workingDyn = (struct settings *)Frens::f_malloc(sizeof(settings));
    if (!workingDyn)
    {
        return false; // allocation failed
    }
    *workingDyn = settings;                 // copy current settings into dynamic block
    struct settings &working = *workingDyn; // keep existing code unchanged (reference alias)
    // Ensure current screenMode is valid; if not, pick first available
#if !HSTX
    {
        int cur = static_cast<int>(working.screenMode);
        if (cur < 0 || cur > 3 || !g_available_screen_modes[cur])
        {
            // find first available
            for (int i = 0; i < 4; ++i)
            {
                if (g_available_screen_modes[i])
                {
                    working.screenMode = static_cast<ScreenMode>(i);
                    break;
                }
            }
        }
    }
#endif

    // Screen row indices:
    // 0: Title (non-selectable)
    // 1..visibleCount: options
    // visibleCount+1: SAVE
    // visibleCount+2: CANCEL
    // visibleCount+3: DEFAULT
    int visibleIndices[MOPT_COUNT];
    int visibleCount = 0;
    for (int i = 0; i < MOPT_COUNT; ++i)
    {
        // -1 is always hidden
        if (g_settings_visibility[i] >= 0)
        {
            if (g_settings_visibility[i] || (i == MenuSettingsIndex::MOPT_EXIT_GAME || i == MenuSettingsIndex::MOPT_SAVE_RESTORE_STATE) && calledFromGame)
            {
                visibleIndices[visibleCount++] = i;
            }
        }
    }
    // Layout rows:
    // 0: title
    // 1: blank spacer after title
    // 2 .. 2+visibleCount-1 : options
    // spacerAfterOptionsRow (blank)
    // paletteStartRow .. paletteStartRow+3 : 4 rows of 16 color blocks (64 colors total)
    // paletteInfoRow: textual FG/BG info using working colors
    // spacerAfterPaletteRow (blank)
    // SAVE
    // CANCEL
    // DEFAULT
    const int rowStartOptions = 2;
    const int spacerAfterOptionsRow = rowStartOptions + visibleCount; // first spacer (blank)
    const int paletteStartRow = spacerAfterOptionsRow + 1;
    const int paletteRowCount = 4;                                // 4 x 16 = 64
    const int paletteInfoRow = paletteStartRow + paletteRowCount; // textual line
    const int spacerAfterPaletteRow = paletteInfoRow + 1;
    const int saveRowScreen = spacerAfterPaletteRow + 1;
    const int cancelRowScreen = saveRowScreen + 1;
    const int defaultRowScreen = cancelRowScreen + 1;
    const int helpRowScreen = defaultRowScreen + 2; // extra spacer before help line
    int selectedRowLocal = rowStartOptions;         // first selectable option row
    bool exitMenu = false;
    bool applySettings = false; // true when SAVE, false when CANCEL
    // lambda to redraw the entire menu
    auto redraw = [&]()
    {
        getButtonLabels(buttonLabel1, buttonLabel2);
        ClearScreen(CWHITE); // Always white background

        int row = 0;

        // Centered Title
        constexpr int titleLen = 13; // "-- Settings --"
        int titleCol = (SCREEN_COLS - titleLen) / 2;
        if (titleCol < 0)
            titleCol = 0;
        putText(titleCol, row++, "-- Settings --", CBLACK, CWHITE);
        // Blank spacer line
        putText(0, row++, "", CBLACK, CWHITE);
        // Render each visible option
        for (int vi = 0; vi < visibleCount; ++vi)
        {
            int optIndex = visibleIndices[vi];
            const char *label = "";
            const char *value = "";
            switch (optIndex)
            {
            case MenuSettingsIndex::MOPT_EXIT_GAME:
            {
                if (calledFromGame)
                {
                    label = "Quit game";
                }
                else
                {
                    label = "Back to main menu";
                }
                value = "";
                break;
            }
            case MenuSettingsIndex::MOPT_SAVE_RESTORE_STATE:
            {
                label = "Save/Load State";
                value = "";
                break;
            }
            case MenuSettingsIndex::MOPT_SCREENMODE:
            {
                label = "Screen Mode";
                switch (working.screenMode)
                {
                case ScreenMode::SCANLINE_1_1:
                    value = "1:1 SCANLINES";
                    break;
                case ScreenMode::SCANLINE_8_7:
                    value = "8:7 SCANLINES";
                    break;
                case ScreenMode::NOSCANLINE_1_1:
                    value = "1:1 NO SCANLINES";
                    break;
                case ScreenMode::NOSCANLINE_8_7:
                    value = "8:7 NO SCANLINES";
                    break;
                default:
                    value = "?";
                    break;
                }
                // If current mode is not available show marker
                if (!g_available_screen_modes[static_cast<int>(working.screenMode)])
                {
                    value = "None"; // fallback when all disabled
                }
                break;
            }
            case MenuSettingsIndex::MOPT_SCANLINES:
            {
#if HSTX
                label = "Scanlines";
                value = working.flags.scanlineOn ? "ON" : "OFF";
#else
                // When !HSTX the scanlines are encoded in screen mode and this option is hidden via visibility array.
                label = "Scanlines";
                value = "-";
#endif
                break;
            }
            case MenuSettingsIndex::MOPT_FPS_OVERLAY:
            {
                label = "Framerate Overlay";
                value = working.flags.displayFrameRate ? "ON" : "OFF";
                break;
            }
            case MenuSettingsIndex::MOPT_AUDIO_ENABLE:
            {
                label = "Audio enabled";
                value = working.flags.audioEnabled ? "ON" : "OFF";
                break;
            }
            case MenuSettingsIndex::MOPT_EXTERNAL_AUDIO:
            {
                label = "External Audio";
                value = working.flags.useExtAudio ? "Enable" : "Disable";
                break;
            }
            case MenuSettingsIndex::MOPT_FONT_COLOR:
            {
                label = "Menu Font Color";
                snprintf(valueBuf, sizeof(valueBuf), "%d", working.fgcolor);
                value = valueBuf;
                break;
            }
            case MenuSettingsIndex::MOPT_FONT_BACK_COLOR:
            {
                label = "Menu Font Back Color";
                snprintf(valueBuf, sizeof(valueBuf), "%d", working.bgcolor);
                value = valueBuf;
                break;
            }
            case MenuSettingsIndex::MOPT_FRUITJAM_VUMETER:
            {
                label = "Fruit Jam VU Meter";
                value = working.flags.enableVUMeter ? "ON" : "OFF";
                break;
            }
            case MenuSettingsIndex::MOPT_FRUITJAM_INTERNAL_SPEAKER:
            {
                label = "Fruit Jam Internal Speaker";
                value = working.flags.fruitJamEnableInternalSpeaker ? "ON" : "OFF";
                break;
            }
            case MenuSettingsIndex::MOPT_FRUITJAM_VOLUME_CONTROL:
            {
                label = "Fruit Jam Volume Control";
                sprintf(valueBuf, "%d", working.fruitjamVolumeLevel)    ;
                value = valueBuf;
                break;
            }
            case MenuSettingsIndex::MOPT_DMG_PALETTE:
            {
                label = "DMG Palette";
                switch (working.flags.dmgLCDPalette)
                {
                case 0:
                    value = "Green";
                    break;
                case 1:
                    value = "Color";
                    break;
                case 2:
                    value = "Black & White";
                    break;
                default:
                    value = "?";
                    break;
                }
                break;
            }
            case MenuSettingsIndex::MOPT_BORDER_MODE:
            {
                label = "Border Mode";
                switch (working.flags.borderMode)
                {
                case FrensSettings::DEFAULTBORDER:
                    value = "Super Gameboy Default";
                    break;
                case FrensSettings::RANDOMBORDER:
                    value = "Super Gameboy Random";
                    break;
                case FrensSettings::THEMEDBORDER:
                    value = "Game-Specific";
                    break;
                default:
                    value = "?";
                    break;
                }
                break;
            }
            case MenuSettingsIndex::MOPT_FRAMESKIP:
            {
                label = "Frame Skip";
                value = working.flags.frameSkip ? "ON" : "OFF";
                break;
            }
            case MenuSettingsIndex::MOPT_RAPID_FIRE_ON_A:
            {
                if (strcmp(buttonLabel1, "B") == 0)
                {
                    label = "Rapid Fire on B";
                }
                else
                {
                    label = "Rapid Fire on A";
                }
                value = working.flags.rapidFireOnA ? "ON" : "OFF";
                break;
            }
            case MenuSettingsIndex::MOPT_RAPID_FIRE_ON_B:
            {
                if (strcmp(buttonLabel2, "A") == 0)
                {
                    label = "Rapid Fire on A";
                }
                else
                {
                    label = "Rapid Fire on B";
                }
                value = working.flags.rapidFireOnB ? "ON" : "OFF";
                break;
            }
            default:
                label = "Unknown";
                value = "";
                break;
            }
            snprintf(line, sizeof(line), "%s%s%s", label, (optIndex == MOPT_EXIT_GAME || optIndex == MOPT_SAVE_RESTORE_STATE) ? "" : ": ", value);
            putText(0, row++, line, CBLACK, CWHITE);
        }
        // Blank spacer after last option
        putText(0, row++, "", CBLACK, CWHITE);
        // 64-color palette grid (4 rows x 16 columns). Each block is a space with fg=bg=colorIndex
        int blocksPerRow = 16;
        int blockRows = paletteRowCount;
        int gridWidth = blocksPerRow; // one char per block
        int gridStartCol = (SCREEN_COLS - gridWidth) / 2;
        if (gridStartCol < 0)
            gridStartCol = 0;
        for (int pr = 0; pr < blockRows; ++pr)
        {
            char tmp[4];
            snprintf(tmp, sizeof(tmp), "%02d", pr * blocksPerRow);
            putText(gridStartCol - 2, row, tmp, CBLACK, CWHITE); // row label
            for (int pc = 0; pc < blocksPerRow; ++pc)
            {
                int colorIndex = pr * blocksPerRow + pc;
                if (colorIndex < 64)
                {
                    putText(gridStartCol + pc, row, " ", colorIndex, colorIndex);
                }
            }
            row++;
        }
        // FG/BG info line centered
        snprintf(line, sizeof(line), "FG=%02d BG=%02d", working.fgcolor, working.bgcolor);
        int infoLen = (int)strlen(line);
        int infoCol = (SCREEN_COLS - infoLen) / 2;
        if (infoCol < 0)
            infoCol = 0;
        putText(infoCol, row++, line, working.fgcolor, working.bgcolor);
        // Spacer after palette/info
        putText(0, row++, "", CBLACK, CWHITE);
        if (settingsChanged)
        {
            putText(0, row++, "SAVE *", CBLACK, CWHITE);
        }
        else
        {
            putText(0, row++, "SAVE", CBLACK, CWHITE);
        }
        putText(0, row++, "CANCEL", CBLACK, CWHITE);
        putText(0, row++, "DEFAULT", CBLACK, CWHITE);
        // Help text (dynamic button labels)

        if (selectedRowLocal < saveRowScreen)
        {
            if (visibleIndices[selectedRowLocal - rowStartOptions] == MOPT_EXIT_GAME ||
                visibleIndices[selectedRowLocal - rowStartOptions] == MOPT_SAVE_RESTORE_STATE)
            {
                snprintf(line, sizeof(line), "UP/DOWN: Move, %s: select", buttonLabel1);
            }
            else
            {
                strcpy(line, "UP/DOWN: Move, LEFT/RIGHT: Change");
            }
        }
        else
        {
            strcpy(line, "UP/DOWN: Move");
        }

        int helpCount = 2;
        row = SCREEN_ROWS - helpCount - 1; // leave one blank row at bottom
        int hlen = (int)strlen(line);
        int col = (SCREEN_COLS - hlen) / 2;
        if (col < 0)
            col = 0;
        putText(col, row++, line, CBLACK, CWHITE);
        if (selectedRowLocal == saveRowScreen)
        {
            snprintf(line, sizeof(line), "%s: Confirm changes", buttonLabel1);
        }
        else if (selectedRowLocal == cancelRowScreen)
        {
            snprintf(line, sizeof(line), "%s: Discard changes", buttonLabel1);
        }
        else if (selectedRowLocal == defaultRowScreen)
        {
            snprintf(line, sizeof(line), "%s: Restore defaults", buttonLabel1);
        }
        else
        {
            strcpy(line, ""); // no second line
        }
        // display helptext
        if (selectedRowLocal >= rowStartOptions && selectedRowLocal < rowStartOptions + visibleCount)
        {
            putText(0, helpRowScreen, g_settings_descriptions[visibleIndices[selectedRowLocal - rowStartOptions]], CBLACK, CWHITE);
        }
        else
        {
            putText(0, helpRowScreen, "                                        ", CBLACK, CWHITE);
        }

        // snprintf(line, sizeof(line),
        //          "%s: SAVE/CANCEL/DEFAULT,  %s: Cancel",
        //          buttonLabel1, buttonLabel2);
        hlen = (int)strlen(line);
        col = (SCREEN_COLS - hlen) / 2;
        if (col < 0)
            col = 0;
        putText(col, row++, line, CBLACK, CWHITE);
        snprintf(line, sizeof(line),
                 "Press %s to go back.", buttonLabel2);
        hlen = (int)strlen(line);
        col = (SCREEN_COLS - hlen) / 2;
        if (col < 0)
            col = 0;
        putText(col, row++, line, CBLACK, CWHITE);
#if 0
        putText(0, helpRowScreen + 3, "System info:", CBLACK, CWHITE);
        Frens::getFsInfo(line, sizeof(line));
        putText(1, helpRowScreen + 4, "SD:", CBLACK, CWHITE);
        putText(5, helpRowScreen + 4, line, CBLACK, CWHITE);
#endif
        drawAllLines(selectedRowLocal);
    }; // redraw lambda
#if HW_CONFIG == 8
    // Initialize menu music
    ///wavplayer::init_memory();
    // Optional: set offset and/or switch to file
    // wavplayer::set_offset_seconds(0.8f); // skip initial silence
    // Uncomment to stream from a file on SD (must be a valid PCM 16-bit stereo WAV)
    char wavPath[40];
    strcpy(wavPath, RECORDEDSAMPLEFILE);
    if ( !Frens::fileExists(wavPath) ) {
        snprintf(wavPath, sizeof(wavPath), DEFAULTSAMPLEFILEFORMAT, FrensSettings::getEmulatorTypeString());
        printf("Menu music file not found at /soundrecorder.wav, trying %s\n", wavPath);
    }
    if (wavplayer::use_file(wavPath)) {
        printf("Streaming menu music from file.\n");
    }
#endif

    waitForNoButtonPress();
    int startFrames = -1;
    while (!exitMenu)
    {
        // Always redraw before reading pad state (requested behavior)
        settingsChanged = (memcmp(&working, &settings, sizeof(settings)) != 0);
        redraw();
        DWORD pad;
        RomSelect_PadState(&pad);
        auto frameCount = Menu_LoadFrame();
        if (startFrames == -1)
        {
            startFrames = frameCount;
        }
        bool pushed = pad != 0;
        int optIndex = -1;
        if (selectedRowLocal >= rowStartOptions && selectedRowLocal < rowStartOptions + visibleCount)
        {
            optIndex = visibleIndices[selectedRowLocal - rowStartOptions]; // map screen row to option index
        }
        if ( optIndex == MOPT_FRUITJAM_VOLUME_CONTROL) {
            // start audio stream for volume adjustment feedback
#if HW_CONFIG == 8
        if (wavplayer::ready()) {
            wavplayer::pump(wavplayer::sample_rate() / 60);
        }
#endif

        }
        if (pushed)
        {
            startFrames = frameCount; // reset idle counter
            // detect SELECT + START
            if ((pad & SELECT) && (pad & START))
            {
                // abort without changes
                exitMenu = true;
                applySettings = false;
                rval = 3; // exit to main menu
                continue;
            }

            if (pad & UP)
            {
                if (selectedRowLocal > rowStartOptions)
                {
                    do
                    {
                        selectedRowLocal--;
                        // printf("Selected row: %d\n", selectedRowLocal);
                    } while (
                        selectedRowLocal == spacerAfterOptionsRow ||
                        (selectedRowLocal >= paletteStartRow && selectedRowLocal < paletteStartRow + paletteRowCount) ||
                        selectedRowLocal == paletteInfoRow ||
                        selectedRowLocal == spacerAfterPaletteRow);
                }
                else
                {
                    selectedRowLocal = defaultRowScreen; // wrap
                }
            }
            else if (pad & DOWN)
            {
                if (selectedRowLocal < defaultRowScreen)
                {
                    do
                    {
                        selectedRowLocal++;
                        // printf("Selected row: %d\n", selectedRowLocal);
                    } while (
                        selectedRowLocal == spacerAfterOptionsRow ||
                        (selectedRowLocal >= paletteStartRow && selectedRowLocal < paletteStartRow + paletteRowCount) ||
                        selectedRowLocal == paletteInfoRow ||
                        selectedRowLocal == spacerAfterPaletteRow);
                }
                else
                {
                    selectedRowLocal = rowStartOptions; // wrap
                }
            }
            else if (pad & LEFT || pad & RIGHT || ((pad & A) && (optIndex == MOPT_EXIT_GAME || optIndex == MOPT_SAVE_RESTORE_STATE)))
            {
                if (optIndex != -1)
                {
                    // int optIndex = visibleIndices[selectedRowLocal - rowStartOptions]; // map screen row to option index
                    bool right = pad & RIGHT;
                    switch (optIndex)
                    {
                    case MOPT_EXIT_GAME:
                    {
                        rval = 3; // exit to main menu
                        exitMenu = true;
                        break;
                    }
                    case MOPT_SAVE_RESTORE_STATE:
                    {
                        rval = 4; // save/restore state
                        exitMenu = true;
                        break;
                    }
                    case MOPT_SCREENMODE:
                    {
                        // Filter cycling: skip unavailable modes using enumeration order (0..3)
                        int cur = static_cast<int>(working.screenMode);
                        // Count available modes
                        int availableCount = 0;
                        for (int i = 0; i < 4; ++i)
                            if (g_available_screen_modes[i])
                                availableCount++;
                        if (availableCount == 0)
                        { /* nothing selectable */
                            break;
                        }
                        if (right)
                        {
                            for (int step = 0; step < 4; ++step)
                            {
                                cur = (cur + 1) & 3; // wrap 0..3
                                if (g_available_screen_modes[cur])
                                {
                                    working.screenMode = static_cast<ScreenMode>(cur);
                                    break;
                                }
                            }
                        }
                        else
                        { // left
                            for (int step = 0; step < 4; ++step)
                            {
                                cur = (cur + 3) & 3; // equivalent to -1 & 3
                                if (g_available_screen_modes[cur])
                                {
                                    working.screenMode = static_cast<ScreenMode>(cur);
                                    break;
                                }
                            }
                        }
                        break;
                    }
                    case MOPT_SCANLINES:
                    {
#if HSTX
                        working.flags.scanlineOn = !working.flags.scanlineOn;
#endif
                        // !HSTX build will never have this option visible.
                        break;
                    }
                    case MOPT_FPS_OVERLAY:
                        working.flags.displayFrameRate = !working.flags.displayFrameRate;
                        break;
                    case MOPT_AUDIO_ENABLE:
                        working.flags.audioEnabled = !working.flags.audioEnabled;
                        working.flags.frameSkip = working.flags.audioEnabled;
                        break;
                    case MOPT_EXTERNAL_AUDIO:
                        working.flags.useExtAudio = !working.flags.useExtAudio;
                        break;
                    case MOPT_FONT_COLOR:
                    {
                        if (right)
                        {
                            working.fgcolor = (working.fgcolor + 1) % 64;
                        }
                        else
                        {
                            working.fgcolor = (working.fgcolor == 0 ? 63 : working.fgcolor - 1);
                        }
                        break;
                    }
                    case MOPT_FONT_BACK_COLOR:
                    {
                        if (right)
                        {
                            working.bgcolor = (working.bgcolor + 1) % 64;
                        }
                        else
                        {
                            working.bgcolor = (working.bgcolor == 0 ? 63 : working.bgcolor - 1);
                        }
                        break;
                    }
                    case MOPT_FRUITJAM_VUMETER:
                        working.flags.enableVUMeter = !working.flags.enableVUMeter;
                        break;
                    case MOPT_DMG_PALETTE:
                    {
                        if (right)
                        {
                            working.flags.dmgLCDPalette = (working.flags.dmgLCDPalette + 1) % 3;
                        }
                        else
                        {
                            working.flags.dmgLCDPalette = (working.flags.dmgLCDPalette == 0 ? 2 : working.flags.dmgLCDPalette - 1);
                        }
                        break;
                    }
                    case MOPT_BORDER_MODE:
                    {
                        if (right)
                        {
                            working.flags.borderMode = (working.flags.borderMode + 1) % 3;
                        }
                        else
                        {
                            working.flags.borderMode = (working.flags.borderMode == 0 ? 2 : working.flags.borderMode - 1);
                        }
                        break;
                    }
                    case MOPT_FRAMESKIP:
                    {
                        working.flags.frameSkip = !working.flags.frameSkip;
                        break;
                    }
                    case MOPT_FRUITJAM_INTERNAL_SPEAKER:
                    {
                        working.flags.fruitJamEnableInternalSpeaker = !working.flags.fruitJamEnableInternalSpeaker;
                        break;
                    }
                    case MOPT_FRUITJAM_VOLUME_CONTROL:
                    {
                        if (right)
                        {
                            if (working.fruitjamVolumeLevel < 24)
                            {
                                working.fruitjamVolumeLevel++;
                                EXT_AUDIO_SETVOLUME(working.fruitjamVolumeLevel);
                            }
                        }
                        else
                        {
                            if (working.fruitjamVolumeLevel > -63)
                            {
                                working.fruitjamVolumeLevel--;
                                EXT_AUDIO_SETVOLUME(working.fruitjamVolumeLevel);
                            }
                        }
                        break;
                    }
                    case MOPT_RAPID_FIRE_ON_A:
                    {

                        working.flags.rapidFireOnA = !working.flags.rapidFireOnA;

                        break;
                    }
                    case MOPT_RAPID_FIRE_ON_B:
                    {

                        working.flags.rapidFireOnB = !working.flags.rapidFireOnB;

                        break;
                    }
                    default:
                        break;
                    }
                }
            }
            else if (pad & A)
            {
                if (selectedRowLocal == saveRowScreen)
                {
                    applySettings = true;
                    exitMenu = true;
                }
                else if (selectedRowLocal == cancelRowScreen)
                {
                    applySettings = false;
                    exitMenu = true;
                }
                else if (selectedRowLocal == defaultRowScreen)
                {
                    FrensSettings::resetsettings(&working);
                }
            }
            else if (pad & B)
            {
                // B acts like cancel
                applySettings = false;
                exitMenu = true;
            }
        }
        if (frameCount - startFrames > 3600)
        {
            // if no input for 3600 frames, start screensaver
            rval = 2;
            break;
        }
    }
    if (applySettings && rval == 0)
    {
        // Copy working settings into global settings and persist.
        // Preserve directory navigation fields that user did not edit here.
        working.firstVisibleRowINDEX = settings.firstVisibleRowINDEX;
        working.selectedRow = settings.selectedRow;
        working.horzontalScrollIndex = settings.horzontalScrollIndex;
        strcpy(working.currentDir, settings.currentDir);
        settings = working;
        FrensSettings::savesettings();
        rval = 1;
    }
    Frens::f_free(workingDyn);
    // restore contents of swap file back to altScreenbuffer when not nullptr
    if (calledFromGame)
    {
#if 0
        FIL fil;
        FRESULT fr;
        fr = f_open(&fil, "/swapfile.DAT", FA_READ);
        if (fr == FR_OK) {
            size_t br;
            fr = f_read(&fil, altscreenBuffer, altscreenBufferSize, &br);
            if (fr != FR_OK || br != altscreenBufferSize) {
                printf("Error reading swapfile.DAT: %d, read %d bytes\n", fr, br);
            } else {        
                printf("Read %d bytes from swapfile.DAT\n", br);
            }
            f_close(&fil);
            // delete the swap file
            fr = f_unlink("/swapfile.DAT");
            if (fr != FR_OK) {
                printf("Error deleting swapfile.DAT: %d\n", fr);
            } else {
                printf("Deleted swapfile.DAT\n");
            }
        } else {
            printf("Error opening swapfile.DAT for reading: %d\n", fr);
        }
#else
        Frens::f_free((void *)screenBuffer);
#endif
        ClearScreen(CBLACK); // Removes artifacts from previous screen
                             // Wait until user has released all buttons
        waitForNoButtonPress();
#if !HSTX
        scaleMode8_7_ = Frens::applyScreenMode(settings.screenMode);
        // Reset the screen mode to the original settings
        // Do not reset the margins when framebuffer is used, this will lock up the display driver
        // Margins will be handled by the framebuffer.
        if (!Frens::isFrameBufferUsed())
        {
            dvi_->getBlankSettings().top = margintop;
            dvi_->getBlankSettings().bottom = marginbottom;
        }
#else
        // Restore scanline setting
        hstx_setScanLines(settings.flags.scanlineOn);
#endif
        // Speaker can be muted/unmuted from settings menu
        EXT_AUDIO_MUTE_INTERNAL_SPEAKER(settings.flags.fruitJamEnableInternalSpeaker == 0);
        EXT_AUDIO_SETVOLUME(settings.fruitjamVolumeLevel);
        Frens::PaceFrames60fps(true);
    }
    wavplayer::reset(); // stop menu music
    return rval;
}

void menu(const char *title, char *errorMessage, bool isFatal, bool showSplash, const char *allowedExtensions, char *rompath)
{
    FRESULT fr;
    FIL fil;
    DWORD PAD1_Latch;
    char curdir[FF_MAX_LFN];
#if !PICO_RP2350
    EXT_AUDIO_DISABLE();
#endif
#if ENABLE_VU_METER
    turnOffAllLeds();
#endif
    artworkEnabled = isArtWorkEnabled();
    crcOffset = FrensSettings::getEmulatorType() == FrensSettings::emulators::NES ? 16 : 0; // crc offset according to  https://github.com/ducalex/retro-go-covers
    printf("Emulator: %s, crcOffset: %d\n", FrensSettings::getEmulatorTypeString(), crcOffset);
#if !HSTX
    int margintop = dvi_->getBlankSettings().top;
    int marginbottom = dvi_->getBlankSettings().bottom;
    printf("Top margin: %d, bottom margin: %d\n", margintop, marginbottom);
    // Use the entire screen resolution of 320x240 pixels. This makes a 40x30 screen with 8x8 font possible.
    scaleMode8_7_ = Frens::applyScreenMode(ScreenMode::NOSCANLINE_1_1);
    dvi_->getBlankSettings().top = 0;
    dvi_->getBlankSettings().bottom = 0;
#else
    hstx_setScanLines(false);
#endif
    abSwapped = 1; // Swap A and B buttons, so menu is consistent accrross different emilators
    Frens::PaceFrames60fps(true);
    //
    menutitle = (char *)title;
    int totalFrames = -1;
    if (settings.selectedRow <= 0)
    {
        settings.selectedRow = STARTROW;
    }
    globalErrorMessage = errorMessage;

    printf("Starting Menu\n");
    // allocate buffers

    printf("Allocating %d bytes for screenbuffer\n", screenbufferSize);
    screenBuffer = (charCell *)Frens::f_malloc(screenbufferSize); // (charCell *)InfoNes_GetRAM(&ramsize);
    size_t directoryContentsBufferSize = 32768;
    // void *buffer = (void *)Frens::f_malloc(directoryContentsBufferSize); // InfoNes_GetChrBuf(&chr_size);
    Frens::RomLister romlister(directoryContentsBufferSize, allowedExtensions);

    if (strlen(errorMessage) > 0)
    {
        if (isFatal) // SD card not working, show error
        {
            DisplayFatalError(errorMessage);
        }
        else
        {
            showMessageBox("An error has occured", CRED, errorMessage);
        }
        showSplash = false;
    }
#if USE_I2S_AUDIO == PICO_AUDIO_I2S_DRIVER_TLV320
    if (EXT_AUDIO_DACERROR())
    {
        DisplayDacError();
    }
#endif
    if (showSplash)
    {
        showSplash = false;
        printf("Showing splash screen\n");
        showSplashScreen();
    }
    srand(get_rand_32()); // Seed the random number generator for screensaver
    romlister.list(settings.currentDir);
    displayRoms(romlister, settings.firstVisibleRowINDEX);
    bool startGame = false;


    waitForNoButtonPress();
    while (1)
    {

        auto frameCount = Menu_LoadFrame();
        // Pump background audio each frame (approx 60 FPS -> ~735 frames @ 44.1kHz)
// #if HW_CONFIG == 8
//         if (wavplayer::ready()) {
//             wavplayer::pump(wavplayer::sample_rate() / 60);
//         }
// #endif
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
        if (PAD1_Latch > 0 || startGame)
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
                fr = f_getcwd(settings.currentDir, FF_MAX_LFN); // f_getcwd(settings.currentDir, FF_MAX_LFN);
                if (fr == FR_OK)
                {

                    if (strcmp(settings.currentDir, "/") != 0)
                    {
                        romlister.list("..");
                        settings.firstVisibleRowINDEX = 0;
                        settings.selectedRow = STARTROW;
                        displayRoms(romlister, settings.firstVisibleRowINDEX);
                        fr = f_getcwd(settings.currentDir, FF_MAX_LFN); // f_getcwd(settings.currentDir, FF_MAX_LFN);
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
            else if ((PAD1_Latch & SELECT) == SELECT)
            {
#if PICO_RP2350
#else
#if HW_CONFIG == 1
                // On RP2040 with Pimoroni Pico DV Demo base, intermittent crashes have been observed in the settings menu
                // from within the SMS emulator only.
                // As a workaround, disable the settings menu for SMS emulator on HW_CONFIG 1.
                // Settings can still be opened in-game.
                if (FrensSettings::getEmulatorType() == FrensSettings::emulators::SMS)
                {
                    // printf("Settings menu not available for SMS emulator\n");
                    continue; // skip other processing this frame
                }
#endif
#endif
                // Open settings menu
                auto settingsResult = showSettingsMenu();
                if (settingsResult == 1)
                {
                    // reload rom list to apply possible changes
                    romlister.list(settings.currentDir);
                }
                if (settingsResult == 2)
                {
                    // start screensaver
                    screenSaver();
                }
                displayRoms(romlister, settings.firstVisibleRowINDEX);
                continue; // skip other processing this frame
            }
            else if ((PAD1_Latch & START) == START && ((PAD1_Latch & SELECT) != SELECT))
            {
#if 0
                showLoadingScreen();
                // reboot and start emulator with currently loaded game
                // Create a file /START indicating not to reflash the already flashed game
                // The emulator will delete this file after loading the game
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

#else

#if 0
                    showLoadingScreen();
                    // reboot and start emulator with currently loaded game
                    // Create a file /START indicating not to reflash the already flashed game
                    // The emulator will delete this file after loading the game
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
#endif

                // show screen with ArtWork

                if (!entries[index].IsDirectory && selectedRomOrFolder && artworkEnabled)
                {
                    // if (strcmp(emulator, "MD") == 0)
                    // {
                    showLoadingScreen("Metadata loading...");
                    //}
                    // romlister.ClearMemory();
                    fr = f_getcwd(curdir, sizeof(curdir)); // f_getcwd(curdir, sizeof(curdir));
                    FSIZE_t romsize = 0;
                    // printf("Current dir: %s\n", curdir);
                    uint32_t crc = GetCRCOfRomFile(curdir, selectedRomOrFolder, rompath, romsize);
                    int startAction = showartwork(crc, romsize);
                    switch (startAction)
                    {
                    case 0:
                        break;
                    case 1:
                        startGame = true;
                        break;
                    case 2:

                        screenSaver();

                        break;
                    default:
                        break;
                    }
                    romlister.list(curdir);
                    displayRoms(romlister, settings.firstVisibleRowINDEX);
                }

#endif
            }
            else if (startGame || ((PAD1_Latch & A) == A && selectedRomOrFolder))
            {
                if (entries[index].IsDirectory && !startGame)
                {
                    romlister.list(selectedRomOrFolder);
                    settings.firstVisibleRowINDEX = 0;
                    settings.selectedRow = STARTROW;
                    displayRoms(romlister, settings.firstVisibleRowINDEX);
                    // get full path name of folder
                    fr = f_getcwd(settings.currentDir, FF_MAX_LFN); //  f_getcwd(settings.currentDir, FF_MAX_LFN);
                    if (fr != FR_OK)
                    {
                        printf("Cannot get current dir: %d\n", fr);
                    }
                    printf("Current dir: %s\n", settings.currentDir);
                }
                else
                {
                    showLoadingScreen();
                    fr = f_getcwd(curdir, sizeof(curdir)); // f_getcwd(curdir, sizeof(curdir));
                    printf("Current dir: %s\n", curdir);
                    if (Frens::isPsramEnabled())
                    {
                        loadRomInPsRam(curdir, selectedRomOrFolder, rompath, errorInSavingRom);
                    }
                    else
                    {
                        // If PSRAM is not enabled, we need to create a file with the full path name of the rom and reboot.
                        // The emulator will read this file and flash the rom in main.cpp.
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
                    if (!errorInSavingRom)
                    {
                        break; // from while(1) loop, so we can reboot or return to main.cpp
                    }
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
            // romlister.ClearMemory();
            screenSaver();
            romlister.list(".");
            displayRoms(romlister, settings.firstVisibleRowINDEX);
        }
    } // while 1

    ClearScreen(CBLACK); // Removes artifacts from previous screen
                         // Wait until user has released all buttons
    waitForNoButtonPress();
    Frens::f_free(screenBuffer);
    // Frens::f_free(buffer);

    FrensSettings::savesettings();
#if !HSTX
    scaleMode8_7_ = Frens::applyScreenMode(settings.screenMode);
    // Reset the screen mode to the original settings
    // Do not reset the margins when framebuffer is used, this will lock up the display driver
    // Margins will be handled by the framebuffer.
    if (!Frens::isFrameBufferUsed())
    {
        dvi_->getBlankSettings().top = margintop;
        dvi_->getBlankSettings().bottom = marginbottom;
    }
#endif
    // When PSRAM is not enabled, we need to reboot the system to start the emulator with the selected rom. In this case
    // a reboot is neccessary to avoid lockups.
    // If PSRAM is enabled, the rom is already loaded in PSRAM and the emulator will start the rom directly and we don't need to reboot.
    if (!Frens::isPsramEnabled())
    {
#if WII_PIN_SDA >= 0 and WII_PIN_SCL >= 0
        wiipad_end();
#endif
        // Don't return from this function call, but reboot in order to get avoid several problems with sound and lockups (WII-pad)
        // After reboot the emulator will flash the rom and start the selected game.
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
    Frens::restoreScanlines();
    Frens::PaceFrames60fps(true); // reset frame pacing
}
