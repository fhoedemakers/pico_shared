
#ifndef FRENSHELPERS
#define FRENSHELPERS
#ifndef HSTX
#define HSTX 1
#endif
#include <string>
#include <algorithm>
#include <memory>
#include <pico/mutex.h>
#if !HSTX
#include "dvi/dvi.h"
#include "dvi_configs.h"
#include "util/exclusive_proc.h"
#else
#include "hstx.h"
#include "mcp4822.h"
#endif
#include "external_audio.h"

#ifndef PSRAM_CS_PIN
#define PSRAM_CS_PIN 0 // 0 is no PSRAM
#endif

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
#define SCREENWIDTH 320
#define SCREENHEIGHT 240

extern uintptr_t ROM_FILE_ADDR ; //0x10090000
extern int maxRomSize;
extern char ErrorMessage[];
#if !HSTX
extern std::unique_ptr<dvi::DVI> dvi_;
extern bool scaleMode8_7_;
#endif
extern char __flash_binary_start;  // defined in linker script
extern char __flash_binary_end; 
extern int abSwapped;      // defined in hid_app.cpp
extern int isManta;        // defined in hid_app.cpp
namespace Frens
{
#if !HSTX
    extern uint8_t *framebuffer1;  // [320 * 240];
    extern uint8_t *framebuffer2;  // [320 * 240];
    extern uint8_t *framebufferCore0;
    extern volatile bool framebuffer1_ready;
    extern volatile bool framebuffer2_ready;
    extern volatile bool use_framebuffer1; // Toggle flag
    extern volatile bool framebuffer1_rendering;
    extern volatile bool framebuffer2_rendering;
    // Mutex for synchronization
    extern mutex_t framebuffer_mutex;
#endif  
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
    bool initAll(char *selectedRom, uint32_t CPUFreqKHz, int marginTop, int marginBottom, size_t audiobufferSize = 256, bool swapbytes = false, bool useFrameBuffer = false);
    void blinkLed(bool on);
    void resetWifi();
    void printbin16(int16_t v);
    uint64_t time_us();
    uint32_t time_ms();
#if !HSTX
    bool isFrameBufferUsed();
    void markFrameReadyForReendering(bool waitForFrameReady = false);
    typedef void (*ProcessScanLineFunction)(int line, uint8_t *framebuffer, uint16_t *dvibuffer);
    void SetFrameBufferProcessScanLineFunction(ProcessScanLineFunction processScanLineFunction);
#endif
    bool isPsramEnabled();
    void *flashromtoPsram(char *selectdRom, bool swapbytes);
    void freePsram(void *pMem);
    void PaceFrames60fps(bool init);

    //extern volatile ProcessScanLineFunction processScanLineFunction;
   
} // namespace Frens


#endif
