#include <stdio.h>
#include <cstring>
#include <malloc.h>
#include "pico.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/rand.h"
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#if PICO_RP2350
#include "hardware/structs/qmi.h"
#else
#include "hardware/structs/ssi.h"
#endif
#include "util/exclusive_proc.h"
#include "FrensHelpers.h"
#if CFG_TUH_RPI_PIO_USB && PICO_RP2350
#include "bsp/board_api.h"
#include "board.h"
#include "pio_usb.h"
#endif
#include "tusb.h"
#include "hardware/dma.h"
#include "hardware/adc.h"

#include "nespad.h"
#include "wiipad.h"
#include "i2c_bus_recovery.h"
#include "settings.h"
#include "recentgames.h"
#include "menu_settings.h" // for g_available_screen_modes visibility

#include "PicoPlusPsram.h"
#include "vumeter.h"

// Pico W devices use a GPIO on the WIFI chip for the LED,
// so when building for Pico W, CYW43_WL_GPIO_LED_PIN will be defined
// NOTE: Building for Pico2 W makes the emulator not work: ioctl timeouts and red flicker
#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

// Valid values arr:
//  44100
//  48000
#ifndef DVIAUDIOFREQ
#define DVIAUDIOFREQ 44100
#endif
#if !HSTX
std::unique_ptr<dvi::DVI> dvi_;
util::ExclusiveProc exclProc_;
static volatile bool vsync = false;
static void (*vsyncWaitTask)(void) = nullptr;
// Returns the active audio output buffer's fill level in permille (0..1000).
// When set, PaceFrames locks the frame rate to that buffer draining to ~half,
// i.e. to the audio-consumption clock. nullptr → fall back to timer pacing.
static int (*audioFillQuery)(void) = nullptr;
static bool paceTimerInited = false;
// Opt-in line-stream mode (see FrensHelpers.h). When lineStreamFill_ is set,
// core1 reads each line via the callback into lineStreamScratch_ and streams it
// to the DMA instead of consuming the validLineQueue. lineStreamActive_ lets
// the producer know core1 is in the callback loop before it tears down the
// source buffer. Default null → unchanged queue behaviour (menu, other ports).
// The scratch line is allocated on demand by setLineStreamFill() so projects
// that never use line-streaming pay no SRAM for it.
static volatile Frens::LineStreamFillFn lineStreamFill_ = nullptr;
static volatile bool lineStreamActive_ = false;
// Display park, for USB drive mode on line-buffer DVI builds (RP2040). Core0
// cannot both feed the DVI line queue and block on SD transfers - five line
// buffers give it about 317us of slack and a single 512-byte SD read already
// costs ~205us - so the picture collapses into TMDS error symbols while the
// host reads the card. Parking core1 stops the serialisers instead, which is
// an honest black screen rather than a broken one. There is no unpark: the
// caller reboots when the user is done. Two bools, no buffers.
static volatile bool displayParkRequested_ = false;
static volatile bool displayParked_ = false;
static uint16_t *lineStreamScratch_ = nullptr;
#endif
char ErrorMessage[ERRORMESSAGESIZE];
bool scaleMode8_7_ = true;
uintptr_t ROM_FILE_ADDR = 0;
int maxRomSize = 0;

namespace Frens
{
    static uint32_t crcOfRom = 0;
    static FATFS fs;
    static bool fatfsUsesPioSpi = false;
    static DWORD totalSpace = 0;
    static DWORD freeSpace = 0;
    static bool extSpeakerEnabled = false;

    // pico_emuLoader bootloader handshake. Two watchdog scratch registers
    // carry one-direction signals between the resident bootloader and the
    // emulator app it launched. Both survive watchdog_reboot (since
    // watchdog_reboot(0,0,0) only clobbers scratch[4]) and are cleared by a
    // cold reset, so the "launched from bootloader" semantic resets correctly
    // when the user power-cycles or flashes a stand-alone image via BOOTSEL.
    //
    //   scratch[6]: bootloader -> emulator. Set to LOADER_LAUNCH_MAGIC by
    //               main.cpp right before app_launch_run(). Read by
    //               isLaunchedFromBootloader().
    //   scratch[7]: emulator -> bootloader. Set to LOADER_RETURN_MAGIC by
    //               rebootToBootloader() right before watchdog_reboot(). The
    //               bootloader checks and clears it in its resume path; if
    //               present, the resume jump is skipped and the picker is
    //               shown instead.
    static constexpr uint32_t LOADER_LAUNCH_MAGIC = 0xB007ED01u;
    static constexpr uint32_t LOADER_RETURN_MAGIC = 0xB007BACEu;
    static constexpr int LOADER_LAUNCH_SCRATCH = 6;
    static constexpr int LOADER_RETURN_SCRATCH = 7;

    bool isLaunchedFromBootloader()
    {
        return watchdog_hw->scratch[LOADER_LAUNCH_SCRATCH] == LOADER_LAUNCH_MAGIC;
    }

    void rebootToBootloader()
    {
        watchdog_hw->scratch[LOADER_RETURN_SCRATCH] = LOADER_RETURN_MAGIC;
        watchdog_reboot(0, 0, 0);
        for (;;) tight_loop_contents();
    }

    void markLaunchedFromBootloader()
    {
        watchdog_hw->scratch[LOADER_LAUNCH_SCRATCH] = LOADER_LAUNCH_MAGIC;
    }

    bool consumeReturnToBootloaderRequest()
    {
        if (watchdog_hw->scratch[LOADER_RETURN_SCRATCH] == LOADER_RETURN_MAGIC) {
            watchdog_hw->scratch[LOADER_RETURN_SCRATCH] = 0;
            return true;
        }
        return false;
    }
#if !HSTX && FRAMEBUFFERISPOSSIBLE
    // uint8_t *framebuffer1; // [320 * 240];
    // uint8_t *framebuffer2; // [320 * 240];
    //  uint8_t *framebufferCore0;

    // Shared state
    // volatile bool framebuffer1_ready = false;
    // volatile bool framebuffer2_ready = false;
    // volatile bool use_framebuffer1 = true; // Toggle flag
    // volatile bool framebuffer1_rendering = false;
    // volatile bool framebuffer2_rendering = false;
    // volatile ProcessScanLineFunction processScanLineFunction;
    // // Mutex for synchronization
     // Word-aligned: the 8:7 scale encoder (encodeTMDS_RGB444_Scaled16_7)
     // casts this buffer to uint32_t* and reads it with ldmia, which hard-faults
     // on Cortex-M if the base is not 4-byte aligned. Plain uint16_t arrays only
     // get 2-byte alignment, so the fault depends on .bss layout (HW_CONFIG).
     alignas(uint32_t) WORD framebuffer[SCREENWIDTH * SCREENHEIGHT];
#endif
   
   
    static bool usingFramebuffer = false;
    bool psRamEnabled = false;
    size_t psramMemorySize = 0;
    static bool byteSwapped = false;

    /* Choose 'C' for Celsius or 'F' for Fahrenheit. */
#define TEMPERATURE_UNITS 'C'

    /* References for this implementation:
     * raspberry-pi-pico-c-sdk.pdf, Section '4.1.1. hardware_adc'
     * pico-examples/adc/adc_console/adc_console.c */
    float read_onboard_temperature(const char unit)
    {
        static bool adc_initialized = false;
        if (!adc_initialized)
        {
            printf("Initializing ADC for temperature sensor...\n");
            adc_init();
            adc_set_temp_sensor_enabled(true);
            adc_select_input(8); 
            adc_initialized = true;    
        }

        /* 12-bit conversion, assume max value == ADC_VREF == 3.3 V */
        const float conversionFactor = 3.3f / (1 << 12);

        float adc = (float)adc_read() * conversionFactor;
        float tempC = 27.0f - (adc - 0.706f) / 0.001721f;

        if (unit == 'C')
        {
            return tempC;
        }
        else if (unit == 'F')
        {
            return tempC * 9 / 5 + 32;
        }

        return -1.0f;
    }
    bool romIsByteSwapped()
    {

        return (FrensSettings::getEmulatorType() == FrensSettings::emulators::GENESIS);
        return false;
    }

    bool isPsramEnabled()
    {
        return psRamEnabled;
    }

    bool initPsram()
    {
        psRamEnabled = false;
        psramMemorySize = 0;
        // Initialize PSRAM if available
#if PICO_RP2350 && PSRAM_CS_PIN
        printf("GetInstance...\n");
        PicoPlusPsram &psram_ = PicoPlusPsram::getInstance();
        if (psram_.GetMemorySize() > 0)
        {
            psRamEnabled = true;
            psramMemorySize = psram_.GetMemorySize();
            printf("PSRAM initialized.\n");
        }
        else
        {
            psRamEnabled = false;
            psramMemorySize = 0;
            printf("PSRAM initialization failed or not present. Games will be loaded into flash.\n");
        }
#else
        printf("PSRAM not available. Games will be loaded into flash.\n");
#endif
        return psRamEnabled;
    }

    bool __not_in_flash_func(isFrameBufferUsed)()
    {
#if !HSTX
        return usingFramebuffer;
#else
        return true; // HSTX always uses framebuffer
#endif
    }

#define STORAGE_CMD_DUMMY_BYTES 1
#define STORAGE_CMD_DATA_BYTES 3
#define STORAGE_CMD_TOTAL_BYTES (STORAGE_CMD_DUMMY_BYTES + STORAGE_CMD_DATA_BYTES)
static uint32_t flashJedecId = 0;
uint __not_in_flash_func(storage_get_flash_capacity)()
{
    // This function needs to be called before any overclock settings are applied,
    // this may crash when PSRAM is also on the board.
    static uint capacity = 0;
    if (capacity != 0)
    {
        return capacity;
    }
    uint8_t txbuf[STORAGE_CMD_TOTAL_BYTES] = {0x9f};
    uint8_t rxbuf[STORAGE_CMD_TOTAL_BYTES] = {0};
    auto irq = save_and_disable_interrupts();
    flash_do_cmd(txbuf, rxbuf, STORAGE_CMD_TOTAL_BYTES);
    restore_interrupts(irq);
    // rxbuf[0] is clocked out while the command byte goes in; the ID follows.
    flashJedecId = (rxbuf[1] << 16) | (rxbuf[2] << 8) | rxbuf[3];
    capacity = 1 << rxbuf[3];
    return capacity;
}

// Full JEDEC (0x9F) ID as 0xMMTTCC: manufacturer, type, capacity exponent.
// Which flash part a board carries decides how far it can be overclocked, so
// this is the first thing to ask for in a "crashes only on my board" report.
uint32_t storage_get_flash_jedec_id()
{
    storage_get_flash_capacity(); // no-op once cached; does the read on first call
    return flashJedecId;
}

// JEDEC manufacturer IDs seen on Picos and Pico-compatible clones. Genuine
// boards are Winbond; anything else is a clone and likely a slower part.
const char *storage_get_flash_manufacturer_name(uint8_t manufacturerId)
{
    switch (manufacturerId)
    {
    case 0xEF: return "Winbond";
    case 0xC8: return "GigaDevice";
    case 0x5E: return "Zbit";
    case 0x0B: return "XTX";
    case 0x68: return "Boya";
    case 0x85: return "Puya";
    case 0xC2: return "Macronix";
    case 0x1F: return "Adesto/Atmel";
    case 0x20: return "Micron";
    default: return "unknown";
    }
}
#if !HSTX
    /// @brief Wait for vertical sync
    void setVSyncWaitTask(void (*task)(void))
    {
        vsyncWaitTask = task;
    }

