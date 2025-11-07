#include "settings.h"
#include <stdio.h>
#include <string.h>
#include "vumeter.h"
struct settings settings;
namespace FrensSettings
{
    #define SETTINGSFILE "/settings_%s.dat" // File to store settings
    static const char *emulatorstrings[4] = { "NES", "SMS", "GB", "MD" };
    static char settingsFileName[21] = {};

    char *getSettingsFileName()
    {
        if (settingsFileName[0] == '\0')
        {
            snprintf(settingsFileName, sizeof(settingsFileName), SETTINGSFILE, emulatorstrings[static_cast<int>(emulatorType)]);
        }
        return settingsFileName;
    }
    void initSettings(emulators emu)
    {
        emulatorType = emu;
    }
    void printsettings()
    {
        printf("Settings Version: %d\n", settings.version);
        printf("ScreenMode: %d\n", (int)settings.screenMode);
        printf("firstVisibleRowINDEX: %d\n", settings.firstVisibleRowINDEX);
        printf("selectedRow: %d\n", settings.selectedRow);
        printf("horzontalScrollIndex: %d\n", settings.horzontalScrollIndex);
        printf("currentDir: %s\n", settings.currentDir);
        printf("fgcolor: %d\n", settings.fgcolor);
        printf("bgcolor: %d\n", settings.bgcolor);
        printf("useExtAudio: %d\n", settings.flags.useExtAudio);
        printf("enableVUMeter: %d\n", settings.flags.enableVUMeter);
        printf("borderMode: %d\n", settings.flags.borderMode);
        printf("dmgLCDPalette: %d\n", settings.flags.dmgLCDPalette);
        printf("audioEnabled: %d\n", settings.flags.audioEnabled);
        printf("displayFrameRate: %d\n", settings.flags.displayFrameRate);
        printf("frameSkip: %d\n", settings.flags.frameSkip);
        printf("\n");
    }
    void resetsettings(struct settings *settingsPtr)
    {
        struct settings &settings = settingsPtr ? *settingsPtr : ::settings;
        // Reset settings to default
        printf("Resetting settings\n");
        settings.screenMode = {};
        settings.firstVisibleRowINDEX = 0;
        settings.selectedRow = 0;
        settings.horzontalScrollIndex = 0;
        settings.fgcolor = DEFAULT_FGCOLOR;
        settings.bgcolor = DEFAULT_BGCOLOR;
        settings.flags.useExtAudio = 0; // default to use DVIAudio
        settings.flags.enableVUMeter = ENABLE_VU_METER ? 1 : 0; // default to ENABLE_VU_METER
        settings.flags.borderMode = THEMEDBORDER;
        settings.flags.dmgLCDPalette = 0; // default DMG LCD Palette, DMG Green
        settings.flags.audioEnabled = 1; // audio on by default
        settings.flags.displayFrameRate = 0; // default: do not show FPS overlay
        settings.flags.frameSkip = 0; // default: frame skipping disabled
        settings.version = SETTINGS_VERSION;
        // clear all the reserved settings
        settings.flags.reserved = 0;
        strcpy(settings.currentDir, "/");
    }

    void savesettings()
    {
        // Save settings to file
        FIL fil;
        UINT bw;
        FRESULT fr;
        printf("Saving settings to %s\n", getSettingsFileName());
        fr = f_open(&fil, getSettingsFileName(), FA_WRITE | FA_CREATE_ALWAYS);
        if (fr == FR_OK)
        {
            fr = f_write(&fil, &settings, sizeof(settings), &bw);
            if (fr)
            {
                printf("Error writing %s: %d\n", getSettingsFileName(), fr);
            }
            else
            {
                printf("Wrote %d bytes to %s\n", bw, getSettingsFileName());
            }
            f_close(&fil);
        }
        else
        {
            printf("Error opening %s: %d\n", getSettingsFileName(), fr);
        }
        printsettings();
    }

    void loadsettings()
    {
        FIL fil;
        UINT br;
        FRESULT fr;
        DIR dir;
        FILINFO fno;
        // Load settings from file
        printf("Loading settings\n");
        // determine size of settings file
        fr = f_stat(getSettingsFileName(), &fno);
        if (fr == FR_OK)
        {
            printf("Size of %s: %lu bytes\n", getSettingsFileName(), fno.fsize);
            if (fno.fsize != sizeof(settings))
            {
                printf("Size of %s is not %d bytes, resetting settings\n", getSettingsFileName(), sizeof(settings));
                resetsettings();
                savesettings();
            }
            else
            {
                fr = f_open(&fil, getSettingsFileName(), FA_READ);
                if (fr == FR_OK)
                {
                    fr = f_read(&fil, &settings, sizeof(settings), &br);
                    if (fr)
                    {
                        printf("Error reading %s: %d\n", getSettingsFileName(), fr);
                        resetsettings();
                    }
                    else
                    {
                        // If file is corrupt, reset settings to default
                        if (br != sizeof(settings)  || settings.version != SETTINGS_VERSION)
                        {
                            if (settings.version != SETTINGS_VERSION) {
                                printf("File %s has wrong version %d, expected %d\n", getSettingsFileName(), settings.version, SETTINGS_VERSION);
                            } else {
                                printf("File %s is corrupt, expected %d bytes, read %d\n", getSettingsFileName(), sizeof(settings), br);
                            }
                            resetsettings();
                            savesettings();
                        }
                        else
                        {
                            printf("Read %d bytes from %s\n", br, getSettingsFileName());
                        }
                    }
                    f_close(&fil);
                }
                else
                {
                    // If file does not exist, reset settings to default
                    if (fr == FR_NO_FILE)
                    {
                        printf("File %s does not exist\n", getSettingsFileName());
                    }
                    else
                    {
                        printf("Error opening %s: %d\n", getSettingsFileName(), fr);
                    }
                    resetsettings();
                }

                // if settings.currentDir is no valid directory, reset settings to default
                if (f_opendir(&dir, settings.currentDir) != FR_OK)
                {
                    printf("Directory %s does not exist\n", settings.currentDir);
                    resetsettings();
                }
            }
        }
        else
        {
            printf("Settings file not found %s: %d\n", getSettingsFileName(), fr);
            resetsettings();
        }
        printsettings();
    }
}
