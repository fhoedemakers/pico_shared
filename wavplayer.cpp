/**
 * @file wavplayer.cpp
 * @brief Simple WAV player. RP2350 only.
 *
 * This module provides a minimal stereo WAV player that can stream
 * audio either from an embedded memory buffer (generated in soundrecorder.cpp)
 * or from a file on the FAT filesystem. The player supports continuous
 * looping with a configurable start offset in seconds.
 *
 * Key characteristics:
 * - Expects PCM format: RIFF/WAVE, `fmt` = PCM (1), 2 channels.
 * - Supported bit depths:
 *   - Memory source: 16‑bit PCM.
 *   - File source: 16‑bit PCM or 24‑bit PCM (down‑converted to 16‑bit).
 * - Streams frames into the external audio queue via `EXT_AUDIO_ENQUEUE_SAMPLE`.
 * - For memory source: loops over the embedded buffer.
 * - For file source: reads chunked frames and loops by seeking to an offset.
 * - Offset is applied in frames based on detected sample rate.
 *
 * Dependencies:
 * - `external_audio.h` for audio enqueue.
 * - `ff.h` (FatFs) for file IO.
 * - `settings.h` to check `audioEnabled`.
 *
 * Threading/real‑time notes:
 * - `pump()` should be called regularly from the audio update loop to keep
 *   the queue filled. It is not re‑entrant.
 * - Functions in this module are not thread‑safe.
 *
 * Royalty free music pack: https://lonepeakmusic.itch.io/retro-midi-music-pack-1
 */
#include "wavplayer.h"

#if PICO_RP2350

#include "FrensHelpers.h"
#include "external_audio.h"
#include "ff.h"
#include "settings.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

// Generated in soundrecorder.cpp
extern const unsigned char samplesound_wav[];

namespace wavplayer
{

    /**
     * @brief Source kind for WAV playback.
     * - `Memory`: Embedded buffer `samplesound_wav`.
     * - `File`: Streamed from a file via FatFs.
     */
    enum class WavSrcKind : uint8_t
    {
        Memory,
        File
    };

    /**
     * @brief Internal playback state (not exposed in the header).
     *
     * Fields cover both memory and file streaming modes. When `kind` is
     * `Memory`, file‑related fields are ignored; when `kind` is `File`, memory
     * pointer is ignored.
     */
    struct WavState
    {
        // Common
        uint32_t sample_rate;  //!< Sample rate in Hz.
        uint16_t bits_per;     //!< Bits per sample in source (16 or 24 for file; 16 for memory).
        uint32_t frames_total; //!< Total frames (informational for memory).
        uint32_t frame_pos;    //!< Current frame index.
        uint32_t start_frame;  //!< Loop start frame computed from `offset_sec`.
        bool ready;            //!< True when the player is configured.
        float offset_sec;      //!< Loop start offset in seconds (>= 0).
        WavSrcKind kind;       //!< Active source kind.

        float duration_sec;    //!< Total duration of the track in seconds.

        // Memory source
        const int16_t *pcm_mem; //!< Pointer to interleaved stereo 16‑bit samples.

        // File source
        FIL fil;                   //!< FatFs file handle.
        bool fileIsOpen;           //!< Whether a file is currently open.
        uint32_t data_start;       //!< Byte offset of `data` payload start.
        uint32_t data_bytes;       //!< Size of `data` payload in bytes.
        uint32_t bytes_per_frame;  //!< Bytes per frame (4 for stereo 16‑bit).
        uint32_t stream_pos_bytes; //!< Current byte offset within data chunk.

        int16_t gain_q15; //!< Master gain in Q15 (0..32767). Default -6 dB (16384).

        bool isplaying; //!< Whether playback is paused.
    };

    static WavState g_wav{};
      // Read chunked frames from file (max 256 frames, up to 6 bytes/frame)
    #define WAVPLAYER_MAX_READ_BYTES (256 * 6)
    static uint8_t *buf = nullptr; //[256 * 6];

