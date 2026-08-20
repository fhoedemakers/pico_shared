#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <algorithm>
#include "pico.h"
#include "RomLister.h"
#include "ff.h"
#include "ffwrappers.h"
#include "settings.h"

// class to listing directories and files for a given extension on sd card
namespace Frens
{
	// Buffer must have sufficient bytes to contain directory contents
	RomLister::RomLister(size_t _buffersize, const char *_allowedExtensions)
	{
		buffersize = _buffersize;
		allowedExtensions = _allowedExtensions;
		entries = nullptr;
		max_entries = buffersize / sizeof(RomEntry);
		const char *delimiters = ", ";
		// extensions = cstr_split(allowedExtensions, delimiters, &numberOfExtensions);
		pFile = (FILINFO *)Frens::f_malloc(sizeof(FILINFO));
		pDir = (DIR *)Frens::f_malloc(sizeof(DIR));
		pTemp = (RomEntry *)Frens::f_malloc(sizeof(RomEntry));
	}

	RomLister::~RomLister()
	{
		printf("Deconstructor RomLister\n");
		if (entries)
		{
			Frens::f_free(entries);
		}
		// if (extensions)
		// {
		// 	for (int i = 0; i < numberOfExtensions; i++)
		// 	{
		// 		Frens::f_free(extensions[i]);
		// 	}
		// 	Frens::f_free(extensions);
		// }
		Frens::f_free(pFile);
		Frens::f_free(pDir);
		Frens::f_free(pTemp);
	}

	RomLister::RomEntry *RomLister::GetEntries()
	{
		return entries;
	}

	char *RomLister::FolderName()
	{
		return directoryname;
	}
	size_t RomLister::Count()
	{
		return numberOfEntries;
	}

