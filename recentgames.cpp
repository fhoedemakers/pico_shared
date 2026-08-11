#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // strcasecmp
#include "recentgames.h"
#include "FrensHelpers.h"
#include "settings.h"

namespace Frens
{
    namespace Recent
    {
        // First line of the list file. Anything else means "not our format" and
        // the list is treated as empty; the next add() rewrites it.
        static const char *MAGICLINE = "#PICORECENT1";
        static const uint32_t FLASHEDROM_MAGIC = 0x314D5246u; // "FRM1"

        // Longest line: emu(5) + crc(8) + size(8) + 3 separators + path + LF + NUL
        #define LINEBUFSIZE (RECENTGAMES_MAXPATH + 32)

        // Built fresh on every call rather than cached: in a multi-emulator
        // build the emulator identity can change while the menu is open.
        static void listFileName(char *buf, size_t bufsize)
        {
            snprintf(buf, bufsize, RECENTGAMESFILE, FrensSettings::getEmulatorTypeString(true));
        }

        // Parses one "<emu>|<crc>|<size>|<path>" line in place (the separators
        // are overwritten with NULs). Returns false for anything malformed.
        static bool parseLine(char *line, Entry *e)
        {
            size_t n = strlen(line);
            while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            {
                line[--n] = 0;
            }
            if (n == 0 || line[0] == '#')
            {
                return false;
            }
            char *p1 = strchr(line, '|');
            if (!p1)
                return false;
            char *p2 = strchr(p1 + 1, '|');
            if (!p2)
                return false;
            char *p3 = strchr(p2 + 1, '|');
            if (!p3)
                return false;
            *p1 = *p2 = *p3 = 0;
            const char *emu = line;
            const char *path = p3 + 1;
            // The path is the last field and runs to end of line, so a '|' can
            // never need escaping - it is not a legal FAT/exFAT filename char.
            if (path[0] != '/' || strlen(path) >= RECENTGAMES_MAXPATH)
            {
                return false;
            }
            strncpy(e->emu, emu, sizeof(e->emu) - 1);
            e->emu[sizeof(e->emu) - 1] = 0;
            e->crc = (uint32_t)strtoul(p1 + 1, nullptr, 16);
            e->size = (uint32_t)strtoul(p2 + 1, nullptr, 16);
            strcpy(e->path, path);
            return true;
        }

        List *load()
        {
            List *list = (List *)Frens::f_malloc(sizeof(List));
            if (!list)
            {
                printf("Recent: cannot allocate %u bytes for list\n", (unsigned)sizeof(List));
                return nullptr;
            }
            list->count = 0;

            char fname[32];
            listFileName(fname, sizeof(fname));

            FIL *fil = (FIL *)Frens::f_malloc(sizeof(FIL));
            char *line = (char *)Frens::f_malloc(LINEBUFSIZE);
            if (!fil || !line)
            {
                // Out of memory for the I/O scratch: hand back an empty list
                // rather than a failure, the menu still works without history.
                Frens::f_free(fil);
                Frens::f_free(line);
                return list;
            }

            if (f_open(fil, fname, FA_READ) == FR_OK)
            {
                if (f_gets(line, LINEBUFSIZE, fil) && strncmp(line, MAGICLINE, strlen(MAGICLINE)) == 0)
                {
                    while (list->count < RECENTGAMES_MAX && f_gets(line, LINEBUFSIZE, fil))
                    {
                        if (parseLine(line, &list->items[list->count]))
                        {
                            list->count++;
                        }
                    }
                }
                else
                {
                    printf("Recent: %s has no valid header, starting a new list\n", fname);
                }
                f_close(fil);
            }
            Frens::f_free(fil);
            Frens::f_free(line);
            return list;
        }

        void free(List *list)
        {
            Frens::f_free(list);
        }