    void setAudioPaceQuery(int (*query)(void))
    {
        audioFillQuery = query;
        paceTimerInited = false;
    }
#endif
    void waitForVSync()
    {
#if !HSTX
        // Framebuffer path and line-stream path both drive `vsync` from core1
        // at frame boundaries; wait for it so core0 stays frame-locked.
        if (Frens::isFrameBufferUsed() || lineStreamActive_)
        {
            while (vsync == false)
            {
                // busy wait
                tight_loop_contents();
            }
        }
#else
        hstx_waitForVSync();
#endif
    }
#if 1
    /// @brief Poor way to pace frames to 60fps
    /// @param init
    void PaceFrames60fps(bool init, bool usePicoDVIvsyncWait)
    {
#if !HSTX
#if USE_PCE_FRAMEBUFFER_PACING
        // Buffer pacing for pico-pcePlus
        // Not used in other emulators for now.
        // Caused some line artifacts in pico-infonesPlus
        if (Frens::isFrameBufferUsed())
        {
            // must be set to true when called from menu
            // otherwise sreensaver will run too fast.
            if (usePicoDVIvsyncWait)
            {
                while (vsync == false)
                {
                    // busy wait
                    tight_loop_contents();
                }
                return;
            }
            // CD games prefetch a sector every frame, regardless of which
            // pacing path runs below, so the CD audio ring never starves.
            if (vsyncWaitTask)
                vsyncWaitTask();

            if (audioFillQuery)
            {
                // Pace to the audio-consumption clock: wait until the active
                // output buffer (HDMI ring or I2S ring — the caller's query
                // abstracts which) drains back to ~half full. This locks the
                // emulator to the exact 60.00fps audio rate so production
                // matches consumption with no drift (a fixed 16667us timer is
                // 59.998fps, which slowly drained the buffer and caused
                // periodic refill-burst artifacts). The half-full buffer
                // cushions brief sub-60fps dips; the >60fps catch-up refills it.
                while (audioFillQuery() > 500)
                {
                    if (vsyncWaitTask)
                        vsyncWaitTask();
                    else
                        tight_loop_contents();
                }
            }
            else
            {
                // Menu / non-CD: slack-aware timer pacing (no continuous audio
                // stream to lock onto). Resync on overrun so a slow frame can't
                // harmonic-lock the loop to 30fps.
                static absolute_time_t next_frame;
                // True PCE NTSC frame period: 1/59.826 ≈ 16715 µs (not 60 Hz).
                if (init || !paceTimerInited)
                {
                    next_frame = make_timeout_time_us(16715);
                    paceTimerInited = true;
                }
                if (time_reached(next_frame))
                {
                    next_frame = make_timeout_time_us(16715);
                }
                else
                {
                    while (!time_reached(next_frame))
                    {
                        if (vsyncWaitTask)
                            vsyncWaitTask(); // keep prefetching CD audio
                        else
                            tight_loop_contents();
                    }
                    next_frame = delayed_by_us(next_frame, 16715);
                }
            }
        }
#else
        if (Frens::isFrameBufferUsed())
        {
            while (vsync == false)
            {
                // busy wait
                tight_loop_contents();
            }
        }
#endif
#else
        hstx_paceFrame(init);
#endif
    }
#endif
    //
    //
    // test if string ends with suffix
    //
    bool endsWith(std::string const &str, std::string const &suffix)
    {
        if (str.length() < suffix.length())
        {
            return false;
        }
        return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
    }
    //
    // returns lowercase of string s
    //
    std::string str_tolower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c)
                       { return std::tolower(c); } // correct
        );
        return s;
    }

    // Check whether a string ends with a given suffix
    bool cstr_endswith(const char *string, const char *width)
    {
        int lstring = strlen(string);
        int wlen = strlen(width);
        if (wlen >= lstring)
        {
            return false;
        }
        int pos = lstring - wlen;
        return (strcmp(string + pos, width) == 0);
    }

    uint64_t time_us()
    {
        absolute_time_t t = get_absolute_time();
        return to_us_since_boot(t);
    }

    uint32_t time_ms()
    {
        absolute_time_t t = get_absolute_time();
        return to_ms_since_boot(t);
    }

    const char *ms_to_d_hhmmss(uint64_t ms, char *buf, size_t bufSize)
    {
        if (!buf || bufSize < 9)
            return nullptr; // at least space for "HH:MM:SS"
        uint64_t total_sec = ms / 1000;
        uint64_t s = total_sec % 60;
        uint64_t total_min = total_sec / 60;
        uint64_t m = total_min % 60;
        uint64_t total_hours = total_min / 60;
        uint64_t h = total_hours % 24;
        uint64_t d = total_hours / 24;

        if (d == 0)
        {
            // HH:MM:SS
            if (snprintf(buf, bufSize, "%02llu:%02llu:%02llu",
                         (unsigned long long)h,
                         (unsigned long long)m,
                         (unsigned long long)s) >= (int)bufSize)
                return nullptr;
        }
        else
        {
            // D:HH:MM:SS (days not zero-padded, hours still zero-padded)
            if (snprintf(buf, bufSize, "%llu:%02llu:%02llu:%02llu",
                         (unsigned long long)d,
                         (unsigned long long)h,
                         (unsigned long long)m,
                         (unsigned long long)s) >= (int)bufSize)
                return nullptr;
        }
        return buf;
    }