    /** @brief Load little‑endian 16‑bit from byte pointer. */
    static inline uint16_t mw_le16(const unsigned char *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
    /** @brief Load little‑endian 32‑bit from byte pointer. */
    static inline uint32_t mw_le32(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

    // Add: detect WAVE_FORMAT_EXTENSIBLE with PCM SubFormat
    static inline bool is_wave_extensible_pcm(const unsigned char *fmt_base, uint32_t fmt_size)
    {
        // Need: fmt chunk size >= 40, cbSize >= 22, and SubFormat == KSDATAFORMAT_SUBTYPE_PCM
        if (fmt_size < 40) return false;
        uint16_t cbSize = mw_le16(&fmt_base[16]);
        if (cbSize < 22) return false;
        static const uint8_t pcm_guid[16] = {
            0x01,0x00,0x00,0x00, 0x00,0x00,0x10,0x00,
            0x80,0x00,0x00,0xAA, 0x00,0x38,0x9B,0x71
        };
        for (int i = 0; i < 16; ++i)
            if (fmt_base[24 + i] != pcm_guid[i]) return false;
        return true;
    }

    /**
     * @brief Convert little-endian signed 24-bit PCM to signed 16-bit.
     * Input points to 3 bytes (LSB..MSB). Performs sign extension then >> 8.
     */
    static inline int16_t pcm24_to_s16(const uint8_t *p)
    {
        int32_t v = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16));
        if (v & 0x00800000)
            v |= 0xFF000000; // sign-extend from 24-bit
        return (int16_t)(v >> 8);
    }

    // Apply master gain with saturation.
    static inline int16_t apply_gain(int16_t s)
    {
        int32_t v = (int32_t)s * (int32_t)g_wav.gain_q15; // Q15 multiply
        v >>= 15;
        if (v > 32767) v = 32767;
        else if (v < -32768) v = -32768;
        return (int16_t)v;
    }

    /**
     * @brief Apply `offset_sec` converting to frames and seek/reset positions.
     *
     * For memory source:
     * - Computes `start_frame` modulo total frames and sets `frame_pos`.
     *
     * For file source:
     * - Computes `stream_pos_bytes` within the `data` chunk, seeks file to
     *   `data_start + stream_pos_bytes`, and sets `frame_pos` accordingly.
     */
    static void apply_offset()
    {
        if (!g_wav.ready)
            return;
        if (g_wav.sample_rate == 0)
            return;

        uint64_t start_frames = (uint64_t)(g_wav.offset_sec * (float)g_wav.sample_rate + 0.5f);
        if (g_wav.kind == WavSrcKind::Memory)
        {
            if (g_wav.frames_total == 0)
                return;
            g_wav.start_frame = (uint32_t)(start_frames % g_wav.frames_total);
            g_wav.frame_pos = g_wav.start_frame;
        }
        else
        { // File
            uint64_t start_bytes = start_frames * g_wav.bytes_per_frame;
            if (g_wav.data_bytes == 0)
                return;
            start_bytes %= g_wav.data_bytes;
            g_wav.stream_pos_bytes = (uint32_t)start_bytes;
            g_wav.frame_pos = (uint32_t)(start_bytes / g_wav.bytes_per_frame);
            g_wav.start_frame = g_wav.frame_pos;
            f_lseek(&g_wav.fil, g_wav.data_start + g_wav.stream_pos_bytes);
        }
    }

    /**
     * @brief Set the loop start offset in seconds and apply immediately.
     * @param seconds Offset in seconds; values < 0 are clamped to 0.
     */
    void set_offset_seconds(float seconds)
    {
        g_wav.offset_sec = seconds < 0.0f ? 0.0f : seconds;
        apply_offset();
    }

    // Optional: runtime volume control (0.0 .. 1.0). Defaults to 0.5 in inits.
    void set_volume_linear(float vol)
    {
        if (vol < 0.0f) vol = 0.0f;
        if (vol > 1.0f) vol = 1.0f;
        g_wav.gain_q15 = (int16_t)(vol * 32767.0f + 0.5f);
        printf("WAV player volume set to %.3f (Q15=%d)\n", vol, g_wav.gain_q15);
    }

