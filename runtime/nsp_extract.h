#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Check if a file is an NSP/XCI package
bool nsp_is_rom(const char *path);

// Find an NSP/XCI file in a given directory
bool nsp_find_in_dir(const char *dir, char *out_path, size_t out_path_sz);

// Decrypts and extracts ExeFS and RomFS from an NSP/XCI file into out_dir.
// Calls progress callback with fractional completion (0.0 to 1.0) and status message.
bool nsp_extract(const char *nsp_path, const char *out_dir, const char *keys_path,
                 void (*progress)(float pct, const char *msg));

// Extract directly from an open POSIX file descriptor (from Android ContentResolver)
bool nsp_extract_fd(int fd, const char *out_dir, const char *keys_path,
                    void (*progress)(float pct, const char *msg));

#ifdef __cplusplus
}
#endif