#define INITIAL_CAPACITY 10
    // Split a string into tokens using the specified delimiters
    // The result is an array of dynamically allocated strings
    char **cstr_split(const char *str, const char *delimiters, int *count)
    {
        if (str == NULL || delimiters == NULL)
        {
            *count = 0;
            return NULL;
        }

        // Create a modifiable copy of the input string, panics when out of memory
        char *str_copy = (char *)Frens::f_malloc(strlen(str) + 1);
        strcpy(str_copy, str);

        // Initial memory allocation for the result array, panics when out of memory
        int capacity = INITIAL_CAPACITY;
        char **result = (char **)Frens::f_malloc(capacity * sizeof(char *));

        *count = 0;
        char *token = strtok(str_copy, delimiters);
        while (token != NULL)
        {
            // Skip empty tokens
            if (*token != '\0')
            {
                // Reallocate if necessary, panics when out of memory
                if (*count >= capacity)
                {
                    capacity *= 2;
                    char **temp = (char **)Frens::f_realloc(result, capacity * sizeof(char *));
                    result = temp;
                }

                // Allocate memory for the token and copy it, panics when out of memory
                result[*count] = (char *)Frens::f_malloc(strlen(token) + 1);
                strcpy(result[*count], token);
                (*count)++;
            }
            token = strtok(NULL, delimiters);
        }
        Frens::f_free(str_copy);
        return result;
    }

    // Get the file name from a full path
    char *GetfileNameFromFullPath(char *fullPath)
    {
        char *fileName = fullPath;
        char *ptr = fullPath;
        while (*ptr)
        {
            if (*ptr == '/')
            {
                fileName = ptr + 1;
            }
            ptr++;
        }
        return fileName;
    }
    char *get_tag_text(const char *xml, const char *tag, char *buffer, size_t bufsize)
    {
        char openTag[64];
        snprintf(openTag, sizeof(openTag), "<%s", tag); // match tag start (can have attributes)

        const char *start = strstr(xml, openTag);
        if (!start)
            return NULL;

        start = strchr(start, '>'); // find '>' after <tag or <tag attr=...>
        if (!start)
            return NULL;
        start++; // move past '>'

        char closeTag[64];
        snprintf(closeTag, sizeof(closeTag), "</%s>", tag);

        const char *end = strstr(start, closeTag);
        if (!end)
            return NULL;

        size_t len = end - start;
        if (len >= bufsize)
            len = bufsize - 1; // truncate if needed
        memcpy(buffer, start, len);
        buffer[len] = '\0';

        return buffer;
    }
    // Strip the extension from a file name
    void stripextensionfromfilename(char *filename)
    {
        char *ptr = filename;
        char *lastdot = filename;
        while (*ptr)
        {
            if (*ptr == '.')
            {
                lastdot = ptr;
            }
            ptr++;
        }
        *lastdot = 0;
    }

    void getextensionfromfilename(const char *filename, char *extension, size_t extSize)
    {
        const char *ptr = filename;
        const char *lastdot = nullptr;
        while (*ptr)
        {
            if (*ptr == '.')
            {
                lastdot = ptr;
            }
            ptr++;
        }
        if (lastdot)
        {
            strncpy(extension, lastdot, extSize);
            extension[extSize - 1] = 0; // ensure null termination
        }
        else
        {
            extension[0] = 0; // no extension found
        }
    }

    // print an int16 as binary
    void printbin16(int16_t v)
    {
        for (int i = 15; i >= 0; i--)
        {
            printf("%d", (v >> i) & 1);
        }
    }

    void getFsInfo(char *fstype, size_t fstypeSize)
    {
        const char *base;
        switch (fs.fs_type)
        {
        case FS_FAT12:
            base = "FAT12";
            break;
        case FS_FAT16:
            base = "FAT16";
            break;
        case FS_FAT32:
            base = "FAT32";
            break;
        case FS_EXFAT:
            base = "EXFAT";
            break;
        default:
            base = "Unknown";
            break;
        }
        snprintf(fstype, fstypeSize, "%s %sSPI %7.2fGB Free:%7.2fGB", base, fatfsUsesPioSpi ? "PIO " : "", totalSpace / 1024.0 / 1024.0, freeSpace / 1024.0 / 1024.0);
    }

    // Initialize the SD card
    bool initSDCard()
    {
        FRESULT fr;
        TCHAR str[40];
        // sleep_ms(1000);

        printf("Mounting SDcard ");

        static pico_fatfs_spi_config_t config = {
            SDCARD_SPI,
            CLK_SLOW_DEFAULT,
            CLK_FAST_DEFAULT_PIO,
            SDCARD_PIN_MISO,
            SDCARD_PIN_CS,
            SDCARD_PIN_SCK,
            SDCARD_PIN_MOSI,
            true // use internal pullup
        };
        bool spi_configured = pico_fatfs_set_config(&config);
        // Try first using SPI
        if (spi_configured)
        {
            printf("using SPI...");
            fatfsUsesPioSpi = false;
        }
        else
        {
            // fall back to PIO SPI
            pico_fatfs_config_spi_pio(SDCARD_PIO, pio_claim_unused_sm(SDCARD_PIO, true));
            printf("using SPI PIO...");
            fatfsUsesPioSpi = true;
        }

        fr = f_mount(&fs, "", 1);
        if (fr != FR_OK)
        {
            snprintf(ErrorMessage, ERRORMESSAGESIZE, "SD card mount error: %d", fr);
            printf(" %s\n", ErrorMessage);
            return false;
        }
        printf("\n");
        switch (fs.fs_type)
        {
        case FS_FAT12:
            printf("Type is FAT12\n");
            break;
        case FS_FAT16:
            printf("Type is FAT16\n");
            break;
        case FS_FAT32:
            printf("Type is FAT32\n");
            break;
        case FS_EXFAT:
            printf("Type is EXFAT\n");
            break;
        default:
            printf("Type is unknown\n");
            break;
        }
        DWORD fre_clust, fre_sect, tot_sect;
        FATFS *fstemp;
        f_getfree("", &fre_clust, &fstemp);
        /* Get total sectors and free sectors */
        tot_sect = (fstemp->n_fatent - 2) * fstemp->csize;
        fre_sect = fre_clust * fstemp->csize;

        /* Print the free space (assuming 512 bytes/sector) */
        totalSpace = tot_sect / 2;
        freeSpace = fre_sect / 2;
        printf("%10lu KiB (%7.2f GB) total drive space.\n%10lu KiB available.\n", tot_sect / 2, fstemp->csize * fstemp->n_fatent * 512E-9, fre_sect / 2);
        fr = f_chdir("/"); // f_chdir("/");
        if (fr != FR_OK)
        {
            snprintf(ErrorMessage, ERRORMESSAGESIZE, "Cannot change dir to / : %d", fr);
            printf("%s\n", ErrorMessage);
            return false;
        }
        // for f_getcwd to work, set
        //   #define FF_FS_RPATH		2
        // in drivers/fatfs/ffconf.h
        fr = f_getcwd(str, sizeof(str));
        ; // f_getcwd(str, sizeof(str));
        if (fr != FR_OK)
        {
            snprintf(ErrorMessage, ERRORMESSAGESIZE, "Cannot get current dir: %d", fr);
            printf("%s\n", ErrorMessage);
            return false;
        }
        printf("Current directory: %s\n", str);
        printf("Creating directory %s\n", GAMESAVEDIR);
        fr = f_mkdir(GAMESAVEDIR);
        if (fr != FR_OK)
        {
            if (fr == FR_EXIST)
            {
                printf("Directory already exists.\n");
            }
            else
            {
                snprintf(ErrorMessage, ERRORMESSAGESIZE, "Cannot create dir %s: %d", GAMESAVEDIR, fr);
                printf("%s\n", ErrorMessage);
                return false;
            }
        }
        return true;
    }

    // Release the volume so nothing of the filesystem is left cached. Used by
    // USB drive mode before it hands the card to a PC: FatFs keeps a window
    // buffer of the last FAT/directory sector it touched, and that would be
    // stale the moment the host writes.
    void unmountSDCard()
    {
        FRESULT fr = f_mount(nullptr, "", 0);
        if (fr != FR_OK)
        {
            printf("SD card unmount error: %d\n", fr);
        }
    }

    // Mount the card again after USB drive mode and return to the directory the
    // rom browser was in. The SPI/PIO configuration from initSDCard() is still
    // in effect, so only the FatFs side has to be redone. Falls back to the
    // root when the host removed or renamed that directory.
    bool remountSDCard()
    {
        FRESULT fr = f_mount(&fs, "", 1);
        if (fr != FR_OK)
        {
            snprintf(ErrorMessage, ERRORMESSAGESIZE, "SD card mount error: %d", fr);
            printf("%s\n", ErrorMessage);
            return false;
        }
        if (settings.currentDir[0] == 0 || f_chdir(settings.currentDir) != FR_OK)
        {
            f_chdir("/");
            strcpy(settings.currentDir, "/");
        }
        return true;
    }

    bool applyScreenMode(ScreenMode screenMode_)
    {
        bool scanLine = false;
        bool scaleMode8_7_ = false;
        switch (screenMode_)
        {
        case ScreenMode::SCANLINE_1_1:
            scaleMode8_7_ = false;
            scanLine = true;
            printf("ScreenMode::SCANLINE_1_1\n");
            break;

        case ScreenMode::SCANLINE_8_7:
            scaleMode8_7_ = true;
            scanLine = true;
            printf("ScreenMode::SCANLINE_8_7\n");
            break;

        case ScreenMode::NOSCANLINE_1_1:
            scaleMode8_7_ = false;
            scanLine = false;
            printf("ScreenMode::NOSCANLINE_1_1\n");
            break;

        case ScreenMode::NOSCANLINE_8_7:
            scaleMode8_7_ = true;
            scanLine = false;
            printf("ScreenMode::NOSCANLINE_8_7\n");
            break;
        }

#if !HSTX
        dvi_->setScanLine(scanLine);
#else
        hstx_setScanLines(scanLine ? 1 : 0);
        hstx_setAspectRatio87(scaleMode8_7_ ? 1 : 0);
        hstx_setScanLineType(settings.scanlineType);
#endif
        return scaleMode8_7_;
    }

    bool screenMode(int incr)
    {
        constexpr int kModeCount = 4;
        int current = static_cast<int>(settings.screenMode);
        int attempts = 0;
        do
        {
            current = (current + incr) & 3; // wrap 0..3
            attempts++;
            if (g_available_screen_modes[current])
                break;
        } while (attempts < kModeCount);

        if (!g_available_screen_modes[current])
        {
            current = static_cast<int>(settings.screenMode);
        }

        settings.screenMode = static_cast<ScreenMode>(current);
        bool scaleMode8_7_ = Frens::applyScreenMode(settings.screenMode);
        FrensSettings::savesettings();
        return scaleMode8_7_;
    }

    void toggleScanLines()
    {
#if !HSTX
#else
        switch (settings.screenMode)
        {
        case ScreenMode::SCANLINE_8_7:    settings.screenMode = ScreenMode::NOSCANLINE_8_7; break;
        case ScreenMode::NOSCANLINE_8_7:  settings.screenMode = ScreenMode::SCANLINE_8_7; break;
        case ScreenMode::SCANLINE_1_1:    settings.screenMode = ScreenMode::NOSCANLINE_1_1; break;
        case ScreenMode::NOSCANLINE_1_1:  settings.screenMode = ScreenMode::SCANLINE_1_1; break;
        default: break;
        }
        applyScreenMode(settings.screenMode);
        FrensSettings::savesettings();
#endif
    }
    void restoreScanlines()
    {
#if !HSTX
#else
        applyScreenMode(settings.screenMode);
#endif
    }

    /// @brief Allocates memory from PSRAM if available, otherwise uses malloc
    /// @param size
    /// @return
    void *f_malloc(size_t size)
    {
        if (size == 0)
        {
            return nullptr;
        }
#if PICO_RP2350 && PSRAM_CS_PIN
        if (isPsramEnabled())
        {
            PicoPlusPsram &psram_ = PicoPlusPsram::getInstance();
            void *pMem = psram_.Malloc(size);
            if (!pMem)
            {
                panic("[f_malloc] Cannot allocate %zu bytes in PSRAM\n", size);
            }
#if F_MALLOC_DEBUG
            printf("[f_malloc] Allocated %zu bytes in PSRAM at %p\n", size, pMem);
#endif
            return pMem;
        }
#endif
        // PSRAM not enabled, use malloc
        void *pMem = malloc(size); // panics if unavailable
#if F_MALLOC_DEBUG
        printf("[f_malloc] Allocated %zu bytes in RAM at %p\n", size, pMem);
#endif
        return pMem;
    }

    /// @brief frees memory allocated by f_malloc
    /// @param pMem
    void f_free(void *pMem)
    {
        if (!pMem)
        {
            return;
        }
#if PICO_RP2350 && PSRAM_CS_PIN
        if (isPsramEnabled())
        {
            PicoPlusPsram &psram_ = PicoPlusPsram::getInstance();
            size_t uFreeing = psram_.GetSize(pMem);
#if F_MALLOC_DEBUG
            printf("[f_malloc] Freeing %zu bytes from PSRAM at %p\n", uFreeing, pMem);
#endif
            psram_.Free(pMem);
            return;
        }
#endif
        // PSRAM not enabled, use free
        if (pMem)
        {
#if F_MALLOC_DEBUG
            printf("[f_malloc] Freeing memory at %p\n", pMem);
#endif
            free(pMem);
        }
    }

    void *f_realloc(void *pMem, const size_t newSize)
    {
        void *newMem = nullptr;
        if (!pMem)
        {
            return nullptr;
        }
#if PICO_RP2350 && PSRAM_CS_PIN
        if (isPsramEnabled())
        {
            PicoPlusPsram &psram_ = PicoPlusPsram::getInstance();
            void *newMem = psram_.Realloc(pMem, newSize);
            if (!newMem)
            {
                panic("Cannot allocate %zu bytes in PSRAM\n", newSize);
            }
#if F_MALLOC_DEBUG
            printf("[f_malloc] Re-Allocated %zu bytes in PSRAM at %p\n", newSize, newMem);
#endif
            return newMem;
        }
#endif
        // PSRAM not enabled, use realloc
        newMem = realloc(pMem, newSize);
#if F_MALLOC_DEBUG
        printf("[f_malloc] Re-Allocated memory at %p to %zu bytes\n", pMem, newSize);
#endif
        return newMem;
    }

    uint GetAvailableMemory()
    {
#if PICO_RP2350 && PSRAM_CS_PIN
        if (isPsramEnabled())
        {
            PicoPlusPsram &psram_ = PicoPlusPsram::getInstance();
            return psram_.GetAvailableBytes();
        }
#endif
        return maxRomSize; // return maxRomSize as a fallback
    }

    /* Snapshot both heaps (libc SRAM + lwmem PSRAM) on one line. Cheap
     * enough to call at phase boundaries; mallinfo() walks the freelist
     * but the lists are short on this build. */
    void dumpHeapStats(const char *tag)
    {
        struct mallinfo mi = mallinfo();
        unsigned sram_inuse  = (unsigned)mi.uordblks;
        unsigned sram_free   = (unsigned)mi.fordblks;
        unsigned sram_arena  = (unsigned)mi.arena;
        unsigned sram_keep   = (unsigned)mi.keepcost; /* largest free contig */
#if PICO_RP2350 && PSRAM_CS_PIN
        if (isPsramEnabled())
        {
            PicoPlusPsram &psram_ = PicoPlusPsram::getInstance();
            lwmem_stats_t st;
            lwmem_get_stats_ex(nullptr, &st);
            printf("[heap] %-14s SRAM arena=%uK in=%uK free=%uK largest=%uK | "
                   "PSRAM total=%uK free=%uK minEver=%uK nAlloc=%u nFree=%u\n",
                   tag,
                   sram_arena >> 10, sram_inuse >> 10, sram_free >> 10, sram_keep >> 10,
                   (unsigned)(st.mem_size_bytes >> 10),
                   (unsigned)(st.mem_available_bytes >> 10),
                   (unsigned)(st.minimum_ever_mem_available_bytes >> 10),
                   (unsigned)st.nr_alloc, (unsigned)st.nr_free);
            return;
        }
#endif
        printf("[heap] %-14s SRAM arena=%uK in=%uK free=%uK largest=%uK | PSRAM disabled\n",
               tag,
               sram_arena >> 10, sram_inuse >> 10, sram_free >> 10, sram_keep >> 10);
    }

    void *flashromtoPsram(char *selectdRom, bool swapbytes, uint32_t &crc, int crcOffset)
    {
#if PICO_RP2350 && PSRAM_CS_PIN
        // Get filesize of rom
        FIL fil;
        FRESULT fr;
        size_t tmpSize;
        bool ok = false;
        printf("Reading current game from %s and starting emulator\n", selectdRom);
        if (swapbytes)
        {
            printf("Rom will be byteswapped.\n");
        }
        // calculate the CRC32 checksum of the rom
        // uint32_t crc;
        // if (compute_crc32(selectdRom, &crc) == 0)
        // {
        //     printf("CRC32 checksum of %s: %08X\n", selectdRom, crc);
        // }
        fr = f_open(&fil, selectdRom, FA_READ);
        if (fr != FR_OK)
        {
            snprintf(ErrorMessage, 40, "Cannot open %s:%d\n", selectdRom, fr);
            printf("%s\n", ErrorMessage);
            selectdRom[0] = 0;
            return nullptr;
        }
        FSIZE_t filesize = f_size(&fil);

        // Large-disc-image fast path: when the file is bigger than what fits
        // in available PSRAM (the .chd images for CD games can be hundreds
        // of MB), we can't preload it. Instead read the first 4 KB into a
        // stack buffer, CRC that as a stable savestate-folder key, and
        // return without allocating. The caller (main.cpp) detects this
        // via ROM_FILE_ADDR == 0 and routes straight to the disc-image
        // opener (LoadDisc) which streams the file from SD.
        {
            uint availMem = Frens::GetAvailableMemory();
            // Leave ~512 KB margin for libchdr / FS buffers / lwmem overhead.
            const FSIZE_t margin = 512 * 1024;
            if (availMem > margin && filesize > (availMem - margin))
            {
                // 4 KB scratch lives in PSRAM via f_malloc + f_free,
                // NOT on the stack. The menu → loadRomInPsRam →
                // flashromtoPsram → f_read → disk_read → rcvr_datablock
                // → rcvr_spi_multi call chain already runs close to
                // PICO_STACK_SIZE (3 KB); a 4 KB local array overflowed
                // into adjacent .bss / framebuffer state. Observed
                // symptom: PicoDVI core1 hardfault in the TMDS encoder
                // while core0 was deep in SD I/O on game select. PSRAM
                // is the right place — it's a one-shot read+CRC and the
                // buffer is freed before we return.
                uint8_t *head = (uint8_t *)Frens::f_malloc(4096);
                if (!head)
                {
                    // PSRAM exhausted (extreme; the size guard above
                    // already implies we're tight). Bail and let the
                    // caller report no rom loaded.
                    f_close(&fil);
                    selectdRom[0] = 0;
                    return nullptr;
                }
                UINT br = 0;
                f_read(&fil, head, 4096, &br);
                f_close(&fil);
                if (br > 0)
                {
                    crc = compute_crc32_buffer(head, br, 0);
                    crcOfRom = crc;
                }
                printf("Skipping PSRAM preload (file %llu bytes > avail %u): "
                       "CRC of first %u bytes = 0x%08X\n",
                       (unsigned long long)filesize, availMem, (unsigned)br, crc);
                Frens::f_free(head);
                return nullptr;
            }
        }

        void *pMem = Frens::f_malloc(filesize);
        if (!pMem)
        {
            snprintf(ErrorMessage, 40, "Cannot allocate %llu bytes in PSRAM\n", filesize);
            printf("%s\n", ErrorMessage);
            selectdRom[0] = 0;
            f_close(&fil);
            return nullptr;
        }
        uint availMem = Frens::GetAvailableMemory();
        printf("Available memory: %zu bytes\n", availMem);
        printf("Filesize: %llu bytes (%llu KB)\n",
               (unsigned long long)filesize,
               (unsigned long long)(filesize / 1024));
        // write contents of file into pMem
        size_t r;
        fr = f_read(&fil, pMem, filesize, &r);
        if (fr != FR_OK)
        {
            snprintf(ErrorMessage, 40, "Cannot read %s:%d\n", selectdRom, fr);
            selectdRom[0] = 0;
            printf("%s\n", ErrorMessage);
            Frens::f_free(pMem);
        }
        else
        {
            if (r != filesize)
            {
                snprintf(ErrorMessage, 40, "Read %d bytes, expected %d bytes\n", r, filesize);
                printf("%s\n", ErrorMessage);
                selectdRom[0] = 0;
                Frens::f_free(pMem);
            }
            else
            {
                if ((crc = compute_crc32_buffer(pMem, filesize, crcOffset)) > 0)
                {
                    crcOfRom = crc;
                    printf("CRC32 checksum of %s in PSRAM: %08X\n", selectdRom, crc);
                }
                else
                {
                    printf("Error calculating CRC32 checksum of %s in PSRAM\n", selectdRom);
                }
                if (swapbytes)
                {
                    printf("Rom is byte swapped: Swapping bytes of rom in PSRAM\n");
                    // swap bytes in pMem
                    // A trailing odd byte has no partner: swapping it would read
                    // and write p[filesize], one byte past the allocation, which
                    // lands on the next lwmem block header. Cart images are
                    // always word-sized, so leave a stray byte untouched (the
                    // emulator rejects such files anyway).
                    for (size_t i = 0; i + 1 < filesize; i += 2)
                    {
                        unsigned char *p = (unsigned char *)pMem;
                        unsigned char temp = p[i];
                        p[i] = p[i + 1];
                        p[i + 1] = temp;
                    }
                }
                ok = true;
                printf("Read %d bytes from %s into PSRAM at %p\n", r, selectdRom, pMem);

                selectdRom[0] = 0; //
            }
            f_close(&fil);
        }
        if (ok)
        {

            // Start emulator with rom in PSRAM
            printf("Starting emulator with rom in PSRAM at %p\n", pMem);
            // return pointer to pMem
            return pMem;
        }
        else
        {
            // return nullptr if error
            return nullptr;
        }
#else
        // PSRAM not enabled, return nullptr
        printf("PSRAM not enabled, cannot flash rom to PSRAM\n");
        selectdRom[0] = 0;
        return nullptr;
#endif
    }
    bool isRomAlreadyInFlash(const char *fullPath, bool swapbytes, uint32_t *sizeOut)
    {
        if (isPsramEnabled() || !fullPath || fullPath[0] == 0)
        {
            return false;
        }
        Recent::FlashedRomRecord *rec =
            (Recent::FlashedRomRecord *)f_malloc(sizeof(Recent::FlashedRomRecord));
        if (!rec)
        {
            return false;
        }
        bool ok = false;
        if (Recent::readFlashedRomRecord(rec) &&
            Recent::flashedRomMatches(rec, fullPath, swapbytes))
        {
            printf("Flash already holds %s (%u bytes), verifying image...\n",
                   fullPath, (unsigned)rec->size);
            // The cheap fields cannot catch the case that matters: under the
            // emuLoader bootloader another emulator's binary shifts
            // ROM_FILE_ADDR and may have overwritten this region while the
            // record still describes it perfectly. Only reading the bytes back
            // settles it, and at ~30 ms per 512 KB that is far cheaper than the
            // erase and program it avoids.
            uint32_t live = update_crc32(0, (const uint8_t *)rec->romFileAddr, rec->size);
            if (live == rec->crcFlashImage)
            {
                // Restore the crc of the rom as read from the card. Without it
                // every save state and artwork path would key off 00000000 and
                // the user would lose sight of their saves for this game.
                crcOfRom = rec->crcPreSwap;
                if (sizeOut)
                {
                    *sizeOut = rec->size;
                }
                printf("Flash image verified (crc %08X).\n", (unsigned)live);
                ok = true;
            }
            else
            {
                printf("Flash image crc mismatch (live %08X, recorded %08X).\n",
                       (unsigned)live, (unsigned)rec->crcFlashImage);
            }
        }
        f_free(rec);
        return ok;
    }

    void flashrom(char *selectedRom, bool swapbytes)
    {
        // Determine loaded rom
        printf("Rebooted by menu\n");
        FIL fil;
        FRESULT fr;
        size_t tmpSize;
        printf("Reading current game from %s and starting emulator\n", ROMINFOFILE);
        if (swapbytes)
        {
            printf("Rom will be byteswapped.\n");
        }
        fr = f_open(&fil, ROMINFOFILE, FA_READ);
        if (fr == FR_OK)
        {
            size_t r;
            fr = f_read(&fil, selectedRom, FF_MAX_LFN, &r);

            if (fr != FR_OK)
            {
                snprintf(ErrorMessage, 40, "Cannot read %s:%d\n", ROMINFOFILE, fr);
                selectedRom[0] = 0;
                printf(ErrorMessage);
            }
            else
            {
                selectedRom[r] = 0;
            }
            f_close(&fil);
        }
        else
        {
            if (fr != FR_NO_FILE)
            {
                snprintf(ErrorMessage, 40, "Cannot open %s:%d\n", ROMINFOFILE, fr);
                printf(ErrorMessage);
            }
        }

        if (selectedRom[0] != 0)
        {
            printf("Starting (%d) %s\n", strlen(selectedRom), selectedRom);
            int crcOffsetUsed = FrensSettings::getEmulatorType() == FrensSettings::emulators::NES ? 16 : 0;
            crcOfRom = 0;

            // Is the image already in flash the one being asked for? Same check
            // the menu uses for START_FLASHED_ROM_WITHOUT_REBOOT, and it
            // restores crcOfRom on success.
            uint32_t flashedSize = 0;
            if (isRomAlreadyInFlash(selectedRom, swapbytes, &flashedSize))
            {
                printf("Not reflashing.\n");
                Recent::add(selectedRom, FrensSettings::getEmulatorTypeString(),
                            crcOfRom, flashedSize);
                return;
            }

            Recent::FlashedRomRecord *rec =
                (Recent::FlashedRomRecord *)f_malloc(sizeof(Recent::FlashedRomRecord));
            {
                printf("Flashing rom.\n");
                // Drop the record before the first erase, so a record that
                // survives always describes complete flash content - a power
                // cut halfway through can never leave one that lies.
                Recent::invalidateFlashedRomRecord();
#if PICO_RP2040
                size_t bufsize = 64 * 1024;
#else
                size_t bufsize = 128 * 1024;
#endif
                BYTE *buffer = (BYTE *)f_malloc(bufsize); // (BYTE *)InfoNes_GetPPURAM(&bufsize);
                auto ofs = ROM_FILE_ADDR - XIP_BASE;
                printf("Writing rom %s to flash %x\n", selectedRom, ofs);
                UINT totalBytes = 0;
#if 0
                int blockCount=0;
#endif
                fr = f_open(&fil, selectedRom, FA_READ);
                bool onOff = true;
                bool flashOK = false;
                UINT bytesRead;
                int crcOffset = crcOffsetUsed;
                if (fr == FR_OK)
                {
                    FSIZE_t filesize = f_size(&fil);
                    printf("Filesize: %llu bytes (%llu KB)\n",
                           (unsigned long long)filesize,
                           (unsigned long long)(filesize / 1024));
                    if (filesize < maxRomSize)
                    {
                        bool readError = false;
                        for (;;)
                        {
                            fr = f_read(&fil, buffer, bufsize, &bytesRead);
                            if (fr == FR_OK)
                            {
                                if (bytesRead == 0)
                                {
                                    break;
                                }
                                crcOfRom = update_crc32(crcOfRom, buffer + crcOffset, bytesRead - crcOffset);
                                crcOffset = 0; // only offset for first block
                                if (swapbytes)
                                {
                                    // Stop before a trailing odd byte: it has no
                                    // partner, and buffer[bytesRead] still holds
                                    // stale data from the previous block.
                                    for (UINT i = 0; i + 1 < bytesRead; i += 2)
                                    {
                                        const unsigned char temp = buffer[i];
                                        buffer[i] = buffer[i + 1];
                                        buffer[i + 1] = temp;
                                    }
                                }
                                blinkLed(onOff);
                                onOff = !onOff;
#if 0
                                printf("Writing block %d (%d bytes) to flash at %x\n", blockCount++, bytesRead, ofs);
#endif
                                // Erase and flash. These disable interrupts around the
                                // operation and preserve the startup flash timing.
                                flashEraseSafe(ofs, bufsize);
                                flashProgramSafe(ofs, buffer, bufsize);
                                ofs += bufsize;
                                totalBytes += bytesRead;
                                // keep the usb stack running
                                tuh_task();
                            }
                            else
                            {
                                readError = true;
                                snprintf(ErrorMessage, 40, "Error reading rom: %d", fr);
                                printf("Error reading rom: %d: %d/%d bytes read\n", fr, totalBytes, filesize);
                                selectedRom[0] = 0;
                                break;
                            }
                        }
                        if (!readError)
                        {
                            // Flash freq is reported again here because erase/program
                            // re-run boot2, which resets the divisor chosen at startup.
                            // This must match the value printed in the startup banner.
                            printf("Wrote %d bytes to flash (flash freq: %d kHz)\n",
                                   totalBytes, getFlashClockHz() / 1000);
                            if (totalBytes != filesize)
                            {
                                snprintf(ErrorMessage, 40, "Size mismatch: %d != %d\n", totalBytes, filesize);
                                printf("%s\n", ErrorMessage);
                                selectedRom[0] = 0;
                            }
                            else
                            {
                                flashOK = true;
                            }
                        }
                    }
                    else
                    {
                        snprintf(ErrorMessage, 40, "ROM too large: %d > %d\n", filesize, maxRomSize);
                        printf("%s\n", ErrorMessage);
                        selectedRom[0] = 0;
                    }
                    f_close(&fil);
                }
                else
                {
                    snprintf(ErrorMessage, 40, "Cannot open rom %d", fr);
                    printf("%s\n", ErrorMessage);
                    selectedRom[0] = 0;
                }
                // Release the 128 KB block buffer before allocating the much
                // smaller recent-list structures, so the two never overlap.
                f_free(buffer);
                printf("Flashing done\n");

                if (flashOK && rec)
                {
                    // Describe what is now in flash, so the next launch of this
                    // same game can skip everything above.
                    FILINFO *fno = (FILINFO *)f_malloc(sizeof(FILINFO));
                    memset(rec, 0, sizeof(Recent::FlashedRomRecord));
                    rec->romFileAddr = (uint32_t)ROM_FILE_ADDR;
                    rec->size = totalBytes;
                    rec->crcPreSwap = crcOfRom;
                    // Covers exactly the rom bytes: the loop programs a full
                    // block even for the last partial one, so anything past
                    // size is stale buffer content.
                    rec->crcFlashImage = update_crc32(0, (const uint8_t *)ROM_FILE_ADDR, totalBytes);
                    if (fno && f_stat(selectedRom, fno) == FR_OK)
                    {
                        rec->fdate = fno->fdate;
                        rec->ftime = fno->ftime;
                    }
                    f_free(fno);
                    rec->byteSwapped = swapbytes ? 1 : 0;
                    rec->crcOffset = (uint8_t)crcOffsetUsed;
                    strncpy(rec->emu, FrensSettings::getEmulatorTypeString(true), sizeof(rec->emu) - 1);
                    strncpy(rec->path, selectedRom, sizeof(rec->path) - 1);
                    Recent::writeFlashedRomRecord(rec);
                    Recent::add(selectedRom, FrensSettings::getEmulatorTypeString(),
                                crcOfRom, totalBytes);
                }
            }
            f_free(rec);
        }
    }
