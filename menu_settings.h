#pragma once
#include <stdint.h>
// Visibility-controlled menu settings for emulator configuration
// Each index corresponds to an option line when settings menu is opened via SELECT.
// Value 1 in g_option_visibility means the option is shown, 0 means hidden for current emulator.

enum MenuSettingsIndex {
    MOPT_EXIT_GAME = 0,
    MOPT_RESET_GAME,
    MOPT_REBOOT_TO_LOADER,   // Only shown when Frens::isLaunchedFromBootloader(): reboots to the emuLoader picker
    MOPT_SAVE_RESTORE_STATE,
    MOPT_SCREENMODE,
    MOPT_SCANLINES,
    MOPT_SCANLINE_TYPE,
    MOPT_FPS_OVERLAY,
    MOPT_AUDIO_ENABLE,
    MOPT_FRAMESKIP,
    MOPT_DISPLAY_MODE,
    MOPT_EXTERNAL_AUDIO,
    MOPT_FONT_COLOR,
    MOPT_FONT_BACK_COLOR,
    MOPT_FRUITJAM_VUMETER,
   // MOPT_FRUITJAM_INTERNAL_SPEAKER,
    MOPT_FRUITJAM_VOLUME_CONTROL,
    MOPT_DMG_PALETTE,
    MOPT_BORDER_MODE,
    MOPT_RAPID_FIRE_ON_A,
    MOPT_RAPID_FIRE_ON_B,
    MOPT_AUTO_INSERT_FDS_DISK_A, // Auto-insert disk side A at boot (On) or wait for user A press (Off)
    MOPT_AUTO_SWAP_FDS_DISK, // New menu option for automatically swapping FDS disk sides when loading a .fds file
    MOPT_FDS_DISK_SWAP,
    MOPT_OVERCLOCK,
    MOPT_FM_AUDIO,
    MOPT_ENTER_BOOTSEL_MODE,
    MOPT_COUNT
};
// Short description (max 40 chars) for each option. Use designated initializers
// so descriptions stay bound to their enum tag — reordering or inserting new
// MOPT_* values cannot silently shift the descriptions out of alignment.
const char* const g_settings_descriptions[MOPT_COUNT] = {
    [MOPT_EXIT_GAME]                = "Exit game and return to main menu",
    [MOPT_RESET_GAME]                = "Reset the currently running game",
    [MOPT_REBOOT_TO_LOADER]          = "Return to emulator selection menu",
    [MOPT_SAVE_RESTORE_STATE]        = "Save or load emulator state",
    [MOPT_SCREENMODE]                = "Screen scaling & scanline mode",
    [MOPT_SCANLINES]                 = "Toggle scanlines effect",
    [MOPT_SCANLINE_TYPE]             = "Scanline type (CRT/LCD style)",
    [MOPT_FPS_OVERLAY]               = "Show FPS (frame rate)",
    [MOPT_AUDIO_ENABLE]              = "Toggle game audio",
    [MOPT_FRAMESKIP]                 = "Skip frames for speed",
    [MOPT_DISPLAY_MODE]              = "HDMI or DVI",
    [MOPT_EXTERNAL_AUDIO]            = "Play audio over audio line out jack",
    [MOPT_FONT_COLOR]                = "Menu text color (0-63)",
    [MOPT_FONT_BACK_COLOR]           = "Menu background color (0-63)",
    [MOPT_FRUITJAM_VUMETER]          = "RGB LEDs show audio level (VU)",
    [MOPT_FRUITJAM_VOLUME_CONTROL]   = "Fruit Jam change volume (-63 to +23 dB)",
    [MOPT_DMG_PALETTE]               = "Color Palette for mono / DMG games",
    [MOPT_BORDER_MODE]               = "Select border artwork",
    [MOPT_RAPID_FIRE_ON_A]           = "Enable rapid fire for this button",
    [MOPT_RAPID_FIRE_ON_B]           = "Enable rapid fire for this button",
    [MOPT_AUTO_INSERT_FDS_DISK_A]    = "Insert disk at boot or stay in BIOS",
    [MOPT_AUTO_SWAP_FDS_DISK]        = "Auto swap disk side when game asks",
    [MOPT_FDS_DISK_SWAP]             = "Eject / insert FDS disk side",
    [MOPT_OVERCLOCK]                 = "Run CPU at high clock (reboots to apply)",
    [MOPT_FM_AUDIO]                  = "YM2413 FM sound (SMS, RP2350 only)",
    [MOPT_ENTER_BOOTSEL_MODE]        = "Reboot to BOOTSEL mode for flashing",
};

extern const int8_t *g_settings_visibility; // Visibility configuration for options menu


// Available screen modes for selection in settings menu
extern const uint8_t *g_available_screen_modes;
