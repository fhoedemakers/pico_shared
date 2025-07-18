#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "ffwrappers.h"

// This file contains some wrapper functions for the FatFs library:
// - my_chdir for f_chdir: Change and keep track of the current working directory.
// - my_getcwd for f_getcwd: Get the current tracked  working directory.
// These wrappers are written because when using exfat filesystem, f_getcwd always returns the root directory.
// See http://elm-chan.org/fsw/ff/doc/getcwd.html
// Using fat32, f_getcwd works as expected.
// The wrappers are used to track the current directory and normalize paths.

#define MAX_SEGMENTS 10 // Maximum number of segments in a path

// Normalize a path by resolving "..", ".", and duplicate slashes
// This function normalizes a given path by removing unnecessary components
// Example: "/home/../user//docs/./file.txt" becomes "/user/docs/file.txt"
// This function uses a stack to keep track of the components of the path
int normalize_path(const char *input, char *output, size_t output_size)
{
    const char *segments[MAX_SEGMENTS];
    int lengths[MAX_SEGMENTS];
    int top = 0;

    const char *p = input;

    while (*p == '/')
        p++; // Skip leading slashes

    while (*p)
    {
        const char *start = p;

        // Move to the end of this segment
        while (*p && *p != '/')
            p++;
        int len = p - start;

        if (len == 0 || (len == 1 && start[0] == '.'))
        {
            // skip
        }
        else if (len == 2 && start[0] == '.' && start[1] == '.')
        {
            if (top > 0)
                top--;
        }
        else
        {
            if (top >= MAX_SEGMENTS)
            {
                // Too many segments — prevent buffer overflow
                return -1;
            }
            segments[top] = start;
            lengths[top] = len;
            top++;
        }

        while (*p == '/')
            p++; // Skip duplicate slashes
    }

    // Build the output path
    char *out = output;
    size_t remaining = output_size;

    if (top == 0)
    {
        if (remaining < 2)
            return -1; // Need space for '/' and '\0'
        *out++ = '/';
        *out = '\0';
        return 0;
    }

    for (int i = 0; i < top; i++)
    {
        printf("Remaining: %d\n", remaining);
        if (remaining < 2)
            return -1; // Need at least '/' + '\0'
        *out++ = '/';
        remaining--;
        printf("Length[i]=%d, remaining=%d\n", lengths[i], remaining);
        if ((size_t)lengths[i] >= remaining)
            return -1; // Not enough space
        memcpy(out, segments[i], lengths[i]);
        out += lengths[i];
        remaining -= lengths[i];
    }

    *out = '\0';
    return 0;
}

static TCHAR current_dir[FF_MAX_LFN] = "/"; // Static variable to store the current directory
// Wrapper function for f_chdir that tracks the current directory
FRESULT my_chdir(const TCHAR *path)
{
#if 1
    TCHAR temp[FF_MAX_LFN];
    TCHAR normalized[FF_MAX_LFN];
    FRESULT fr;
    // Check if path is absolute
    if (path[0] == '/')
    {
        strncpy(temp, path, sizeof(temp) - 1);
        temp[sizeof(temp) - 1] = '\0'; // Ensure null termination
    }
    else
    {
        // If the path is relative, append it to the current directory
        snprintf(temp, sizeof(temp), "%s/%s", current_dir, path);
    }
    // Normalize the resulting path (resolve "..", ".", and duplicate slashes)
    if (normalize_path(temp, normalized, sizeof(normalized)) == 0)
    {
        // Normalization successful, proceed with changing directory
        // Call the actual f_chdir function
        if ((fr = f_chdir(normalized)) == FR_OK)
        {
            // Update the static variable if f_chdir succeeds
            strncpy(current_dir, normalized, sizeof(current_dir) - 1);
            current_dir[sizeof(current_dir) - 1] = '\0'; // Ensure null termination
        }
        printf("Changed directory to: %s\n", current_dir);
    }
    else
    {
        printf("Error normalizing path: %s\n", temp);
        fr = FR_INVALID_PARAMETER; // Return error if normalization fails
    }
    return fr;
#else
    return f_chdir(path); // Call the actual f_chdir function
#endif
}

// Function to retrieve the current directory
//
// my_getcwd is a wrapper for f_getcwd that returns the current tracked directory.
// This is necessary because on some filesystems (e.g., exFAT), f_getcwd always returns the root directory.
// The function uses a static variable (current_dir) to keep track of the current directory as changed by my_chdir.
// It copies the tracked directory to the provided buffer, ensuring null termination and checking for buffer size.
// Returns FR_OK on success, or FR_INVALID_PARAMETER if the buffer is too small.
const FRESULT my_getcwd(TCHAR *buffer, UINT len)
{
#if 1
    // char tempdir[FF_MAX_LFN];
    if (len < strlen(current_dir) + 1)
    {
        return FR_INVALID_PARAMETER; // Buffer too small
    }
    strncpy(buffer, current_dir, len);
    buffer[len - 1] = '\0'; // Ensure null termination
    // printf("Current tracked directory     : %s\n", buffer);
    // f_getcwd(tempdir, sizeof(tempdir));
    // printf("Directory reported by f_getcwd: %s\n", tempdir);
    return FR_OK;
#else
    return f_getcwd(buffer, len); // Call the actual f_getcwd function
    // Note: This may not work as expected with exFAT, as it always returns the root directory.
#endif
}