#if !HSTX
    /// @brief Render function in core1 to render line by line
    /// @param
    /// @return
    void __not_in_flash_func(core1_main)()
    {
        while (true)
        {
            dvi_->registerIRQThisCore();
            // Line-stream mode has no producer queue to wait on; only the
            // default queue model needs a first valid line before starting.
            if (!lineStreamFill_)
                dvi_->waitForValidLine();

            dvi_->start();
            while (!exclProc_.isExist() && !displayParkRequested_)
            {
                Frens::LineStreamFillFn fn = lineStreamFill_;
                if (fn)
                {
                    // Line-stream mode: read each line via the callback into the
                    // scratch buffer and stream it straight to the DMA. The
                    // convertScanBuffer12bpp(line,...) call blocks on the free
                    // TMDS queue, so this loop is paced to the DMA's 60Hz.
                    // Raise vsync at frame start so the producer (core0) can
                    // phase-lock its render to this read pass (waitForVSync) and
                    // stay ahead of the read -> no crawling tear seam.
                    lineStreamActive_ = true;
                    vsync = false;
                    for (int line = 0; line < SCREENHEIGHT; ++line)
                    {
                        fn(line, lineStreamScratch_);
                        dvi_->convertScanBuffer12bpp(line, lineStreamScratch_, 640);
                    }
                    vsync = true;
                }
                else if (scaleMode8_7_)
                {
                    lineStreamActive_ = false;
                    // Default
                    dvi_->convertScanBuffer12bppScaled16_7(34, 32, 288 * 2);
                    // dvi_->convertScanBuffer12bppScaled16_7(0,0 , 320 * 2);
                    //  34 + 252 + 34
                    //  32 + 576 + 32
                }
                else
                {
                    lineStreamActive_ = false;
                    //
                    dvi_->convertScanBuffer12bpp();
                }
            }

            dvi_->unregisterIRQThisCore();
            dvi_->stop();

            // Parked: dvi_->stop() above disabled the serialisers, so stay out
            // of the loop rather than restarting them. Only a reboot leaves.
            while (displayParkRequested_)
            {
                displayParked_ = true;
                tight_loop_contents();
            }
            displayParked_ = false;

            exclProc_.processOrWaitIfExist();
        }
    }

    // Stop the DVI output and leave core1 idling. Returns once core1 has
    // acknowledged, so the caller knows the serialisers are really off. No-op
    // where core1 does not drive a line-buffer display.
    void parkDisplayCore1()
    {
        if (displayParkRequested_)
        {
            return;
        }
        displayParkRequested_ = true;
        // Core1 finishes the frame it is on before checking, so give it time.
        absolute_time_t deadline = make_timeout_time_ms(200);
        while (!displayParked_ && !time_reached(deadline))
        {
            tight_loop_contents();
        }
        printf("Display parked for USB drive mode (core1 idle, DVI stopped)\n");
    }

    void setLineStreamFill(LineStreamFillFn fn)
    {
        if (fn && !lineStreamScratch_)
        {
            // Allocate the per-line scratch (SRAM, for fast core1 reads) on
            // first use. Kept allocated and reused across games — not freed on
            // unregister, which would race core1 still finishing a frame.
            lineStreamScratch_ = (uint16_t *)malloc(640 * sizeof(uint16_t));
        }
        if (fn && !lineStreamScratch_)
            return; // allocation failed: stay in queue mode rather than deref null
        lineStreamFill_ = fn;
    }

    bool lineStreamActive()
    {
        return lineStreamActive_;
    }

    static WORD *buffer;

    /// @brief Render function in core1 to render the framebuffers
    /// @param
    /// @return
    void __not_in_flash_func(coreFB_main)()
    {
#if FRAMEBUFFERISPOSSIBLE
        WORD *framebufferCore1 = framebuffer;
        dvi_->registerIRQThisCore();
        dvi_->start();
        int fb1 = 0;
        int fb2 = 0;

        while (true)
        {
            vsync = false;
            for (int line = 0; line < SCREENHEIGHT; ++line)
            {
                // processScanLineFunction(line - startLine, framebufferCore1, buffer);
                // point buffer to correct scanline
                buffer = &framebufferCore1[line * SCREENWIDTH];
                if (scaleMode8_7_)
                {
                    // printf("8_7 Scaling\n");
                    dvi_->convertScanBuffer12bppScaled16_7(34, 32, 288 * 2, line, buffer, 640);
                    // 34 + 252 + 34
                    // 32 + 576 + 32
                }
                else
                {
                    // printf("line: %d\n", line);
                    dvi_->convertScanBuffer12bpp(line, buffer, 640);
                }
            }
            vsync = true;
        }
#endif
    }