    /**
     * @brief Initialize playback from embedded `samplesound_wav` buffer.
     *
     * Parses the RIFF/WAVE header from the in‑memory buffer, validates that the
     * stream is PCM, stereo, 16‑bit, and sets up internal state to loop the
     * audio. On validation failure, leaves the player not ready.
     */
    void init_memory()
    {
        const unsigned char *wav = samplesound_wav;
        if (!(wav[0] == 'R' && wav[1] == 'I' && wav[2] == 'F' && wav[3] == 'F'))
        {
            g_wav.ready = false;
            return;
        }
        if (!(wav[8] == 'W' && wav[9] == 'A' && wav[10] == 'V' && wav[11] == 'E'))
        {
            g_wav.ready = false;
            return;
        }

        // Find fmt chunk (robust scan in case of extra chunks)
        uint32_t p = 12;
        uint32_t fmt_off = 0, fmt_size = 0;
        uint32_t data_off = 0, data_size = 0;
        for (;;)
        {
            char id0 = wav[p + 0], id1 = wav[p + 1], id2 = wav[p + 2], id3 = wav[p + 3];
            uint32_t sz = mw_le32(&wav[p + 4]);
            if (id0 == 'f' && id1 == 'm' && id2 == 't' && id3 == ' ')
            {
                fmt_off = p + 8;
                fmt_size = sz;
            }
            if (id0 == 'd' && id1 == 'a' && id2 == 't' && id3 == 'a')
            {
                data_off = p + 8;
                data_size = sz;
                break;
            }
            p += 8 + sz;
            if (p > 0x1000)
            {
                g_wav.ready = false;
                return;
            } // simple guard
        }

        uint16_t audio_format = mw_le16(&wav[fmt_off + 0]);
        uint16_t channels = mw_le16(&wav[fmt_off + 2]);
        uint32_t sample_rate = mw_le32(&wav[fmt_off + 4]);
        uint16_t bits_per = mw_le16(&wav[fmt_off + 14]);

        // Memory source currently supports only 16-bit PCM (allow WAVE_FORMAT_EXTENSIBLE PCM)
        bool pcm_ok = (audio_format == 1) ||
                      (audio_format == 65534 && is_wave_extensible_pcm(&wav[fmt_off], fmt_size));

        if (!pcm_ok || channels != 2 || bits_per != 16 || sample_rate == 0 || data_size < 4)
        {
            g_wav.ready = false;
            return;
        }

        g_wav.kind = WavSrcKind::Memory;
        g_wav.sample_rate = sample_rate;
        g_wav.bits_per = 16;
        g_wav.bytes_per_frame = 4;
        g_wav.pcm_mem = (const int16_t *)(&wav[data_off]);
        g_wav.frames_total = data_size / 4;
        g_wav.frame_pos = 0;
        g_wav.start_frame = 0;
        g_wav.ready = (g_wav.frames_total > 0);
        g_wav.fileIsOpen = false;
        g_wav.isplaying = false;
        g_wav.gain_q15 = 16384; // -6 dB default
        g_wav.duration_sec = (g_wav.sample_rate ? (float)g_wav.frames_total / (float)g_wav.sample_rate : 0.0f);
        apply_offset();
    }

