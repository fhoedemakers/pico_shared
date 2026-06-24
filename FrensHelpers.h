
#ifndef FRENSHELPERS
#define FRENSHELPERS
#if GPIOHSTXD0 && GPIOHSTXD1 && GPIOHSTXD2 && GPIOHSTXCK && PICO_RP2350
#define HSTX 1
#else
#define HSTX 0
#endif
#define FRAMEBUFFERISPOSSIBLE ( !HSTX && PICO_RP2350 )
#include <string>
#include <algorithm>
#include <memory>
#include <pico/mutex.h>
#include "hardware/vreg.h"
#include "hardware/pll.h"
#include "ff.h"
#include "ffwrappers.h"
#include "tf_card.h"
#include "crc32.h"
#include "FlashParams.h"
#if !HSTX
#include "dvi/dvi.h"
#include "dvi_configs.h"
#include "util/exclusive_proc.h"
#else
#include "hstx.h"
// #include "mcp4822.h"     // SPI Audio using MCP4822 DAC. Works but not used
#endif

#include "external_audio.h"

#ifndef PSRAM_CS_PIN
#define PSRAM_CS_PIN 0 // 0 is no PSRAM
#endif
#if !HSTX
#define FILEXTFORSEARCH ".444"
#else
#define FILEXTFORSEARCH ".555"
#endif
#ifndef WIIPAD_DELAYED_START
#define WIIPAD_DELAYED_START 0
#endif
enum class ScreenMode
    {
        SCANLINE_8_7,
        NOSCANLINE_8_7,
        SCANLINE_1_1,
        NOSCANLINE_1_1,
        MAX,
    };

enum class ScanlineType : uint8_t
    {
        SIMPLE = 0,
        LCD = 1,
        MAX
    };

#define CBLACK 15
#define CWHITE 48
#define CRED 21
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

#ifndef RETROJAM
#define RETROJAM 0
#endif
#ifndef SGX
#define SGX 0
#endif
#ifndef F_MALLOC_DEBUG
#define F_MALLOC_DEBUG 0
#endif
#ifndef ENABLEDVI
#define ENABLEDVI 0 
#endif
#ifndef BOOTLOADER_BUILD
#define BOOTLOADER_BUILD 0
#endif
extern uintptr_t ROM_FILE_ADDR ; //0x10090000
extern int maxRomSize;
extern char ErrorMessage[];
extern bool scaleMode8_7_;
#if !HSTX
extern std::unique_ptr<dvi::DVI> dvi_;
#endif
extern char __flash_binary_start;  // defined in linker script
extern char __flash_binary_end; 
extern int abSwapped;      // defined in hid_app.cpp

namespace Frens
{
#if !HSTX && FRAMEBUFFERISPOSSIBLE
    // extern uint8_t *framebuffer1;  // [320 * 240];
    // extern uint8_t *framebuffer2;  // [320 * 240];
    // extern uint8_t *framebufferCore0;
    // extern volatile bool framebuffer1_ready;
    // extern volatile bool framebuffer2_ready;
    // extern volatile bool use_framebuffer1; // Toggle flag
    // extern volatile bool framebuffer1_rendering;
    // extern volatile bool framebuffer2_rendering;
    // // Mutex for synchronization
    // extern mutex_t framebuffer_mutex;
    extern WORD framebuffer[];
#endif  
    bool endsWith(std::string const &str, std::string const &suffix);
    std::string str_tolower(std::string s);

    bool cstr_endswith(const char *string, const char *width);
    char **cstr_split(const char *str, const char *delimiters, int *count);
    void stripextensionfromfilename(char *filename);
    void getextensionfromfilename(const char *filename, char *extension, size_t extSize);
    char *GetfileNameFromFullPath(char *fullPath);
    bool initSDCard();
    bool applyScreenMode(ScreenMode screenMode_);
    bool screenMode(int incr);
    void flashrom(char *selectedRom);
    void __not_in_flash_func(core1_main)();
    // Opt-in line-stream mode for core1 (RP2040 DVI, no framebuffer). When a
    // fill callback is registered, core1 continuously reads a source one line
    // at a time via the callback (filling dst with RGB565) and streams it to
    // the DMA, instead of consuming the validLineQueue. Pass nullptr to return
    // to the default queue model. lineStreamActive() reports whether core1 is
    // currently running the callback loop — used to safely tear down the source
    // buffer before freeing it.
    typedef void (*LineStreamFillFn)(int line, uint16_t *dst);
    void setLineStreamFill(LineStreamFillFn fn);
    bool lineStreamActive();
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
    bool isFrameBufferUsed();
    void getFsInfo(char *fstype, size_t fstypeSize);
#if !HSTX && 0
    