#if 0
    void SetFrameBufferProcessScanLineFunction(ProcessScanLineFunction processScanLineFunction)
    {
        if (isFrameBufferUsed())
        {
            mutex_enter_blocking(&framebuffer_mutex);
            Frens::processScanLineFunction = processScanLineFunction;
            memset(framebuffer1, 255, SCREENWIDTH * SCREENHEIGHT);
            memset(framebuffer2, 255, SCREENWIDTH * SCREENHEIGHT);
            mutex_exit(&framebuffer_mutex);
        }
    }
#endif
#endif // DVI

    void blinkLed(bool on)
    {
#if LED_GPIO_PIN > -1
#if LED_GPIO_PIN > 0
        gpio_put(LED_GPIO_PIN, on);
#elif defined(PICO_DEFAULT_LED_PIN)
        gpio_put(PICO_DEFAULT_LED_PIN, on);
#elif defined(CYW43_WL_GPIO_LED_PIN) // && !USE_I2S_AUDIO https://github.com/fhoedemakers/pico-infonesPlus/issues/132 Solved?
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
#else
        // No LED pin defined
        (void)on; // Suppress unused parameter warning
#endif
#else
        (void)on; // Suppress unused parameter warning
#endif
    }

    // Initialize the LED
    // Note that activationg the LED on the PICO W makes the board unstable and
    // completely unresponsive. This is why building for PICO W is not recommended. Use Pico build instead.
    // LED_GPIO_PIN -1 : No Onboard LED
    // LED_GPIO_PIN 0  : Onboard LED
    // LED_GPIO_PIN > 0: Onboard LED on GPIO pin LED_GPIO_PIN. (Feather DVI as a different onboard led pin)
    int initLed()
    {
#if LED_GPIO_PIN > -1
#if LED_GPIO_PIN > 0
        gpio_init(LED_GPIO_PIN);
        gpio_set_dir(LED_GPIO_PIN, GPIO_OUT);
        gpio_put(LED_GPIO_PIN, 1);
#elif defined(PICO_DEFAULT_LED_PIN)
        gpio_init(PICO_DEFAULT_LED_PIN);
        gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
        gpio_put(PICO_DEFAULT_LED_PIN, 1);
#elif defined(CYW43_WL_GPIO_LED_PIN) //  && !USE_I2S_AUDIO https://github.com/fhoedemakers/pico-infonesPlus/issues/132 Solved?
        return cyw43_arch_init();
#endif
#endif
        return PICO_OK;
    }

    /// @brief Finds an unused DMA channel.
    /// This function iterates through the available DMA channels (0-11) and returns the first unused channel.
    /// If no unused channel is found, it will panic.
    /// @param startChannel The channel to start searching from. If -1, it starts from 0.
    /// @return the number of the unused DMA channel (0-11).
    int GetUnUsedDMAChan(int startChannel)
    {
        // Get an unused DMA channel
        int dma_chan = -1;
        int startChan;

        if (startChannel == -1)
        {
#if !HSTX
            startChan = 0;
#else
            startChan = 2; // HSTX uses DMA channel 0 (DMACHPING) and 1 (DMACHPONG) on core1, avoid this core claiming them
#endif
        }
        else
        {
            startChan = startChannel; // Use the provided start channel
        }
        printf("Searching for unused DMA channel starting from %d...", startChan);
        for (int i = startChan; i < 12; i++)
        {
            if (!dma_channel_is_claimed(i))
            {
                dma_chan = i;
                printf(" found DMA channel %d\n", dma_chan);
                break;
            }
        }
        if (dma_chan == -1)
        {
            panic("No unused DMA channel found");
        }
        return dma_chan;
    }

    void initVintageControllers(uint32_t CPUFreqKHz)
    {
#if NES_PIN_CLK != -1
        nespad_begin(0, CPUFreqKHz, NES_PIN_CLK, NES_PIN_DATA, NES_PIN_LAT, NES_PIO);
#endif
#if NES_PIN_CLK_1 != -1
        nespad_begin(1, CPUFreqKHz, NES_PIN_CLK_1, NES_PIN_DATA_1, NES_PIN_LAT_1, NES_PIO_1);
#endif
        // Initialize the Wii Pad, but only if the pins are defined and WIIPAD_DELAYED_START is not set
        // This allows for delayed initialization if needed to avoid conflicts with other I2C devices
        // like the DAC on Fruit Jam.
        // https://github.com/fhoedemakers/pico-genesisPlus/issues/10
#if !WIIPAD_DELAYED_START and WII_PIN_SDA >= 0 and WII_PIN_SCL >= 0
        wiipad_begin();
#elif WII_PIN_SDA >= 0 and WII_PIN_SCL >= 0 and (USE_I2S_AUDIO == PICO_AUDIO_I2S_DRIVER_TLV320)
        // The TLV320 DAC (initialized right after this via EXT_AUDIO_SETUP)
        // shares the I2C bus with the Wii-extension port. An uninitialized
        // SNES-classic-mini pad attached at power-on wedges the bus and makes
        // every DAC transaction time out (res=-2). Recover the bus and give an
        // attached pad its extension-init so it goes quiet before the DAC
        // driver takes over. The DAC is held in reset meanwhile so it cannot
        // observe the pad traffic (tlv320_hardware_reset() releases it later).
        gpio_init(PICO_AUDIO_I2S_RESET_PIN);
        gpio_put(PICO_AUDIO_I2S_RESET_PIN, 0);
        gpio_set_dir(PICO_AUDIO_I2S_RESET_PIN, GPIO_OUT);
        i2c_bus_clear(WII_PIN_SDA, WII_PIN_SCL, "pad-preinit");
        wiipad_begin(); // fast NACK, no delays, when no pad is attached
        i2c_bus_clear(WII_PIN_SDA, WII_PIN_SCL, "pre-DAC");
#endif
    }

    // Initialize the PIO USB board
    // replaces board_init() in $PICO_SDK_PATH/lib/tinyusb/src/hw/bsp/rp2040/family.c
    void pio_usb_board_init(void)
    {
#if PICO_RP2350
#if (CFG_TUH_ENABLED && CFG_TUH_RPI_PIO_USB) || (CFG_TUD_ENABLED && CFG_TUD_RPI_PIO_USB)
        // power on the PIO USB VBUSEN pin if needed.
#ifdef PICO_DEFAULT_PIO_USB_VBUSEN_PIN
        gpio_init(PICO_DEFAULT_PIO_USB_VBUSEN_PIN);
        gpio_set_dir(PICO_DEFAULT_PIO_USB_VBUSEN_PIN, GPIO_OUT);
        gpio_put(PICO_DEFAULT_PIO_USB_VBUSEN_PIN, PICO_DEFAULT_PIO_USB_VBUSEN_STATE);
#endif

        // rp2040 use pico-pio-usb for host tuh_configure() can be used to passed pio configuration to the host stack
        // Note: tuh_configure() must be called before tuh_init()
        pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
        // find an unused DMA channel
        pio_cfg.tx_ch = GetUnUsedDMAChan(-1); // -1 find the first unused DMA channel
        //
        pio_cfg.pio_rx_num = PIO_USB_USE_PIO;
        pio_cfg.pio_tx_num = PIO_USB_USE_PIO;
        pio_cfg.pin_dp = PICO_DEFAULT_PIO_USB_DP_PIN;
        tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
#endif
#endif
    }
    void initDVandAudio(int marginTop, int marginBottom, size_t audioBufferSize)
    {
#if !HSTX
        dvi_ = std::make_unique<dvi::DVI>(pio0, &DVICONFIG,
                                          dvi::getTiming640x480p60Hz());

//    dvi_->setAudioFreq(48000, 25200, 6144);
#if 0
#if DVIAUDIOFREQ == 53280
        dvi_->setAudioFreq(DVIAUDIOFREQ, 22708, 6144);
#else
        dvi_->setAudioFreq(DVIAUDIOFREQ, 28000, 6272);
         //    dvi_->setAudioFreq(48000, 25200, 6144);
#endif
#else
        // Switch to standard 48 kHz HDMI audio timing.
        // For 25.2 MHz pixel clock (640x480p60), a common standard tuple is N=6144, CTS=25200 giving exactly 48 kHz.
        // Pass CTS=0 to auto compute correct CTS for current (possibly overclocked) pixel clock
        dvi_->setAudioFreq(DVIAUDIOFREQ, 0, 6144);
#endif
        dvi_->allocateAudioBuffer(audioBufferSize);
        //    dvi_->setExclusiveProc(&exclProc_);

        dvi_->getBlankSettings().top = marginTop * 2;
        dvi_->getBlankSettings().bottom = marginBottom * 2;
        // dvi_->setScanLine(true);
        // 空サンプル詰めとく
        dvi_->getAudioRingBuffer().advanceWritePointer(255);
#else
        hstx_init(settings.flags.useDVIModeForHDMI);
#if 0
        // For now use an MCP4822 DAC for audio output
        // https://ww1.microchip.com/downloads/aemDocuments/documents/OTH/ProductDocuments/DataSheets/20002249B.pdf
        // This is only used for HSTX, not for DVI.
        // The MCP4822 is a dual channel 12-bit DAC.
        // The DAC must be connected to the correct GPIO pins and must have an audio Jack connected.
        // see drivers/pico_audio_mcp4822/mcp4822.h for the pin definitions.
        mcp4822_init();
#endif
#endif
    }

    /// @brief Init dv and audio with default audio buffer size of 256
    /// @param marginTop
    /// @param marginBottom
    void initDVandAudio(int marginTop, int marginBottom)
    {
        initDVandAudio(marginTop, marginBottom, 256);
    }

    /// @brief Initialize SD Card, Audio, Video etc...
    /// @param selectedRom   The user selected rom
    /// @param CPUFreqKHz    Clock frequency in kHz of the cpu
    /// @param marginTop     Top Margin in lines.    (ignored when framebuffer is used)
    /// @param marginBottom  Bottom Margin in lines. (Ignored when framebuffer is used)
    /// @param audiobufferSize Size of the audio buffer
    /// @param swapbytes Swap bytes when loading Roms (Master System, Game Gear)
    /// @param useFrameBuffer Use framebuffer when possible
    /// @return
    bool initAll(char *selectedRom, uint32_t CPUFreqKHz, int marginTop, int marginBottom, size_t audiobufferSize, bool swapbytes, bool useFrameBuffer)

    {
        dumpHeapStats("initAll/enter");
        byteSwapped = swapbytes;
        bool ok = false;
        int rc = initLed();
        dumpHeapStats("initAll/initLed");
        if (rc != PICO_OK)
        {
            printf("Error initializing LED: %d\n", rc);
        }
       
        dumpHeapStats("initAll/preInitPsram");
        if (initPsram() == false)
        {
            printf("PSRAM not enabled, using flash for rom storage\n");
            auto flashcap = storage_get_flash_capacity();
            printf("Flash capacity: %d bytes (%d Kbytes)\n", flashcap, flashcap / 1024);
            // Calculate the address in flash where roms will be stored
            printf("Flash binary start    : 0x%08x\n", &__flash_binary_start);
            printf("Flash binary end      : 0x%08x\n", &__flash_binary_end);
            // printf("Flash size in bytes   :   %8d (%d)Kbytes\n", PICO_FLASH_SIZE_BYTES, PICO_FLASH_SIZE_BYTES / 1024);
            printf("Flash size in bytes   :   %8d (%d Kbytes)\n", flashcap, flashcap / 1024);
            // uint8_t *flash_end = (uint8_t *)&__flash_binary_start + PICO_FLASH_SIZE_BYTES - 1;
            uint8_t *flash_end = (uint8_t *)&__flash_binary_start + flashcap - 1;
            printf("Flash end             : 0x%08x\n", flash_end);
            printf("Size program in flash :   %8d bytes (%d) Kbytes\n", &__flash_binary_end - &__flash_binary_start, (&__flash_binary_end - &__flash_binary_start) / 1024);
            // Place ROM one full flash sector above FlashParams so the sector
            // holding FlashParams is never erased when (re)flashing a ROM.
            ROM_FILE_ADDR = FLASHPARAM_ADDRESS + FLASH_SECTOR_SIZE;
            // ROM_FILE_ADDR =  0x1004a000;
            //  calculate max rom size
            maxRomSize = flash_end - (uint8_t *)ROM_FILE_ADDR;
            printf("ROM_FILE_ADDR         : 0x%08x\n", ROM_FILE_ADDR);
            printf("Max ROM size          :   %8d bytes (%d) KBytes\n", maxRomSize, maxRomSize / 1024);
        }
        else
        {
            maxRomSize = psramMemorySize;
            printf("  PSRAM size            :   %8zu bytes (%zu) KBytes\n", psramMemorySize, psramMemorySize / 1024);
            printf("  Max ROM size          :   %8zu bytes (%zu) KBytes\n", maxRomSize, maxRomSize / 1024);
        }
        // auto cap = storage_get_flash_capacity();
        // printf("Total flash size: %d bytes (%d Kbytes)\n", cap,cap/ 1024);
        // reset settings to default in case SD card could not be mounted
        FrensSettings::resetsettings();
        dumpHeapStats("initAll/preInitSD");
        if (initSDCard())
        {
            ok = true;
            dumpHeapStats("initAll/postInitSD");
            FrensSettings::loadsettings();
            dumpHeapStats("initAll/postLoadSettings");
            // When a game is started from the menu, the menu will reboot the device.
            // After reboot the emulator will start the selected game.
            // The watchdog timer is used to detect if the reboot was caused by the menu.
            // Use watchdog_enable_caused_reboot in stead of watchdog_caused_reboot because
            // when reset is pressed while in game, the watchdog will also be triggered.
            if (watchdog_enable_caused_reboot() && !isPsramEnabled())
            {
                // If the watchdog was triggered, we assume that the menu started the game.
                // So we flash the rom to flash memory.
                printf("Rebooted by menu, flashing rom.\n");
                flashrom(selectedRom, byteSwapped);
            }
        }
#if !HSTX && FRAMEBUFFERISPOSSIBLE
        usingFramebuffer = useFrameBuffer;
        if (usingFramebuffer)
        {
            // always allocate framebuffer in SRAM
            //printf("Allocating %d bytes for framebuffer in SRAM\n", SCREENWIDTH * SCREENHEIGHT * sizeof(WORD));
            //framebuffer = (WORD *)malloc(SCREENWIDTH * SCREENHEIGHT * sizeof(WORD));
            memset(framebuffer, 0, SCREENWIDTH * SCREENHEIGHT * sizeof(WORD));
            marginTop = marginBottom = 0; // ignore margins when using framebuffer
        }
#endif // DVI
        dumpHeapStats("initAll/preDVandAudio");
        initDVandAudio(marginTop, marginBottom, audiobufferSize);
        dumpHeapStats("initAll/postDVandAudio");
        // init USB driver
        // USB driver is initalized after display driver to prevent the display driver
        // from using the PIO state machines already claimed by the USB driver.
        // This is only needed for the PIO USB driver.
#if CFG_TUH_RPI_PIO_USB && PICO_RP2350
        printf("Using PIO USB.\n");
        dumpHeapStats("initAll/preUSB");
        pio_usb_board_init();
        dumpHeapStats("initAll/postPioUsbBoard");
        tusb_rhport_init_t host_init = {
            .role = TUSB_ROLE_HOST,
            .speed = TUSB_SPEED_AUTO};
        tusb_init(BOARD_TUH_RHPORT, &host_init);
        dumpHeapStats("initAll/postTusbInit");

        if (board_init_after_tusb)
        {
            board_init_after_tusb();
        }
        dumpHeapStats("initAll/postBoardAfterTusb");
#else
        printf("Using internal USB.\n");
        dumpHeapStats("initAll/preUSB");
#if FRENS_USB_MSC
        // The argument-less tusb_init() brings up every enabled stack, and with
        // USB drive mode compiled in that includes the device stack. Only the
        // host is wanted at boot: the device side is started on demand by
        // Frens::usbMscBegin() and stopped again on the way out.
        {
            tusb_rhport_init_t host_init = {
                .role = TUSB_ROLE_HOST,
                .speed = TUSB_SPEED_AUTO};
            tusb_init(BOARD_TUH_RHPORT, &host_init);
        }
#else
        tusb_init();
#endif
        dumpHeapStats("initAll/postTusbInit");
#endif
#if !HSTX
        // Add a small stack for core1
        static uint32_t core1_stack[512 / sizeof(uint32_t)];
#if FRAMEBUFFERISPOSSIBLE
      
        if (usingFramebuffer)
        {   
            multicore_launch_core1_with_stack(coreFB_main,  core1_stack, sizeof(core1_stack));
        }
        else
        {
            multicore_launch_core1_with_stack(core1_main,  core1_stack, sizeof(core1_stack));
        }
#else
        multicore_launch_core1_with_stack(core1_main,  core1_stack, sizeof(core1_stack));
#endif
#endif // DVI
        initVintageControllers(CPUFreqKHz);
        // TODO: DMA chan 1-3 are used for PIO0, chan 4-7 for PIO1, Assuming PIO1 is used for audio.
        EXT_AUDIO_SETUP(USE_I2S_AUDIO, DVIAUDIOFREQ, GetUnUsedDMAChan(4)); // Initialize external audio if needed
        srand(get_rand_32());                                              // Seed the random number generator with a random value
#if ENABLE_VU_METER
        initializeNeoPixelStrip();
#endif
        return ok;
    }
