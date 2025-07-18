#ifndef FFWRAPPERS_DEFINED
#define FFWRAPPERS_DEFINED
#ifdef __cplusplus
extern "C" {
#endif
#include "ff.h" // Include the FatFs library header

int normalize_path(const char *input, char *output, size_t output_size);
FRESULT my_chdir(const TCHAR *path);
FRESULT my_getcwd(TCHAR *buffer, UINT len);

#ifdef __cplusplus
}
#endif

#endif  