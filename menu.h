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
bool showSaveStateMenu(int (*savestatefunc)(const char *path), int (*loadstatefunc)(const char *path), const char *extraMessage, SaveStateTypes quickSave);
void getQuickSavePath(char *path, size_t pathsize);
void getSaveStatePath(char *path, size_t pathsize, int slot);
void getAutoSaveStatePath(char *path, size_t pathsize);
bool isAutoSaveStateConfigured();
#endif