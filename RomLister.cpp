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
		strcpy(directoryname, directoryName);
		FILINFO file;
		RomEntry tempEntry;
		if (directoryname == "")
		{
			return;
		}
		if (entries == nullptr)
		{
			printf("Allocating %d bytes for directory contents\n", buffersize);
			entries = (RomEntry *)Frens::f_malloc(buffersize);
		}
		// Clear previous entries
		DIR dir;
		printf("chdir(%s)\n", directoryName);
		// for f_getcwd to work, set
		//   #define FF_FS_RPATH		2
		// in ffconf.c
		fr = my_chdir(directoryName); // f_chdir(directoryName);
		if (fr != FR_OK)
		{
			printf("Error changing dir: %d\n", fr);
			return;
		}
		printf("Listing current directory, reading maximum %d entries.\n", max_entries);
		uint availMem = Frens::GetAvailableMemory();
		printf("Available memory: %d bytes\n", availMem);
		f_opendir(&dir, ".");
		while (f_readdir(&dir, &file) == FR_OK && file.fname[0])
		{
			if (numberOfEntries < max_entries)
			{
				if (file.fname[0] == '.' ||
					strcasecmp(file.fname, "System Volume Information") == 0 ||
					strcasecmp(file.fname, "SAVES") == 0 ||
					strcasecmp(file.fname, "EDFC") == 0 ||
					strcasecmp(file.fname, "Metadata") == 0)
				{
					// skip hidden files and directories like .git, .config, .Trash, also "." and ".."
					// printf("Skipping hidden file or directory %s\n", file.fname);
					continue;
				}
				if (strlen(file.fname) < ROMLISTER_MAXPATH)
				{
					struct RomEntry romInfo;
					strcpy(romInfo.Path, file.fname);
					romInfo.IsDirectory = file.fattrib & AM_DIR;
					// if (!romInfo.IsDirectory && Frens::cstr_endswith(romInfo.Path, ".nes"))
					if (!romInfo.IsDirectory)
					{
						if (IsextensionAllowed(romInfo.Path))
						{
							if (file.fsize < availMem)
							{
								entries[numberOfEntries++] = romInfo;
							}
							else
							{
								printf("Skipping %s, %d KBytes too large.\n", file.fname, (file.fsize - maxRomSize) / 1024);
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
					printf("Filename too long: %s\n", file.fname);
				}
			}
			else
			{
				if (numberOfEntries == max_entries)
				{
					printf("Max entries reached.\n");
				}
				printf("Skipping %s\n", file.fname);
			}
		}
		f_closedir(&dir);
		// (bubble) Sort
		if (numberOfEntries > 1)
		{
			for (int pass = 0; pass < numberOfEntries - 1; ++pass)
			{
				for (int j = 0; j < numberOfEntries - 1 - pass; ++j)
				{
					int result = 0;
					// Directories first in the list
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
						tempEntry = entries[j];
						entries[j] = entries[j + 1];
						entries[j + 1] = tempEntry;
					}
				}
			}
		}
		printf("Sort done\n");
	}
}