    /**
     * @brief Prepare playback from a WAV file on the FatFs filesystem.
     * @param path Path to a WAV file (PCM/stereo/16‑bit).
     * @return true on success; false if the file cannot be opened or validated.
     *
     * If another file is already open, it is closed first. The function reads a
     * small header buffer, scans for `fmt` and `data` chunks, validates format,
     * and initializes streaming state. On success, playback starts from the
     * configured `offset_sec`.
     */
    bool use_file(const char *path)
    {
        if (!path || !*path)
            return false;
        if (g_wav.fileIsOpen)
        {
            printf("WAV: Closing previously open file.\n");
            f_close(&g_wav.fil);
            g_wav.fileIsOpen = false;
        }
        FRESULT fr = f_open(&g_wav.fil, path, FA_READ);
        if (fr != FR_OK)
        {
            printf("WAV: f_open failed %d\n", fr);
            return false;
        }
        g_wav.fileIsOpen = true;
        // Read header into larger buffer
        const uint32_t HDR_READ_BYTES = 2048;
        uint8_t *hdr = (uint8_t *)Frens::f_malloc(HDR_READ_BYTES);
        // Note Pico panics if allocation fails
        UINT rd = 0;
        fr = f_read(&g_wav.fil, hdr, HDR_READ_BYTES, &rd);
        if (fr != FR_OK || rd < HDR_READ_BYTES  )
        {
            printf("WAV: f_read failed with error %d or too small (%u bytes read)\n", fr, rd);
            Frens::f_free(hdr);
            f_close(&g_wav.fil);
            g_wav.fileIsOpen = false;
            return false;
        }

        if (!(hdr[0] == 'R' && hdr[1] == 'I' && hdr[2] == 'F' && hdr[3] == 'F'))
        {
            printf("WAV: Invalid RIFF header\n");
            Frens::f_free(hdr);
            f_close(&g_wav.fil);
            g_wav.fileIsOpen = false;
            return false;
        }
        if (!(hdr[8] == 'W' && hdr[9] == 'A' && hdr[10] == 'V' && hdr[11] == 'E'))
        {
            printf("WAV: Invalid WAVE header\n");
            Frens::f_free(hdr);
            f_close(&g_wav.fil);
            g_wav.fileIsOpen = false;
            return false;
        }

        // Chunk scan with padding handling
        uint32_t p = 12;
        uint32_t fmt_off = 0, fmt_size = 0;
        uint32_t data_off = 0, data_size = 0;
        while (p + 8 <= rd)
        {
            uint32_t sz = mw_le32(&hdr[p + 4]);
            if (hdr[p + 0] == 'f' && hdr[p + 1] == 'm' && hdr[p + 2] == 't' && hdr[p + 3] == ' ')
            {
                fmt_off = p + 8;
                fmt_size = sz;
            }
            if (hdr[p + 0] == 'd' && hdr[p + 1] == 'a' && hdr[p + 2] == 't' && hdr[p + 3] == 'a')
            {
                data_off = p + 8;
                data_size = sz;
                break;
            }
            // advance with even-byte padding
            uint32_t next = p + 8 + sz + (sz & 1);
            if (next <= p) break; // overflow guard
            p = next;
        }
        if (!fmt_off || !data_off)
        {
            printf("WAV: Missing fmt or data chunk (fmt_off=%u, data_off=%u)\n", fmt_off, data_off);
            Frens::f_free(hdr);
            f_close(&g_wav.fil);
            g_wav.fileIsOpen = false;
            return false;
        }

        uint16_t audio_format = mw_le16(&hdr[fmt_off + 0]);
        uint16_t channels = mw_le16(&hdr[fmt_off + 2]);
        uint32_t sample_rate = mw_le32(&hdr[fmt_off + 4]);
        uint16_t bits_per = mw_le16(&hdr[fmt_off + 14]);

        bool pcm_ok = (audio_format == 1) ||
                      (audio_format == 65534 && is_wave_extensible_pcm(&hdr[fmt_off], fmt_size));

        if (!pcm_ok || channels != 2 || (bits_per != 16 && bits_per != 24) || sample_rate == 0 || data_size < 4)
        {
            printf("WAV: Unsupported format %u %u ch %u bits %u Hz %u data_size\n", audio_format, channels, bits_per, sample_rate, data_size);
            Frens::f_free(hdr);
            f_close(&g_wav.fil);
            g_wav.fileIsOpen = false;
            return false;
        }
       
        g_wav.kind = WavSrcKind::File;
        g_wav.sample_rate = sample_rate;
        g_wav.bits_per = bits_per;
        g_wav.bytes_per_frame = (bits_per == 24) ? 6u : 4u;
        g_wav.data_start = data_off;
        g_wav.data_bytes = data_size;
        g_wav.frames_total = data_size / g_wav.bytes_per_frame; // informational
        g_wav.stream_pos_bytes = 0;
        g_wav.frame_pos = 0;
        g_wav.start_frame = 0;
        g_wav.ready = true;
        g_wav.isplaying = false;
        g_wav.gain_q15 = 16384; // -6 dB default
        g_wav.duration_sec = (g_wav.sample_rate ? (float)g_wav.frames_total / (float)g_wav.sample_rate : 0.0f);
        // Seek to beginning to allow lseek to data later
        f_lseek(&g_wav.fil, 0);
        apply_offset();
        printf("WAV format %u, %u ch, %u bits, %u Hz, %u bytes, duration %f sec\n", audio_format, channels, bits_per, sample_rate, data_size, g_wav.duration_sec);
        printf("WAV player, playing from file: %s\n", path);
#if EXT_AUDIO_IS_ENABLED
        if (settings.flags.useExtAudio)
        {
            printf("WAV player using EXT_AUDIO\n");
            set_volume_linear(0.1f);
        }
        else
        {
            printf("WAV player using DVI/HSTX audio\n");
            set_volume_linear(0.5f); 
        }
#else 
        printf("WAV player using DVI/HSTX audio\n");
        set_volume_linear(0.5f); 

#endif // EXT_AUDIO_IS_ENABLED

        // free header buffer now that parsing is done
        Frens::f_free(hdr);
        // allocate buffer
        if ( !buf ) {
            buf = (uint8_t *)Frens::f_malloc(WAVPLAYER_MAX_READ_BYTES);
        }
       
        return true;
    }
  
