#include <stdio.h>
#include <string.h>
#include <memory>
#include "pico.h"
#include "pico/stdlib.h"
#include "pico/rand.h"
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "hardware/divider.h"
#include "pico/bootrom.h"
#include "tusb.h"
#include "FrensHelpers.h"
#include "FrensFonts.h"
#include "gamepad.h"
#include "RomLister.h"
#include "recentgames.h"
#include "menu.h"
#include "nespad.h"
#include "wiipad.h"
#include "menu_settings.h"
#include "usb_msc.h"

#include "font_8x8.h"
#include "settings.h"
#include "ffwrappers.h"
#include "vumeter.h"
#include "DefaultSS.h"
#include <stdint.h>
#include "wavplayer.h"
const int8_t *g_settings_visibility;
const uint8_t *g_available_screen_modes;

// FDS disk-swap hooks. Null in builds (or ROM contexts) where FDS is
// not active; the menu option is also kept hidden in those cases via
// g_settings_visibility[MOPT_FDS_DISK_SWAP].
static const MenuFdsHooks *s_fdsHooks = nullptr;
void menuSetFdsHooks(const MenuFdsHooks *hooks) { s_fdsHooks = hooks; }

// LEFT/RIGHT preview the disk choice without committing — A on the
// option commits (or triggers Reset). -1 means "not initialised yet,
// use the live current side". Set to "current side" each time the
// menu opens.
static int s_fdsPendingChoice = -1;

// Cassette-deck hooks, same contract as the FDS ones above. Null unless an emulator
// registers them; the option is hidden via g_settings_visibility[MOPT_CASSETTE] too.
static const MenuCassetteHooks *s_cassetteHooks = nullptr;
void menuSetCassetteHooks(const MenuCassetteHooks *hooks) { s_cassetteHooks = hooks; }

// The deck's pending state, previewed by LEFT/RIGHT and committed by A. Encoded as one
// index into "Empty, then (Play, Record WAV, Record CAS) per tape" so a single value can
// be cycled through with no separate mode cursor. -1 means "seed from the live state".
static int s_cassettePendingChoice = -1;

// The deck has two things to choose - which tape and what to do with it - but only
// LEFT/RIGHT to choose with, so both are flattened into one cycle:
//
//   0            Empty (eject)
//   1 .. n       Play tape 0 .. n-1
//   n+1          Record to a new WAV      } these ask for a label on commit
//   n+2          Record to a new .cas     }
//   n+3          Rewind                   (offered only while a tape is playing)
//
// Same shape as the FDS "Reset" entry: an action sitting at the end of the value cycle.
enum { CAS_MODE_EMPTY = 0, CAS_MODE_PLAY = 1, CAS_MODE_REC_WAV = 2, CAS_MODE_REC_CAS = 3 };

static int cassetteIsPlaying()
{
    return (s_cassetteHooks && s_cassetteHooks->get_mode &&
            s_cassetteHooks->get_mode() == CAS_MODE_PLAY);
}

static int cassetteChoiceCount(int n) { return n + 3 + (cassetteIsPlaying() ? 1 : 0); }

// Where the live deck state sits in that cycle, used to seed the preview on menu open.
static int cassetteLiveChoice(int n)
{
    if (!s_cassetteHooks || !s_cassetteHooks->get_mode) return 0;
    int m = s_cassetteHooks->get_mode();
    int sel = s_cassetteHooks->get_selected ? s_cassetteHooks->get_selected() : -1;
    switch (m)
    {
        case CAS_MODE_PLAY:    return (sel >= 0 && sel < n) ? sel + 1 : 0;
        case CAS_MODE_REC_WAV: return n + 1;
        case CAS_MODE_REC_CAS: return n + 2;
        default:               return 0;
    }
}

static const char *cassetteChoiceLabel(int choice, int n, char *buf, size_t bufsize)
{
    if (choice == 0)     return "Empty";
    if (choice == n + 1) return "Record WAV";
    if (choice == n + 2) return "Record CAS";
    if (choice == n + 3) return "Rewind";

    const char *name = s_cassetteHooks->get_tape_name ? s_cassetteHooks->get_tape_name(choice - 1) : "";
    // Drop the extension - the mode already says which format it is.
    char stem[TAPE_LABEL_MAX];
    snprintf(stem, sizeof(stem), "%s", name);
    char *dot = strrchr(stem, '.');
    if (dot) *dot = 0;
    snprintf(buf, bufsize, "Play %s", stem);
    return buf;
}
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
const static char *connectedGamePadName[2];
const static char *connectedGamePadShortName[2];


#define SCREENBUFCELLS SCREEN_ROWS *SCREEN_COLS
charCell *screenBuffer;

static char *selectedRomOrFolder;
static bool errorInSavingRom = false;
static char *globalErrorMessage;
// Path picked in the recently played list, owned by menu() (heap, not stack -
// this is FF_MAX_LFN + 1 bytes). Shared with showSettingsMenu, which can open
// the same list. nullptr disables the feature rather than crashing.
static char *recentLaunchPath = nullptr;
// Set by startRom when the rom is already in flash and verified, so menu() can
// return to the emulator instead of rebooting. Only ever true when
// START_FLASHED_ROM_WITHOUT_REBOOT is enabled.
static bool skipRebootAfterMenu = false;

// static bool artworkEnabled = false;
static uint8_t crcOffset = 0; // Default offset for CRC calculation
#define LONG_PRESS_TRESHOLD (500)
#define REPEAT_DELAY (40)

static char buttonLabel1[2]; // e.g., "A", "B", "X", "O"
static char buttonLabel2[2]; // e.g., "A", "B", "
static char line[41];
static char valueBuf[16]; // separate buffer for numeric values
static bool exitMenu = false;
static bool settingsActive = false;
static WORD *WorkLineRom = nullptr;

#if PICO_RP2350
// Track current WAV playback path and state while in the menu
static char lastWavPath[FF_MAX_LFN] = {0};
#endif

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

// Menu bits for one GPIO port. A NES pad shifts out its buttons in menu order
// already (bit0=A, bit1=B, bit2=Select, ...), so that word is used as-is, the
// way this port has always worked. A SNES pad puts B and Y where a NES pad has
// A and B, and its A and X two bytes further up, so its four face buttons are
// named rather than taken positionally: A chooses and B goes back, matching USB
// and Wii Classic pads instead of moving "choose" onto B.
//
// Only a SNES pad ever drives bits 8-11, so a pad that has not proven itself one
// keeps the NES order - correct for a NES pad (which announces itself every
// frame through its ID nibble) and for a SNES->NES adapter cable, which reports
// NES buttons in NES order. A real SNES pad settles it on the first A press.
static inline int nespadMenuBits(uint16_t ext, uint8_t type)
{
    if (type != NESPAD_TYPE_SNES)
    {
        return (int)(ext & 0xFF);
    }
    int v = ext & (SELECT | START | UP | DOWN | LEFT | RIGHT); // same bits on both pads
    if (ext & (1u << 8)) v |= A;
    if (ext & (1u << 0)) v |= B;
    if (ext & (1u << 9)) v |= X; // "button 3": opens the recently played list
    if (ext & (1u << 1)) v |= Y;
    return v;
}

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

void getButtonLabels(char *buttonLabel1, char *buttonLabel2)
{
    auto &gp = io::getCurrentGamePadState(0);
    if (strcmp(gp.GamePadName, "Dual Shock 4") == 0 || strcmp(gp.GamePadName, "Dual Sense") == 0 || strcmp(gp.GamePadName, "PSClassic") == 0)
    {
        strcpy(buttonLabel1, "O");
        strcpy(buttonLabel2, "X");
    }
    else if (strcmp(gp.GamePadName, "XInput") == 0 || strncmp(gp.GamePadName, "Genesis", 7) == 0 || strcmp(gp.GamePadName, "MDArcade") == 0)
    {
        strcpy(buttonLabel1, "B");
        strcpy(buttonLabel2, "A");
    }
    else if (strcmp(gp.GamePadName, "Keyboard") == 0)
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
    char PATH[FF_MAX_LFN];
    FILINFO fi;
    static bool artworkEnabled = false;
    static FrensSettings::emulators lastEmulatorType = FrensSettings::emulators::MULTI;
    FrensSettings::emulators currentEmulatorType = FrensSettings::getEmulatorType();
    if (lastEmulatorType == currentEmulatorType)
    {
        return artworkEnabled;
    }

    PATH[0] = 0;
    const char *emulator = FrensSettings::getEmulatorTypeString();

    switch (currentEmulatorType)
    {
    case FrensSettings::emulators::NES:

        snprintf(PATH, sizeof(PATH), "/Metadata/%s/Images/160/D/D0E96F6B.444", emulator);
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
    case FrensSettings::emulators::PCE:
        snprintf(PATH, sizeof(PATH), "/Metadata/%s/Images/160/5/599EAD9B.444", emulator);
        break;
    case FrensSettings::emulators::O2EM:
        snprintf(PATH, sizeof(PATH), "/Metadata/%s/Images/160/0/084EE035.444", emulator);
        break;
     case FrensSettings::emulators::SNES:
        snprintf(PATH, sizeof(PATH), "/Metadata/%s/Images/160/0/00FAD8FD.444", emulator);
        break;
    case FrensSettings::emulators::TI99:
        // The other emulators probe for one known title's artwork file. There is no
        // published TI-99/4A metadata pack to pick a CRC from yet, so probe the image
        // directory itself - present only when a pack has been installed.
        snprintf(PATH, sizeof(PATH), "/Metadata/%s/Images/160", emulator);
        break;
    default:
        return false;
    }
    lastEmulatorType = FrensSettings::getEmulatorType();
    if (PATH[0])
    {
        printf("Checking for artwork at: %s\n", PATH);
        FRESULT res = f_stat(PATH, &fi);
        artworkEnabled = (res == FR_OK);
        // printf("Artwork %s for %s\n", exists ? "enabled" : "not found", emulator);
    }
    return artworkEnabled;
}

