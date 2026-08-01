#ifndef ROMSELECT
#define ROMSELECT
#include <stdint.h>
#include <stddef.h>
// Pulled in here rather than left to the caller for MENU80COLS: splash.cpp
// includes menu.h *before* FrensHelpers.h, and a missing MENU80COLS would give
// that translation unit a different SCREEN_COLS than menu.cpp — i.e. a
// different grid stride for the same screenBuffer. FrensHelpers.h is guarded,
// so this is free everywhere it is already included first.
#include "FrensHelpers.h"
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

// Character-grid row stride, and the widest this build can ever display.
// Compile-time on purpose: screenBuffer's stride has to be constant so that
// every `row * SCREEN_COLS + col` index stays correct no matter how many
// columns are currently visible. The *visible* count is menuVisibleCols below.
#if MENU80COLS
#define SCREEN_COLS 80
#else
#define SCREEN_COLS 40
#endif
// Rows are unchanged at 80 columns: vertical line-doubling is still in play, so
// an 8x8 source cell scans out as 8x16 physical pixels — exactly the classic
// VGA 80x30 text cell. Every row constant below therefore holds for both widths.
#define SCREEN_ROWS 30

#define STARTROW 3
#define ENDROW (SCREEN_ROWS - 5)
#define PAGESIZE (ENDROW - STARTROW + 1)

// Columns currently visible: 40 or 80. Runtime, because the artwork and
// screensaver screens need the framebuffer back in 16bpp 320-wide mode, and
// because 80 columns is gated on PSRAM, which is probed at boot.
extern int menuVisibleCols;
// Set the visible column count and repaint. Clamps to 40 unless this is an
// 80-column build on a board that actually has PSRAM.
void menuSetColumns(int cols);
// Flip to 0 to allow 80 columns on HSTX boards without PSRAM. On by default
// because screenBuffer doubles to 7200 bytes, and Frens::f_malloc puts it in
// PSRAM when present — keeping it off the tight SRAM arena entirely.
#ifndef MENU80COLS_REQUIRE_PSRAM
#define MENU80COLS_REQUIRE_PSRAM 1
#endif

#define VISIBLEPATHSIZE (menuVisibleCols - 3)
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

void menuPumpBlankFrames(int count);
bool showSaveStateMenu(int (*savestatefunc)(const char *path), int (*loadstatefunc)(const char *path), const char *extraMessage, SaveStateTypes quickSave);
void getButtonLabels(char *buttonLabel1, char *buttonLabel2);
void getQuickSavePath(char *path, size_t pathsize);
void getSaveStatePath(char *path, size_t pathsize, int slot);
void getAutoSaveStatePath(char *path, size_t pathsize);
bool isAutoSaveStateConfigured();
#endif