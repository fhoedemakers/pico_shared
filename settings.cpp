#include "settings.h"
#include <stdio.h>
#include <string.h>
#include <strings.h> // for strcasecmp
#include "vumeter.h"
struct settings settings;
namespace FrensSettings
{
    #define SETTINGSFILE "/settings_%s.dat" // File to store settings
    static const char *emulatorstrings[5] = { "NES", "SMS", "GB", "MD", "MUL" };
    static char settingsFileName[21] = {};
    static emulators emulatorTypeForSettings = emulators::MULTI;
    char *getSettingsFileName()
    {
        if (settingsFileName[0] == '\0')
        {
            snprintf(settingsFileName, sizeof(settingsFileName), SETTINGSFILE, emulatorstrings[static_cast<int>(emulatorTypeForSettings)]);
        }
        return settingsFileName;
    }
    void initSettings(emulators emu)
    {
        emulatorType = emulatorTypeForSettings = emu;
        // pick a random initial emulator type to show artwork in the menu
        if ( emu == emulators::MULTI ) {
            emulatorType = emulators::NES;
        }   
        loadsettings();
    }
    void setEmulatorType(const char * fileextension)
    {
        if (strcasecmp(fileextension, ".nes") == 0)
        {
            if ( emulatorType == emulators::NES ) return;
            emulatorType = emulators::NES;
        }
        else if (strcasecmp(fileextension, ".sms") == 0 || strcasecmp(fileextension, ".gg") == 0)
        {
            if ( emulatorType == emulators::SMS ) return;
            emulatorType = emulators::SMS;
        }
        else if (strcasecmp(fileextension, ".gb") == 0 || strcasecmp(fileextension, ".gbc") == 0)
        {
            if ( emulatorType == emulators::GAMEBOY ) return;
            emulatorType = emulators::GAMEBOY;
        }
        else if (strcasecmp(fileextension, ".gen") == 0 || strcasecmp(fileextension, ".md") == 0|| strcasecmp(fileextension, ".bin") == 0)
        {
           
            if ( emulatorType == emulators::GENESIS ) return;
            emulatorType = emulators::GENESIS;
        }
        else
        {
            if ( emulatorType == emulators::MULTI ) return;
            emulatorType = emulators::MULTI;
        }
        printf("Detected ROM extension: %s\n", fileextension);
        //snprintf(settingsFileName, sizeof(settingsFileName), SETTINGSFILE, emulatorstrings[static_cast<int>(emulatorType)]);
        // loadsettings();
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
        printf("scanlineOn: %d\n", settings.flags.scanlineOn);
        printf("fruitJamEnableInternalSpeaker: %d\n", settings.flags.fruitJamEnableInternalSpeaker);
        printf("fruitjamVolumeLevel: %d\n", settings.fruitjamVolumeLevel);
        printf("rapidFireOnA: %d\n", settings.flags.rapidFireOnA);
        printf("rapidFireOnB: %d\n", settings.flags.rapidFireOnB);
        printf("\n");
    }
    void resetsettings(struct settings *settingsPtr)
    {
        struct settings &settings = settingsPtr ? *settingsPtr : ::settings;
        // Reset settings to default
        printf("Resetting settings\n");
        settings.screenMode = (emulatorType == emulators::NES) ? ScreenMode::SCANLINE_8_7 : ScreenMode::NOSCANLINE_1_1;
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
        settings.flags.frameSkip = 1; // default: frame skipping enabled (Genesis needs it)
        settings.flags.fruitJamEnableInternalSpeaker = 1; // default: enable Fruit Jam internal speaker
        settings.flags.rapidFireOnA = 0; // default: rapid fire off
        settings.flags.rapidFireOnB = 0; // default: rapid fire off
        settings.version = SETTINGS_VERSION;
        // clear all the reserved settings
        settings.flags.reserved = 0;
        settings.fruitjamVolumeLevel = 16; // default volume level in db to mid (0-24)
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
    // Get the current emulator type, this may be different from emulatorTypeForSettings when in multi-emulator mode
    emulators getEmulatorType()
    {
        return emulatorType;
    }
    // Get the current emulator type as a string, this may be different from emulatorTypeForSettings when in multi-emulator mode
    const char *getEmulatorTypeString(bool forSettings)
    {
        if (forSettings)
        {
            return emulatorstrings[static_cast<int>(emulatorTypeForSettings)];
        }
        return emulatorstrings[static_cast<int>(emulatorType)];
    }
    
    // Get the current emulator type for settings, used in multi-emulator mode
    emulators getEmulatorTypeForSettings()
    {
        return emulatorTypeForSettings;
    }
}
