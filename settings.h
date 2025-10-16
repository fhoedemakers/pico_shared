#ifndef SETTINGS
#define SETTINGS
#include "ff.h"
#include "FrensHelpers.h"
#include <stdint.h>
#define SETTINGSFILE "/settings.dat" // File to store settings
extern struct settings settings;
#define SETTINGS_VERSION 100

// Border rendering mode enumeration
enum BorderMode {
    DEFAULTBORDER = 0,  // Use default static border
    RANDOMBORDER = 1,   // Pick a random border each time (implementation dependent)
    THEMEDBORDER = 2    // Use a border that matches current theme/game
};

struct settings
{

    unsigned short version = SETTINGS_VERSION; // version of settings structure
    union
    {
        ScreenMode screenMode;
        uint8_t scanlineOn;
    };
    unsigned short firstVisibleRowINDEX;
    unsigned short selectedRow;
    unsigned short horzontalScrollIndex;
    unsigned short fgcolor;
    unsigned short bgcolor;
    // int reserved[3];
    char currentDir[FF_MAX_LFN];
    struct
    {
        unsigned short useExtAudio : 1;   // 0 = use DVIAudio, 1 = use external Audio
        unsigned short enableVUMeter : 1; // 0 = disable VU meter, 1 = enable VU meter
        unsigned short borderMode : 2;    // BorderMode enum (2 bits)
        unsigned short reserved : 12;
    } flags; // Total 16 bits
};
namespace Frens
{
    void savesettings();
    void loadsettings();
    void resetsettings();
}
#endif