    void markFrameReadyForReendering(bool waitForFrameReady = false);
    typedef void (*ProcessScanLineFunction)(int line, uint8_t *framebuffer, uint16_t *dvibuffer);
    void SetFrameBufferProcessScanLineFunction(ProcessScanLineFunction processScanLineFunction);
#endif
    bool isPsramEnabled();
    void *flashromtoPsram(char *selectdRom, bool swapbytes, uint32_t &crc, int crcOffset);
    void PaceFrames60fps(bool init, bool usePicoDVIvsyncWait = false);
    // Optional task run repeatedly while PaceFrames60fps is waiting out slack
    // before the next frame (framebuffer DVI path only). Lets the otherwise
    // idle wait do useful work — e.g. prefetch CD audio sectors from SD — so it
    // overlaps the wait instead of adding to frame time. Pass nullptr to clear.
    void setVSyncWaitTask(void (*task)(void));
    // Audio-clock pacing (framebuffer DVI path). The query returns the active
    // audio output buffer's fill in permille (0..1000); PaceFrames waits until
    // it drains below ~half, locking the frame rate to audio consumption. This
    // is output-agnostic — the caller's query picks HDMI ring vs I2S ring.
    // Pass nullptr to fall back to plain timer pacing.
    void setAudioPaceQuery(int (*query)(void));
    void toggleScanLines();
    void restoreScanlines();
    void *f_malloc(size_t size);
    void f_free(void *pMem);
    void *f_realloc(void *pMem, const size_t newSize);
    uint GetAvailableMemory();
    int GetUnUsedDMAChan(int startChannel);
    char *get_tag_text(const char *xml, const char *tag, char *buffer, size_t bufsize);
    void waitForVSync();
    bool romIsByteSwapped();
    uint32_t getFrameCount();
    const char* ms_to_d_hhmmss(uint64_t ms, char* buf, size_t bufSize);
    void setClocksAndStartStdio(uint32_t cpuFreqKHz, vreg_voltage voltage);
    //extern volatile ProcessScanLineFunction processScanLineFunction;
    void loadOverLay(const char *filename, const char *overlay);
    FRESULT pick_random_file_fullpath(const char *path, char *chosen, size_t bufsize);
    uint32_t getCrcOfLoadedRom();
    bool fileExists(const char *filename);
    float read_onboard_temperature(const char unit);

     void pollHeadPhoneJack();
     bool isHeadPhoneJackConnected();

    // pico_emuLoader bootloader<->emulator handshake (see FrensHelpers.cpp).
    // Built on watchdog scratch registers, which survive watchdog_reboot but
    // are cleared by cold reset -- so the "launched from bootloader" semantic
    // resets correctly on power-cycle or after a BOOTSEL flash.
    //
    // Emulator side:
    //   isLaunchedFromBootloader() -- true if this image was started by the
    //       resident bootloader (rather than flashed standalone via BOOTSEL).
    //   rebootToBootloader() -- ask the bootloader to skip its resume path
    //       on the next boot (so it shows the picker) and watchdog-reboot.
    //       Does not return on success.
    // Bootloader side:
    //   markLaunchedFromBootloader() -- call right before app_launch_run() so
    //       the launched image sees isLaunchedFromBootloader() == true.
    //   consumeReturnToBootloaderRequest() -- in the resume path: returns
    //       true and clears the request if the emulator asked to return to
    //       the picker; the bootloader should then skip its resume jump.
    bool isLaunchedFromBootloader();
    void rebootToBootloader();
    void markLaunchedFromBootloader();
    bool consumeReturnToBootloaderRequest();

} // namespace Frens


#endif