#if !HSTX && 0
    void markFrameReadyForReendering(bool waitForFrameReady)
    {
        // switch framebuffers
        // Lock the mutex only to update shared state
        mutex_enter_blocking(&framebuffer_mutex);
        if (use_framebuffer1)
        {
            framebuffer1_ready = true;
            framebuffer2_ready = false;
        }
        else
        {
            framebuffer1_ready = false;
            framebuffer2_ready = true;
        }
        use_framebuffer1 = !use_framebuffer1; // Toggle the framebuffer
        framebufferCore0 = use_framebuffer1 ? framebuffer1 : framebuffer2;
        mutex_exit(&framebuffer_mutex);
        // Wait if core1 is still rendering the framebuffer whe just switched to
#if 0
        int start = time_us_64();
#endif
        if (waitForFrameReady)
        {
            while ((use_framebuffer1 && framebuffer1_rendering) || (!use_framebuffer1 && framebuffer2_rendering))
            {
                tight_loop_contents();
            }
        }
#if 0
        int end = time_us_64();
        printf("Core 0: Switching framebuffers took %ld us\n", end - start);
#endif
        // continue processing next frame while the other core renders the framebuffer
    }
#endif // DVI
    void resetWifi()
    {
#if defined(CYW43_WL_GPIO_LED_PIN)
        printf("Deinitializing CYW43\n");
        cyw43_arch_deinit();
#endif
    }

