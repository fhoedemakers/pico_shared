#ifndef FRENSHELPERS
#define FRENSHELPERS
#include <string>
#include <algorithm>
#include <memory>

#include "dvi/dvi.h"
#include "dvi_configs.h"
enum class ScreenMode
    {
        SCANLINE_8_7,
        NOSCANLINE_8_7,
        SCANLINE_1_1,
        NOSCANLINE_1_1,
        MAX,
    };
#define CBLACK 15
#define CWHITE 48
#define CRED 6
#define CGREEN 0x2A
#define CBLUE 2
#define CLIGHTBLUE 0x11
#define DEFAULT_FGCOLOR CBLACK // 60
#define DEFAULT_BGCOLOR CWHITE

#define ERRORMESSAGESIZE 40
#define GAMESAVEDIR "/SAVES"
#define ROMINFOFILE "/currentloadedrom.txt"
extern uintptr_t ROM_FILE_ADDR ; //0x10090000
extern int maxRomSize;
extern char ErrorMessage[];
extern std::unique_ptr<dvi::DVI> dvi_;
extern bool scaleMode8_7_;

extern char __flash_binary_start;  // defined in linker script
extern char __flash_binary_end; 

namespace Frens
{
    bool endsWith(std::string const &str, std::string const &suffix);
    std::string str_tolower(std::string s);

    bool cstr_endswith(const char *string, const char *width);
    char **cstr_split(const char *str, const char *delimiters, int *count);
    void stripextensionfromfilename(char *filename);
    char *GetfileNameFromFullPath(char *fullPath);
    bool initSDCard();
    bool applyScreenMode(ScreenMode screenMode_);
    bool screenMode(int incr);
    void flashrom(char *selectedRom);
    void __not_in_flash_func(core1_main)();
    int initLed();
    void initVintageControllers(uint32_t CPUFreqKHz);
    void initDVandAudio(int marginTop, int marginBottom);
    void initDVandAudio(int marginTop, int marginBottom, size_t audioBufferSize);
    bool initAll(char *selectedRom, uint32_t CPUFreqKHz, int marginTop, int marginBottom, size_t audiobufferSize = 256, bool swapbytes = false);
    void blinkLed(bool on);
    void resetWifi();
    void printbin16(int16_t v);
    uint64_t time_us();
    uint32_t time_ms();
   
} // namespace Frens


#endif
