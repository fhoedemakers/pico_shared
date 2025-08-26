
#include <stdio.h>
#include <cstring>
#include "pico.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/rand.h"
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "util/exclusive_proc.h"
#include "FrensHelpers.h"
#if CFG_TUH_RPI_PIO_USB && PICO_RP2350
#include "bsp/board_api.h"
#include "board.h"
#include "pio_usb.h"
#endif
#include "tusb.h"
#include "hardware/dma.h"

#include "nespad.h"
#include "wiipad.h"
#include "settings.h"

#include "PicoPlusPsram.h"
// Pico W devices use a GPIO on the WIFI chip for the LED,
// so when building for Pico W, CYW43_WL_GPIO_LED_PIN will be defined
// NOTE: Building for Pico2 W makes the emulator not work: ioctl timeouts and red flicker
#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

#ifndef DVIAUDIOFREQ
#define DVIAUDIOFREQ 44100
#endif
#if !HSTX
std::unique_ptr<dvi::DVI> dvi_;
util::ExclusiveProc exclProc_;
volatile bool vsync = false;
#endif
char ErrorMessage[ERRORMESSAGESIZE];
bool scaleMode8_7_ = true;
uintptr_t ROM_FILE_ADDR = 0;
int maxRomSize = 0;

namespace Frens
{
    static FATFS fs;
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

#endif
    WORD *framebuffer;
    static bool usingFramebuffer = false;
    bool psRamEnabled = false;
    size_t psramMemorySize = 0;
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
#if !HSTX
    bool __not_in_flash_func(isFrameBufferUsed)()
    {
        return usingFramebuffer;
    }
#endif
#define STORAGE_CMD_DUMMY_BYTES 1
#define STORAGE_CMD_DATA_BYTES 3
#define STORAGE_CMD_TOTAL_BYTES (STORAGE_CMD_DUMMY_BYTES + STORAGE_CMD_DATA_BYTES)
    uint storage_get_flash_capacity()
    {
        uint8_t txbuf[STORAGE_CMD_TOTAL_BYTES] = {0x9f};
        uint8_t rxbuf[STORAGE_CMD_TOTAL_BYTES] = {0};
        auto irq = save_and_disable_interrupts();
        flash_do_cmd(txbuf, rxbuf, STORAGE_CMD_TOTAL_BYTES);
        restore_interrupts(irq);

        return 1 << rxbuf[3];
    }

