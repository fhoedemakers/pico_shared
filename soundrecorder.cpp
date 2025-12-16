/**
 * @file soundrecorder.cpp
 * @brief Simple PCM recorder that buffers audio in RAM and writes a WAV file when full or stopped.
 *
 * This module accumulates 16-bit PCM frames in an in-memory buffer up to MAXBYTESTORECORD,
 * then writes a valid WAV file header followed by the PCM data to SOUNDRECORDERFILE using FatFs.
 *
 * Constraints:
 * - Fixed sample rate (SAMPLE_RATE).
 * - 16-bit samples, stereo (2 channels) expected by write_wav_header invocation.
 * - Max recording size is 2 MB (configurable via MAXBYTESTORECORD).
 *
 * Lifecycle:
 * - startRecording(): allocates buffer and begins recording.
 * - recordFrame(): appends PCM frames, auto-flushes and stops when buffer is full.
 * - isRecording(): reports current recording state.
 * - On flush, the buffer is freed and internal counters reset.
 *
 * Thread-safety:
 * - Not thread-safe; expected to be called from a single audio/logic context.
 *
 * Error handling:
 * - Prints diagnostic messages on file I/O errors.
 * - If allocation fails (pcmBuffer == nullptr), subsequent calls to recordFrame are no-ops.
 *
 * WAV output:
 * - RIFF/WAVE PCM with 44-byte header.
 * - Header fields set for channels=2, bits_per_sample=16, sample_rate=SAMPLE_RATE.
 */

#include "soundrecorder.h"
#include "ff.h"
#include "FrensHelpers.h"
#include <cstring>

#define MAXBYTESTORECORD 1024 * 1024 * 5 // 5 MB max recording size or available memory.
#define SAMPLE_RATE 44100

namespace SoundRecorder
{
    static bool recording = false;
    int16_t *pcmBuffer = nullptr;
    static size_t recordedBytes = 0;
    static FIL fil;
    static FRESULT fr;

    /**
     * @brief Write a 44-byte WAV header for PCM data.
     * @param f            Opened FatFs file handle (write mode).
     * @param data_bytes   Size of PCM payload in bytes.
     * @param channels     Number of audio channels (e.g., 2 for stereo).
     * @param sample_rate  Samples per second (Hz).
     * @param bits_per_sample Bits per sample (e.g., 16).
     *
     * Notes:
     * - Performs little-endian writes directly into a local header buffer.
     * - Does not close the file; caller retains ownership.
     */
    static void write_wav_header(FIL* f, uint32_t data_bytes, uint16_t channels,
                                 uint32_t sample_rate, uint16_t bits_per_sample) {
        uint8_t hdr[44] = {0};
        uint32_t byte_rate = sample_rate * channels * (bits_per_sample / 8);
        uint16_t block_align = channels * (bits_per_sample / 8);
        uint32_t riff_size = 36 + data_bytes;
        printf("Writing WAV header: data_bytes=%u, channels=%u, sample_rate=%u, bits_per_sample=%u\n",
               data_bytes, channels, sample_rate, bits_per_sample);
        memcpy(hdr + 0,  "RIFF", 4);
        *(uint32_t*)(hdr + 4)  = riff_size;      // little-endian
        memcpy(hdr + 8,  "WAVE", 4);
        memcpy(hdr + 12, "fmt ", 4);
        *(uint32_t*)(hdr + 16) = 16;             // PCM fmt chunk size
        *(uint16_t*)(hdr + 20) = 1;              // PCM format
        *(uint16_t*)(hdr + 22) = channels;
        *(uint32_t*)(hdr + 24) = sample_rate;
        *(uint32_t*)(hdr + 28) = byte_rate;
        *(uint16_t*)(hdr + 32) = block_align;
        *(uint16_t*)(hdr + 34) = bits_per_sample;
        memcpy(hdr + 36, "data", 4);
        *(uint32_t*)(hdr + 40) = data_bytes;

        UINT bw;
        f_write(f, hdr, sizeof(hdr), &bw);
        if (bw < sizeof(hdr)) {
            printf("Error writing WAV header: %u bytes written\n", bw);
        } else {
            printf("WAV header written: %u bytes\n", bw);
        }
    }

