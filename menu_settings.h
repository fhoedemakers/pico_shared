#pragma once
#include <stdint.h>
// Visibility-controlled menu settings for emulator configuration
// Each index corresponds to an option line when settings menu is opened via SELECT.
// Value 1 in g_option_visibility means the option is shown, 0 means hidden for current emulator.

enum MenuSettingsIndex {
    MOPT_EXIT_GAME = 0,
    MOPT_SCREENMODE,
    MOPT_SCANLINES,
    MOPT_FPS_OVERLAY,
    MOPT_AUDIO_ENABLE,
    MOPT_FRAMESKIP,
    MOPT_EXTERNAL_AUDIO,
    MOPT_FONT_COLOR,
    MOPT_FONT_BACK_COLOR,
    MOPT_FRUITJAM_VUMETER,
    MOPT_FRUITJAM_INTERNAL_SPEAKER,
    MOPT_DMG_PALETTE,
    MOPT_BORDER_MODE,
    MOPT_RAPID_FIRE_ON_A,
    MOPT_RAPID_FIRE_ON_B,
    MOPT_COUNT
};
// Create and initialize an array which explains each option in a short description of max 40 characters
const char* const g_settings_descriptions[MOPT_COUNT] = {
    "Exit game and return to main menu",
    "Screen scaling & scanline mode",
    "Toggle scanlines effect",
    "Show FPS (frame rate)",
    "Toggle game audio",
    "Skip frames for speed",
    "Play audio over audio line out jack",
    "Menu text color (0-63)",
    "Menu background color (0-63)",
    "RGB LEDs show audio level (VU)",
    "Enable Fruit Jam speaker",
    "Color Palette for mono / DMG games",
    "Select border artwork",
    "Enable rapid fire for this button",
    "Enable rapid fire for this button"
};

extern const uint8_t g_settings_visibility[MOPT_COUNT];

// Available screen modes for selection in settings menu
extern const uint8_t g_available_screen_modes[];