        // Rewrites the whole file. At <= ~5 KB this is a single cluster write
        // and it keeps on-disk order identical to display order.
        static bool save(const List *list)
        {
            char fname[32];
            listFileName(fname, sizeof(fname));

            FIL *fil = (FIL *)Frens::f_malloc(sizeof(FIL));
            char *line = (char *)Frens::f_malloc(LINEBUFSIZE);
            if (!fil || !line)
            {
                Frens::f_free(fil);
                Frens::f_free(line);
                return false;
            }

            bool ok = false;
            FRESULT fr = f_open(fil, fname, FA_WRITE | FA_CREATE_ALWAYS);
            if (fr == FR_OK)
            {
                UINT bw;
                int len = snprintf(line, LINEBUFSIZE, "%s\n", MAGICLINE);
                ok = (f_write(fil, line, len, &bw) == FR_OK && bw == (UINT)len);
                for (int i = 0; ok && i < list->count; i++)
                {
                    const Entry &e = list->items[i];
                    len = snprintf(line, LINEBUFSIZE, "%s|%08X|%08X|%s\n",
                                   e.emu, (unsigned)e.crc, (unsigned)e.size, e.path);
                    ok = (f_write(fil, line, len, &bw) == FR_OK && bw == (UINT)len);
                }
                f_close(fil);
                if (!ok)
                {
                    printf("Recent: error writing %s\n", fname);
                }
            }
            else
            {
                printf("Recent: cannot create %s: %d\n", fname, fr);
            }
            Frens::f_free(fil);
            Frens::f_free(line);
            return ok;
        }

        bool add(const char *fullPath, const char *emu, uint32_t crc, uint32_t size)
        {
            if (!fullPath || fullPath[0] == 0)
            {
                return false;
            }
            if (strlen(fullPath) >= RECENTGAMES_MAXPATH)
            {
                printf("Recent: path too long, not added: %s\n", fullPath);
                return false;
            }

            List *list = load();
            if (!list)
            {
                return false;
            }

            // Drop any existing entry for the same file. FAT is case
            // insensitive, so /ROMS/x.md and /roms/x.md are the same game.
            int w = 0;
            for (int r = 0; r < list->count; r++)
            {
                if (strcasecmp(list->items[r].path, fullPath) != 0)
                {
                    if (w != r)
                    {
                        list->items[w] = list->items[r];
                    }
                    w++;
                }
            }
            list->count = (uint8_t)w;

            // Make room at the front, discarding the oldest when full.
            int keep = (list->count < RECENTGAMES_MAX) ? list->count : RECENTGAMES_MAX - 1;
            for (int i = keep; i > 0; i--)
            {
                list->items[i] = list->items[i - 1];
            }
            Entry &e = list->items[0];
            strcpy(e.path, fullPath);
            strncpy(e.emu, emu ? emu : "", sizeof(e.emu) - 1);
            e.emu[sizeof(e.emu) - 1] = 0;
            e.crc = crc;
            e.size = size;
            list->count = (uint8_t)(keep + 1);

            printf("Recent: %s (crc %08X, %u bytes)\n", fullPath, (unsigned)crc, (unsigned)size);
            bool ok = save(list);
            free(list);
            return ok;
        }

        bool removeAt(List *list, int index)
        {
            if (!list || index < 0 || index >= list->count)
            {
                return false;
            }
            for (int i = index; i + 1 < list->count; i++)
            {
                list->items[i] = list->items[i + 1];
            }
            list->count--;
            return save(list);
        }

        const char *displayName(const Entry &e)
        {
            const char *slash = strrchr(e.path, '/');
            return slash ? slash + 1 : e.path;
        }

        // ---- flashed-ROM record ----

        bool readFlashedRomRecord(FlashedRomRecord *out)
        {
            if (!out)
            {
                return false;
            }
            FIL *fil = (FIL *)Frens::f_malloc(sizeof(FIL));
            if (!fil)
            {
                return false;
            }
            bool ok = false;
            if (f_open(fil, FLASHEDROMFILE, FA_READ) == FR_OK)
            {
                UINT br = 0;
                if (f_size(fil) == sizeof(FlashedRomRecord) &&
                    f_read(fil, out, sizeof(FlashedRomRecord), &br) == FR_OK &&
                    br == sizeof(FlashedRomRecord) &&
                    out->magic == FLASHEDROM_MAGIC)
                {
                    ok = true;
                }
                f_close(fil);
            }
            Frens::f_free(fil);
            return ok;
        }

