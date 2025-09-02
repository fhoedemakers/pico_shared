#ifndef SETTINGS
#define SETTINGS
#include "ff.h"
#include "FrensHelpers.h"
#define SETTINGSFILE "/settings.dat" // File to store settings
extern struct settings settings;

struct settings
{
    union
    {
        ScreenMode screenMode;
        int scanlineOn;
    };
    int firstVisibleRowINDEX;
    int selectedRow;
    int horzontalScrollIndex;
    int fgcolor;
    int bgcolor;
    // int reserved[3];
    char currentDir[FF_MAX_LFN];
    struct
    {
        unsigned short useExtAudio : 1; // 0 = use DVIAudio, 1 = use external Audio
        unsigned short enableVUMeter : 1; // 0 = disable VU meter, 1 = enable VU meter
        unsigned short reserved : 14;
    } flags;
};
namespace Frens
{
    void savesettings();
    void loadsettings();
    void resetsettings();
}
#endif