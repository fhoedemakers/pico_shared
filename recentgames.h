#pragma once

#include <stdint.h>
#include "ff.h"

// Recently-played games list (one per emulator) plus the record of which ROM
// image currently sits in XIP flash on boards without PSRAM.
//
// The list lives in the SD root as /recent_<EMU>.txt, one line per game, most
// recent first. It is plain text on purpose: the binary precedent here
// (settings.cpp) validates by exact file size and silently resets everything on
// a mismatch. A malformed text list degrades to "empty" instead, is repairable
// on a PC, and tolerates extra fields added by a later firmware.
//
// The flashed-ROM record (/flashedrom.dat) is binary: it is read by flashrom()
// before video, USB and core1 exist, holds only CRCs and an XIP address, and is
// never meant to be hand-edited.

#define RECENTGAMES_MAX 20
#define RECENTGAMES_MAXPATH (FF_MAX_LFN + 1)
#define RECENTGAMESFILE "/recent_%s.txt"
#define FLASHEDROMFILE "/flashedrom.dat"

namespace Frens
{
    namespace Recent
    {
        struct Entry
        {
            char path[RECENTGAMES_MAXPATH]; // absolute, exactly what f_open gets
            char emu[6];                    // getEmulatorTypeString() at launch time
            uint32_t crc;                   // savestate/artwork key (informational)
            uint32_t size;                  // rom file size in bytes
        };

        // ~5.5 KB. Always heap-allocated by load() - never a stack local, the
        // menu call chain runs close to PICO_STACK_SIZE (3 KB).
        struct List
        {
            uint8_t count;
            Entry items[RECENTGAMES_MAX];
        };

        // Reads /recent_<EMU>.txt. A missing, empty or malformed file yields an
        // empty list rather than a failure. Returns nullptr only when the
        // allocation failed. Release with free().
        List *load();
        void free(List *list);

        // Most-recently-used insert: drops any existing entry with the same path
        // (case-insensitive, FAT is case-insensitive), puts the new one first,
        // caps the list at RECENTGAMES_MAX and rewrites the file. Allocates and
        // releases its own List, so callers need not hold one.
        bool add(const char *fullPath, const char *emu, uint32_t crc, uint32_t size);

        // Drops items[index] from the list in memory and rewrites the file, so
        // the caller's List stays in sync without reloading.
        bool removeAt(List *list, int index);

        // Filename part of the entry path. Points into e.path, no allocation.
        const char *displayName(const Entry &e);

        // ---- flashed-ROM record (boards without PSRAM) ----

        // Describes the ROM image currently programmed into XIP flash. Written
        // after a verified-complete flash, deleted before the first erase, so a
        // record that exists always describes complete flash content.
        struct FlashedRomRecord
        {
            uint32_t magic;
            uint32_t romFileAddr;   // ROM_FILE_ADDR the image was written to
            uint32_t size;          // rom file size == bytes covered by crcFlashImage
            uint32_t crcPreSwap;    // CRC of the SD file; restores crcOfRom on the skip path
            uint32_t crcFlashImage; // CRC over the image as written to flash
            uint16_t fdate;         // f_stat of the source file when it was flashed
            uint16_t ftime;
            uint8_t byteSwapped; // the swapbytes flag used while writing
            uint8_t crcOffset;   // offset used for crcPreSwap (16 for NES, else 0)
            char emu[6];         // getEmulatorTypeString(true) of the writer
            char path[RECENTGAMES_MAXPATH];
        };

        bool readFlashedRomRecord(FlashedRomRecord *out);
        // Stamps the magic itself, so a caller cannot forget it.
        bool writeFlashedRomRecord(FlashedRomRecord *rec);
        void invalidateFlashedRomRecord();

        // Cheap identity check - magic, emulator, load address, byte-swap flag,
        // path, and the source file's size and modification time. No flash read
        // and no CRC, so it is safe to call from the menu.
        bool flashedRomMatches(const FlashedRomRecord *rec, const char *fullPath, bool swapbytes);

        // Index in list of the entry whose image is in flash, or -1. Always -1
        // when PSRAM is enabled (there is no flashed ROM then).
        int flashedIndex(const List *list);
    }
}