    /**
     * @brief Push up to `frames_to_push` frames into the audio queue.
     * @param frames_to_push Number of frames to attempt to enqueue.
     *
     * Behavior:
     * - Memory: Reads frames from the embedded buffer and loops at `start_frame`.
     * - File: Reads chunked frames (up to 256 at a time). On reaching EOF or an
     *   error, seeks to `start_frame` and continues looping.
     *
     * Requires `settings.flags.audioEnabled` to be true; otherwise returns.
     */
    void pump(uint32_t frames_to_push)
    {
        if (!g_wav.ready || !g_wav.isplaying)
            return;
        // if (!settings.flags.audioEnabled)
        //     return;

        if (g_wav.kind == WavSrcKind::Memory)
        {
#if !HSTX
#if EXT_AUDIO_IS_ENABLED
            if (settings.flags.useExtAudio)
            {
                while (frames_to_push--)
                {
                    const int16_t *p = g_wav.pcm_mem + (g_wav.frame_pos * 2);
                    int16_t l = p[0], r = p[1];
                    l = apply_gain(l); r = apply_gain(r);
                    EXT_AUDIO_ENQUEUE_SAMPLE(l, r);
                    g_wav.frame_pos++;
                    if (g_wav.frame_pos >= g_wav.frames_total)
                    {
                        g_wav.frame_pos = g_wav.start_frame;
                    }
                }
            }
            else
            {
                while (frames_to_push)
                {
                    auto &ring = dvi_->getAudioRingBuffer();
                    auto n = std::min<uint32_t>(frames_to_push, ring.getWritableSize());
                    if (!n) {
                        return; // no accumulation; compute at end
                    }
                    auto dst = ring.getWritePointer();
                    for (uint32_t i = 0; i < n; ++i)
                    {
                        const int16_t *p = g_wav.pcm_mem + (g_wav.frame_pos * 2);
                        int16_t l = p[0], r = p[1];
                        l = apply_gain(l); r = apply_gain(r);
                        dst[i] = {l, r};
                        g_wav.frame_pos++;
                        if (g_wav.frame_pos >= g_wav.frames_total)
                        {
                            g_wav.frame_pos = g_wav.start_frame;
                        }
                    }
                    ring.advanceWritePointer(n);
                    frames_to_push -= n;
                }
            }
#else
            // No external audio compiled in: always use DVI ring
            while (frames_to_push)
            {
                auto &ring = dvi_->getAudioRingBuffer();
                auto n = std::min<uint32_t>(frames_to_push, ring.getWritableSize());
                if (!n) {
                    return; // no accumulation; compute at end
                }
                auto dst = ring.getWritePointer();
                for (uint32_t i = 0; i < n; ++i)
                {
                    const int16_t *p = g_wav.pcm_mem + (g_wav.frame_pos * 2);
                    int16_t l = p[0], r = p[1];
                    l = apply_gain(l); r = apply_gain(r);
                    dst[i] = {l, r};
                    g_wav.frame_pos++;
                    if (g_wav.frame_pos >= g_wav.frames_total)
                    {
                        g_wav.frame_pos = g_wav.start_frame;
                    }
                }
                ring.advanceWritePointer(n);
                frames_to_push -= n;
            }
#endif // EXT_AUDIO_IS_ENABLED
#else
            // HSTX build: must use external audio
            while (frames_to_push--)
            {
                const int16_t *p = g_wav.pcm_mem + (g_wav.frame_pos * 2);
                int16_t l = p[0], r = p[1];
                l = apply_gain(l); r = apply_gain(r);
                if (settings.flags.useExtAudio) {
                    EXT_AUDIO_ENQUEUE_SAMPLE(l, r);
                } else {
                    hstx_push_audio_sample(l, r);
                }
                g_wav.frame_pos++;
                if (g_wav.frame_pos >= g_wav.frames_total)
                {
                    g_wav.frame_pos = g_wav.start_frame;
                }
            }
#endif // !HSTX
        }
        else
        {
            while (frames_to_push)
            {
                uint32_t want_frames = std::min<uint32_t>(frames_to_push, 256);
                uint32_t bytes_avail = g_wav.data_bytes - g_wav.stream_pos_bytes;
                if (bytes_avail == 0)
                {
                    // Loop to offset
                    g_wav.stream_pos_bytes = g_wav.start_frame * g_wav.bytes_per_frame;
                    f_lseek(&g_wav.fil, g_wav.data_start + g_wav.stream_pos_bytes);
                    bytes_avail = g_wav.data_bytes - g_wav.stream_pos_bytes;
                    g_wav.frame_pos = g_wav.start_frame; // keep position consistent
                }
                uint32_t bytes_to_read = std::min<uint32_t>(want_frames * g_wav.bytes_per_frame, bytes_avail);
                UINT rd = 0;
                FRESULT fr = f_read(&g_wav.fil, buf, bytes_to_read, &rd);
                if (fr != FR_OK || rd == 0)
                {
                    // On error, attempt loop
                    g_wav.stream_pos_bytes = g_wav.start_frame * g_wav.bytes_per_frame;
                    f_lseek(&g_wav.fil, g_wav.data_start + g_wav.stream_pos_bytes);
                    continue;
                }
                g_wav.stream_pos_bytes += rd;

                uint32_t got_frames = rd / g_wav.bytes_per_frame;

#if !HSTX
#if EXT_AUDIO_IS_ENABLED
                if (settings.flags.useExtAudio)
                {
                    if (g_wav.bits_per == 16)
                    {
                        const int16_t *src16 = (const int16_t *)buf;
                        for (uint32_t i = 0; i < got_frames; ++i)
                        {
                            int16_t l = *src16++;
                            int16_t r = *src16++;
                            l = apply_gain(l); r = apply_gain(r);
                            EXT_AUDIO_ENQUEUE_SAMPLE(l, r);
                        }
                    }
                    else
                    {
                        const uint8_t *p = (const uint8_t *)buf;
                        for (uint32_t i = 0; i < got_frames; ++i)
                        {
                            int16_t l = pcm24_to_s16(p); p += 3;
                            int16_t r = pcm24_to_s16(p); p += 3;
                            l = apply_gain(l); r = apply_gain(r);
                            EXT_AUDIO_ENQUEUE_SAMPLE(l, r);
                        }
                    }
                    frames_to_push -= got_frames;
                }
                else
                {
                    uint32_t consumed_frames = 0;
                    while (got_frames)
                    {
                        auto &ring = dvi_->getAudioRingBuffer();
                        uint32_t n = std::min<uint32_t>(got_frames, ring.getWritableSize());
                        if (!n) {
                            return; // no accumulation; compute at end
                        }
                        auto dst = ring.getWritePointer();
                        if (g_wav.bits_per == 16)
                        {
                            const int16_t *src16 = (const int16_t *)buf + consumed_frames * 2;
                            for (uint32_t i = 0; i < n; ++i)
                            {
                                int16_t l = *src16++;
                                int16_t r = *src16++;
                                l = apply_gain(l); r = apply_gain(r);
                                dst[i] = {l, r};
                            }
                        }
                        else
                        {
                            const uint8_t *p = (const uint8_t *)buf + consumed_frames * 6;
                            for (uint32_t i = 0; i < n; ++i)
                            {
                                int16_t l = pcm24_to_s16(p); p += 3;
                                int16_t r = pcm24_to_s16(p); p += 3;
                                l = apply_gain(l); r = apply_gain(r);
                                dst[i] = {l, r};
                            }
                        }
                        ring.advanceWritePointer(n);
                        frames_to_push -= n;
                        got_frames -= n;
                        consumed_frames += n;
                    }
                }
#else
                // No external audio compiled in: always use DVI ring
                {
                    uint32_t consumed_frames = 0;
                    while (got_frames)
                    {
                        auto &ring = dvi_->getAudioRingBuffer();
                        uint32_t n = std::min<uint32_t>(got_frames, ring.getWritableSize());
                        if (!n) {
                            return; // no accumulation; compute at end
                        }
                        auto dst = ring.getWritePointer();
                        if (g_wav.bits_per == 16)
                        {
                            const int16_t *src16 = (const int16_t *)buf + consumed_frames * 2;
                            for (uint32_t i = 0; i < n; ++i)
                            {
                                int16_t l = *src16++;
                                int16_t r = *src16++;
                                l = apply_gain(l); r = apply_gain(r);
                                dst[i] = {l, r};
                            }
                        }
                        else
                        {
                            const uint8_t *p = (const uint8_t *)buf + consumed_frames * 6;
                            for (uint32_t i = 0; i < n; ++i)
                            {
                                int16_t l = pcm24_to_s16(p); p += 3;
                                int16_t r = pcm24_to_s16(p); p += 3;
                                l = apply_gain(l); r = apply_gain(r);
                                dst[i] = {l, r};
                            }
                        }
                        ring.advanceWritePointer(n);
                        frames_to_push -= n;
                        got_frames -= n;
                        consumed_frames += n;
                    }
                }
#endif // EXT_AUDIO_IS_ENABLED
#else
                // HSTX build: must use external audio
                if (g_wav.bits_per == 16)
                {
                    const int16_t *src16 = (const int16_t *)buf;
                    for (uint32_t i = 0; i < got_frames; ++i)
                    {
                        int16_t l = *src16++;
                        int16_t r = *src16++;
                        l = apply_gain(l); r = apply_gain(r);
                        if (settings.flags.useExtAudio) {
                            EXT_AUDIO_ENQUEUE_SAMPLE(l, r);
                        } else {
                            hstx_push_audio_sample(l, r);
                        }
                    }
                }
                else
                {
                    const uint8_t *p = (const uint8_t *)buf;
                    for (uint32_t i = 0; i < got_frames; ++i)
                    {
                        int16_t l = pcm24_to_s16(p); p += 3;
                        int16_t r = pcm24_to_s16(p); p += 3;
                        l = apply_gain(l); r = apply_gain(r);
                        EXT_AUDIO_ENQUEUE_SAMPLE(l, r);
                    }
                }
                frames_to_push -= got_frames;
#endif // !HSTX

                // If we ended exactly at EOF, next loop will reset to offset
                if (g_wav.stream_pos_bytes >= g_wav.data_bytes)
                {
                    g_wav.stream_pos_bytes = g_wav.start_frame * g_wav.bytes_per_frame;
                    f_lseek(&g_wav.fil, g_wav.data_start + g_wav.stream_pos_bytes);
                    g_wav.frame_pos = g_wav.start_frame;
                }
            }
        }

        // Compute seconds since loop start (no accumulation across loops)
        if (g_wav.sample_rate)
        {
            uint32_t frames_since_start = (g_wav.frame_pos >= g_wav.start_frame)
                                            ? (g_wav.frame_pos - g_wav.start_frame)
                                            : 0u;
        }
    }