    /// @brief Wait for vertical sync
    void waitForVSync()
    {
#if !HSTX
        if (Frens::isFrameBufferUsed())
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
    /// @brief Poor way to pace frames to 60fps
    /// @param init
    void PaceFrames60fps(bool init)
    {
#if !HSTX
        if (Frens::isFrameBufferUsed())
        {
            while (vsync == false)
            {
                // busy wait
                tight_loop_contents();
            }
        }
#else
        static absolute_time_t next_frame_time = {0};
        if (init)
        {
            next_frame_time = make_timeout_time_us(0); // Reset frame time to 0
        }
        // Adjust to about 60fps
        if (to_us_since_boot(next_frame_time) == 0)
        {
            next_frame_time = make_timeout_time_us(0);
        }
        // Pace to 60fps
        sleep_until(next_frame_time);
        next_frame_time = delayed_by_us(next_frame_time, 16666); // 1/60s = 16666us
#endif
    }

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

        // Create a modifiable copy of the input string
        char *str_copy = strdup(str);
        if (str_copy == NULL)
        {
            *count = 0;
            return NULL;
        }

        // Initial memory allocation for the result array
        int capacity = INITIAL_CAPACITY;
        char **result = (char **)malloc(capacity * sizeof(char *));
        if (result == NULL)
        {
            free(str_copy);
            *count = 0;
            return NULL;
        }

        *count = 0;
        char *token = strtok(str_copy, delimiters);
        while (token != NULL)
        {
            // Skip empty tokens
            if (*token != '\0')
            {
                // Reallocate if necessary
                if (*count >= capacity)
                {
                    capacity *= 2;
                    char **temp = (char **)realloc(result, capacity * sizeof(char *));
                    if (temp == NULL)
                    {
                        // Memory allocation failed, clean up
                        for (int i = 0; i < *count; ++i)
                        {
                            free(result[i]);
                        }
                        free(result);
                        free(str_copy);
                        *count = 0;
                        return NULL;
                    }
                    result = temp;
                }

                // Allocate memory for the token and copy it
                result[*count] = strdup(token);
                if (result[*count] == NULL)
                {
                    // Memory allocation failed, clean up
                    for (int i = 0; i < *count; ++i)
                    {
                        free(result[i]);
                    }
                    free(result);
                    free(str_copy);
                    *count = 0;
                    return NULL;
                }
                (*count)++;
            }
            token = strtok(NULL, delimiters);
        }

        free(str_copy);
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

    // print an int16 as binary
    void printbin16(int16_t v)
    {
        for (int i = 15; i >= 0; i--)
        {
            printf("%d", (v >> i) & 1);
        }
    }

    // End of variuos helper functions

    // Initialize the SD card
    bool initSDCard()
    {
        FRESULT fr;
        TCHAR str[40];
        sleep_ms(1000);

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
        }
        else
        {
            // fall back to PIO SPI
            pico_fatfs_config_spi_pio(SDCARD_PIO, pio_claim_unused_sm(SDCARD_PIO, true));
            printf("using SPI PIO...");
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
        printf("%10lu KiB (%7.2f GB) total drive space.\n%10lu KiB available.\n", tot_sect / 2, fstemp->csize * fstemp->n_fatent * 512E-9, fre_sect / 2);
        fr = my_chdir("/"); // f_chdir("/");
        if (fr != FR_OK)
        {
            snprintf(ErrorMessage, ERRORMESSAGESIZE, "Cannot change dir to / : %d", fr);
            printf("%s\n", ErrorMessage);
            return false;
        }
        // for f_getcwd to work, set
        //   #define FF_FS_RPATH		2
        // in drivers/fatfs/ffconf.h
        fr = my_getcwd(str, sizeof(str));
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
#if !HSTX
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
            // case ScreenMode::MAX:
            //     scaleMode8_7_ = false;
            //     scanLine = false;
            //     printf("ScreenMode::MAX\n");
            //     break;
        }

        dvi_->setScanLine(scanLine);
        return scaleMode8_7_;
    }

    bool screenMode(int incr)
    {
        bool scaleMode8_7_;
        settings.screenMode = static_cast<ScreenMode>((static_cast<int>(settings.screenMode) + incr) & 3);
        scaleMode8_7_ = Frens::applyScreenMode(settings.screenMode);
        savesettings();
        return scaleMode8_7_;
    }
#endif
    void toggleScanLines()
    {
#if !HSTX
#else
        settings.scanlineOn = settings.scanlineOn != 0 ? 0 : 1; // toggle scanlineOn
        savesettings();
        hstx_setScanLines(settings.scanlineOn);
        printf("Scanlines %s\n", settings.scanlineOn ? "enabled" : "disabled");
#endif
    }
    void restoreScanlines()
    {
#if !HSTX
#else
        hstx_setScanLines(settings.scanlineOn > 0);
#endif
        printf("Restoring scanlines: %s\n", settings.scanlineOn ? "enabled" : "disabled");
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
                panic("Cannot allocate %zu bytes in PSRAM\n", size);
            }
            printf("Allocated %zu bytes in PSRAM at %p\n", size, pMem);
            return pMem;
        }
#endif
        // PSRAM not enabled, use malloc
        void *pMem = malloc(size); // panics if unavailable
        printf("Allocated %zu bytes in RAM at %p\n", size, pMem);
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
            printf("Freeing %zu bytes from PSRAM at %p\n", uFreeing, pMem);
            psram_.Free(pMem);
            return;
        }