	bool RomLister::IsextensionAllowed(char *filename)
	{
		if (!allowedExtensions || strlen(allowedExtensions) == 0)
		{
			return true; // all extensions allowed
		}
		char extension[10];
		Frens::getextensionfromfilename(filename, extension, sizeof(extension));
		const char *start = allowedExtensions;
		const char *end;
		size_t ext_len = strlen(extension);

		while (1)
		{
			end = strchr(start, ' ');

			size_t token_len = end ? (size_t)(end - start)
								   : strlen(start);

			if (token_len == ext_len &&
				strncasecmp(start, extension, ext_len) == 0)
			{
				return true;
			}

			if (!end)
			{
				break;
			}

			start = end + 1;
		}
		return false;
	}
	void RomLister::list(const char *directoryName)
	{
		FRESULT fr;
		numberOfEntries = 0;
		// safer copy
		strncpy(directoryname, directoryName, sizeof(directoryname) - 1);
		directoryname[sizeof(directoryname) - 1] = '\0';

		// Fix: real empty check (old code compared pointer)
		if (directoryname[0] == '\0')
		{
			return;
		}

		if (entries == nullptr)
		{
			printf("Allocating %d bytes for directory contents\n", buffersize);
			entries = (RomEntry *)Frens::f_malloc(buffersize);
		}
		// Clear previous entries
		printf("chdir(%s)\n", directoryName);
		// for f_getcwd to work, set
		//   #define FF_FS_RPATH		2
		// in ffconf.c
		fr = f_chdir(directoryName);
		if (fr != FR_OK)
		{
			printf("Error changing dir to %s: %d\nTrying /", directoryName, fr);
			fr = f_chdir("/");
			if (fr != FR_OK)
			{
				printf("Error changing dir to /: %d\n", fr);
				return;
			}
		}
		printf("Listing current directory, reading maximum %d entries.\n", max_entries);
		uint availMem = Frens::GetAvailableMemory();
		printf("Available memory: %d bytes\n", availMem);
		f_opendir(pDir, ".");
		while (f_readdir(pDir, pFile) == FR_OK && pFile->fname[0])
		{
			if (numberOfEntries < max_entries)
			{
				if (pFile->fattrib & AM_HID || pFile->fname[0] == '.' ||
					strcasecmp(pFile->fname, "System Volume Information") == 0 ||
					strcasecmp(pFile->fname, "SAVES") == 0 ||
					strcasecmp(pFile->fname, "EDFC") == 0 ||
					strcasecmp(pFile->fname, "Metadata") == 0 ||
					strcasecmp(pFile->fname, "SAVESTATES") == 0 ||
					strcasecmp(pFile->fname, "BIOS") == 0)
				{
					continue;
				}
				if (strlen(pFile->fname) < ROMLISTER_MAXPATH)
				{
					RomEntry romInfo;
					strcpy(romInfo.Path, pFile->fname);
					romInfo.IsDirectory = pFile->fattrib & AM_DIR;
					if (!romInfo.IsDirectory)
					{
						if (IsextensionAllowed(romInfo.Path))
						{
							// Streamed CD-image extensions (.cue/.chd) are
							// read sector-by-sector from SD by the CD-ROM
							// emulator, never preloaded — exempt them from
							// the size check, which compares the file size
							// against available PSRAM and would otherwise
							// reject hundreds-of-MB CHDs that fit on SD fine.
							char ext[10];
							Frens::getextensionfromfilename(romInfo.Path, ext, sizeof(ext));
							const bool streamedExt =
								(strcasecmp(ext, ".cue") == 0) ||
								(strcasecmp(ext, ".chd") == 0);
							if (streamedExt || pFile->fsize < availMem)
							{
								entries[numberOfEntries++] = romInfo;
							}
							else
							{
								printf("Skipping %s, %d KBytes too large.\n", pFile->fname, (pFile->fsize - maxRomSize) / 1024);
							}
						} else {
							// always allow .wav files for wavplayer on RP2350
#if PICO_RP2350
							if ( Frens::cstr_endswith(romInfo.Path, ".wav") || Frens::cstr_endswith(romInfo.Path, ".WAV") ) {
								entries[numberOfEntries++] = romInfo;
							} 
#endif
						}
					}
					else
					{
						entries[numberOfEntries++] = romInfo;
					}
				}
				else
				{
					//#printf("Filename too long: %s\n", pFile->fname);
				}
			}
			else
			{
				printf("Skipping %s, maxentries %d reached\n", pFile->fname, max_entries);
				break;
			}
		}
		f_closedir(pDir);

		// PCE CD-ROM: a .cue or .chd in this folder means it's a CD game
		// folder; any .pce file here is presumed a per-game BIOS (loaded by
		// LoadDisc as a per-game override over /bios/), not a HuCard. Hide
		// it from the menu so it can't be picked accidentally. No-op for
		// folders without a CD image (other emulators / HuCard-only folders)
		// so it's safe to apply universally.
		{
			bool hasCue = false;
			for (size_t i = 0; i < numberOfEntries; i++) {
				if (entries[i].IsDirectory) continue;
				char ext[10];
				Frens::getextensionfromfilename(entries[i].Path, ext, sizeof(ext));
				if (strcasecmp(ext, ".cue") == 0 ||
				    strcasecmp(ext, ".chd") == 0) { hasCue = true; break; }
			}
			if (hasCue) {
				size_t write = 0;
				size_t hidden = 0;
				for (size_t read = 0; read < numberOfEntries; read++) {
					if (!entries[read].IsDirectory) {
						char ext[10];
						Frens::getextensionfromfilename(entries[read].Path, ext, sizeof(ext));
						// Hide both naming conventions for PC Engine system
						// BIOS files dropped alongside CD images: .pce ROM
						// dumps and the common "cd_bios.rom" name.
						if (strcasecmp(ext, ".pce") == 0 ||
						    strcasecmp(entries[read].Path, "cd_bios.rom") == 0) {
							hidden++;
							continue;
						}
					}
					if (write != read) entries[write] = entries[read];
					write++;
				}
				numberOfEntries = write;
				if (hidden > 0) {
					printf("RomLister: hiding %u .pce file(s) in CD folder %s "
					       "(presumed BIOS)\n",
					       (unsigned)hidden, directoryname);
				}
			}
		}

		// TI-99/4A: a cartridge is often several files - GAMEC.bin (ROM), GAMED.bin
		// (second bank), GAMEG.bin (GROM), GAME0.bin (console GROM replacement).
		// Selecting any one of them loads the whole set, so listing every part turns
		// one cartridge into three or four confusing menu entries. Hide the secondary
		// parts when their primary is present in the same folder; a GROM-only cartridge
		// has no primary and stays visible.
		//
		// Gated on the emulator type so this is a strict no-op everywhere else - other
		// emulators use .bin too.
		if (FrensSettings::getEmulatorTypeForSettings() == FrensSettings::emulators::TI99)
		{
			auto primary_exists = [&](const char *name, size_t len) -> bool {
				// name/len describe "<base>X.bin"; look for the same base with a
				// primary suffix. 8 and 9 are single-file multi-bank images.
				static const char primaries[] = { 'C', '8', '9' };
				char candidate[ROMLISTER_MAXPATH];
				if (len >= sizeof(candidate)) return false;
				for (size_t p = 0; p < sizeof(primaries); p++) {
					memcpy(candidate, name, len + 1);
					candidate[len - 5] = primaries[p];
					for (size_t i = 0; i < numberOfEntries; i++) {
						if (entries[i].IsDirectory) continue;
						if (strcasecmp(entries[i].Path, candidate) == 0) return true;
					}
				}
				return false;
			};

			size_t write = 0, hidden = 0;
			for (size_t read = 0; read < numberOfEntries; read++) {
				const char *name = entries[read].Path;
				size_t len = strlen(name);
				if (!entries[read].IsDirectory && len >= 6 &&
				    strcasecmp(name + len - 4, ".bin") == 0)
				{
					char part = (char)toupper((unsigned char)name[len - 5]);
					if ((part == 'D' || part == 'G' || part == '0') &&
					    primary_exists(name, len))
					{
						hidden++;
						continue;
					}
				}
				if (write != read) entries[write] = entries[read];
				write++;
			}
			numberOfEntries = write;
			if (hidden > 0) {
				printf("RomLister: hiding %u TI-99/4A cartridge part file(s) in %s\n",
				       (unsigned)hidden, directoryname);
			}
		}

		// Sort: directories first (case-insensitive), then files (case-insensitive)
		if (numberOfEntries > 1)
		{
			   std::stable_sort(entries, entries + numberOfEntries, [](const RomEntry &a, const RomEntry &b) {
				   if (a.IsDirectory != b.IsDirectory)
				   {
					   return a.IsDirectory && !b.IsDirectory; // directories come before files
				   }
				   return strcasecmp(a.Path, b.Path) < 0; // case-insensitive alphabetical
			   });
		}
		printf("Sort done (std::sort)\n");
	}
}