    /** @brief Current sample rate in Hz (0 if not initialized). */
    uint32_t sample_rate() { return g_wav.sample_rate; }
    /** @brief Whether the player is configured and able to pump audio. */
    bool ready() { return g_wav.ready; }
    float total_seconds() { return g_wav.duration_sec; }

    // Report current position within the track (0..duration), independent of loop offset.
    float current_position_seconds()
    {
        if (!g_wav.ready || g_wav.sample_rate == 0)
            return 0.0f;

        uint32_t pos_frames = 0;
        if (g_wav.kind == WavSrcKind::Memory)
        {
            if (g_wav.frames_total)
                pos_frames = g_wav.frame_pos % g_wav.frames_total;
        }
        else
        {
            pos_frames = g_wav.stream_pos_bytes / g_wav.bytes_per_frame;
            if (g_wav.frames_total)
                pos_frames %= g_wav.frames_total;
        }
        return (float)pos_frames / (float)g_wav.sample_rate;
    }

    /**
     * @brief Close any open file and reset state to defaults.
     *
     * Closes the file if open, clears the internal `WavState`, and logs a
     * message. After reset, the player is not ready until reinitialized.
     */
    void reset()
    {
        if (g_wav.fileIsOpen)
        {
            f_close(&g_wav.fil);
        }
        if (buf)
        {
            Frens::f_free(buf);
            buf = nullptr;
        }
        g_wav = WavState{}; // zero/false all fields
        printf("Wav player reset.\n");
    }
    void pause()
    {
        g_wav.isplaying = false;
    }
    void resume()
    {
        g_wav.isplaying = true;
    }
  
    bool isPlaying()
    {
        return g_wav.isplaying;
    }
} // namespace wavplayer

#endif // HW_CONFIG == 8
