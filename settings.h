#ifndef SETTINGS
#define SETTINGS
#include "ff.h"
#include "FrensHelpers.h"
#define SETTINGSFILE "/settings.dat" // File to store settings
extern struct settings settings;

struct settings
{
    union {
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
    int useExtAudio; // 0 = use DVIAudio, 1 = use external Audio
};
namespace Frens
{
    void savesettings();
    void loadsettings();
    void resetsettings();
}
#endif