#ifndef ROMSELECT
#define ROMSELECT
#include <stdint.h>
#include <stddef.h>
#define SWVERSION "VX.X"

#if PICO_RP2350
#if __riscv
#define PICOHWNAME_ "rp2350-riscv"
#else
#define PICOHWNAME_ "rp2350-arm"
#endif
#else
#define PICOHWNAME_ "rp2040"
#endif

#define SCREEN_COLS 40
#define SCREEN_ROWS 30

#define STARTROW 3
#define ENDROW (SCREEN_ROWS - 5)
#define PAGESIZE (ENDROW - STARTROW + 1)

#define VISIBLEPATHSIZE (SCREEN_COLS - 3)   
struct charCell
{
    uint8_t fgcolor;
    uint8_t bgcolor;
    char charvalue;
};
// Maximum number of save state slots
#define MAXSAVESTATESLOTS 6
#define SAVESTATEDIR "/SAVESTATES"
#define SLOTFORMAT SAVESTATEDIR "/%s/%08X/slot%d.sta"
#define QUICKSAVEFILEFORMAT SAVESTATEDIR "/%s/%08X/slot%d.sta"
#define AUTOSAVEFILEISCONFIGUREDFORMAT SAVESTATEDIR "/%s/%08X/AUTO.cfg"
#define AUTOSAVEFILEFORMAT SAVESTATEDIR "/%s/%08X/auto.sta"
#define RECORDEDSAMPLEFILE "/soundrecorder.wav"
#define DEFAULTSAMPLEFILEFORMAT "/Metadata/%s/sample.wav"
enum SaveStateTypes { NONE, SAVE, LOAD, SAVE_AND_EXIT, LOAD_AND_START };
extern charCell *screenBuffer;
#define screenbufferSize  (sizeof(charCell) * SCREEN_COLS * SCREEN_ROWS)
void menu(const char *title, char *errorMessage, bool isFatalError, bool showSplash, const char *allowedExtensions, char *rompath);
void ClearScreen(int color);
void putText(int x, int y, const char *text, int fgcolor, int bgcolor, bool wraplines = false, int offset = 0);
void splash();  // is emulator specific
int showSettingsMenu(bool calledFromGame = false);

// Optional FDS disk-swap hooks. The NES emulator wires these up to its
// FDS implementation at startup; other emulators leave it null and the
// menu hides the option via g_settings_visibility[MOPT_FDS_DISK_SWAP].
struct MenuFdsHooks
{
    int  (*get_swap_value)();          // 0..NumSides-1 for "Side N", NumSides for "Ejected"
    int  (*get_num_sides)();           // total disk sides
    void (*request_swap)(int newSide); // schedule eject + insert with newSide
    void (*request_eject)();           // hold disk ejected
};
void menuSetFdsHooks(const MenuFdsHooks *hooks);

// Optional cassette-deck hooks, same idea as MenuFdsHooks above. The TI-99/4A emulator
// wires these to its CS1/CS2 implementation; other emulators leave it null and the menu
// hides the option via g_settings_visibility[MOPT_CASSETTE].
//
// A tape carries no name of its own - SAVE CS1 supplies only the device - so recording
// asks for a label, which is what default_name / name_exists are for.
struct MenuCassetteHooks
{
    int  (*get_num_tapes)();                    // tapes found in the tape folder
    const char *(*get_tape_name)(int index);    // display name for 0..num-1
    int  (*get_selected)();                     // loaded tape, or -1 for none
    int  (*get_mode)();                         // 0 empty, 1 play, 2 record WAV, 3 record CAS
    void (*default_name)(char *buf, size_t n);  // pre-fills the label field
    int  (*name_exists)(const char *name, int mode);   // drives the overwrite confirm
    int  (*commit)(int index, int mode, const char *name);  // name only for record modes
    void (*rewind)();
    void (*refresh)();                          // rescan the tape folder
};
void menuSetCassetteHooks(const MenuCassetteHooks *hooks);

// Longest tape label the menu will show or let you type, plus the terminator.
#define TAPE_LABEL_MAX 25

// Put a tape in the deck because the console just asked for one. Called from the
// emulator's frame loop, not from the menu: SAVE CS1 / OLD CS1 name no file, so the
// choice can only be made at the moment the DSR starts reading or writing. Pass 1 to
// record (prompts for a label) or 0 to play (offers the tapes on the card). Allocates
// and frees screenBuffer itself, like showSettingsMenu(true).
bool menuCassettePrompt(int wantRecord);

// Modal single-line text entry, driven from the USB keyboard. `buf` is pre-filled with a
// default and edited in place; returns true if the user confirmed with ENTER, false if
// they cancelled with ESC. Requires a keyboard: callers should check
// io::getCurrentKeyboardState().connected first and fall back to the default if absent.
bool showTextEntry(const char *prompt, char *buf, size_t bufsize);

void menuPumpBlankFrames(int count);
bool showSaveStateMenu(int (*savestatefunc)(const char *path), int (*loadstatefunc)(const char *path), const char *extraMessage, SaveStateTypes quickSave);
void getButtonLabels(char *buttonLabel1, char *buttonLabel2);
void getQuickSavePath(char *path, size_t pathsize);
void getSaveStatePath(char *path, size_t pathsize, int slot);
void getAutoSaveStatePath(char *path, size_t pathsize);
bool isAutoSaveStateConfigured();
#endif