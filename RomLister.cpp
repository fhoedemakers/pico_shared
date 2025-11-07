#include <stdio.h>
#include <string.h>
#include "pico.h"
#include "RomLister.h"
#include "ff.h"
#include "ffwrappers.h"

// class to listing directories and files for a given extension on sd card
namespace Frens
{
	// Buffer must have sufficient bytes to contain directory contents
	RomLister::RomLister(size_t _buffersize, const char *allowedExtensions)
	{
		buffersize = _buffersize;
		entries = nullptr;
		max_entries = buffersize / sizeof(RomEntry);
		const char *delimiters = ", ";
		extensions = cstr_split(allowedExtensions, delimiters, &numberOfExtensions);
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
		if (extensions)
		{
			for (int i = 0; i < numberOfExtensions; i++)
			{
				Frens::f_free(extensions[i]);
			}
			Frens::f_free(extensions);
		}
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
		if (numberOfExtensions == 0)
		{
			return true;
		}
		for (int i = 0; i < numberOfExtensions; i++)
		{
			if (Frens::cstr_endswith(filename, extensions[i]))
			{
				return true;
			}
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
			printf("Error changing dir: %d\n", fr);
			return;
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
					strcasecmp(pFile->fname, "Metadata") == 0)
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
							// availMem already computed earlier (unchanged)
							if (pFile->fsize < availMem)
							{
								entries[numberOfEntries++] = romInfo;
							}
							else
							{
								printf("Skipping %s, %d KBytes too large.\n", pFile->fname, (pFile->fsize - maxRomSize) / 1024);
							}
						}
					}
					else
					{
						entries[numberOfEntries++] = romInfo;
					}
				}
				else
				{
					printf("Filename too long: %s\n", pFile->fname);
				}
			}
			else
			{
				printf("Skipping %s, maxentries %d reached\n", pFile->fname, max_entries);
				break;
			}
		}
		f_closedir(pDir);
		// (bubble) Sort
		if (numberOfEntries > 1)
		{
			for (int pass = 0; pass < numberOfEntries - 1; ++pass)
			{
				for (int j = 0; j < numberOfEntries - 1 - pass; ++j)
				{
					int result = 0;
					if (entries[j].IsDirectory && entries[j + 1].IsDirectory == false)
					{
						continue;
					}
					bool swap = false;
					if (entries[j].IsDirectory == false && entries[j + 1].IsDirectory)
					{
						swap = true;
					}
					else
					{
						result = strcasecmp(entries[j].Path, entries[j + 1].Path);
					}
					if (swap || result > 0)
					{
						*pTemp = entries[j];
						entries[j] = entries[j + 1];
						entries[j + 1] = *pTemp;
					}
				}
			}
		}
		printf("Sort done\n");
	}
}
