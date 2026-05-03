#ifndef SETTINGS
#define SETTINGS
#include "ff.h"
#include "FrensHelpers.h"
#include <stdint.h>


extern struct settings settings;
#define SETTINGS_VERSION 110

struct settings
{
    unsigned short version = SETTINGS_VERSION; // version of settings structure
   
    ScreenMode screenMode;
    short firstVisibleRowINDEX;
    short selectedRow;
    short horzontalScrollIndex;
    unsigned short fgcolor;
    unsigned short bgcolor;
    // int reserved[3];
    char currentDir[FF_MAX_LFN];
    int8_t fruitjamVolumeLevel; // Volume level for Fruit Jam internal speaker in db
    struct
    {
        unsigned short useExtAudio : 1;      // 0 = use DVIAudio, 1 = use external Audio
        unsigned short enableVUMeter : 1;    // 0 = disable VU meter, 1 = enable VU meter
        unsigned short borderMode : 2;       // BorderMode enum (2 bits)
        unsigned short dmgLCDPalette : 2;    // DMG LCD Palette (2 bits) 0=Green 1=Color 2=B&W
        unsigned short audioEnabled : 1;     // 1 = audio on, 0 = audio muted
        unsigned short displayFrameRate : 1; // 1 = show FPS overlay, 0 = do not show
        unsigned short frameSkip : 1;        // 1 = enable frame skipping, 0 = disable frame skipping
        unsigned short scanlineOn : 1;        // 1 = scanlines on, 0 = scanlines off
       // unsigned short fruitJamEnableInternalSpeaker : 1; // 1 = enable Fruit Jam internal speaker, 0 = disable
        unsigned short rapidFireOnA : 1;      // 1 = rapid fire on A button, 0 = off
        unsigned short rapidFireOnB : 1;      // 1 = rapid fire on B button, 0 = off
        unsigned short useDVIModeForHDMI : 1;      // 1 = use DVI mode for HDMI output (lower latency, but no audio), 0 = use HDMI mode (required for audio, but slightly higher latency)
        unsigned short autoSwapFDS : 1;      // 1 = automatically swap FDS disk sides when loading a .fds file, 0 = do not auto swap (user must manually select "FDS Disk Swap" in settings menu to swap sides). Default to on, because it's less confusing for users if the correct disk side is automatically loaded.
        unsigned short reserved : 2;         // keep struct size the same
    } flags; // Total 16 bits
   
};
namespace FrensSettings
{
   
    // Border rendering mode enumeration
    enum BorderMode
    {
        DEFAULTBORDER = 0, // Use default static border
        RANDOMBORDER = 1,  // Pick a random border each time (implementation dependent)
        THEMEDBORDER = 2   // Use a border that matches current theme/game
    };
    typedef enum {
        NES = 0,
        SMS = 1,
        GAMEBOY = 2,
        GENESIS = 3,
        MULTI = 4
    } emulators;
    static emulators emulatorType = NES;
    void initSettings(emulators emu) ;
    void setEmulatorType(const char * fileextension);
    void savesettings();
    void loadsettings();
    void resetsettings(struct settings *settings = nullptr);
    emulators getEmulatorType();
    const char *getEmulatorTypeString(bool forSettings = false);
    emulators getEmulatorTypeForSettings();
}
extern const int8_t *g_settings_visibility;
extern const uint8_t *g_available_screen_modes;
// Non-const so the FDS disk-swap entry can be flipped on at runtime
// when a .fds image is loaded (see main.cpp).
extern int8_t g_settings_visibility_nes[];
extern const int8_t g_settings_visibility_gb[];
extern const int8_t g_settings_visibility_sms[];
extern const int8_t g_settings_visibility_md[];
#endif