    /**
     * @brief Flush buffered PCM to SOUNDRECORDERFILE as a WAV and reset counters.
     *
     * Behavior:
     * - Opens/creates file, writes header and PCM, closes file.
     * - Logs errors on partial or failed writes.
     * - Leaves buffer allocation/frees to the caller (recordFrame handles free on full).
     */
    static void flush_to_file() {
        if (!pcmBuffer || recordedBytes == 0) return;

        fr = f_open(&fil, SOUNDRECORDERFILE, FA_WRITE | FA_CREATE_ALWAYS);
        if (fr == FR_OK) {
            UINT bw = 0;
            write_wav_header(&fil, (uint32_t)recordedBytes, 2, SAMPLE_RATE, 16);
            fr = f_write(&fil, pcmBuffer, (UINT)recordedBytes, &bw);
            f_close(&fil);
            if (fr != FR_OK || bw < (UINT)recordedBytes) {
                printf("Error writing sound recorder file: %d\n", fr);
            } else {
                printf("WAV written: %u bytes of PCM\n", bw);
            }
        } else {
            printf("Error opening sound recorder file: %d\n", fr);
        }
    }

    /**
     * @brief Returns whether recording is currently active.
     * @return true if startRecording() has set recording and buffer exists; false otherwise.
     */
    bool isRecording()
    {
        return recording;
    };

    /**
     * @brief Start a new recording session by allocating an internal buffer and resetting counters.
     *
     * Notes:
     * - Allocates up to MAXBYTESTORECORD bytes via Frens::f_malloc.
     * - If allocation fails, Frens:f_malloc causes a panic.
     */
    void startRecording()
    {
        printf("Starting sound recording, max size %d bytes\n", MAXBYTESTORECORD);
        recording = true;
        recordedBytes = 0;
        auto memsize = MAXBYTESTORECORD;
        auto available_mem = Frens::GetAvailableMemory();
        printf("Available memory before allocation: %zu bytes\n", available_mem);
        if ( available_mem < MAXBYTESTORECORD ) {
            
            memsize = available_mem - 10240; // leave some headroom
            printf("Warning: Not enough memory to allocate full recording buffer!\n");
            printf("Adjusting recording buffer size to %zu bytes\n", memsize);
        }
        pcmBuffer = (int16_t *)Frens::f_malloc(memsize);
    };

    /**
     * @brief Append a frame of PCM samples to the internal buffer.
     * @param pcmData    Pointer to interleaved 16-bit PCM samples.
     * @param numSamples Number of int16 samples to copy (not bytes).
     *
     * Behavior:
     * - Copies as many bytes as fit; if buffer fills, flushes to WAV, frees buffer, and stops.
     * - If not recording or buffer is nullptr, returns immediately.
     * - Assumes sample format compatible with write_wav_header settings (stereo, 16-bit).
     */
    void recordFrame(const int16_t *pcmData, size_t numSamples)
    {
        // Placeholder implementation
        if (!recording || pcmBuffer == nullptr)
        {
            return;
        }

        size_t bytesToCopy = numSamples * sizeof(int16_t);
        size_t available = (size_t)(MAXBYTESTORECORD)-recordedBytes;

        if (available == 0)
        {
            return;
        }

        size_t copyBytes = bytesToCopy <= available ? bytesToCopy : available;
        uint8_t *dst = reinterpret_cast<uint8_t *>(pcmBuffer) + recordedBytes;
        memcpy(dst, pcmData, copyBytes);
        recordedBytes += copyBytes;

        if (recordedBytes >= (size_t)(MAXBYTESTORECORD))
        {
            printf("Sound recorder buffer full, stopping recording.\n");
            printf("Recorded %zu bytes of audio data.\n", recordedBytes);
            printf("Writing sound recorder file %s\n", SOUNDRECORDERFILE);
            recording = false;
            flush_to_file();
            Frens::f_free(pcmBuffer);
            pcmBuffer = nullptr;
            recordedBytes = 0;
        }
    };
} // namespace SoundRecorder