#if !PICO_RP2350
    // Divisor relaxFlashTimingForClock() installed, or 0 if it left boot2's choice
    // alone. Anything that re-runs boot2 undoes it, so we need to know what to
    // put back -- see flashEraseSafe()/flashProgramSafe() below.
    static uint32_t appliedFlashDivisor = 0;

    // Write the XIP SSI baud rate divisor. BAUDR is only writable while the SSI is
    // disabled, and XIP is dead for that window, so this must execute from RAM and
    // must not be interrupted. Core1 is not running yet when this is called.
    static void __no_inline_not_in_flash_func(setFlashDivisor)(uint32_t div)
    {
        ssi_hw->ssienr = 0;
        ssi_hw->baudr = div;
        ssi_hw->ssienr = 1;
        (void)ssi_hw->ssienr; // let the enable land before we return to XIP
    }

    // How fast the fitted flash part may be clocked, by JEDEC manufacturer.
    static uint32_t flashMaxClockKHz(uint8_t manufacturerId)
    {
        switch (manufacturerId)
        {
        case 0xEF:
            // Winbond W25Q, what genuine Picos ship. Rated 133 MHz, and Raspberry Pi
            // validated clk_sys/2 against it, so leave those boards exactly as they
            // have always run: 126 MHz at a 252 MHz overclock.
            return 133000;
        default:
            // Clone parts are typically rated 100-108 MHz (the BoyaMicro BY25Q16 that
            // prompted this is 108). Stay below the slowest of them. Also covers a
            // failed/absent JEDEC read, which reports manufacturer 0.
            return 94500;
        }
    }

    /*
     * RP2040 counterpart of the RP2350 QMI timing fix below.
     *
     * boot2 drives the flash at clk_sys / PICO_FLASH_SPI_CLKDIV, and boards/pico.h
     * overrides the SDK's generic default of 4 with 2 because a genuine Pico carries a
     * 133 MHz W25Q16JV. Clone boards commonly carry a ~104 MHz part instead, so at
     * 252 MHz clk_sys the flash would be clocked at 126 MHz. Beyond spec the reads do
     * not fault, they return garbage: the very first instruction fetched after the
     * clk_sys mux switches decodes to nonsense and the core hard-faults inside
     * clock_configure(), which looks like a crash in set_sys_clock_khz() itself.
     *
     * So pick the smallest (even) divisor that keeps the flash within what the fitted
     * part is rated for, and apply it *before* raising clk_sys. A genuine Winbond board
     * lands back on the divisor boot2 already programmed and is left untouched; the
     * Boya clone gets divisor 4, i.e. 63 MHz instead of 126.
     *
     * This only ever slows the flash down. If boot2 chose a more conservative divisor
     * than we compute -- board headers know things we do not -- we defer to it.
     */
    static void relaxFlashTimingForClock(uint32_t cpuFreqKHz)
    {
        uint8_t manufacturer = (storage_get_flash_jedec_id() >> 16) & 0xff;
        uint32_t max_flash_khz = flashMaxClockKHz(manufacturer);
        uint32_t div = (cpuFreqKHz + max_flash_khz - 1) / max_flash_khz;
        div = (div + 1) & ~1u; // BAUDR must be even
        if (div < 2)
        {
            div = 2;
        }
        if (div <= ssi_hw->baudr)
        {
            return; // already at or below the rate this part can take
        }
        uint32_t irq = save_and_disable_interrupts();
        setFlashDivisor(div);
        appliedFlashDivisor = div;
        restore_interrupts(irq);
    }

    // Put our divisor back after something has re-run boot2. Must itself live in
    // RAM: the caller may not touch XIP between the boot2 re-run and this call.
    static void __no_inline_not_in_flash_func(restoreFlashDivisor)()
    {
        if (appliedFlashDivisor != 0 && ssi_hw->baudr != appliedFlashDivisor)
        {
            setFlashDivisor(appliedFlashDivisor);
        }
    }