int Menu_LoadFrame()
{
    //Frens::waitForVSync();
    Frens::PaceFrames60fps(false, true);
#if NES_PIN_CLK != -1
    nespad_read_start();
#endif

    auto count =
#if !HSTX
        dvi_->getFrameCounter();
#else
        hstx_getframecounter();
#endif
    Frens::pollHeadPhoneJack();
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
    // Initialize the Wii Pad here if delayed start is enabled, after the DAC has been initialized.
#if WIIPAD_DELAYED_START and WII_PIN_SDA >= 0 and WII_PIN_SCL >= 0
    // check only every 60 frames.
    if (!wiipad_is_connected() && onOff)
    {
        wiipad_begin();
    }
#endif
    // play audio stream if active and not paused
    wavplayer::pump(wavplayer::sample_rate() / 60);
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
    auto &gp2 = io::getCurrentGamePadState(1);
    uint32_t combinedButtons = gp.buttons | gp2.buttons;
    connectedGamePadName[0] = gp.GamePadName;
    connectedGamePadName[1] = gp2.GamePadName;
    connectedGamePadShortName[0] = gp.GamePadShortName;
    connectedGamePadShortName[1] = gp2.GamePadShortName;

    int v = (combinedButtons & io::GamePadState::Button::LEFT ? LEFT : 0) |
            (combinedButtons & io::GamePadState::Button::RIGHT ? RIGHT : 0) |
            (combinedButtons & io::GamePadState::Button::UP ? UP : 0) |
            (combinedButtons & io::GamePadState::Button::DOWN ? DOWN : 0) |
            (combinedButtons & io::GamePadState::Button::A ? A : 0) |
            (combinedButtons & io::GamePadState::Button::B ? B : 0) |
            (combinedButtons & io::GamePadState::Button::SELECT ? SELECT : 0) |
            (combinedButtons & io::GamePadState::Button::START ? START : 0) |
            // Genesis pads report their C button as Button::C, not Button::X.
            // Both are "button 3" as far as the menu is concerned (X on SNES,
            // Y on XInput, Triangle on PlayStation, C on Genesis), so either
            // one opens the recently played list. On the original 3-button
            // Genesis Mini pad C doubles as SELECT (hid_app.cpp), and the
            // browser tests SELECT first, so there it still opens the settings
            // menu - unchanged, and never both at once.
            (combinedButtons & (io::GamePadState::Button::X | io::GamePadState::Button::C) ? X : 0) |
            (combinedButtons & io::GamePadState::Button::Y ? Y : 0) |
            0;

#if NES_PIN_CLK != -1
    v |= nespadMenuBits(nespad_states_ext[0], nespad_padtype[0]);
#endif
#if NES_PIN_CLK_1 != -1
    v |= nespadMenuBits(nespad_states_ext[1], nespad_padtype[1]);
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
    if ( p1 & SELECT )
    {
#if HSTX
       //printf("SELECT pressed, opening options menu\n");
       if (pushed & A){
           
            v = p1 =pushed = 0; // Clear all inputs to prevent accidental menu navigation after resetting to DVI mode           
            if (!settings.flags.useDVIModeForHDMI) {
                 printf("SELECT + A detected, defaulting to DVI\n");
                settings.flags.useDVIModeForHDMI = 1; // Force DVI 
                FrensSettings::savesettings();
                exitMenu = true; // Signal to exit menu after saving settings
            }
        
           
       }
#endif
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

            fgcolor = settingsActive ? NesMenuPalette[CWHITE] : NesMenuPalette[settings.bgcolor];
            bgcolor = settingsActive ? NesMenuPalette[CBLACK] : NesMenuPalette[settings.fgcolor];
        }
        else
        {

            fgcolor = NesMenuPalette[screenBuffer[charIndex].fgcolor];
            bgcolor = NesMenuPalette[screenBuffer[charIndex].bgcolor];
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
    char tmpstr[24];
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
        if ( connectedGamePadShortName[0] != nullptr && connectedGamePadShortName[1] != nullptr)
        {
            snprintf(tmpstr, sizeof(tmpstr), "%s/%s", connectedGamePadShortName[0], connectedGamePadShortName[1]);
        }
        else
        {
            if (connectedGamePadName[0] != nullptr)
            {
                snprintf(tmpstr, sizeof(tmpstr), "%s", connectedGamePadName[0]);
            }
            else
            {
                if (connectedGamePadName[1] != nullptr)
                {
                    snprintf(tmpstr, sizeof(tmpstr), "%s", connectedGamePadName[1]);
                }
                else {
                    snprintf(tmpstr, sizeof(tmpstr), "No USB GamePad");
                }
            }
        }
        putText(SCREEN_COLS / 2 - strlen(tmpstr) / 2, SCREEN_ROWS - 1, tmpstr, CBLUE, CWHITE);
        snprintf(s, sizeof(s), "%c%dK %c%c",
                 Frens::isPsramEnabled() ? 'P' : 'F',
                 maxRomSize / 1024,
                 WIIPAD_IS_CONNECTED() ? 'W' : ' ',
                 EXT_AUDIO_IS_ENABLED ? (USE_PICO_EXTRAS_I2S ? 'E' : 'L') : ' ');
        putText(1, SCREEN_ROWS - 1, s, settings.fgcolor, settings.bgcolor);
        snprintf(s, sizeof(s), "%s:Open %s:Back", buttonLabel1, buttonLabel2);

        putText(1, ENDROW + 2, s, settings.fgcolor, settings.bgcolor);
        bool artworkEnabled = isArtWorkEnabled();
        if (artworkEnabled)
        {
            strcpy(s, "START:Info");
            putText(17, ENDROW + 2, s, settings.fgcolor, settings.bgcolor);
        }
        int optionsRow = artworkEnabled ? ENDROW + 3 : ENDROW + 2;

        if (strcmp(connectedGamePadName[0], "Genesis Mini 2") == 0 || strcmp(connectedGamePadName[0], "MDArcade") == 0)
        {
            strcpy(s, "Mode:Settings");
        }
        else
        {
            if (strncmp(connectedGamePadName[0] , "Genesis", 7) == 0)
            {
                strcpy(s, "C:Settings");
            }
            else
            {
                strcpy(s, "SELECT:Settings" );
            }
        }
        putText(17, optionsRow, s, settings.fgcolor, settings.bgcolor);
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

inline void showhdmilabel()
{
    short fgcolor = settingsActive ? CBLACK : settings.fgcolor;
    short bgcolor = settingsActive ? CWHITE : settings.bgcolor;
#if HSTX
    if (video_output_get_dvi_mode())
    {
        putText(SCREEN_COLS - 4, 0, "DVI", fgcolor, bgcolor);
    }
    else
    {
        putText(SCREEN_COLS - 5, 0, "HDMI", fgcolor, bgcolor);
    }
#else
    putText(SCREEN_COLS - 5, 0, "HDMI", fgcolor, bgcolor);
#endif
}

char *menutitle = nullptr;

// Returns SWVERSION, or build date/time as "DD/MM[/YY] HH:MM" when SWVERSION is "VX.X".
static const char *getVersionString(char *buf, size_t bufsize, bool showYear = false)
{
    if (strcmp(SWVERSION, "VX.X") == 0) {
        const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
        const char *d = __DATE__;
        const char *t = __TIME__;
        int day = (d[4] == ' ' ? 0 : (d[4] - '0') * 10) + (d[5] - '0');
        int m = 0;
        for (int i = 0; i < 12; i++) {
            if (months[i * 3] == d[0] && months[i * 3 + 1] == d[1] && months[i * 3 + 2] == d[2]) {
                m = i + 1;
                break;
            }
        }
        if (showYear)
            snprintf(buf, bufsize, "%02d/%02d/%.2s %.5s", day, m, d + 9, t);
        else
            snprintf(buf, bufsize, "%02d/%02d %.5s", day, m, t);
    } else {
        snprintf(buf, bufsize, "%s", SWVERSION);
    }
    return buf;
}

void displayRoms(Frens::RomLister &romlister, int startIndex)
{
    char buffer[ROMLISTER_MAXPATH + 4];
    char s[SCREEN_COLS + 1];
    auto y = STARTROW;
    auto entries = romlister.GetEntries();
    ClearScreen(settings.bgcolor);
    snprintf(s, sizeof(s), "- %s -", menutitle);
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 0, s, settings.fgcolor, settings.bgcolor);
    snprintf(buffer, sizeof(buffer), "%uMHZ", clock_get_hz(clk_sys) / 1000000);
    showhdmilabel();
    putText(1, 0, buffer, settings.fgcolor, settings.bgcolor);
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
    {
        char versionStr[30];
        getVersionString(versionStr, sizeof(versionStr));
        putText(SCREEN_COLS - strlen(versionStr) - 1, SCREEN_ROWS - 1, versionStr, settings.fgcolor, settings.bgcolor);
    }

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
void menuPumpBlankFrames(int count)
{
#if !HSTX
    int margintop = dvi_->getBlankSettings().top;
    int marginbottom = dvi_->getBlankSettings().bottom;
    scaleMode8_7_ = Frens::applyScreenMode(ScreenMode::NOSCANLINE_1_1);
    dvi_->getBlankSettings().top = 0;
    dvi_->getBlankSettings().bottom = 0;
#endif
    for (int i = 0; i < count; i++)
    {
#if HSTX
        memset(hstx_getframebuffer(), 0, SCREENWIDTH * SCREENHEIGHT * sizeof(WORD));
#else
#if FRAMEBUFFERISPOSSIBLE
        if (Frens::isFrameBufferUsed())
        {
            memset(Frens::framebuffer, 0, SCREENWIDTH * SCREENHEIGHT * sizeof(WORD));
        }
        else
        {
#endif

            for (int line = 0; line < SCREENHEIGHT; line++)
            {
                auto b = dvi_->getLineBuffer();
                memset(b->data(), 0, SCREENWIDTH * sizeof(uint16_t));
                dvi_->setLineBuffer(line, b);
            }
#if FRAMEBUFFERISPOSSIBLE
        }
#endif
#endif
        Menu_LoadFrame();
    }
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

// =====================================================================================
// Single-line text entry, typed on the USB keyboard.
//
// Deliberately does NOT poll RomSelect_PadState. hid_app.cpp maps the same keyboard
// report onto a gamepad slot as well - A becomes SELECT, S becomes START, Z and X become
// A and B - so a loop that read pad state here would treat typing as menu navigation and
// typing a name like "SAM" would walk out of the dialog. ENTER and ESC are the only way
// out, which is why callers must check that a keyboard is actually attached.
// =====================================================================================
static char hidKeyToAscii(uint8_t hid, bool shift)
{
    if (hid >= HID_KEY_A && hid <= HID_KEY_Z) return (char)('A' + (hid - HID_KEY_A));
    if (hid >= HID_KEY_1 && hid <= HID_KEY_9) return (char)('1' + (hid - HID_KEY_1));
    if (hid == HID_KEY_0) return '0';
    if (hid == HID_KEY_MINUS) return shift ? '_' : '-';
    if (hid == HID_KEY_SPACE) return '-';       // spaces would be eaten by putText
    return 0;
}

bool showTextEntry(const char *prompt, char *buf, size_t bufsize)
{
    // putText renders '_' as a space and collapses runs of real spaces, so the field is
    // drawn with '.' for the empty cells rather than blanks.
    const size_t maxLen = (bufsize > 25) ? 24 : bufsize - 1;
    size_t len = strnlen(buf, maxLen);
    buf[len] = 0;

    // Seed the "already seen" set from whatever is held right now. Getting here means the
    // user pressed the menu's A button, and on a keyboard that is the Z key - without
    // this, opening the dialog types a Z into the field.
    uint8_t prevKeys[6];
    memcpy(prevKeys, io::getCurrentKeyboardState().keycode, sizeof(prevKeys));

    uint32_t heldFrames = 0;
    uint8_t heldKey = 0;

    while (true)
    {
        char field[40];
        size_t i = 0;
        for (; i < len && i < sizeof(field) - 2; i++) field[i] = buf[i];
        field[i++] = '['; // caret
        field[i] = 0;

        ClearScreen(settings.bgcolor);
        int row = SCREEN_ROWS / 2 - 2;
        putText(centerColClamped(strlen(prompt)), row, prompt, settings.fgcolor, settings.bgcolor);
        row += 2;
        putText(centerColClamped(strlen(field)), row, field, settings.bgcolor, settings.fgcolor);
        row += 2;
        putText(centerColClamped(24), row, "ENTER:Save__ESC:Cancel_", settings.fgcolor, settings.bgcolor);

        drawAllLines(-1);
        Menu_LoadFrame();

        const io::KeyboardState &kb = io::getCurrentKeyboardState();
        bool shift = (kb.modifier & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT)) != 0;

        // Auto-repeat, only for Backspace - holding a letter down to fill the field is
        // never what anyone wants.
        bool repeat = false;
        if (heldKey == HID_KEY_BACKSPACE)
        {
            bool stillDown = false;
            for (int k = 0; k < 6; k++) if (kb.keycode[k] == heldKey) stillDown = true;
            if (stillDown)
            {
                heldFrames++;
                if (heldFrames > (LONG_PRESS_TRESHOLD / 16) && (heldFrames % 3) == 0) repeat = true;
            }
            else { heldKey = 0; heldFrames = 0; }
        }

        for (int k = 0; k < 6; k++)
        {
            uint8_t code = kb.keycode[k];
            if (!code) continue;

            bool isNew = true;
            for (int p = 0; p < 6; p++) if (prevKeys[p] == code) isNew = false;
            if (!isNew) continue;

            if (code == HID_KEY_ENTER || code == HID_KEY_KEYPAD_ENTER)
            {
                if (len == 0) continue;         // an empty name is not a name
                return true;
            }
            if (code == HID_KEY_ESCAPE) return false;
            if (code == HID_KEY_BACKSPACE)
            {
                if (len) buf[--len] = 0;
                heldKey = code;
                heldFrames = 0;
                continue;
            }
            char c = hidKeyToAscii(code, shift);
            if (c && len < maxLen) buf[len++] = c, buf[len] = 0;
        }

        if (repeat && len) buf[--len] = 0;

        memcpy(prevKeys, kb.keycode, sizeof(prevKeys));
    }
}

// Warn before committing an overclock whose target clock exceeds this (kHz).
static constexpr uint32_t OVERCLOCK_WARN_KHZ = 378000;

// Format a vreg_voltage enum as "X.XX V". The enum values are contiguous from
// VREG_VOLTAGE_0_85 upward, so index a millivolt table by the offset.
static void formatVregVoltage(vreg_voltage v, char *buf, size_t n)
{
    static const uint16_t mv[] = {
        850, 900, 950, 1000, 1050, 1100, 1150, 1200, 1250, 1300,   // 0.85 .. 1.30
        1350, 1400, 1500, 1600, 1650, 1700, 1800, 1900, 2000,      // 1.35 .. 2.00
        2350, 2500, 2650, 2800, 3000, 3150, 3300};                 // 2.35 .. 3.30
    int idx = (int)v - (int)VREG_VOLTAGE_0_85;
    if (idx >= 0 && idx < (int)(sizeof(mv) / sizeof(mv[0])))
    {
        unsigned m = mv[idx];
        snprintf(buf, n, "%u.%02u V", m / 1000, (m % 1000) / 10);
    }
    else
    {
        snprintf(buf, n, "?.?? V");
    }
}

// Fill a rectangular block of the character grid with a solid color (spaces).
static void fillRect(int x, int y, int w, int h, int color)
{
    for (int r = 0; r < h; r++)
    {
        for (int c = 0; c < w; c++)
        {
            int col = x + c;
            int rowIdx = y + r;
            if (col < 0 || col >= SCREEN_COLS || rowIdx < 0 || rowIdx >= SCREEN_ROWS)
                continue;
            int idx = rowIdx * SCREEN_COLS + col;
            screenBuffer[idx].charvalue = ' ';
            screenBuffer[idx].fgcolor = color;
            screenBuffer[idx].bgcolor = color;
        }
    }
}

// Full-screen warning shown before enabling an overclock that boots the CPU
// above the safe default clock. The message sits inside a red box and shows the
// target clock and voltage. Returns true if the user confirms (A: Continue),
// false to back out (B: Undo). Modeled on showDialogYesNo.
static bool showOverclockWarning(uint32_t targetMHz, vreg_voltage targetVoltage)
{
    char msg[SCREEN_COLS + 1];
    char voltStr[10];
    formatVregVoltage(targetVoltage, voltStr, sizeof(voltStr));

    ClearScreen(settings.bgcolor);

    // Red alert box with white text.
    const int boxW = 33;
    const int boxH = 9;
    const int boxX = centerColClamped(boxW);
    const int boxY = SCREEN_ROWS / 2 - boxH / 2 - 2;
    fillRect(boxX, boxY, boxW, boxH, CRED);

    int row = boxY + 1;
    const char *title = "!! OVERCLOCK WARNING !!";
    putText(centerColClamped(strlen(title)), row, title, CWHITE, CRED);
    row += 2;
    snprintf(msg, sizeof(msg), "Runs at %u MHz / %s.", (unsigned)targetMHz, voltStr);
    putText(centerColClamped(strlen(msg)), row, msg, CWHITE, CRED);
    row += 1;
    const char *l2 = "This can cause serious wear";
    putText(centerColClamped(strlen(l2)), row, l2, CWHITE, CRED);
    row += 1;
    const char *l3 = "and overheating of the board.";
    putText(centerColClamped(strlen(l3)), row, l3, CWHITE, CRED);
    row += 2;
    const char *l4 = "Enable it at your own risk.";
    putText(centerColClamped(strlen(l4)), row, l4, CWHITE, CRED);

    // Button prompts below the box, in the normal menu colors.
    getButtonLabels(buttonLabel1, buttonLabel2);
    row = boxY + boxH + 1;
    snprintf(msg, sizeof(msg), "%s:Continue", buttonLabel1);
    putText(centerColClamped(strlen(msg)), row, msg, settings.fgcolor, settings.bgcolor);
    row += 1;
    snprintf(msg, sizeof(msg), "%s:Undo", buttonLabel2);
    putText(centerColClamped(strlen(msg)), row, msg, settings.fgcolor, settings.bgcolor);

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
// --- Controller Test screen (Settings > Controller Test) -------------------
// Shows a SNES-pad graphic that follows whichever input source was last
// active, plus a status list of all sources. Reads the raw per-source globals
// instead of RomSelect_PadState: the menu mask merges all sources, drops L/R,
// and its HSTX SELECT+A branch switches to DVI mode - unacceptable while a
// tester is mashing buttons. Canonical button order is the SNES serial layout
// of nespad_states_ext[]:
// bit0=B 1=Y 2=Select 3=Start 4=Up 5=Down 6=Left 7=Right 8=A 9=X 10=L 11=R
// A NES pad - including a SNES->NES adapter, which emulates one - shifts out
// the same first 8 bits with different meanings: bit0=A, bit1=B, and it has no
// A/X/L/R at all. So the two low buttons are named from nespad_padtype[]. Only
// a SNES pad can prove itself (by A/X/L/R appearing on the wire); a NES pad
// proves itself through the ID nibble, but an 8-bit adapter that idles the data
// line high looks like an idle SNES pad, so an unproven port is named as a NES
// pad - the common case on this port - while A/X/L/R stay on screen so pressing
// one switches to SNES names.

enum CtSource
{
    CT_SRC_GPIO1,
    CT_SRC_GPIO2,
    CT_SRC_USB1,
    CT_SRC_USB2,
    CT_SRC_WII,
    CT_SRC_COUNT
};
static const char *const ctSrcNames[CT_SRC_COUNT] = {"GPIO1", "GPIO2", "USB1", "USB2", "Wii"};

// io::GamePadState::Button -> canonical SNES serial order
static uint16_t ctFromUsb(uint32_t b)
{
    using B = io::GamePadState::Button;
    uint16_t v = 0;
    if (b & B::B) v |= 1u << 0;
    if (b & B::Y) v |= 1u << 1;
    if (b & B::SELECT) v |= 1u << 2;
    if (b & B::START) v |= 1u << 3;
    if (b & B::UP) v |= 1u << 4;
    if (b & B::DOWN) v |= 1u << 5;
    if (b & B::LEFT) v |= 1u << 6;
    if (b & B::RIGHT) v |= 1u << 7;
    if (b & B::A) v |= 1u << 8;
    if (b & B::X) v |= 1u << 9;
    if (b & B::L) v |= 1u << 10;
    if (b & B::R) v |= 1u << 11;
    return v;
}

#if WII_PIN_SDA >= 0 and WII_PIN_SCL >= 0
// wiipad_read() layout (bit0=A 1=B ... 8=X 9=Y) -> canonical: swap A/B and
// X/Y bit positions; Select/Start/dpad/L/R already line up.
static uint16_t ctFromWii(uint16_t w)
{
    uint16_t v = w & 0x0CFC; // Select, Start, dpad, L, R unchanged
    if (w & (1u << 0)) v |= 1u << 8;  // A
    if (w & (1u << 1)) v |= 1u << 0;  // B
    if (w & (1u << 8)) v |= 1u << 9;  // X
    if (w & (1u << 9)) v |= 1u << 1;  // Y
    return v;
}
#endif

// Sample every compiled-in source into cur[] (canonical order) and return the
// OR of all of them. Relies on the caller having pumped Menu_LoadFrame(),
// which refreshes nespad_states_ext[] and the USB gamepad state.
static uint16_t ctSampleSources(uint16_t cur[CT_SRC_COUNT])
{
#if NES_PIN_CLK != -1
    cur[CT_SRC_GPIO1] = nespad_states_ext[0];
#endif
#if NES_PIN_CLK_1 != -1
    cur[CT_SRC_GPIO2] = nespad_states_ext[1];
#endif
    cur[CT_SRC_USB1] = ctFromUsb(io::getCurrentGamePadState(0).buttons);
    cur[CT_SRC_USB2] = ctFromUsb(io::getCurrentGamePadState(1).buttons);
#if WII_PIN_SDA >= 0 and WII_PIN_SCL >= 0
    cur[CT_SRC_WII] = ctFromWii(wiipad_read());
#endif
    uint16_t merged = 0;
    for (int i = 0; i < CT_SRC_COUNT; i++)
    {
        merged |= cur[i];
    }
    return merged;
}

static const char *ctUsbStatus(int idx)
{
    auto &gp = io::getCurrentGamePadState(idx);
    if (!gp.isConnected())
    {
        return "not connected";
    }
    return gp.GamePadName ? gp.GamePadName : "connected";
}

struct CtButton
{
    const char *label;    // SNES name (canonical order)
    const char *nesLabel; // name on a NES pad; nullptr = the pad lacks this button
    uint8_t col;
    uint8_t row;
    uint16_t mask;
};
static const CtButton ctButtons[12] = {
    {"[ L ]", nullptr, 3, 4, 1u << 10}, {"[ R ]", nullptr, 32, 4, 1u << 11},
    {"( ^ )", "( ^ )", 5, 6, 1u << 4},  {"( X )", nullptr, 29, 6, 1u << 9},
    {"( < )", "( < )", 2, 7, 1u << 6},  {"( > )", "( > )", 8, 7, 1u << 7},
    {"[SEL]", "[SEL]", 14, 7, 1u << 2}, {"[STA]", "[STA]", 20, 7, 1u << 3},
    {"( Y )", "( B )", 26, 7, 1u << 1}, {"( A )", nullptr, 32, 7, 1u << 8},
    {"( v )", "( v )", 5, 8, 1u << 5},  {"( B )", "( A )", 29, 8, 1u << 0},
};

// Name to print in a button cell. nullptr = the pad does not have that button.
static const char *ctLabel(const CtButton &b, uint8_t type)
{
    if (type == NESPAD_TYPE_SNES)
    {
        return b.label;
    }
    if (type == NESPAD_TYPE_NES)
    {
        return b.nesLabel; // nullptr for A/X/L/R: a NES pad has none of them
    }
    // Not proven either way: NES names for the two shared buttons, but keep
    // A/X/L/R on screen so pressing one identifies a SNES pad.
    return b.nesLabel ? b.nesLabel : b.label;
}
// USB and Wii states are converted to canonical (SNES) order before they get
// here, so only the two GPIO ports can be talking to something else.
static uint8_t ctSourceType(int src)
{
    switch (src)
    {
#if NES_PIN_CLK != -1
    case CT_SRC_GPIO1:
        return nespad_padtype[0];
#endif
#if NES_PIN_CLK_1 != -1
    case CT_SRC_GPIO2:
        return nespad_padtype[1];
#endif
    default:
        return NESPAD_TYPE_SNES;
    }
}

static const char *ctPadTypeName(uint8_t type)
{
    switch (type)
    {
    case NESPAD_TYPE_NES:
        return "NES pad, 8 buttons";
    case NESPAD_TYPE_SNES:
        return "SNES pad, 12 buttons";
    default:
        return "NES or SNES pad";
    }
}

static const char *const ctPadTop = ".------------------------------------.";
static const char *const ctPadMid = "|                                    |";
static const char *const ctPadBot = "'------------------------------------'";

static void ctDrawSourceRow(int row, int src, int active, const char *status)
{
    char line[SCREEN_COLS + 1];
    snprintf(line, sizeof(line), "%c %-5s %s", (src == active) ? '>' : ' ', ctSrcNames[src], status);
    line[SCREEN_COLS - 1] = '\0'; // drawn at col 1; putText does not clip at end of row
    putText(1, row, line, (src == active) ? CGREEN : settings.fgcolor, settings.bgcolor);
}

static void showControllerTestScreen()
{
    constexpr int exitHoldFrames = 120; // 2 s at 60 fps
    constexpr uint16_t selectStart = (1u << 2) | (1u << 3);
    uint16_t cur[CT_SRC_COUNT] = {0};
    uint16_t prev[CT_SRC_COUNT] = {0};
    bool seen[CT_SRC_COUNT] = {false};
    int active = -1;
    int holdFrames = 0;
    char line[SCREEN_COLS + 1];

    waitForNoButtonPress(); // absorb the A press that opened the screen
    while (true)
    {
        memcpy(prev, cur, sizeof(prev));
        uint16_t merged = ctSampleSources(cur);
        for (int i = 0; i < CT_SRC_COUNT; i++)
        {
            if (cur[i] & ~prev[i]) // newest 0->1 edge wins; held pads don't flap
            {
                active = i;
            }
            if (cur[i])
            {
                seen[i] = true;
            }
        }
        if ((merged & selectStart) == selectStart)
        {
            if (++holdFrames >= exitHoldFrames)
            {
                break;
            }
        }
        else
        {
            holdFrames = 0;
        }

        uint8_t padType = (active >= 0) ? ctSourceType(active) : NESPAD_TYPE_UNKNOWN;

        ClearScreen(settings.bgcolor);
        const char *title = "-- Controller Test --";
        putText(centerColClamped(strlen(title)), 0, title, settings.fgcolor, settings.bgcolor);
        if (active < 0)
        {
            const char *prompt = "Press any button on a controller";
            putText(centerColClamped(strlen(prompt)), 2, prompt, settings.fgcolor, settings.bgcolor);
        }
        else
        {
            switch (active)
            {
            case CT_SRC_GPIO1:
            case CT_SRC_GPIO2:
                snprintf(line, sizeof(line), "Testing: %s (%s)", ctSrcNames[active], ctPadTypeName(padType));
                break;
            case CT_SRC_USB1:
            case CT_SRC_USB2:
                snprintf(line, sizeof(line), "Testing: %s %s", ctSrcNames[active], ctUsbStatus(active - CT_SRC_USB1));
                break;
            default:
                snprintf(line, sizeof(line), "Testing: Wii Classic");
                break;
            }
            line[SCREEN_COLS - 1] = '\0'; // drawn at col 1; putText does not clip at end of row
            putText(1, 2, line, settings.fgcolor, settings.bgcolor);
        }

        putText(1, 5, ctPadTop, settings.fgcolor, settings.bgcolor);
        putText(1, 6, ctPadMid, settings.fgcolor, settings.bgcolor);
        putText(1, 7, ctPadMid, settings.fgcolor, settings.bgcolor);
        putText(1, 8, ctPadMid, settings.fgcolor, settings.bgcolor);
        putText(1, 9, ctPadBot, settings.fgcolor, settings.bgcolor);
        uint16_t shown = (active >= 0) ? cur[active] : 0;
        for (const auto &b : ctButtons)
        {
            const char *label = ctLabel(b, padType);
            if (label == nullptr)
            {
                putText(b.col, b.row, "  -  ", settings.fgcolor, settings.bgcolor); // not on a NES pad
                continue;
            }
            bool on = (shown & b.mask) != 0;
            putText(b.col, b.row, label, on ? CWHITE : settings.fgcolor, on ? CGREEN : settings.bgcolor);
        }
        if (padType == NESPAD_TYPE_UNKNOWN && (active == CT_SRC_GPIO1 || active == CT_SRC_GPIO2))
        {
            // Names shown are the NES ones; a SNES pad renames B/Y and lights
            // A/X/L/R as soon as one of those four is pressed.
            const char *note = "Press A,X,L,R to detect a SNES pad";
            putText(centerColClamped(strlen(note)), 10, note, settings.fgcolor, settings.bgcolor);
        }

        putText(1, 11, "Sources:", settings.fgcolor, settings.bgcolor);
        int row = 12;
#if NES_PIN_CLK != -1
        ctDrawSourceRow(row++, CT_SRC_GPIO1, active, seen[CT_SRC_GPIO1] ? "input seen" : "no input");
#endif
#if NES_PIN_CLK_1 != -1
        ctDrawSourceRow(row++, CT_SRC_GPIO2, active, seen[CT_SRC_GPIO2] ? "input seen" : "no input");
#endif
        ctDrawSourceRow(row++, CT_SRC_USB1, active, ctUsbStatus(0));
        ctDrawSourceRow(row++, CT_SRC_USB2, active, ctUsbStatus(1));
#if WII_PIN_SDA >= 0 and WII_PIN_SCL >= 0
        ctDrawSourceRow(row++, CT_SRC_WII, active, wiipad_is_connected() ? "connected" : "not detected");
#endif

        if (active == CT_SRC_GPIO1 || active == CT_SRC_GPIO2)
        {
            // The 16 bits as they came off the data line: what the pad sent us,
            // before any interpretation. Tells a NES pad (top digit F) from a
            // SNES pad, and shows whether a button reaches us at all.
            snprintf(line, sizeof(line), "Sent by pad: %04X hex (1 = pressed)",
                     nespad_raw_ext[active - CT_SRC_GPIO1]);
            putText(1, 18, line, settings.fgcolor, settings.bgcolor);
        }

        const char *hint = "Hold SELECT+START 2 sec to exit";
        putText(centerColClamped(strlen(hint)), 26, hint, settings.fgcolor, settings.bgcolor);
        if (holdFrames > 0)
        {
            int filled = holdFrames / (exitHoldFrames / 20); // bar has 20 cells
            line[0] = '[';
            memset(line + 1, '-', 20);
            line[21] = ']';
            line[22] = '\0';
            putText(9, 27, line, settings.fgcolor, settings.bgcolor);
            if (filled > 0)
            {
                memset(line, '#', filled);
                line[filled] = '\0';
                putText(10, 27, line, CWHITE, CGREEN);
            }
        }

        drawAllLines(-1);
        Menu_LoadFrame();
    }
    // Wait for every source to read released before returning, so the held
    // SELECT+START cannot edge-trigger the settings menu's own abort combo.
    uint16_t merged;
    do
    {
        drawAllLines(-1);
        Menu_LoadFrame();
        merged = ctSampleSources(cur);
    } while (merged != 0);
}

#if FRENS_USB_MSC
static void showLoadingScreen(const char *message, int framesToWait); // defined below

// USB drive mode: hand the SD card to a PC as a mass storage device and sit
// here until the host lets go or the user presses B. Rom browser only - the
// settings menu never offers this entry when it was opened from a game.
//
// Nothing in this loop may touch FatFs: Frens::usbMscBegin() unmounted the
// volume and the host owns the filesystem until Frens::usbMscEnd() puts it
// back. Returns true when the host wrote to the card, so the caller can make
// the browser re-read the directory.
static bool showUsbDriveScreen()
{
    // Long enough for a PC to notice and enumerate a new device, short enough
    // that a board with no other input is never stuck here. Needed because on
    // boards without PIO USB the host stack is down while we are mounted, and
    // HW_CONFIG 10 has no NES port either, so B is not always reachable.
    constexpr uint32_t noHostTimeoutMs = 20000;
    char buttonLabel1[10], buttonLabel2[10];
    char line[SCREEN_COLS + 1];
    DWORD pad;

    waitForNoButtonPress(); // absorb the A press that opened the screen
    // Before any screen is drawn: the warning below prints these, and an
    // uninitialised buffer there showed a bare ": continue : back".
    getButtonLabels(buttonLabel1, buttonLabel2);

    ClearScreen(settings.bgcolor);
    drawAllLines(-1);
    Menu_LoadFrame();

#if !HSTX
    // Line-buffer DVI (no framebuffer, i.e. RP2040): core0 has to hand the DVI
    // driver a scanline every 63.5us and only five line buffers of slack, about
    // 317us. A single 512-byte SD read costs roughly 205us and the host reads
    // in bursts, so the picture collapses into TMDS error symbols - a red field
    // with the image rolling through it - for as long as the PC is busy. There
    // is no way to serve both from one core, so offer to switch the display off
    // instead of showing a broken one.
    const bool blackout = !Frens::isFrameBufferUsed();
    if (blackout)
    {
        ClearScreen(settings.bgcolor);
        const char *w0 = "-- USB Drive Mode --";
        const char *w1 = "This board cannot drive the screen";
        const char *w2 = "while the card is on your computer.";
        const char *w3 = "The screen goes black until you finish.";
        putText(centerColClamped(strlen(w0)), 0, w0, settings.fgcolor, settings.bgcolor);
        putText(centerColClamped(strlen(w1)), 5, w1, settings.fgcolor, settings.bgcolor);
        putText(centerColClamped(strlen(w2)), 6, w2, settings.fgcolor, settings.bgcolor);
        putText(centerColClamped(strlen(w3)), 8, w3, settings.fgcolor, settings.bgcolor);
        // How to get out, shown here because once the screen is black this is
        // the only instruction the user will have had.
        const char *w4 = "When you are done, eject the drive";
        putText(centerColClamped(strlen(w4)), 12, w4, settings.fgcolor, settings.bgcolor);
        snprintf(line, sizeof(line), "on your computer, or press %s.", buttonLabel2);
        putText(centerColClamped(strlen(line)), 13, line, settings.fgcolor, settings.bgcolor);
        const char *w5 = "The console then restarts.";
        putText(centerColClamped(strlen(w5)), 15, w5, settings.fgcolor, settings.bgcolor);
        snprintf(line, sizeof(line), "%s: continue    %s: back", buttonLabel1, buttonLabel2);
        putText(centerColClamped(strlen(line)), 19, line, settings.fgcolor, settings.bgcolor);

        while (true)
        {
            drawAllLines(-1);
            Menu_LoadFrame();
            RomSelect_PadState(&pad);
            if (pad & B)
            {
                return false; // cancelled, nothing touched yet
            }
            if (pad & A)
            {
                break;
            }
        }
        waitForNoButtonPress();
        // Stop the serialisers and idle core1. One-way - we reboot on the way
        // out - so nothing has to put the display back together afterwards.
        Frens::parkDisplayCore1();
    }
#else
    const bool blackout = false;
#endif

    if (!Frens::usbMscBegin())
    {
        if (!blackout)
        {
            showMessageBox("Cannot read SD card", CRED, "USB drive mode unavailable");
        }
        return false;
    }

    uint32_t started = Frens::time_ms();
    bool done = false;

    while (!done)
    {
        if (blackout)
        {
            // Display is off and core1 is idle, so there is nothing to draw and
            // nothing to starve: pump USB flat out and only glance at the pad.
            // The GPIO controller port still works (it is PIO on core0); USB
            // pads do not, the host stack having given the port to the device.
            for (int i = 0; i < 256; ++i)
            {
                Frens::usbMscTask();
            }
#if NES_PIN_CLK != -1
            nespad_read_start();
            nespad_read_finish();
#endif
            RomSelect_PadState(&pad);
            if (pad & B)
            {
                done = true;
            }
            else if (Frens::usbMscEverConnected())
            {
                done = !Frens::usbMscHostConnected();
            }
            else if (Frens::time_ms() - started > noHostTimeoutMs)
            {
                done = true;
            }
            continue;
        }

        bool mounted = Frens::usbMscHostConnected();

        ClearScreen(settings.bgcolor);
        const char *title = "-- USB Drive Mode --";
        putText(centerColClamped(strlen(title)), 0, title, settings.fgcolor, settings.bgcolor);

        if (mounted)
        {
            const char *l1 = Frens::usbMscHostSuspended()
                                 ? "SD card is mounted (computer asleep)."
                                 : "SD card is mounted on your computer.";
            const char *l2 = "Copy or delete files, then eject the";
            const char *l3 = "drive on your computer.";
            putText(centerColClamped(strlen(l1)), 8, l1, settings.fgcolor, settings.bgcolor);
            putText(centerColClamped(strlen(l2)), 10, l2, settings.fgcolor, settings.bgcolor);
            putText(centerColClamped(strlen(l3)), 11, l3, settings.fgcolor, settings.bgcolor);
        }
        else
        {
            const char *l1 = "Connect the USB port to a computer.";
            const char *l2 = "Waiting for the computer...";
            putText(centerColClamped(strlen(l1)), 8, l1, settings.fgcolor, settings.bgcolor);
            putText(centerColClamped(strlen(l2)), 10, l2, settings.fgcolor, settings.bgcolor);
        }

        snprintf(line, sizeof(line), "Eject on the computer, or press %s.", buttonLabel2);
        putText(centerColClamped(strlen(line)), 15, line, settings.fgcolor, settings.bgcolor);
#if !CFG_TUH_RPI_PIO_USB
        const char *warn = "USB controllers are off until you exit.";
        putText(centerColClamped(strlen(warn)), 17, warn, settings.fgcolor, settings.bgcolor);
#endif

        // Repaint with the device stack pumped between scanlines. On the
        // line-buffer video path drawline() blocks waiting for a free buffer,
        // so that loop is where the frame's idle time actually is; pumping
        // here keeps transfers moving without disturbing frame pacing.
        for (int lineNr = 0; lineNr < 240; ++lineNr)
        {
            drawline(lineNr, -1);
            Frens::usbMscTask();
        }
        // On the framebuffer paths drawing is nearly free and the wait sits in
        // Menu_LoadFrame() instead, which would cap tud_task() at 60 calls a
        // second and throttle the transfer to a crawl. Spend that time on USB.
        absolute_time_t until = make_timeout_time_ms(8);
        while (!time_reached(until))
        {
            Frens::usbMscTask();
        }
        // Safe with the host torn down: tuh_task() returns at once when the
        // host stack is not initialised.
        Menu_LoadFrame();

        RomSelect_PadState(&pad);
        if (pad & B)
        {
            done = true;
        }
        else if (Frens::usbMscEverConnected())
        {
            // A computer had the drive; leave the moment it lets go. A bus
            // suspend does not count as letting go, so a sleeping host does
            // not drop us out mid-copy.
            done = !Frens::usbMscHostConnected();
        }
        else if (Frens::time_ms() - started > noHostTimeoutMs)
        {
            done = true; // no computer on the other end
        }
    }

    bool wrote = Frens::usbMscMediaDirty();
    Frens::usbMscEnd();

    if (Frens::usbMscNeedsRebootOnExit())
    {
        // Boards without PIO USB cannot leave USB drive mode cleanly: handing
        // rhport 0 back to the USB host forces a tud_deinit() that leaks two
        // hardware spinlocks TinyUSB never frees, and only eight are claimable
        // in total. Rebooting costs a couple of seconds, rebuilds the USB host
        // and the FatFs mount from scratch, and re-reads the card the PC just
        // wrote to - which is what we would be doing on the way out anyway.
        showLoadingScreen("Restarting", 60);
        Frens::resetWifi();
        // watchdog_reboot(), not watchdog_enable(): watchdog_enable() stamps
        // scratch[4] with the SDK magic that watchdog_enable_caused_reboot()
        // looks for, which is how menu() tells the emulator "a rom was picked,
        // flash it and start it" (FrensHelpers.cpp initAll). Rebooting that
        // way out of USB drive mode made the next boot call flashrom() on a
        // stale path and report "Not a NES rom file". watchdog_reboot(0,0,0)
        // clears that magic, so this comes back up in the rom browser - the
        // same thing rebootToBootloader() relies on.
        watchdog_reboot(0, 0, 0);
        while (1)
        {
            tight_loop_contents();
        }
    }

    ClearScreen(settings.bgcolor);
    drawAllLines(-1);
    Menu_LoadFrame();
    waitForNoButtonPress(); // do not let the exit press fall through to the menu
    return wrote;
}
#endif // FRENS_USB_MSC

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
    {
        char versionStr[30];
        getVersionString(versionStr, sizeof(versionStr), true);
        putText(SCREEN_COLS - strlen(versionStr) - 2, SCREEN_ROWS - 2, versionStr, DEFAULT_FGCOLOR, DEFAULT_BGCOLOR);
    }
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
#if PICO_RP2350
    if (!wavplayer::isPlaying())
    {
#endif
        screenSaverWithArt(!isArtWorkEnabled());
#if PICO_RP2350
    }
#endif
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
static void showLoadingScreen(const char *message = nullptr, int framesToWait = 0)
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
            fr = f_read(&fil, HSTX_GETFRAMEBUFFER(), 153600, &r);
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
        while (framesToWait-- > 0)
        {
            Menu_LoadFrame();
            DrawScreen(-1);
        }
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

// Writes ROMINFOFILE with exactly "<dir>/<name>" - no newline, no terminating
// NUL - which is byte for byte what the previous f_putc loop produced, so an
// older firmware flashed back onto the same card still reads it. flashrom()
// picks it up after the reboot. FIL comes off the heap: the menu call chain
// runs close to PICO_STACK_SIZE.
static bool writeRomInfoFile(const char *dir, const char *name)
{
    FIL *fil = (FIL *)Frens::f_malloc(sizeof(FIL));
    char *path = (char *)Frens::f_malloc(RECENTGAMES_MAXPATH);
    if (!fil || !path)
    {
        snprintf(globalErrorMessage, 40, "Out of memory starting game");
        Frens::f_free(fil);
        Frens::f_free(path);
        return false;
    }
    int len = snprintf(path, RECENTGAMES_MAXPATH, "%s/%s", dir, name);
    bool ok = false;
    printf("Creating %s: %s\n", ROMINFOFILE, path);
    FRESULT fr = f_open(fil, ROMINFOFILE, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr == FR_OK)
    {
        UINT bw = 0;
        ok = (f_write(fil, path, len, &bw) == FR_OK && bw == (UINT)len);
        if (!ok)
        {
            snprintf(globalErrorMessage, 40, "Error writing file %d", fr);
            printf("%s\n", globalErrorMessage);
        }
        f_close(fil);
    }
    else
    {
        printf("Cannot create %s:%d\n", ROMINFOFILE, fr);
        snprintf(globalErrorMessage, 40, "Cannot create %s:%d", ROMINFOFILE, fr);
    }
    Frens::f_free(fil);
    Frens::f_free(path);
    return ok;
}

// Loads the selected rom the way this board needs it: straight into PSRAM, or
// via ROMINFOFILE plus a reboot into flashrom(). dir has no trailing slash,
// name is the bare file name. Returns true when the caller should leave the
// browser loop.
static bool startRom(char *dir, char *name, char *rompath)
{
    errorInSavingRom = false;
    skipRebootAfterMenu = false;
    if (Frens::isPsramEnabled())
    {
        ErrorMessage[0] = 0;
        loadRomInPsRam(dir, name, rompath, errorInSavingRom);
        // ROM_FILE_ADDR stays 0 both when loading failed and when the file was
        // too large to preload - which for a .cue/.chd disc image is a normal,
        // successful launch that streams from the card. Only the error message
        // tells the two apart: flashromtoPsram sets it on every failure path
        // and leaves it alone for the streamed case.
        if (!errorInSavingRom && ErrorMessage[0] == 0)
        {
            FILINFO *fno = (FILINFO *)Frens::f_malloc(sizeof(FILINFO));
            uint32_t size = 0;
            if (fno && f_stat(rompath, fno) == FR_OK)
            {
                size = (uint32_t)fno->fsize;
            }
            Frens::f_free(fno);
            Frens::Recent::add(rompath, FrensSettings::getEmulatorTypeString(),
                               Frens::getCrcOfLoadedRom(), size);
        }
    }
    else
    {
        // No PSRAM: record the choice and reboot. flashrom() reads the file and
        // either programs the rom into flash or, when the image already there
        // is the one asked for, skips straight to running it. It also adds the
        // game to the recently played list, because the crc it needs is only
        // available while it reads the rom.
        if (!writeRomInfoFile(dir, name))
        {
            printf("startRom: writing %s failed\n", ROMINFOFILE);
            errorInSavingRom = true;
        }
#if START_FLASHED_ROM_WITHOUT_REBOOT
        // Testing shortcut: the rom is already in flash and verified, so there
        // is nothing to program and nothing the reboot has to set up that the
        // PSRAM boards do not already do by returning here. ROMINFOFILE was
        // written above regardless, so a later reset still comes back to this
        // same game. Off by default - see START_FLASHED_ROM_WITHOUT_REBOOT.
        else
        {
            char *fullPath = (char *)Frens::f_malloc(RECENTGAMES_MAXPATH);
            if (fullPath)
            {
                snprintf(fullPath, RECENTGAMES_MAXPATH, "%s/%s", dir, name);
                uint32_t size = 0;
                // romIsByteSwapped() is the best the menu has; flashrom() was
                // handed initAll()'s flag. If a build ever disagrees the check
                // simply fails and we take the normal reboot, never the wrong
                // image.
                if (Frens::isRomAlreadyInFlash(fullPath, Frens::romIsByteSwapped(), &size))
                {
                    printf("Starting %s from flash without rebooting\n", fullPath);
                    strncpy(rompath, fullPath, FF_MAX_LFN - 1);
                    rompath[FF_MAX_LFN - 1] = 0;
                    Frens::Recent::add(fullPath, FrensSettings::getEmulatorTypeString(),
                                       Frens::getCrcOfLoadedRom(), size);
                    skipRebootAfterMenu = true;
                }
            }
            Frens::f_free(fullPath);
        }
#endif
    }
    return !errorInSavingRom;
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
    snprintf(path, pathsize, QUICKSAVEFILEFORMAT, FrensSettings::getEmulatorTypeString(), Frens::getCrcOfLoadedRom(), MAXSAVESTATESLOTS - 1);
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

bool isAutoSaveStateConfigured()
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
    if (fr == FR_OK)
    {
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
    if (fr == FR_OK)
    {
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
    if (fr == FR_OK)
    {
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
    dvi_->getBlankSettings().top = 0;
    dvi_->getBlankSettings().bottom = 0;
#endif
    scaleMode8_7_ = Frens::applyScreenMode(ScreenMode::NOSCANLINE_1_1);
    getAutoSaveStatePath(tmppath, sizeof(tmppath));
    bool autosaveFileExists = Frens::fileExists(tmppath);
    // Handle quicksave and quick load
    if (quickSave == SaveStateTypes::LOAD || quickSave == SaveStateTypes::SAVE || quickSave == SaveStateTypes::LOAD_AND_START || quickSave == SaveStateTypes::SAVE_AND_EXIT)
    {

        if (quickSave == SaveStateTypes::LOAD)
        {
            getQuickSavePath(tmppath, sizeof(tmppath));
            // do nothing if file does not exist
            if (Frens::fileExists(tmppath))
            {
                bool ok = true;
                ok = showDialogYesNo("Load quick save state?");
                if (ok)
                {
                    printf("Loading quick save from %s\n", tmppath);
                    if (loadstatefunc(tmppath) == 0)
                    {
                        printf("Quick load successful\n");
                        // showMessageBox("State loaded successfully.", settings.fgcolor);
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
                        // showMessageBox("State saved successfully.", settings.fgcolor);
                    }
                    else
                    {
                        printf("Quick save failed\n");
                        showMessageBox("Failed to save state.", CRED);
                    }
                }
            }
        }
        else if (quickSave == SaveStateTypes::LOAD_AND_START)
        {
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
                        // showMessageBox("State loaded successfully.", settings.fgcolor);
                    }
                    else
                    {
                        printf("Auto load failed\n");
                        showMessageBox("State load failed. Returning to menu.", CRED);
                        saveStateLoadedOK = false;
                    }
                }
            }
        }
        else if (quickSave == SaveStateTypes::SAVE_AND_EXIT)
        {
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
                        // showMessageBox("State saved successfully.", settings.fgcolor);
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
        exitMenu = false;
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
                if (i == (MAXSAVESTATESLOTS - 1))
                {
                    snprintf(linebuf, sizeof(linebuf), "Quick Save: %s%s", status, (i == selected && saved) ? " Saved" : "");
                }
                else
                {
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
                }
                else if (confirmSlot == MAXSAVESTATESLOTS)
                {
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
                if (selected < MAXSAVESTATESLOTS)
                {
                    saveSlotHasData = saveslots[selected];
                }
                else
                {
                    saveSlotHasData = autosaveFileExists;
                }
                // General instructions (each action on its own line)
                if (selected == MAXSAVESTATESLOTS)
                {
                    putText(0, ENDROW - 7, "LEFT/RIGHT: Toggle autosave", settings.fgcolor, settings.bgcolor);
                }
                // if ( selected < MAXSAVESTATESLOTS) {
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
                // snprintf(linebuf, sizeof(linebuf), "SELECT + %s : Quick Save", buttonLabel1);
                snprintf(linebuf, sizeof(linebuf), "START + DOWN : Quick Save");
                putText(1, SCREEN_ROWS - 3, linebuf, settings.fgcolor, settings.bgcolor);
                // snprintf(linebuf, sizeof(linebuf), "SELECT + %s : Quick Load", buttonLabel2);
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
                if (selected < MAXSAVESTATESLOTS)
                {
                    saveSlotHasData = saveslots[selected];
                    getSaveStatePath(tmppath, sizeof(tmppath), selected);
                }
                else
                {
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
                    // showMessageBox("Loaded state from", CBLUE, tmppath, "Press any button to resume game.");
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
                if (selected < MAXSAVESTATESLOTS)
                {
                    saveSlotHasData = saveslots[selected];
                    getSaveStatePath(tmppath, sizeof(tmppath), selected);
                }
                else
                {
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
                        // showMessageBox("Auto save disabled.", CBLUE);
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
                if (selected < MAXSAVESTATESLOTS)
                {
                    saveSlotHasData = saveslots[selected];
                    getSaveStatePath(tmppath, sizeof(tmppath), selected);
                }
                else
                {
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
                        if (selected < MAXSAVESTATESLOTS)
                        {
                            saveslots[selected] = 1;
                        }
                        else
                        {
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

    scaleMode8_7_ = Frens::applyScreenMode(settings.screenMode);
#if !HSTX
    if (!Frens::isFrameBufferUsed())
    {
        dvi_->getBlankSettings().top = margintop;
        dvi_->getBlankSettings().bottom = marginbottom;
    }
#endif
    Frens::PaceFrames60fps(true, true);
    //Frens::waitForVSync();
    printf("Exiting save state menu.\n");
    Frens::f_free(screenBuffer);
    return saveStateLoadedOK;
}

// --- Recently played games ---
// Modal list of the games most recently started on this emulator, newest first.
// Returns 1 when the user picked one (outPath receives its absolute path), 0
// otherwise (B, idle timeout, empty list or out of memory).
//
// PRECONDITION: the caller already owns screenBuffer and has put the display in
// NOSCANLINE_1_1 with zeroed margins - menu() does both before its loop. Unlike
// showSaveStateMenu this function must therefore NOT allocate screenBuffer and
// must NOT touch applyScreenMode / the dvi blank settings: doing so would leak
// the menu's buffer and swap the pointer under it.
static int showRecentGamesMenu(char *outPath, size_t outPathSize)
{
    Frens::Recent::List *list = Frens::Recent::load();
    if (!list || list->count == 0)
    {
        showMessageBox(list ? "No recently played games yet." : "Out of memory.",
                       list ? settings.fgcolor : CRED);
        Frens::Recent::free(list);
        return 0;
    }

    // -1 on PSRAM boards and whenever nothing recognisable is in flash.
    const int flashedIdx = Frens::Recent::flashedIndex(list);
    const bool showReady = (flashedIdx >= 0);
    // Width of the name column; the [READY] tag claims the last 8 columns.
    const int fieldw = showReady ? SCREEN_COLS - 9 : SCREEN_COLS - 2;

    int selected = 0;
    int scroll = 0; // horizontal scroll of the highlighted row only
    int idleStart = -1;
    int rc = 0;
    // Deliberately not the file-static exitMenu: showSettingsMenu is sitting in
    // while (!exitMenu) when it calls us.
    bool done = false;
    DWORD pad = 0;

    auto redraw = [&](int confirmIndex = -1)
    {
        char linebuf[SCREEN_COLS + 8];
        ClearScreen(settings.bgcolor);
        getButtonLabels(buttonLabel1, buttonLabel2);
        const char *title = "-- Recently Played --";
        putText(centerColClamped(strlen(title)), 0, title, settings.fgcolor, settings.bgcolor);
        snprintf(linebuf, sizeof(linebuf), "%d game%s%s", list->count,
                 list->count == 1 ? "" : "s", showReady ? "___[READY]:already in flash" : "");
        putText(1, 1, linebuf, settings.fgcolor, settings.bgcolor);
        for (auto i = 1; i < SCREEN_COLS - 1; i++)
        {
            putText(i, STARTROW - 1, "-", settings.fgcolor, settings.bgcolor);
        }

        // RECENTGAMES_MAX rows fit between STARTROW and ENDROW, so the list
        // never needs paging or scroll indicators.
        for (int i = 0; i < list->count; i++)
        {
            const Frens::Recent::Entry &e = list->items[i];
            // File name only, never the directory. A name too long for the
            // column scrolls, but only on the highlighted row.
            const char *name = Frens::Recent::displayName(e);
            const char *src = (i == selected) ? name + scroll : name;
            snprintf(linebuf, sizeof(linebuf), "%-*.*s%s", fieldw, fieldw, src,
                     (i == flashedIdx) ? "_[READY]" : "");
            // putText collapses runs of real spaces, which would pull the
            // [READY] tag left out of its column and shift any name containing
            // a double space. It renders '_' as a space without collapsing, so
            // that is what the rest of this menu pads with.
            for (char *c = linebuf; *c; c++)
            {
                if (*c == ' ')
                {
                    *c = '_';
                }
            }
            int fg = settings.fgcolor;
            int bg = settings.bgcolor;
            if (i == confirmIndex)
            {
                fg = CWHITE;
                bg = CRED;
            }
            else if (i == selected && confirmIndex < 0)
            {
                fg = settings.bgcolor;
                bg = settings.fgcolor;
            }
            putText(1, STARTROW + i, linebuf, fg, bg);
        }
        for (auto i = 1; i < SCREEN_COLS - 1; i++)
        {
            putText(i, ENDROW - 1, "-", settings.fgcolor, settings.bgcolor);
        }

        if (confirmIndex >= 0)
        {
            putText(0, ENDROW + 1, "Remove this game from the list?", settings.fgcolor, settings.bgcolor);
            snprintf(linebuf, sizeof(linebuf), "%s:Remove__%s:Cancel", buttonLabel1, buttonLabel2);
            putText(0, ENDROW + 2, linebuf, settings.fgcolor, settings.bgcolor);
        }
        else
        {
            snprintf(linebuf, sizeof(linebuf), "%s_____:Start game", buttonLabel1);
            putText(0, ENDROW + 1, linebuf, settings.fgcolor, settings.bgcolor);
            putText(0, ENDROW + 2, "SELECT:Remove from list", settings.fgcolor, settings.bgcolor);
            if (isArtWorkEnabled())
            {
                putText(0, ENDROW + 3, "START :Info", settings.fgcolor, settings.bgcolor);
            }
            snprintf(linebuf, sizeof(linebuf), "%s_____:Back", buttonLabel2);
            putText(0, SCREEN_ROWS - 1, linebuf, settings.fgcolor, settings.bgcolor);
        }
        // Not DrawScreen(): that stamps the rom browser footer over these rows.
        drawAllLines(-1);
    };

    waitForNoButtonPress();

    while (!done)
    {
        redraw();
        RomSelect_PadState(&pad);
        int frame = Menu_LoadFrame();
        if (idleStart < 0 || pad)
        {
            idleStart = frame;
        }
        if ((frame - idleStart) > 3600)
        {
            break;
        }

        if (pad & UP)
        {
            selected = (selected > 0) ? selected - 1 : list->count - 1;
            scroll = 0;
        }
        else if (pad & DOWN)
        {
            selected = (selected + 1 < list->count) ? selected + 1 : 0;
            scroll = 0;
        }
        else if (pad & A)
        {
            strncpy(outPath, list->items[selected].path, outPathSize - 1);
            outPath[outPathSize - 1] = 0;
            // Not Frens::fileExists(): that keeps its FILINFO (288 bytes) on
            // the stack, and this runs one or two frames deep inside menu().
            // When the allocation fails we cannot check, so assume the game is
            // there and let the launch report any real problem - refusing to
            // start over a failed malloc would be a lie about the SD card.
            FILINFO *fno = (FILINFO *)Frens::f_malloc(sizeof(FILINFO));
            FRESULT statResult = fno ? f_stat(outPath, fno) : FR_OK;
            Frens::f_free(fno);
            bool exists = (statResult == FR_OK);
            if (!exists)
            {
                showMessageBox("Game is no longer on the SD card.", CRED,
                               "Use SELECT to remove it.");
                outPath[0] = 0;
            }
            else
            {
                rc = 1;
                done = true;
            }
        }
        else if (pad & B)
        {
            done = true;
        }
        else if (pad & SELECT)
        {
            // Nested confirm, same shape as the save state menu: reuse redraw()
            // with the row flagged, and keep pumping exactly one frame per pass.
            waitForNoButtonPress();
            bool deciding = true;
            while (deciding)
            {
                redraw(selected);
                RomSelect_PadState(&pad);
                Menu_LoadFrame();
                if (pad & A)
                {
                    Frens::Recent::removeAt(list, selected);
                    if (list->count == 0)
                    {
                        done = true;
                    }
                    else if (selected >= list->count)
                    {
                        selected = list->count - 1;
                    }
                    deciding = false;
                }
                else if (pad & B)
                {
                    deciding = false;
                }
            }
            scroll = 0;
            idleStart = -1;
            waitForNoButtonPress();
        }
        else if ((pad & START) && isArtWorkEnabled())
        {
            if (showartwork(list->items[selected].crc, list->items[selected].size) == 1)
            {
                strncpy(outPath, list->items[selected].path, outPathSize - 1);
                outPath[outPathSize - 1] = 0;
                rc = 1;
                done = true;
            }
            idleStart = -1; // frames spent in the artwork screen are not idle time
        }
        else if (frame % 30 == 0)
        {
            // Same cadence as the browser's horizontal scroll of the selected
            // row. Resets to 0 as soon as the rest fits, so it never runs off
            // the end of the name.
            const char *p = Frens::Recent::displayName(list->items[selected]);
            scroll = ((int)strlen(p + scroll) >= fieldw) ? scroll + 1 : 0;
        }
    }

    ClearScreen(settings.bgcolor);
    waitForNoButtonPress();
    Frens::Recent::free(list);
    return rc;
}

// --- Settings Menu Implementation ---
// returns 0 if no changes, 1 if settings applied
//         2 start screensaver
//         3 exit to menu
//         6 start the game in recentLaunchPath (rom browser only - the option
//           is hidden when calledFromGame, so this never reaches an emulator)
// =====================================================================================
// Cassette prompts, driven by the console rather than by the menu.
//
// SAVE CS1 and OLD CS1 carry no filename - the TI cassette device takes only a device
// name - so there is nothing to pick a file from until the console actually asks. The
// emulator spots that from the DSR's first CRU access and calls in here, which is the
// same moment the console is telling the user to press RECORD or PLAY.
// =====================================================================================
static bool cassettePickTape()
{
    int n = s_cassetteHooks->get_num_tapes ? s_cassetteHooks->get_num_tapes() : 0;
    if (n <= 0)
    {
        showMessageBox("No tapes found in", CWHITE, "/saves/ti99/tapes");
        return false;
    }

    int sel = 0;
    const int rows = SCREEN_ROWS - 9;
    DWORD pad;
    waitForNoButtonPress();

    while (true)
    {
        ClearScreen(settings.bgcolor);
        putText(centerColClamped(15), 2, "Insert a tape:", settings.fgcolor, settings.bgcolor);

        int first = (sel >= rows) ? sel - rows + 1 : 0;
        int row = 4;
        for (int i = first; i < n && row < 4 + rows; i++, row++)
        {
            char line[SCREEN_COLS];
            snprintf(line, sizeof(line), "%s", s_cassetteHooks->get_tape_name(i));
            char *dot = strrchr(line, '.');
            if (dot) *dot = 0;
            if (i == sel) putText(4, row, line, settings.bgcolor, settings.fgcolor);
            else          putText(4, row, line, settings.fgcolor, settings.bgcolor);
        }

        getButtonLabels(buttonLabel1, buttonLabel2);
        char help[SCREEN_COLS];
        snprintf(help, sizeof(help), "%s:Load__%s:Cancel", buttonLabel1, buttonLabel2);
        putText(centerColClamped(strlen(help)), SCREEN_ROWS - 3, help, settings.fgcolor, settings.bgcolor);

        drawAllLines(-1);
        RomSelect_PadState(&pad);
        Menu_LoadFrame();

        if      (pad & DOWN) sel = (sel + 1) % n;
        else if (pad & UP)   sel = (sel + n - 1) % n;
        else if (pad & A)    return (s_cassetteHooks->commit(sel, CAS_MODE_PLAY, nullptr) == 0);
        else if (pad & B)    return false;
    }
}

bool menuCassettePrompt(int wantRecord)
{
    if (!s_cassetteHooks || !s_cassetteHooks->commit) return false;

    // Same screen setup showSettingsMenu does when it is opened from a running game.
    int margintop = 0, marginbottom = 0;
    screenBuffer = (charCell *)Frens::f_malloc(screenbufferSize);
    if (!screenBuffer) return false;
#if !HSTX
    margintop = dvi_->getBlankSettings().top;
    marginbottom = dvi_->getBlankSettings().bottom;
    dvi_->getBlankSettings().top = 0;
    dvi_->getBlankSettings().bottom = 0;
#endif
    scaleMode8_7_ = Frens::applyScreenMode(ScreenMode::NOSCANLINE_1_1);

    if (s_cassetteHooks->refresh) s_cassetteHooks->refresh();

    bool ok = false;
    if (wantRecord)
    {
        // This is where a tape gets its label: the console has just started writing and
        // will not tell us a name, because there is no name to tell.
        char label[TAPE_LABEL_MAX] = {0};
        if (s_cassetteHooks->default_name) s_cassetteHooks->default_name(label, sizeof(label));

        bool go = true;
        if (io::getCurrentKeyboardState().connected)
            go = showTextEntry("Label this tape:", label, sizeof(label));

        if (go && s_cassetteHooks->name_exists && s_cassetteHooks->name_exists(label, CAS_MODE_REC_WAV))
            go = showDialogYesNo("Tape exists. Overwrite?");

        if (go)
        {
            ok = (s_cassetteHooks->commit(-1, CAS_MODE_REC_WAV, label) == 0);
            if (!ok) showMessageBox("Could not start recording.", CWHITE);
        }
    }
    else
    {
        ok = cassettePickTape();
    }

    ClearScreen(CBLACK);
    waitForNoButtonPress();
    Frens::f_free((void *)screenBuffer);
    screenBuffer = nullptr;
    scaleMode8_7_ = Frens::applyScreenMode(settings.screenMode);
#if !HSTX
    if (!Frens::isFrameBufferUsed())
    {
        dvi_->getBlankSettings().top = margintop;
        dvi_->getBlankSettings().bottom = marginbottom;
    }
#endif
    return ok;
}

int showSettingsMenu(bool calledFromGame)
{
    bool settingsChanged = false;
    int rval = 0;
    int margintop = 0;
    int marginbottom = 0;
    settingsActive = true;
    // Re-seed the FDS preview from the live current side on every menu
    // open. The render switch fills it in on first draw.
    s_fdsPendingChoice = -1;

    // Same for the cassette deck, and rescan the tape folder while we are here rather
    // than from the redraw lambda - SD access per frame would stall the menu.
    s_cassettePendingChoice = -1;
    if (s_cassetteHooks && s_cassetteHooks->refresh) s_cassetteHooks->refresh();


    // #if HSTX
    //     if (settings.flags.useDVIModeForHDMI)
    //     {
    //         video_output_request_resync();
    //     }
    // #endif
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
        dvi_->getBlankSettings().top = 0;
        dvi_->getBlankSettings().bottom = 0;
#endif
        scaleMode8_7_ = Frens::applyScreenMode(ScreenMode::NOSCANLINE_1_1);
    }

    // Local working copy of settings.
    struct settings *workingDyn = (struct settings *)Frens::f_malloc(sizeof(settings));
    if (!workingDyn)
    {
        return false; // allocation failed
    }
    memcpy(workingDyn, &settings, sizeof(settings)); // byte-exact copy including padding for memcmp
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
    // FDS disk swap goes first so it's auto-selected when an FDS game
    // is running and the BIOS prompts the user to flip the disk.
    if (g_settings_visibility[MOPT_FDS_DISK_SWAP] > 0)
    {
        visibleIndices[visibleCount++] = MOPT_FDS_DISK_SWAP;
    }
    // Recently played is a rom-browser feature and is never offered in-game:
    // starting another game from inside a running one has no clean teardown
    // path. Gating it here keeps it out of visibleIndices entirely when the
    // in-game menu is open, so it cannot be highlighted and its handler cannot
    // run. It is forced visible instead of read from g_settings_visibility[]
    // because every emulator sizes that array [MOPT_COUNT] with a positional
    // initializer list and so leaves the new trailing entry zero. It goes near
    // the top because it is the one entry that starts a game.
    if (!calledFromGame && g_settings_visibility[MOPT_RECENT_GAMES] >= 0)
    {
        visibleIndices[visibleCount++] = MOPT_RECENT_GAMES;
    }
    for (int i = 0; i < MOPT_COUNT; ++i)
    {
        if (i == MOPT_FDS_DISK_SWAP) continue; // already handled above
        if (i == MOPT_RECENT_GAMES) continue;  // already handled above
        // The three action entries that close the list are appended after this
        // loop in a fixed order, so skip them here.
        if (i == MOPT_CONTROLLER_TEST) continue;
        if (i == MOPT_ENTER_BOOTSEL_MODE) continue;
        if (i == MOPT_USB_DRIVE_MODE) continue;
        // Overclock is reachable only from the file-browser menu — applying it
        // mid-game would reboot the box and drop unsaved emulator state.
        if (i == MOPT_OVERCLOCK && calledFromGame) continue;
        // -1 is always hidden
        if (g_settings_visibility[i] >= 0)
        {
            if (g_settings_visibility[i] || (i == MenuSettingsIndex::MOPT_EXIT_GAME || i == MenuSettingsIndex::MOPT_SAVE_RESTORE_STATE || i == MenuSettingsIndex::MOPT_RESET_GAME) && calledFromGame)
            {
                visibleIndices[visibleCount++] = i;
            }
        }
    }
    // Fixed tail of the list: Controller test, Enter BOOTSEL mode, USB drive
    // mode. Their order cannot come from the loop above because the enum in
    // menu_settings.h is append-only - every emulator sizes its
    // g_settings_visibility_* array [MOPT_COUNT] with a positional initializer
    // list, so renumbering MOPT_* values would silently shift their settings.
    // Appending here keeps the enum untouched and pins the display order.
    if (g_settings_visibility[MOPT_CONTROLLER_TEST] > 0)
    {
        visibleIndices[visibleCount++] = MOPT_CONTROLLER_TEST;
    }
    if (g_settings_visibility[MOPT_ENTER_BOOTSEL_MODE] > 0)
    {
        visibleIndices[visibleCount++] = MOPT_ENTER_BOOTSEL_MODE;
    }
#if FRENS_USB_MSC
    // USB drive mode hands the raw SD card to a PC, so it is a rom-browser
    // feature only: in-game there are save files open and the rom is mapped out
    // of flash, and letting a host rewrite the card underneath corrupts both.
    // Leaving it out of visibleIndices when the in-game menu is open means it
    // cannot be highlighted and its handler cannot run. It is forced visible
    // rather than read from g_settings_visibility[] for the same reason as
    // MOPT_RECENT_GAMES above: sibling emulators leave the trailing entry zero.
    if (!calledFromGame)
    {
        visibleIndices[visibleCount++] = MOPT_USB_DRIVE_MODE;
    }
#endif
    // Layout rows (option list is a scrollable window of optionWindowSize rows):
    //   title, blank,
    //   upIndicatorRow,
    //   rowStartOptions .. rowStartOptions+optionWindowSize-1 (option slots),
    //   downIndicatorRow,
    //   blank,
    //   actionRowScreen (SAVE / CANCEL / DEFAULT, fixed),
    //   blank,
    //   paletteStartRow .. paletteStartRow+3 (4x16 palette),
    //   blank,
    //   helpRowScreen (option description),
    //   ... free ...,
    //   bottom-anchored hint lines at SCREEN_ROWS-3 .. SCREEN_ROWS-1.
    const int optionWindowSize  = 12;
    const int rowStartOptions   = 3;
    const int upIndicatorRow    = rowStartOptions - 1;
    const int downIndicatorRow  = rowStartOptions + optionWindowSize;
    const int actionRowScreen   = downIndicatorRow + 2;
    const int paletteStartRow   = actionRowScreen + 2;
    const int paletteRowCount   = 4;
    const int helpRowScreen     = paletteStartRow + paletteRowCount + 1;
    int  selectedOptionIndex = 0;                   // logical 0..visibleCount-1
    int  firstVisibleOption  = 0;                   // scroll offset
    bool onActionRow         = (visibleCount == 0); // no options -> start on action row
    int  actionSubSelect     = 0;                   // 0=SAVE, 1=CANCEL, 2=DEFAULT
    exitMenu = false;
    bool applySettings = false; // true when SAVE, false when CANCEL
    bool mediaChanged = false;  // a PC wrote to the card in USB drive mode
    // lambda to redraw the entire menu
    auto redraw = [&]()
    {
        getButtonLabels(buttonLabel1, buttonLabel2);
        ClearScreen(CWHITE); // Always white background

        int row = 0;
        showhdmilabel();
        // Centered Title
        constexpr int titleLen = 13; // "-- Settings --"
        int titleCol = (SCREEN_COLS - titleLen) / 2;
        if (titleCol < 0)
            titleCol = 0;
        putText(titleCol, row++, "-- Settings --", CBLACK, CWHITE);
        // Blank spacer line
        putText(0, row++, "", CBLACK, CWHITE);
        // Up-scroll indicator (centered): shown when there are options above the window
        putText(SCREEN_COLS / 2, upIndicatorRow,
                (firstVisibleOption > 0) ? "^" : " ", CBLACK, CWHITE);
        // Render the visible option window (up to optionWindowSize entries)
        row = rowStartOptions;
        const int lastVi = (firstVisibleOption + optionWindowSize < visibleCount)
                               ? firstVisibleOption + optionWindowSize
                               : visibleCount;
        for (int vi = firstVisibleOption; vi < lastVi; ++vi)
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
            case MenuSettingsIndex::MOPT_RESET_GAME:
            {
                label = "Reset game";
                value = "";
                break;
            }
             case MenuSettingsIndex::MOPT_ENTER_BOOTSEL_MODE:
            {


                label = "Enter BOOTSEL Mode";
                value = "";
                break;
            }
            case MenuSettingsIndex::MOPT_CONTROLLER_TEST:
            {
                label = "Controller Test";
                value = "";
                break;
            }
            case MenuSettingsIndex::MOPT_RECENT_GAMES:
            {
                label = "Recently played";
                value = "";
                break;
            }
            case MenuSettingsIndex::MOPT_USB_DRIVE_MODE:
            {
                label = "USB drive mode";
                value = "";
                break;
            }
            case MenuSettingsIndex::MOPT_REBOOT_TO_LOADER:
            {
                label = "Return to emulator selection";
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
            case MenuSettingsIndex::MOPT_SCANLINE_TYPE:
            {
                label = "Scanline Type";
                if (working.screenMode == ScreenMode::SCANLINE_8_7)
                {
                    value = "Simple";
                }
                else
                {
                    switch ((ScanlineType)working.scanlineType)
                    {
                    case ScanlineType::SIMPLE:
                        value = "Simple";
                        break;
                    case ScanlineType::LCD:
                        value = "LCD";
                        break;
                    default:
                        value = "?";
                        break;
                    }
                }
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
              case MenuSettingsIndex::MOPT_DISPLAY_MODE:
            {
                label = "Display Mode";
                value = working.flags.useDVIModeForHDMI ? "DVI" : "HDMI";
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
            case MenuSettingsIndex::MOPT_OVERCLOCK:
            {
                label = "Overclock";
                value = working.flags.overclock ? "ON" : "OFF";
                break;
            }
            case MenuSettingsIndex::MOPT_FM_AUDIO:
            {
                label = "YM2413 FM";
                value = working.flags.useFM ? "ON" : "OFF";
                break;
            }
            // case MenuSettingsIndex::MOPT_FRUITJAM_INTERNAL_SPEAKER:
            // {
            //     label = "Fruit Jam Internal Speaker";
            //     value = working.flags.fruitJamEnableInternalSpeaker ? "ON" : "OFF";
            //     break;
            // }
            case MenuSettingsIndex::MOPT_FRUITJAM_VOLUME_CONTROL:
            {
                label = "Fruit Jam Volume Control";
                sprintf(valueBuf, "%d", working.fruitjamVolumeLevel);
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
            case MenuSettingsIndex::MOPT_AUTO_SWAP_FDS_DISK:
            {
                label = "FDS Auto Swap Disk side";
                value = working.flags.autoSwapFDS ? "ON" : "OFF";
                break;
            }
            case MenuSettingsIndex::MOPT_AUTO_INSERT_FDS_DISK_A:
            {
                label = "FDS Auto Insert Disk 1 On Start";
                value = working.flags.autoInsertDiskA ? "ON" : "OFF";
                break;
            }
            case MenuSettingsIndex::MOPT_FDS_DISK_SWAP:
            {
                label = "Select disk";
                static char fdsBuf[16];
                if (!s_fdsHooks || !s_fdsHooks->get_num_sides)
                {
                    value = "N/A";
                }
                else
                {
                    int n = s_fdsHooks->get_num_sides();
                    if (s_fdsPendingChoice < 0)
                    {
                        // First render after menu open: seed pending choice
                        // from the live current side (or 0 if ejected).
                        int v = s_fdsHooks->get_swap_value();
                        s_fdsPendingChoice = (v >= n) ? 0 : v;
                    }
                    if (s_fdsPendingChoice == n)
                    {
                        value = "Reset";
                    }
                    else if (n == 1)
                    {
                        value = "Side A";
                    }
                    else
                    {
                        // 0 -> "Side A", 1 -> "Side B", ...
                        snprintf(fdsBuf, sizeof(fdsBuf), "Side %c",
                                 'A' + s_fdsPendingChoice);
                        value = fdsBuf;
                    }
                }
                break;
            }
            case MenuSettingsIndex::MOPT_CASSETTE:
            {
                label = "Cassette";
                static char casBuf[40];
                if (!s_cassetteHooks)
                {
                    value = "N/A";
                }
                else
                {
                    int n = s_cassetteHooks->get_num_tapes ? s_cassetteHooks->get_num_tapes() : 0;
                    if (s_cassettePendingChoice < 0)
                        s_cassettePendingChoice = cassetteLiveChoice(n);
                    value = cassetteChoiceLabel(s_cassettePendingChoice, n, casBuf, sizeof(casBuf));
                }
                break;
            }
            default:
                label = "Unknown";
                value = "";
                break;
            }
            snprintf(line, sizeof(line), "%s%s%s", label, (optIndex == MOPT_EXIT_GAME || optIndex == MOPT_SAVE_RESTORE_STATE || optIndex == MOPT_ENTER_BOOTSEL_MODE || optIndex == MOPT_REBOOT_TO_LOADER || optIndex == MOPT_RESET_GAME || optIndex == MOPT_CONTROLLER_TEST || optIndex == MOPT_RECENT_GAMES || optIndex == MOPT_USB_DRIVE_MODE) ? "" : ": ", value);
            putText(0, row++, line, CBLACK, CWHITE);
        }
        // Down-scroll indicator (centered): shown when there are options below the window
        putText(SCREEN_COLS / 2, downIndicatorRow,
                (firstVisibleOption + optionWindowSize < visibleCount) ? "v" : " ", CBLACK, CWHITE);
        // Render SAVE / CANCEL / DEFAULT at a fixed row, with per-word highlighting
        row = actionRowScreen;
        {
            const char *saveLabel  = settingsChanged ? "SAVE*" : "SAVE";
            const char *labels[3]  = { saveLabel, "CANCEL", "DEFAULT" };
            int lens[3]            = { (int)strlen(labels[0]), (int)strlen(labels[1]), (int)strlen(labels[2]) };
            const int gap          = 2; // spaces between words
            int totalLen           = lens[0] + gap + lens[1] + gap + lens[2];
            int startCol           = (SCREEN_COLS - totalLen) / 2;
            if (startCol < 0) startCol = 0;
            int col3 = startCol;
            for (int ai = 0; ai < 3; ++ai)
            {
                int fg = (onActionRow && actionSubSelect == ai) ? CWHITE : CBLACK;
                int bg = (onActionRow && actionSubSelect == ai) ? CBLACK : CWHITE;
                putText(col3, row, labels[ai], fg, bg);
                col3 += lens[ai] + gap;
            }
            row++;
        }
        // 64-color palette grid (4 rows x 16 columns). Each block is a space with fg=bg=colorIndex
        row = paletteStartRow;
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
            // FG= after first palette row, BG= after second
            int afterGrid = gridStartCol + blocksPerRow + 1;
            if (pr == 0)
            {
                snprintf(line, sizeof(line), "FG=%02d", working.fgcolor);
                putText(afterGrid, row, line, working.fgcolor, working.bgcolor);
            }
            else if (pr == 1)
            {
                snprintf(line, sizeof(line), "BG=%02d", working.bgcolor);
                putText(afterGrid, row, line, working.fgcolor, working.bgcolor);
            }
            row++;
        }
        // Help text (dynamic button labels)

        if (!onActionRow && visibleCount > 0)
        {
            int curOpt = visibleIndices[selectedOptionIndex];
            if (curOpt == MOPT_EXIT_GAME ||
                curOpt == MOPT_SAVE_RESTORE_STATE ||
                curOpt == MOPT_ENTER_BOOTSEL_MODE ||
                curOpt == MOPT_REBOOT_TO_LOADER ||
                curOpt == MOPT_RESET_GAME ||
                curOpt == MOPT_CONTROLLER_TEST ||
                curOpt == MOPT_RECENT_GAMES ||
                curOpt == MOPT_USB_DRIVE_MODE)
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
            // On the action row: show LEFT/RIGHT + confirm hint
            snprintf(line, sizeof(line), "LEFT/RIGHT: Select, %s: Confirm", buttonLabel1);
        }

        int helpCount = 2;
        row = SCREEN_ROWS - helpCount - 1; // leave one blank row at bottom
        int hlen = (int)strlen(line);
        int col = (SCREEN_COLS - hlen) / 2;
        if (col < 0)
            col = 0;
        putText(col, row++, line, CBLACK, CWHITE);
        if (onActionRow)
        {
            const char *actionHints[3] = { "Confirm changes", "Discard changes", "Restore defaults" };
            snprintf(line, sizeof(line), "%s: %s", buttonLabel1, actionHints[actionSubSelect]);
        }
        else
        {
            strcpy(line, ""); // no second line
        }
        // display helptext
        if (!onActionRow && visibleCount > 0)
        {
            putText(0, helpRowScreen, g_settings_descriptions[visibleIndices[selectedOptionIndex]], CBLACK, CWHITE);
        }
        else
        {
            putText(0, helpRowScreen, "                                        ", CBLACK, CWHITE);
        }

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
        // Suppress row-level highlight for action row; per-word colors handle it.
        // Selected screen row is derived from the logical option index + scroll offset.
        int displayRow = onActionRow
                             ? -1
                             : rowStartOptions + (selectedOptionIndex - firstVisibleOption);
        drawAllLines(displayRow);
    }; // redraw lambda
    // for volume control option: initialize audio stream
#if USE_I2S_AUDIO == PICO_AUDIO_I2S_DRIVER_TLV320
    // Initialize menu music
    /// wavplayer::init_memory();
    // Optional: set offset and/or switch to file
    // wavplayer::set_offset_seconds(0.8f); // skip initial silence
    // Uncomment to stream from a file on SD (must be a valid PCM 16-bit stereo WAV)
    char wavPath[40];
    strcpy(wavPath, RECORDEDSAMPLEFILE);
    if (!Frens::fileExists(wavPath))
    {
        snprintf(wavPath, sizeof(wavPath), DEFAULTSAMPLEFILEFORMAT, FrensSettings::getEmulatorTypeString());
        printf("Menu music file not found at /soundrecorder.wav, trying %s\n", wavPath);
    }
    if (wavplayer::use_file(wavPath))
    {

        printf("Streaming menu music from file.\n");
        // wavplayer::resume();
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
        if (!onActionRow && selectedOptionIndex >= 0 && selectedOptionIndex < visibleCount)
        {
            optIndex = visibleIndices[selectedOptionIndex];
        }
#if USE_I2S_AUDIO == PICO_AUDIO_I2S_DRIVER_TLV320 && PICO_RP2350
        if (optIndex == MOPT_FRUITJAM_VOLUME_CONTROL)
        {
            // resume audio stream for volume adjustment feedback
            wavplayer::resume();
        }
        else
        {
            // pause audio stream
            wavplayer::pause();
        }
#endif
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
                const int maxFirst = (visibleCount > optionWindowSize)
                                         ? visibleCount - optionWindowSize
                                         : 0;
                if (onActionRow)
                {
                    // re-enter list at the bottom; scroll so last option is visible
                    if (visibleCount > 0)
                    {
                        onActionRow = false;
                        selectedOptionIndex = visibleCount - 1;
                        firstVisibleOption = maxFirst;
                    }
                }
                else if (selectedOptionIndex == 0)
                {
                    onActionRow = true; // wrap to action row
                }
                else
                {
                    selectedOptionIndex--;
                    if (selectedOptionIndex < firstVisibleOption)
                        firstVisibleOption = selectedOptionIndex; // scroll up
                }
            }
            else if (pad & DOWN)
            {
                if (onActionRow)
                {
                    // re-enter list at the top
                    if (visibleCount > 0)
                    {
                        onActionRow = false;
                        selectedOptionIndex = 0;
                        firstVisibleOption = 0;
                    }
                }
                else if (selectedOptionIndex == visibleCount - 1)
                {
                    onActionRow = true; // wrap to action row
                }
                else
                {
                    selectedOptionIndex++;
                    if (selectedOptionIndex >= firstVisibleOption + optionWindowSize)
                        firstVisibleOption = selectedOptionIndex - optionWindowSize + 1; // scroll down
                }
            }
            else if (pad & LEFT || pad & RIGHT || ((pad & A) && (optIndex == MOPT_EXIT_GAME || optIndex == MOPT_SAVE_RESTORE_STATE || optIndex == MOPT_ENTER_BOOTSEL_MODE || optIndex == MOPT_REBOOT_TO_LOADER || optIndex == MOPT_RESET_GAME || optIndex == MOPT_FDS_DISK_SWAP || optIndex == MOPT_CASSETTE || optIndex == MOPT_CONTROLLER_TEST || optIndex == MOPT_RECENT_GAMES || optIndex == MOPT_USB_DRIVE_MODE)))
            {
                // LEFT/RIGHT on the action row cycles sub-selection
                if (onActionRow && (pad & (LEFT | RIGHT)))
                {
                    if (pad & RIGHT)
                        actionSubSelect = (actionSubSelect + 1) % 3;
                    else
                        actionSubSelect = (actionSubSelect + 2) % 3;
                }
                else if (optIndex != -1)
                {
                     if (optIndex == MOPT_ENTER_BOOTSEL_MODE && pad & A)
                    {
                       reset_usb_boot(0, 0);
                    }
                    if (optIndex == MOPT_REBOOT_TO_LOADER && pad & A)
                    {
                       Frens::rebootToBootloader(); // does not return
                    }
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
                     case MOPT_RESET_GAME:
                    {
                        rval = 5; // reset game
                        exitMenu = true;
                        break;
                    }
                    case MOPT_CONTROLLER_TEST:
                    {
                        // LEFT/RIGHT also reach this case via the OR-list above;
                        // only A opens the screen.
                        if (pad & A)
                        {
                            showControllerTestScreen();
                            startFrames = -1; // re-seed idle counter: frames spent in the
                                              // test screen must not trip the screensaver
                        }
                        break;
                    }
                    case MOPT_USB_DRIVE_MODE:
                    {
#if FRENS_USB_MSC
                        // LEFT/RIGHT also reach this case via the OR-list above;
                        // only A opens the screen. Rom browser only - see the
                        // visibleIndices gate at the top of this function.
                        if (pad & A)
                        {
                            // Close the menu music file first. USB drive mode
                            // unmounts FatFs and hands the raw card to a PC, so
                            // no file handle may stay open across it - and this
                            // function opened one itself further up.
                            wavplayer::reset();
                            if (showUsbDriveScreen())
                            {
                                mediaChanged = true;
                            }
#if USE_I2S_AUDIO == PICO_AUDIO_I2S_DRIVER_TLV320
                            // Re-open it against the remounted volume so the
                            // volume option still previews audio.
                            wavplayer::use_file(wavPath);
#endif
                            startFrames = -1; // re-seed idle counter: frames spent
                                              // in USB drive mode are not idle time
                        }
#endif
                        break;
                    }
                    case MOPT_RECENT_GAMES:
                    {
                        // Only reachable from the rom browser: the option is not
                        // added to visibleIndices when calledFromGame, so rval 6
                        // can never be returned to an emulator's main loop.
                        if (pad & A)
                        {
                            if (recentLaunchPath &&
                                showRecentGamesMenu(recentLaunchPath, RECENTGAMES_MAXPATH) == 1)
                            {
                                rval = 6; // menu() starts recentLaunchPath
                                exitMenu = true;
                            }
                            startFrames = -1;
                        }
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
                        if (working.screenMode == ScreenMode::SCANLINE_8_7)
                            working.scanlineType = (uint8_t)ScanlineType::SIMPLE;
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
                    case MOPT_SCANLINE_TYPE:
                    {
                        if (working.screenMode != ScreenMode::SCANLINE_8_7)
                        {
                            int t = working.scanlineType;
                            if (right)
                                t = (t + 1) % (int)ScanlineType::MAX;
                            else
                                t = (t == 0) ? (int)ScanlineType::MAX - 1 : t - 1;
                            working.scanlineType = (uint8_t)t;
                        }
                        break;
                    }
                    case MOPT_FPS_OVERLAY:
                        working.flags.displayFrameRate = !working.flags.displayFrameRate;
                        break;
                    case MOPT_AUDIO_ENABLE:
                        working.flags.audioEnabled = !working.flags.audioEnabled;
                        working.flags.frameSkip = working.flags.audioEnabled;
                        break;
                    case MOPT_DISPLAY_MODE:
                        working.flags.useDVIModeForHDMI = !working.flags.useDVIModeForHDMI;
                        working.flags.useExtAudio = working.flags.useDVIModeForHDMI;
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
                    case MOPT_OVERCLOCK:
                        working.flags.overclock = !working.flags.overclock;
                        break;
                    case MOPT_FM_AUDIO:
                        working.flags.useFM = !working.flags.useFM;
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
                    // case MOPT_FRUITJAM_INTERNAL_SPEAKER:
                    // {
                    //     working.flags.fruitJamEnableInternalSpeaker = !working.flags.fruitJamEnableInternalSpeaker;
                    //     break;
                    // }
                    case MOPT_FRUITJAM_VOLUME_CONTROL:
                    {
                        if (right)
                        {
                            if (working.fruitjamVolumeLevel < 23)
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
                    case MOPT_AUTO_SWAP_FDS_DISK:
                    {
                        working.flags.autoSwapFDS = !working.flags.autoSwapFDS;
                        break;
                    }
                    case MOPT_AUTO_INSERT_FDS_DISK_A:
                    {
                        working.flags.autoInsertDiskA = !working.flags.autoInsertDiskA;
                        break;
                    }
                    case MOPT_FDS_DISK_SWAP:
                    {
                        if (!s_fdsHooks || !s_fdsHooks->get_num_sides) break;
                        int n = s_fdsHooks->get_num_sides();
                        if (n <= 0) break;
                        int total = n + 1; // sides + Reset

                        if (s_fdsPendingChoice < 0)
                        {
                            int v = s_fdsHooks->get_swap_value();
                            s_fdsPendingChoice = (v >= n) ? 0 : v;
                        }

                        if (pad & A)
                        {
                            // Commit. n means "Reset", anything else is a side index.
                            if (s_fdsPendingChoice == n)
                            {
                                rval = 5; // reset game (same as MOPT_RESET_GAME)
                            }
                            else
                            {
                                if (s_fdsHooks->request_swap)
                                    s_fdsHooks->request_swap(s_fdsPendingChoice);
                                rval = 0; // stay in game, no settings save needed
                            }
                            exitMenu = true;
                            s_fdsPendingChoice = -1; // reset for next open
                        }
                        else
                        {
                            // LEFT/RIGHT just preview the next choice; do not
                            // commit until the user presses A.
                            s_fdsPendingChoice = right
                                ? (s_fdsPendingChoice + 1) % total
                                : (s_fdsPendingChoice + total - 1) % total;
                        }
                        break;
                    }
                    case MOPT_CASSETTE:
                    {
                        if (!s_cassetteHooks) break;
                        int n = s_cassetteHooks->get_num_tapes ? s_cassetteHooks->get_num_tapes() : 0;
                        int total = cassetteChoiceCount(n);
                        if (total <= 0) break;

                        if (s_cassettePendingChoice < 0)
                            s_cassettePendingChoice = cassetteLiveChoice(n);

                        if (!(pad & A))
                        {
                            // LEFT/RIGHT only previews; nothing is opened until A.
                            s_cassettePendingChoice = right
                                ? (s_cassettePendingChoice + 1) % total
                                : (s_cassettePendingChoice + total - 1) % total;
                            break;
                        }

                        int choice = s_cassettePendingChoice;

                        if (choice == n + 3)                    // Rewind: acts, changes nothing
                        {
                            if (s_cassetteHooks->rewind) s_cassetteHooks->rewind();
                        }
                        else if (choice == n + 1 || choice == n + 2)
                        {
                            int recMode = (choice == n + 1) ? CAS_MODE_REC_WAV : CAS_MODE_REC_CAS;

                            // SAVE CS1 carries no filename, so this is where the tape gets
                            // labelled. Without a keyboard there is no ENTER to confirm
                            // with, so the default name is used as-is.
                            char label[TAPE_LABEL_MAX] = {0};
                            if (s_cassetteHooks->default_name)
                                s_cassetteHooks->default_name(label, sizeof(label));

                            bool go = true;
                            if (io::getCurrentKeyboardState().connected)
                                go = showTextEntry("Label this tape:", label, sizeof(label));

                            if (go && s_cassetteHooks->name_exists &&
                                s_cassetteHooks->name_exists(label, recMode))
                            {
                                go = showDialogYesNo("Tape exists. Overwrite?");
                            }
                            if (go && s_cassetteHooks->commit &&
                                s_cassetteHooks->commit(-1, recMode, label) != 0)
                            {
                                showMessageBox("Could not start recording.", CRED);
                            }
                            if (s_cassetteHooks->refresh) s_cassetteHooks->refresh();
                        }
                        else if (s_cassetteHooks->commit)
                        {
                            // Empty, or Play a tape. Play always reopens at the start,
                            // which is what BASIC's CHECK TAPE verify pass needs after a
                            // save: switch from Record to Play and the tape is rewound.
                            if (s_cassetteHooks->commit(choice - 1,
                                                        choice == 0 ? CAS_MODE_EMPTY : CAS_MODE_PLAY,
                                                        nullptr) != 0)
                            {
                                showMessageBox("Could not read that tape.", CRED);
                            }
                        }

                        rval = 0;                   // stay in the game, nothing to save
                        exitMenu = true;
                        s_cassettePendingChoice = -1;
                        break;
                    }
                    default:
                        break;
                    }
                }
            }
            else if (pad & A)
            {
                if (onActionRow)
                {
                    switch (actionSubSelect)
                    {
                    case 0: // SAVE
                        applySettings = true;
                        exitMenu = true;
                        break;
                    case 1: // CANCEL
                        applySettings = false;
                        exitMenu = true;
                        break;
                    case 2: // DEFAULT
                        FrensSettings::resetsettings(&working);
                        break;
                    }
                }
            }
            else if (pad & B)
            {
                // B acts like cancel
                applySettings = false;
                exitMenu = true;
            }
        }
        // startFrames == -1 means "re-seed on the next pass" - set by the
        // handlers that open a screen of their own, because the frames spent
        // in there are not idle time. It must not be treated as a timestamp:
        // frameCount - (-1) is over 3600 for all but the first minute of
        // uptime, which silently turned any result those handlers had just
        // set into rval 2 (screensaver).
        if (startFrames != -1 && frameCount - startFrames > 3600)
        {
            // if no input for 3600 frames, start screensaver
            rval = 2;
            break;
        }
    }
    if (applySettings && (rval == 0 || rval == 5))
    {
#if HW_CONFIG != 7
        // Enabling overclock (OFF->ON) boots into a higher clock. If that target
        // exceeds the safe default, warn first and let the user back out. Checked
        // before the commit below, while settings still holds the old value.
        if (working.flags.overclock && !settings.flags.overclock &&
            Frens::getMaxFreqKHz() > OVERCLOCK_WARN_KHZ)
        {
            if (!showOverclockWarning(Frens::getMaxFreqKHz() / 1000, Frens::getMaxVoltage()))
            {
                working.flags.overclock = 0; // Undo: keep overclock OFF, no reboot
            }
        }
#endif
        // Copy working settings into global settings and persist.
        // Preserve directory navigation fields that user did not edit here.
        working.firstVisibleRowINDEX = settings.firstVisibleRowINDEX;
        working.selectedRow = settings.selectedRow;
        working.horzontalScrollIndex = settings.horzontalScrollIndex;
        strcpy(working.currentDir, settings.currentDir);
        settings = working;
        FrensSettings::savesettings();
        if (rval == 0) rval = 1;

        // If the overclock toggle disagrees with the live clock, rewrite
        // FlashParams and reboot. writeFlashParamsToFlash arms the watchdog
        // and never returns.
#if HW_CONFIG != 7
        uint32_t liveKHz   = clock_get_hz(clk_sys) / 1000;
        uint32_t targetKHz = (settings.flags.overclock || settings.flags.useFM) ? Frens::getMaxFreqKHz() : Frens::getMinFreqKHz();
        //vreg_voltage targetV = (settings.flags.overclock || settings.flags.useFM) ? Frens::getMaxVoltage() : Frens::getMinVoltage();
        if (liveKHz != targetKHz ) //&& FrensSettings::getEmulatorType() != FrensSettings::emulators::SNES)
        {
            showLoadingScreen((settings.flags.overclock || settings.flags.useFM) ? "Enabling overclock" : "Disabling overclock", 60);
            if (liveKHz < targetKHz)
            {
                if (Frens::WriteMaxValuesToFlash() == false)
                {
                    printf("Failed to write max values to flash\n");
                }
            }
            else
            {
                if (Frens::WriteMinValuesToFlash() == false)
                {
                    printf("Failed to write min values to flash\n");
                }
            }
        }
#endif
    }
    // A PC wrote to the card while in USB drive mode, so the rom list on screen
    // is out of date. rval 1 is what makes the browser re-run romlister.list()
    // for the current directory. Done after the commit block above so pending
    // setting edits are still saved normally.
    if (mediaChanged && rval == 0)
    {
        rval = 1;
    }
    Frens::f_free(workingDyn);
    // restore contents of swap file back to altScreenbuffer when not nullptr
    if (calledFromGame)
    {

        ClearScreen(CBLACK); // Removes artifacts from previous screen
        waitForNoButtonPress();
        Frens::f_free((void *)screenBuffer);
        screenBuffer = nullptr;

      
        scaleMode8_7_ = Frens::applyScreenMode(settings.screenMode);
#if !HSTX
        // Do not reset the margins when framebuffer is used, this will lock up the display driver
        // Margins will be handled by the framebuffer.
        if (!Frens::isFrameBufferUsed())
        {
            dvi_->getBlankSettings().top = margintop;
            dvi_->getBlankSettings().bottom = marginbottom;
        }
#endif
        // Speaker can be muted/unmuted from settings menu
        //EXT_AUDIO_MUTE_INTERNAL_SPEAKER(settings.flags.fruitJamEnableInternalSpeaker == 0);
        EXT_AUDIO_SETVOLUME(settings.fruitjamVolumeLevel);
        Frens::PaceFrames60fps(true, true);
        //Frens::waitForVSync();
    }
#if USE_I2S_AUDIO == PICO_AUDIO_I2S_DRIVER_TLV320
    wavplayer::reset(); // stop menu music
#endif
    settingsActive = false; 
    Frens::PaceFrames60fps(true); // ensure normal timing after menu
    return rval;
}
void setclockInFlashAndReboot(uint32_t freq, vreg_voltage voltage)
{
    Frens::FlashParams flashParams;
    flashParams.cpuFreqKHz = freq;
    flashParams.voltage = voltage;
    auto flashparamInFlash = ((uintptr_t)&__flash_binary_end + 0xFFF) & ~0xFFF;
    flashparamInFlash -= XIP_BASE;
    printf("Writing clock params to flash at 0x%08X: freq %d kHz, voltage %d\n", (unsigned int)flashparamInFlash, flashParams.cpuFreqKHz, (int)flashParams.voltage);
}

void menu(const char *title, char *errorMessage, bool isFatal, bool showSplash, const char *allowedExtensions, char *rompath)
{
    FRESULT fr;
    // No FIL here on purpose: it is 592 bytes and this frame lives for the
    // whole menu, on a 3 KB stack. File access goes through helpers that take
    // their FIL off the heap.
    DWORD PAD1_Latch;
    char curdir[FF_MAX_LFN];
    auto clockFreq = clock_get_hz(clk_sys) / 1000; // in kHz
#if !PICO_RP2350
    EXT_AUDIO_DISABLE();
#endif
#if ENABLE_VU_METER
    turnOffAllLeds();
#endif
    // artworkEnabled = isArtWorkEnabled();
    crcOffset = FrensSettings::getEmulatorType() == FrensSettings::emulators::NES ? 16 : 0; // crc offset according to  https://github.com/ducalex/retro-go-covers
    printf("Emulator: %s, crcOffset: %d\n", FrensSettings::getEmulatorTypeString(), crcOffset);
#if !HSTX
    int margintop = dvi_->getBlankSettings().top;
    int marginbottom = dvi_->getBlankSettings().bottom;
    printf("Top margin: %d, bottom margin: %d\n", margintop, marginbottom);
    dvi_->getBlankSettings().top = 0;
    dvi_->getBlankSettings().bottom = 0;
#endif
    scaleMode8_7_ = Frens::applyScreenMode(ScreenMode::NOSCANLINE_1_1);
    abSwapped = 1; // Swap A and B buttons, so menu is consistent across different emulators
    Frens::PaceFrames60fps(true, true);
    //Frens::waitForVSync();
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
            showMessageBox("An error has occurred", CRED, errorMessage);
        }
        showSplash = false;
    }
#if USE_I2S_AUDIO == PICO_AUDIO_I2S_DRIVER_TLV320
    if (EXT_AUDIO_DACERROR())
    {
        DisplayDacError();
    }
#endif
    if (showSplash && !watchdog_enable_caused_reboot())
    {
        showSplash = false;
#if !BOOTLOADER_BUILD
        printf("Showing splash screen\n");
        showSplashScreen();
#else
        printf("Bootloader build, skipping splash screen\n");
#endif
    }
    srand(get_rand_32()); // Seed the random number generator for screensaver
    // Scratch for the path picked in the recently played list. Allocated after
    // the error handling above, which can call DisplayFatalError and never
    // return. Feature degrades gracefully when it cannot be allocated.
    recentLaunchPath = (char *)Frens::f_malloc(RECENTGAMES_MAXPATH);
    if (recentLaunchPath)
    {
        recentLaunchPath[0] = 0;
    }
    romlister.list(settings.currentDir);
    displayRoms(romlister, settings.firstVisibleRowINDEX);
    bool startGame = false;
    bool startRecent = false;
    int oldIndex = -1;
    bool isWav = false;
    waitForNoButtonPress();
    while (1)
    {
        char fileExt[8];
        auto frameCount = Menu_LoadFrame();
      
        auto index = settings.selectedRow - STARTROW + settings.firstVisibleRowINDEX;
        auto entries = romlister.GetEntries();
        selectedRomOrFolder = (romlister.Count() > 0) ? entries[index].Path : nullptr;
   
#if PICO_RP2350
        if (selectedRomOrFolder  && entries[index].IsDirectory == false )
        {
            // check if selected file is a .wav file
            Frens::getextensionfromfilename(selectedRomOrFolder, fileExt, sizeof(fileExt));
            isWav = (strcasecmp(fileExt, ".wav") == 0);
        } else {
            isWav = false;
        }
#endif
// SGX auto-clock-switch is only safe on builds using PIO-USB. On
// TinyUSB-native-USB builds PLL_USB must stay at 48 MHz, so we can't decouple
// clk_hstx from clk_sys at 378 MHz — running there would produce visible TMDS
// artifacts. Skip the auto-switch in that case; the .sgx ROM still loads and
// runs at the build's default 252 MHz (slower on the heavier titles but clean).
#if (HSTX && SGX && CFG_TUH_RPI_PIO_USB) 
        // Skip per-ROM auto-switch when user explicitly locked overclock on:
        // the system stays at FLASHPARAM_MAX_FREQ_KHZ for every ROM.
        if (!settings.flags.overclock && selectedRomOrFolder && entries[index].IsDirectory == false && oldIndex != index)
        {
            oldIndex = index;
            if ( strncasecmp(fileExt, ".sgx", 4) == 0)
            {
                if (clockFreq != Frens::getMaxFreqKHz())
                {
                    char message[40];
                    snprintf(message, sizeof(message), "Setting clock to  %dMHZ for SGX", Frens::getMaxFreqKHz() /1000);
                    showLoadingScreen(message, 60);
                    FrensSettings::savesettings(); // save current settings before changing clock
                    if (Frens::WriteMaxValuesToFlash() == false)
                    {
                        printf("Failed to write flash params for high clock\n");
                    }
                }
            } else {
                if (clockFreq != Frens::getMinFreqKHz())
                {
                    char message[40];
                    snprintf(message, sizeof(message), "Setting clock to  %dMHZ", Frens::getMinFreqKHz() /1000);
                    showLoadingScreen(message, 60);
                    FrensSettings::savesettings(); // save current settings before changing clock
                    if (Frens::WriteMinValuesToFlash() == false)
                    {
                        printf("Failed to write flash params for low clock\n");
                    }
                }
            }
        }
#endif
#if RETROJAM
        // retroJam: adjust clock speed and crc offset based on selected ROM type
        if (selectedRomOrFolder && entries[index].IsDirectory == false && oldIndex != index)
        {        
            oldIndex = index;
            // set emulator type based on file extension of the currently selected ROM           
            if (!isWav)
            {
                FrensSettings::setEmulatorType((const char *)fileExt);
                crcOffset = FrensSettings::getEmulatorType() == FrensSettings::emulators::NES ? 16 : 0; // crc offset according to  https://github.com/ducalex/retro-go-covers
            }
            // printf("Emulator: %s, settingstype %s, crcOffset: %d, Current clock freq: %d kHz\n", FrensSettings::getEmulatorTypeString(), FrensSettings::getEmulatorTypeString(true), crcOffset, (unsigned int)clockFreq);
            // Sega Genesis: adjust to higher clock speed.
            // printf(" Current clock freq: %d kHz\n", (unsigned int)clockFreq);
            if (FrensSettings::getEmulatorType() == FrensSettings::emulators::GENESIS && !isWav)
            {
                // Skip clock switch when user locked overclock on (stay at MAX).
                if (!settings.flags.overclock && clockFreq != Frens::getMaxFreqKHz())
                {
                    char message[40];
                    snprintf(message, sizeof(message), "Setting clock to  %dMHZ", Frens::getMaxFreqKHz() /1000);
                    showLoadingScreen(message, 60);
                    FrensSettings::savesettings(); // save current settings before changing clock
                    if (Frens::WriteMaxValuesToFlash() == false)
                    {
                        printf("Failed to write flash params for high clock\n");
                    }
                }
            }
            else
            {
                // Skip clock switch when user locked overclock on (stay at MAX).
                if (!settings.flags.overclock && clockFreq != Frens::getMinFreqKHz())
                {
                    char message[40];
                    snprintf(message, sizeof(message), "Setting clock to  %dMHZ", Frens::getMinFreqKHz() /1000);
                    showLoadingScreen(message, 60);
                    FrensSettings::savesettings(); // save current settings before changing clock
                    if (Frens::WriteMinValuesToFlash() == false)
                    {
                        printf("Failed to write flash params for low clock\n");
                    }
                }
            }
        }
#endif
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
#if !HSTX
            if ((PAD1_Latch)&UP && (PAD1_Latch & SELECT))
            {
                if (clockFreq == Frens::getMaxFreqKHz())
                {
                    printf("Emergency reset to low clock speed requested\n");
                    // Emergency reset to default settings and clock speed
                    // This can be used to in case there is no display or unstable display because of high clock speed settings
                    FrensSettings::resetsettings();
                    FrensSettings::savesettings();
                    Frens::WriteMinValuesToFlash();
                } else {
                    printf("Emergency reset requested, but already at low clock speed\n");
                }
            }
#endif
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
                oldIndex = -1;
                fr = f_getcwd(settings.currentDir, FF_MAX_LFN); // f_getcwd(settings.currentDir, FF_MAX_LFN);
                if (fr == FR_OK)
                {

                    if (strcmp(settings.currentDir, "/") != 0)
                    {
                        // Capture the directory we're leaving so we can
                        // re-highlight it in the parent listing.
                        char childName[ROMLISTER_MAXPATH] = {0};
                        const char *slash = strrchr(settings.currentDir, '/');
                        if (slash && *(slash + 1) != '\0')
                        {
                            strncpy(childName, slash + 1, sizeof(childName) - 1);
                        }

                        romlister.list("..");

                        int foundIndex = -1;
                        if (childName[0] != '\0')
                        {
                            auto *parentEntries = romlister.GetEntries();
                            for (size_t i = 0; i < romlister.Count(); ++i)
                            {
                                if (parentEntries[i].IsDirectory &&
                                    strcasecmp(parentEntries[i].Path, childName) == 0)
                                {
                                    foundIndex = (int)i;
                                    break;
                                }
                            }
                        }

                        if (foundIndex >= 0)
                        {
                            settings.firstVisibleRowINDEX =
                                (foundIndex / PAGESIZE) * PAGESIZE;
                            settings.selectedRow =
                                STARTROW + (foundIndex - settings.firstVisibleRowINDEX);
                        }
                        else
                        {
                            settings.firstVisibleRowINDEX = 0;
                            settings.selectedRow = STARTROW;
                        }
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
                if (settingsResult == 6)
                {
                    // A game was picked in the recently played list.
                    startRecent = true;
                    break;
                }
                displayRoms(romlister, settings.firstVisibleRowINDEX);
                totalFrames = -1; // re-seed: frames spent in the settings menu are not idle time
                continue;         // skip other processing this frame
            }
            else if ((PAD1_Latch & X) == X)
            {
                // Recently played games. Also reachable from the settings menu
                // (SELECT), which is the route for pads without an X button -
                // notably a NES pad on the GPIO port.
                if (recentLaunchPath && showRecentGamesMenu(recentLaunchPath, RECENTGAMES_MAXPATH) == 1)
                {
                    startRecent = true;
                    break; // launch it below, outside the browser loop
                }
                displayRoms(romlister, settings.firstVisibleRowINDEX);
                totalFrames = -1; // re-seed: frames spent in the list are not idle time
                continue;         // skip other processing this frame
            }
            else if ((PAD1_Latch & START) == START && ((PAD1_Latch & SELECT) != SELECT) && !isWav)
            {
                // show screen with ArtWork

                if (!entries[index].IsDirectory && selectedRomOrFolder && isArtWorkEnabled())
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
            }
            else if ((startGame || (PAD1_Latch & A) == A) && selectedRomOrFolder && !isWav)
            {
                oldIndex = -1;
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
#if PICO_RP2350
                    // Stop any playing WAV file
                    wavplayer::reset();
                    lastWavPath[0] = '\0';
#endif
                    showLoadingScreen();
                    fr = f_getcwd(curdir, sizeof(curdir)); // f_getcwd(curdir, sizeof(curdir));
                    printf("Current dir: %s\n", curdir);
                    if (startRom(curdir, selectedRomOrFolder, rompath))
                    {
                        break; // from while(1) loop, so we can reboot or return to main.cpp
                    }
                }
            }
            else if (((PAD1_Latch & A) == A || (PAD1_Latch & START) == START) && selectedRomOrFolder && isWav)
            {
#if PICO_RP2350
                // Build full path of highlighted WAV
                fr = f_getcwd(curdir, sizeof(curdir));
                char fullWavPath[FF_MAX_LFN];
                snprintf(fullWavPath, sizeof(fullWavPath), "%s/%s", curdir, selectedRomOrFolder);

                // If same track is already playing, stop it; else start new track
                if (wavplayer::isPlaying() && strcmp(fullWavPath, lastWavPath) == 0)
                {
                    printf("Stopping WAV playback: %s\n", fullWavPath);
                    wavplayer::reset();
                    lastWavPath[0] = '\0';
                }
                else
                {
                    printf("Playing WAV file: %s\n", fullWavPath);
                    wavplayer::reset();
                    if (wavplayer::use_file(fullWavPath))
                    {
                        EXT_AUDIO_SETVOLUME(settings.fruitjamVolumeLevel);
                        wavplayer::resume();
                        strncpy(lastWavPath, fullWavPath, sizeof(lastWavPath) - 1);
                        lastWavPath[sizeof(lastWavPath) - 1] = '\0';
                    }
                    else
                    {
                        printf("Error opening WAV file: %s\n", fullWavPath);
                    }
                }
#endif
            }
        }
        // Refresh selectedRomOrFolder in case navigation changed selectedRow this frame
        auto newIdx = settings.selectedRow - STARTROW + settings.firstVisibleRowINDEX;
        selectedRomOrFolder = (romlister.Count() > 0) ? entries[newIdx].Path : nullptr;

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

            if (!wavplayer::isPlaying())
            {
                screenSaver();
                romlister.list(".");
                displayRoms(romlister, settings.firstVisibleRowINDEX);
            }
        }
    } // while 1

    // A game picked from the recently played list is started here, outside the
    // browser loop, so it takes exactly the same path as a normal launch.
    if (startRecent)
    {
        printf("Recent: launching '%s'\n", recentLaunchPath ? recentLaunchPath : "(null)");
        showLoadingScreen();
        char *slash = recentLaunchPath ? strrchr(recentLaunchPath, '/') : nullptr;
        if (slash)
        {
            *slash = 0; // split "<dir>/<name>" in place; dir is "" in the root
            bool ok = startRom(recentLaunchPath, slash + 1, rompath);
            printf("Recent: startRom(dir='%s', name='%s') -> %d, rompath='%s'\n",
                   recentLaunchPath, slash + 1, ok, rompath);
        }
        else
        {
            printf("Recent: no directory separator in path, cannot start\n");
        }
    }

    ClearScreen(CBLACK); // Removes artifacts from previous screen
                         // Wait until user has released all buttons
    waitForNoButtonPress();
    Frens::f_free(screenBuffer);
    Frens::f_free(recentLaunchPath);
    recentLaunchPath = nullptr;
    // Frens::f_free(buffer);

    FrensSettings::savesettings();
     // Reset the screen mode to the original settings
    scaleMode8_7_ = Frens::applyScreenMode(settings.screenMode);
#if !HSTX
   
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
    // skipRebootAfterMenu is the START_FLASHED_ROM_WITHOUT_REBOOT testing path:
    // the rom is already in flash and verified, so there is nothing to program.
    if (!Frens::isPsramEnabled() && !skipRebootAfterMenu)
    {
#if WII_PIN_SDA >= 0 and WII_PIN_SCL >= 0
        wiipad_end();
#endif
        // Don't return from this function call, but reboot in order to get avoid several problems with sound and lockups (WII-pad)
        // After reboot the emulator will flash the rom and start the selected game.
        Frens::resetWifi();
        printf("Rebooting to start %s\n", rompath[0] ? rompath : "(rom named in " ROMINFOFILE ")");
        watchdog_enable(1, 1);
        while (1)
        {
            tight_loop_contents();
            // printf("Waiting for reboot...\n");
        };
        // Never return
    }
    Frens::restoreScanlines();
    Frens::PaceFrames60fps(true, true); // reset frame pacing
    //Frens::waitForVSync();
}