        bool writeFlashedRomRecord(FlashedRomRecord *rec)
        {
            if (!rec)
            {
                return false;
            }
            rec->magic = FLASHEDROM_MAGIC;
            FIL *fil = (FIL *)Frens::f_malloc(sizeof(FIL));
            if (!fil)
            {
                return false;
            }
            bool ok = false;
            FRESULT fr = f_open(fil, FLASHEDROMFILE, FA_WRITE | FA_CREATE_ALWAYS);
            if (fr == FR_OK)
            {
                UINT bw = 0;
                ok = (f_write(fil, rec, sizeof(FlashedRomRecord), &bw) == FR_OK &&
                      bw == sizeof(FlashedRomRecord));
                f_close(fil);
            }
            if (!ok)
            {
                printf("Recent: cannot write %s: %d\n", FLASHEDROMFILE, fr);
            }
            Frens::f_free(fil);
            return ok;
        }

        void invalidateFlashedRomRecord()
        {
            // Deleted before the first erase, so a record that survives always
            // describes complete flash content - a power cut mid-flash cannot
            // leave a record that lies.
            f_unlink(FLASHEDROMFILE);
        }

        bool flashedRomMatches(const FlashedRomRecord *rec, const char *fullPath, bool swapbytes)
        {
            if (!rec || !fullPath || fullPath[0] == 0)
            {
                return false;
            }
            if (rec->magic != FLASHEDROM_MAGIC)
            {
                return false;
            }
            if (rec->romFileAddr != (uint32_t)ROM_FILE_ADDR)
            {
                printf("Recent: flashed rom was loaded at %08X, now %08X\n",
                       (unsigned)rec->romFileAddr, (unsigned)ROM_FILE_ADDR);
                return false;
            }
            if (rec->byteSwapped != (swapbytes ? 1 : 0))
            {
                return false;
            }
            if (strcasecmp(rec->emu, FrensSettings::getEmulatorTypeString(true)) != 0)
            {
                printf("Recent: flash holds a %s rom, this is %s\n",
                       rec->emu, FrensSettings::getEmulatorTypeString(true));
                return false;
            }
            if (strcasecmp(rec->path, fullPath) != 0)
            {
                return false;
            }

            // Size and modification time catch the case the flash CRC cannot:
            // the user replaced the file on the card with a different game
            // under the same name.
            FILINFO *fno = (FILINFO *)Frens::f_malloc(sizeof(FILINFO));
            if (!fno)
            {
                return false;
            }
            bool ok = false;
            if (f_stat(fullPath, fno) == FR_OK)
            {
                ok = ((uint32_t)fno->fsize == rec->size &&
                      fno->fdate == rec->fdate &&
                      fno->ftime == rec->ftime);
                if (!ok)
                {
                    printf("Recent: %s changed on the card since it was flashed\n", fullPath);
                }
            }
            Frens::f_free(fno);
            return ok;
        }

        int flashedIndex(const List *list)
        {
            if (!list || list->count == 0 || Frens::isPsramEnabled())
            {
                return -1;
            }
            FlashedRomRecord *rec = (FlashedRomRecord *)Frens::f_malloc(sizeof(FlashedRomRecord));
            if (!rec)
            {
                return -1;
            }
            int idx = -1;
            // Path, size and load address only - no f_stat per entry. This tag
            // is informational; flashrom() does the authoritative check (including
            // a CRC of the image itself) before it decides to skip a re-flash.
            if (readFlashedRomRecord(rec) &&
                rec->romFileAddr == (uint32_t)ROM_FILE_ADDR &&
                strcasecmp(rec->emu, FrensSettings::getEmulatorTypeString(true)) == 0)
            {
                for (int i = 0; i < list->count; i++)
                {
                    if (rec->size == list->items[i].size &&
                        strcasecmp(rec->path, list->items[i].path) == 0)
                    {
                        idx = i;
                        break;
                    }
                }
            }
            Frens::f_free(rec);
            return idx;
        }
    }
}