#endif

    /*
     * flash_range_erase() and flash_range_program() finish by calling
     * flash_enable_xip_via_boot2(), which re-runs boot2 and reprograms BAUDR to
     * PICO_FLASH_SPI_CLKDIV -- silently undoing relaxFlashTimingForClock(). The
     * SDK's flash_restore_hardware_state() does not cover this: on RP2040 it
     * saves the QSPI pads and nothing else.
     *
     * Left alone, a clone board would boot safely and then be put back to
     * clk_sys/2 by the first ROM write, for the rest of the session.
     *
     * Restoring the divisor after these calls return is not enough -- execution
     * returns to XIP immediately, so the fetch of the very next instruction
     * already happens at the unsafe rate. These wrappers run from RAM so the
     * divisor is back in place before any flash access can occur.
     */
    void __no_inline_not_in_flash_func(flashEraseSafe)(uint32_t flashOffset, size_t count)
    {
        uint32_t irq = save_and_disable_interrupts();
        flash_range_erase(flashOffset, count);
#if !PICO_RP2350
        restoreFlashDivisor();
#endif
        restore_interrupts(irq);
    }

    void __no_inline_not_in_flash_func(flashProgramSafe)(uint32_t flashOffset, const uint8_t *data, size_t count)
    {
        uint32_t irq = save_and_disable_interrupts();
        flash_range_program(flashOffset, data, count);
#if !PICO_RP2350
        restoreFlashDivisor();
#endif
        restore_interrupts(irq);
    }

    // Rate the QSPI flash is actually being clocked at, i.e. clk_sys divided by
    // whatever the flash interface is programmed to. Reported at startup so a bug
    // report shows straight away whether the flash is being driven out of spec.
    uint32_t getFlashClockHz()
    {
#if PICO_RP2350
        uint32_t div = qmi_hw->m[0].timing & QMI_M0_TIMING_CLKDIV_BITS;
#else
        uint32_t div = ssi_hw->baudr;
#endif
        return div ? clock_get_hz(clk_sys) / div : 0;
    }

    // Set CPU clock to desired speed
    // Set HSTX clock to 126 MHz if HSTX is used so the HSTX display driver can output at no more than 60Hz
    // True when setClocksAndStartStdio() repurposed PLL_USB as the 126 MHz
    // HSTX source. The native USB device controller needs PLL_USB at 48 MHz,
    // so USB drive mode has to borrow it back for the duration.
    static bool pllUsbTakenForHstx = false;
    static bool usbClockBorrowed = false;

    // Give the native USB controller a valid 48 MHz clock.
    //
    // On HSTX builds that use PIO USB for gamepads, setClocksAndStartStdio()
    // drives clk_hstx from PLL_USB at 126 MHz, because deriving it from
    // PLL_SYS propagates CPU-clock jitter into the TMDS bit clock. That leaves
    // clk_usb running at 126 MHz instead of 48, which is why a PC reports
    // "USB device not recognized": the device attaches but cannot enumerate.
    //
    // clk_sys is a whole multiple of 126 MHz at the frequencies this runs at
    // (252 and 378 MHz), so HSTX can be moved onto clk_sys for the duration
    // and PLL_USB handed back to USB. The picture stays up; the only cost is
    // the TMDS jitter the comment above describes, which is of no consequence
    // for a static text screen. Returns false if the live clk_sys cannot
    // source HSTX, in which case the caller must not enter USB drive mode.
    bool usbDeviceClockAcquire()
    {
#if HSTX
        if (!pllUsbTakenForHstx || usbClockBorrowed)
        {
            return true; // PLL_USB is already at 48 MHz
        }
        const uint32_t hstx_hz = 126000000u;
        uint32_t sys_hz = clock_get_hz(clk_sys);
        if (sys_hz == 0 || (sys_hz % hstx_hz) != 0)
        {
            printf("usbDeviceClockAcquire: clk_sys %lu is not a multiple of 126 MHz\n",
                   (unsigned long)sys_hz);
            return false;
        }
        // Move HSTX off PLL_USB first so the display never loses its clock.
        if (!clock_configure(clk_hstx, 0,
                             CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS,
                             sys_hz, hstx_hz))
        {
            printf("usbDeviceClockAcquire: cannot source HSTX from clk_sys\n");
            return false;
        }
        // PLL_USB is free now: put back the 48 MHz the USB hardware needs.
        pll_deinit(pll_usb);
        pll_init(pll_usb, PLL_USB_REFDIV, PLL_USB_VCO_FREQ_HZ,
                 PLL_USB_POSTDIV1, PLL_USB_POSTDIV2);
        clock_configure(clk_usb, 0,
                        CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                        USB_CLK_HZ, USB_CLK_HZ);
        usbClockBorrowed = true;
        printf("USB drive mode: PLL_USB back to 48 MHz, HSTX now on clk_sys %lu\n",
               (unsigned long)sys_hz);
#endif
        return true;
    }

    // Undo usbDeviceClockAcquire(): PLL_USB returns to 126 MHz and HSTX goes
    // back onto it, restoring the low-jitter TMDS clock the emulator wants.
    void usbDeviceClockRelease()
    {
#if HSTX
        if (!usbClockBorrowed)
        {
            return;
        }
        const uint32_t hstx_hz = 126000000u;
        pll_deinit(pll_usb);
        pll_init(pll_usb, 1, 756000000, 6, 1); // 756 / (6*1) = 126 MHz
        clock_configure(clk_hstx, 0,
                        CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                        hstx_hz, hstx_hz);
        usbClockBorrowed = false;
        printf("USB drive mode ended: HSTX back on PLL_USB at 126 MHz\n");
#endif
    }

    void setClocksAndStartStdio(uint32_t cpuFreqKHz, vreg_voltage voltage)
    {
        // Call this function before setting the clock to a higher frequency.
        // This will ensure that consecutive calls to   storage_get_flash_capacity();
        // will not crash the board when the clock is set to a higher frequency than the default 125 MHz.
        storage_get_flash_capacity();
        // Set voltage and clock frequency
        vreg_disable_voltage_limit();
        vreg_set_voltage(voltage);
        // Let the core rail reach the new voltage before asking the chip to run at
        // an overclocked speed. PicoDVI's examples do the same before their 252 MHz
        // switch; without it the whole chip, flash interface included, is briefly
        // running fast at the old voltage. The HSTX path below already waits.
        sleep_ms(10);
#if !HSTX
#if !PICO_RP2350
        // Slow the flash down before clk_sys goes up, or clone boards with a 104 MHz
        // QSPI part hard-fault on the first instruction fetched at the new clock.
        relaxFlashTimingForClock(cpuFreqKHz);
#endif
        set_sys_clock_khz(cpuFreqKHz, true);
        sleep_ms(100);
#else
        if (cpuFreqKHz >= 378000)
        {
            /*
             * When overclocking above ~378 MHz we must slow down and relax the execute-in-place
             * (XIP) external flash access timings to stay within the flash device's max SPI
             * frequency and preserve reliable instruction fetches.
             *
             * qmi_hw->m[0].timing directly programs the QMI (Quad/Octal Memory Interface) timing
             * register for XIP region 0.
             *
             * Value 0x60007304 (fields per RP2350 datasheet):
             *   0x3004  : core base latency / data strobe/sample delay settings
             *   0x0070  : additional dummy cycles + turnaround adjustments
             *   0x60000000 : sets a higher clock divisor (4x flash divisor path enabled)
             *
             * In short: this applies a safer (slower) flash access profile so the very high
             * system clock does not overdrive the flash. Without this, random faults or hard
             * crashes can occur when fetching code/data from XIP at these frequencies.
             */
            /*
             * Frequency-aware divisor. The old fixed 0x60007304 hard-coded
             * CLKDIV=4, so the flash clock tracked clk_sys: fine at 378 MHz
             * (94.5 MHz flash) but 432/480/504 gave 108/120/126 MHz, past
             * what the QSPI part will sample reliably — and XIP traffic
             * peaks under emulator load, so that shows up as a hard fault
             * seconds in rather than at boot.
             *
             * Pick the smallest divisor that keeps flash <= 94.5 MHz (the
             * rate proven at 378 MHz), and scale RXDELAY so the read sample
             * point stays at roughly the same absolute delay in ns (3 sys
             * cycles @ 378 MHz = 7.9 ns). At 378 this reproduces the old
             * constant exactly (div 4, rxdelay 3 -> 0x60007304), so nothing
             * changes for existing builds; at 504 it gives div 6 (84 MHz
             * flash) and rxdelay 4.
             *
             * Field layout (RP2350 QMI M0_TIMING): COOLDOWN[31:30]=1,
             * PAGEBREAK[29:28]=2 (1024), MAX_SELECT[21:16]=0,
             * MIN_DESELECT[15:11]=14, RXDELAY[10:8], CLKDIV[7:0].
             */
            const uint32_t max_flash_khz = 94500;
            uint32_t flash_div = (cpuFreqKHz + max_flash_khz - 1) / max_flash_khz;
            if (flash_div < 4)
                flash_div = 4;
            uint32_t rxdelay = (3 * cpuFreqKHz + 189000) / 378000; // 3 @378, 4 @504
            if (rxdelay > 7)
                rxdelay = 7; // RXDELAY is 3 bits
            qmi_hw->m[0].timing = 0x60000000u | (14u << 11) | (rxdelay << 8) | flash_div;
        }

        sleep_ms(100);
        set_sys_clock_khz(cpuFreqKHz, true);
        sleep_ms(100);
        // Reconfigure HSTX clock to 126 MHz, so display can run at 60Hz.
        //
        // SGX-at-378 MHz path only: *force* the PLL_USB-sourced HSTX route
        // even for clk_sys values that are integer multiples of 126. Reason:
        // deriving clk_hstx from clk_sys propagates PLL_SYS jitter into the
        // TMDS bit clock at 378 MHz, producing dots / short dotted lines on
        // strict HDMI receivers (eye-pattern closure). PLL_USB at a fixed
        // 126 MHz keeps the TMDS clock decoupled from CPU clock.
        //
        // Only safe to reconfigure PLL_USB when the build uses PIO-USB for
        // gamepads (CFG_TUH_RPI_PIO_USB=1). On TinyUSB-native-USB builds
        // PLL_USB MUST stay at 48 MHz for USB hardware, so we keep the
        // original clk_sys-derived HSTX path. Those builds should either
        // stay at 252 MHz for SGX (no artifacts) or accept the dots at
        // 378 MHz — the chip has no third clean PLL to use for HSTX.
        bool hstx_ok = true;
#if (SGX || GENESIS_OVERCLOCK_HSTX_FIX || NES_OVERCLOCK_FIX) && CFG_TUH_RPI_PIO_USB
        const bool force_pll_usb_hstx = true;
#else
        const bool force_pll_usb_hstx = false;
#endif
        if (force_pll_usb_hstx || ((cpuFreqKHz / 1000) % 126 != 0))
        {
            // (Re)configure PLL_USB for 126 MHz HSTX source.
            pll_deinit(pll_usb);
            pll_init(pll_usb, 1, 756000000, 6, 1); // 756 / (6*1) = 126 MHz
            // Remember that PLL_USB no longer carries 48 MHz. USB drive mode
            // needs it back before the native USB device controller can
            // enumerate - see usbDeviceClockAcquire().
            pllUsbTakenForHstx = true;

            const uint32_t target_hstx_hz = 126000000u;
            hstx_ok = clock_configure(
                clk_hstx,
                0,
                CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                target_hstx_hz,
                target_hstx_hz);

            // Keep clk_peri sourced from PLL_SYS at sys_hz — same as the
            // previous non-SGX path that the user has confirmed boots at
            // 378 MHz. Re-routing clk_peri during clock init (which the
            // earlier revision did) caused a boot loop. Peripheral drivers
            // read clock_get_hz(clk_peri) for their own dividers, so they
            // adapt to whatever clk_peri ends up at.
            clock_configure(clk_peri,
                            0,
                            CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                            cpuFreqKHz * 1000,
                            cpuFreqKHz * 1000);
        }
        else
        {
            // DO NOT touch pll_usb: keep its 48 MHz for USB.
            // Derive 126 MHz HSTX from clk_sys (cpuFreqKHz * 1000 input).
            // This works only when clock is set to 126, 252 or 378 MHz.
            const uint32_t sys_hz = cpuFreqKHz * 1000;
            const uint32_t target_hstx_hz = 126000000u;

            // Select clk_sys as AUX source and let clock framework set divider.
            hstx_ok = clock_configure(
                clk_hstx,
                0,
                CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS,
                sys_hz,
                target_hstx_hz);

            // Keep clk_peri in sync with clk_sys
            clock_configure(clk_peri,
                            0,
                            CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                            sys_hz,
                            sys_hz);
        }
#endif

        stdio_init_all();
        sleep_ms(50); // wait for UART to settle
#if HSTX
        if (!hstx_ok)
        {
            printf("HSTX clock configure failed\n");
        }
#endif
    }

    FRESULT pick_random_file_fullpath(const char *dirpath, char *out_path, size_t out_size)
    {
        static const int MAX_ITER = 504;
        FRESULT fr;
        DIR dir;
        FILINFO fno;
        UINT file_count = 0;
        int iter = 0;
        *out_path = 0;

        fr = f_opendir(&dir, dirpath);
        if (fr != FR_OK)
        {
            printf("Error: Unable to open directory: %s, error code: %d\n", dirpath, fr);
            return fr;
        }

        while (1)
        {

            fr = f_readdir(&dir, &fno);
            if (fr != FR_OK || fno.fname[0] == 0)
                break; // End or error
            if (fno.fattrib & AM_DIR)
                continue; // Skip directories
                          // skip files with wrong extension
            // skip hidden files
            if (fno.fattrib & AM_HID || fno.fname[0] == '.')
                continue;
            // Bail out if f_readdir loops too many. Can be caused by bad sd card?
            if (++iter > MAX_ITER)
            {
                printf("Error: Too many files in directory, aborting search.\n");
                f_closedir(&dir);
                return FR_INT_ERR;
            }
            // Check file extension
            int l = strlen(fno.fname);
            if (l > 4 && strcmp(&fno.fname[l - 4], FILEXTFORSEARCH) == 0)
            {
                // Found a file with the correct extension
            }
            else
            {
                continue;
            }
            file_count++;

            // Reservoir sampling: replace current choice with probability 1/file_count
            if ((rand() % file_count) == 0)
            {
                const char *name = fno.fname;
                // Build full path: "<dirpath>/<filename>"
                size_t dir_len = strlen(dirpath);
                if (dir_len + 1 + strlen(name) + 1 > out_size)
                {
                    // Output buffer too small, skip this file and continue
                    printf("Output buffer too small for path: %s/%s\n", dirpath, name);
                    continue;
                }

                strcpy(out_path, dirpath);
                if (dirpath[dir_len - 1] != '/' && dirpath[dir_len - 1] != '\\')
                {
                    strcat(out_path, "/");
                }
                strcat(out_path, name);
            }
        }
        if (file_count > 0)
        {
            // strcpy(out_path, "/Metadata/SMS/Images/160/0/00C34D94.444"); // For testing only
            // strcpy(out_path, "/Metadata/SMS/Images/160/0/0B1BA87F.444"); //
            printf("Picked random file: %s\n", out_path);
        }
        else
        {
            printf("No files found in directory: %s\n", dirpath);
        }
        f_closedir(&dir);

        return (file_count > 0) ? FR_OK : FR_NO_FILE;
    }

    /// @brief Load an overlay from file or from memory
    /// The overlay file must be a binary file with a 4 byte header followed by
    /// SCREENWIDTH * SCREENHEIGHT * 2 bytes of pixel data in 16 bit 555 or 444 format.
    /// The first two bytes of the header is the width (little endian)
    /// The next two bytes of the header is the height (little endian)
    /// The overlay is loaded into the framebuffer.
    /// If the file cannot be loaded, the overlay is loaded from memory.
    /// The overlay in memory must have the same format as the file.
    /// If both file and memory overlay are not available, no overlay is loaded.
    /// @param filename
    /// @param overlay
    void loadOverLay(const char *filename, const char *overlay)
    {
#if PICO_RP2350
        // Only possible when using framebuffer
        if (!Frens::isFrameBufferUsed())
        {
            return;
        }

        if (filename != nullptr)
        {
            FIL fil;
            FRESULT fr;
            size_t filesize;
            fr = f_open(&fil, filename, FA_READ);
            if (fr == FR_OK)
            {

                filesize = f_size(&fil);
                if (filesize < (4 + SCREENWIDTH * SCREENHEIGHT * sizeof(WORD)))
                {
                    printf("Overlay file %s too small: %d bytes\n", filename, filesize);
                    f_close(&fil);
                    return;
                }
                // Go past the 4 byte header
                f_lseek(&fil, 4);
                UINT br;
#if !HSTX
                fr = f_read(&fil, framebuffer, filesize - 4, &br);
#else
                fr = f_read(&fil, hstx_getframebuffer(), filesize - 4, &br);
#endif

                if (fr != FR_OK || br != filesize - 4)
                {
                    printf("Cannot read overlay file %s: %d/%d bytes read\n", filename, fr, br);
                    f_close(&fil);
                    return;
                }
                f_close(&fil);
                printf("Loaded overlay file %s: %d bytes\n", filename, br + 4);
                return;
            }
            else
            {
                printf("Cannot open overlay file %s: %d\n", filename, fr);
            }
        }
        if (overlay == nullptr)
        {
            printf("No overlay data provided\n");
            return;
        }
        // If we get here, we failed to load the overlay from file, try to load from memory
        uint16_t width = *((uint16_t *)overlay);
        // next two bytes is height
        uint16_t height = *((uint16_t *)(overlay + 2));
        if (width != SCREENWIDTH || height != SCREENHEIGHT)
        {
            printf("Overlay size %dx%d does not match screen size %dx%d\n", width, height, SCREENWIDTH, SCREENHEIGHT);
            return;
        }
        printf("Loading default overlay %dx%d\n", width, height);
#if !HSTX
        memcpy(framebuffer, overlay + 4, width * height * sizeof(WORD));
#else
        memcpy(hstx_getframebuffer(), overlay + 4, width * height * sizeof(WORD));
#endif
#endif
    }
    uint32_t getCrcOfLoadedRom()
    {
        return crcOfRom;
    }

    /// @brief Check if a file exists
    /// @param filepath
    /// @return
    bool fileExists(const char *filepath)
    {
        FILINFO fno;
        return (f_stat(filepath, &fno) == FR_OK);
    }

    void pollHeadPhoneJack()
    {
#if EXT_AUDIO_IS_ENABLED
        auto hpToggle = EXT_AUDIO_POLL_HEADPHONE();
        if (hpToggle != HP_TOGGLE_NONE)
        {
            extSpeakerEnabled = (hpToggle == HP_TOGGLE_CONNECT) ? true : false;
            // printf("Headphone toggle detected. Headphones %s\n", extSpeakerEnabled ? "unplugged, using speakers" : "plugged in, using headphones");
        }
#endif
    }

    bool isHeadPhoneJackConnected()
    {
        return extSpeakerEnabled;
    }
}
// C-compatible wrappers
extern "C"
{
    void *frens_f_malloc(size_t size)
    {
        return Frens::f_malloc(size);
    }

    void frens_f_free(void *ptr)
    {
        Frens::f_free(ptr);
    }

    void *frens_f_realloc(void *ptr, size_t newSize)
    {
        // Frens::f_realloc returns nullptr / panics when ptr is null, so handle
        // the malloc/free degenerate cases here for C callers that expect
        // standard realloc semantics.
        if (!ptr) return Frens::f_malloc(newSize);
        if (newSize == 0) { Frens::f_free(ptr); return nullptr; }
        return Frens::f_realloc(ptr, newSize);
    }
}