#endif
        // PSRAM not enabled, use free
        if (pMem)
        {
            printf("Freeing memory at %p\n", pMem);
            free(pMem);
        }
    }

    void *flashromtoPsram(char *selectdRom, bool swapbytes, uint32_t &crc)
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
        void *pMem = Frens::f_malloc(filesize);
        if (!pMem)
        {
            snprintf(ErrorMessage, 40, "Cannot allocate %d bytes in PSRAM\n", filesize);
            printf("%s\n", ErrorMessage);
            selectdRom[0] = 0;
            f_close(&fil);
            return nullptr;
        }
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
                if ((crc = compute_crc32_buffer(pMem, filesize)) > 0)
                {
                    printf("CRC32 checksum of %s in PSRAM: %08X\n", selectdRom, crc);
                }
                else
                {
                    printf("Error calculating CRC32 checksum of %s in PSRAM\n", selectdRom);
                }
                if (swapbytes)
                {
                    // swap bytes in pMem
                    for (size_t i = 0; i < filesize; i += 2)
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
        }
        else
        {
            snprintf(ErrorMessage, 40, "Cannot open %s:%d\n", ROMINFOFILE, fr);
            printf(ErrorMessage);
        }
        f_close(&fil);
        if (selectedRom[0] != 0)
        {
            printf("Starting (%d) %s\n", strlen(selectedRom), selectedRom);
            printf("Checking for /START file. (Is start pressed in Menu?)\n");
            fr = f_unlink("/START");
            if (fr == FR_NO_FILE)
            {
                printf("Start not pressed, flashing rom.\n");
#if PICO_RP2040
                size_t bufsize = 64 * 1024;
#else
                size_t bufsize = 128 * 1024;
#endif
                BYTE *buffer = (BYTE *)malloc(bufsize); // (BYTE *)InfoNes_GetPPURAM(&bufsize);
                auto ofs = ROM_FILE_ADDR - XIP_BASE;
                printf("Writing rom %s to flash %x\n", selectedRom, ofs);
                UINT totalBytes = 0;
#if 0
                int blockCount=0;
#endif
                fr = f_open(&fil, selectedRom, FA_READ);
                bool onOff = true;
                UINT bytesRead;
                if (fr == FR_OK)
                {
                    UINT filesize = f_size(&fil);
                    printf("Filesize: %d bytes (%dKB)\n", filesize, filesize / 1024);
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
                                if (swapbytes)
                                {
                                    for (int i = 0; i < bytesRead; i += 2)
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
                                // Disable interupts, erase, flash and enable interrupts
                                uint32_t ints = save_and_disable_interrupts();
                                flash_range_erase(ofs, bufsize);
                                flash_range_program(ofs, buffer, bufsize);
                                restore_interrupts(ints);
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
                            printf("Wrote %d bytes to flash\n", totalBytes);
                            if (totalBytes != filesize)
                            {
                                snprintf(ErrorMessage, 40, "Size mismatch: %d != %d\n", totalBytes, filesize);
                                printf("%s\n", ErrorMessage);
                                selectedRom[0] = 0;
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
                free(buffer);
                printf("Flashing done\n");
            }
            else
            {
                if (fr != FR_OK)
                {
                    snprintf(ErrorMessage, 40, "Cannot delete /START file %d", fr);
                    printf("%s\n", ErrorMessage);
                    selectedRom[0] = 0;
                }
                else
                {
                    printf("Start pressed in menu, not flashing rom.\n");
                }
            }
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
            dvi_->waitForValidLine();

            dvi_->start();
            while (!exclProc_.isExist())
            {
                if (scaleMode8_7_)
                {
                    // Default
                    dvi_->convertScanBuffer12bppScaled16_7(34, 32, 288 * 2);
                    // dvi_->convertScanBuffer12bppScaled16_7(0,0 , 320 * 2);
                    //  34 + 252 + 34
                    //  32 + 576 + 32
                }
                else
                {
                    //
                    dvi_->convertScanBuffer12bpp();
                }
            }

            dvi_->unregisterIRQThisCore();
            dvi_->stop();

            exclProc_.processOrWaitIfExist();
        }
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
        int frame = 0;

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
#elif defined(CYW43_WL_GPIO_LED_PIN) && !USE_I2S_AUDIO
        // Because of a conflict with the i2s audio, there is no led support when using i2s audio
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
#elif defined(CYW43_WL_GPIO_LED_PIN) && !USE_I2S_AUDIO
        // For Pico W devices we need to initialize the driver
        // Because of a conflict with the i2s audio, there is no led support when using i2s audio
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
#if WII_PIN_SDA >= 0 and WII_PIN_SCL >= 0
        wiipad_begin();
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
        dvi_->setAudioFreq(DVIAUDIOFREQ, 28000, 6272);
        // dvi_->setAudioFreq(53267, 28000, 6272);

        dvi_->allocateAudioBuffer(audioBufferSize);
        //    dvi_->setExclusiveProc(&exclProc_);

        dvi_->getBlankSettings().top = marginTop * 2;
        dvi_->getBlankSettings().bottom = marginBottom * 2;
        // dvi_->setScanLine(true);
        // 空サンプル詰めとく
        dvi_->getAudioRingBuffer().advanceWritePointer(255);
#else
        hstx_init();
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
        bool ok = false;
        int rc = initLed();
        if (rc != PICO_OK)
        {
            printf("Error initializing LED: %d\n", rc);
        }
        // Init PSRAM if available, otherwise use flash memory to store roms.
        if (initPsram() == false)
        {
            auto flashcap = storage_get_flash_capacity();
            // Calculate the address in flash where roms will be stored
            printf("Flash binary start    : 0x%08x\n", &__flash_binary_start);
            printf("Flash binary end      : 0x%08x\n", &__flash_binary_end);
            // printf("Flash size in bytes   :   %8d (%d)Kbytes\n", PICO_FLASH_SIZE_BYTES, PICO_FLASH_SIZE_BYTES / 1024);
            printf("Flash size in bytes   :   %8d (%d Kbytes)\n", flashcap, flashcap / 1024);
            // uint8_t *flash_end = (uint8_t *)&__flash_binary_start + PICO_FLASH_SIZE_BYTES - 1;
            uint8_t *flash_end = (uint8_t *)&__flash_binary_start + flashcap - 1;
            printf("Flash end             : 0x%08x\n", flash_end);
            printf("Size program in flash :   %8d bytes (%d) Kbytes\n", &__flash_binary_end - &__flash_binary_start, (&__flash_binary_end - &__flash_binary_start) / 1024);
            // round ROM_FILE_ADDRESS address up to 4k boundary of flash_binary_end
            ROM_FILE_ADDR = ((uintptr_t)&__flash_binary_end + 0xFFF) & ~0xFFF;
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
        resetsettings();
        if (initSDCard())
        {
            ok = true;
            loadsettings();
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
                flashrom(selectedRom, swapbytes);
            }
        }
#if !HSTX && FRAMEBUFFERISPOSSIBLE
        usingFramebuffer = useFrameBuffer;
        if (usingFramebuffer)
        {
            // always allocate framebuffer in SRAM
            framebuffer = (WORD *)malloc(SCREENWIDTH * SCREENHEIGHT * sizeof(WORD));
            memset(framebuffer, 0, SCREENWIDTH * SCREENHEIGHT * sizeof(WORD));
            marginTop = marginBottom = 0; // ignore margins when using framebuffer
        }
#endif // DVI
        initDVandAudio(marginTop, marginBottom, audiobufferSize);
        // init USB driver
        // USB driver is initalized after display driver to prevent the display driver
        // from using the PIO state machines already claimed by the USB driver.
        // This is only needed for the PIO USB driver.
#if CFG_TUH_RPI_PIO_USB && PICO_RP2350
        printf("Using PIO USB.\n");
        pio_usb_board_init();
        tusb_rhport_init_t host_init = {
            .role = TUSB_ROLE_HOST,
            .speed = TUSB_SPEED_AUTO};
        tusb_init(BOARD_TUH_RHPORT, &host_init);

        if (board_init_after_tusb)
        {
            board_init_after_tusb();
        }
#else
        printf("Using internal USB.\n");
        tusb_init();
#endif
#if !HSTX
#if FRAMEBUFFERISPOSSIBLE
        if (usingFramebuffer)
        {

            multicore_launch_core1(coreFB_main);
        }
        else
        {
            multicore_launch_core1(core1_main);
        }
#else
        multicore_launch_core1(core1_main);
#endif
#endif // DVI
        initVintageControllers(CPUFreqKHz);
        // TODO: DMA chan 1-3 are used for PIO0, chan 4-7 for PIO1, Assuming PIO1 is used for audio.
        EXT_AUDIO_SETUP(USE_I2S_AUDIO, DVIAUDIOFREQ, GetUnUsedDMAChan(4)); // Initialize external audio if needed
        srand(get_rand_32());                                              // Seed the random number generator with a random value
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
}