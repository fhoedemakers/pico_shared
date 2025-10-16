#pragma once

#include <stdint.h>



// Computes the CRC32 checksum of a file, skipping the first 16 bytes.
// Returns 0 on success, -1 on error. The result is stored in crc_out.
uint32_t compute_crc32(const char* filename, int offset, FSIZE_t &romsize);
uint32_t compute_crc32_buffer(const void* data, size_t size, int offset );
uint32_t update_crc32(uint32_t crc, const uint8_t* data, UINT length) ;


