#include "wavplayer.h"

#if HW_CONFIG == 8

#include "external_audio.h"
#include "ff.h"
#include "settings.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

// Generated in soundrecorder.cpp
extern const unsigned char samplesound_wav[];

namespace wavplayer {

enum class WavSrcKind : uint8_t { Memory, File };

struct WavState {
    // Common
    uint32_t sample_rate;     // Hz
    uint32_t frames_total;    // total frames (informational for memory)
    uint32_t frame_pos;       // current frame index
    uint32_t start_frame;     // loop start frame computed from offset
    bool ready;
    float offset_sec;
    WavSrcKind kind;

    // Memory source
    const int16_t *pcm_mem;

    // File source
    FIL fil;
    bool fileIsOpen;
    uint32_t data_start;      // byte offset of 'data' payload start
    uint32_t data_bytes;      // size of 'data' payload in bytes
    uint32_t bytes_per_frame; // 4 for stereo 16-bit
    uint32_t stream_pos_bytes;// current byte offset within data chunk
};

static WavState g_wav{};

static inline uint16_t mw_le16(const unsigned char *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static inline uint32_t mw_le32(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static void apply_offset()
{
    if (!g_wav.ready) return;
    if (g_wav.sample_rate == 0) return;

    uint64_t start_frames = (uint64_t)(g_wav.offset_sec * (float)g_wav.sample_rate + 0.5f);
    if (g_wav.kind == WavSrcKind::Memory) {
        if (g_wav.frames_total == 0) return;
        g_wav.start_frame = (uint32_t)(start_frames % g_wav.frames_total);
        g_wav.frame_pos   = g_wav.start_frame;
    } else { // File
        uint64_t start_bytes = start_frames * g_wav.bytes_per_frame;
        if (g_wav.data_bytes == 0) return;
        start_bytes %= g_wav.data_bytes;
        g_wav.stream_pos_bytes = (uint32_t)start_bytes;
        g_wav.frame_pos        = (uint32_t)(start_bytes / g_wav.bytes_per_frame);
        g_wav.start_frame      = g_wav.frame_pos;
        f_lseek(&g_wav.fil, g_wav.data_start + g_wav.stream_pos_bytes);
    }
}

void set_offset_seconds(float seconds)
{
    g_wav.offset_sec = seconds < 0.0f ? 0.0f : seconds;
    apply_offset();
}

void init_memory()
{
    const unsigned char *wav = samplesound_wav;
    if (!(wav[0]=='R'&&wav[1]=='I'&&wav[2]=='F'&&wav[3]=='F')) { g_wav.ready=false; return; }
    if (!(wav[8]=='W'&&wav[9]=='A'&&wav[10]=='V'&&wav[11]=='E')) { g_wav.ready=false; return; }

    // Find fmt chunk (robust scan in case of extra chunks)
    uint32_t p = 12;
    uint32_t fmt_off = 0, fmt_size = 0;
    uint32_t data_off = 0, data_size = 0;
    for (;;) {
        char id0 = wav[p+0], id1 = wav[p+1], id2 = wav[p+2], id3 = wav[p+3];
        uint32_t sz = mw_le32(&wav[p+4]);
        if (id0=='f'&&id1=='m'&&id2=='t'&&id3==' ') { fmt_off = p+8; fmt_size = sz; }
        if (id0=='d'&&id1=='a'&&id2=='t'&&id3=='a') { data_off = p+8; data_size = sz; break; }
        p += 8 + sz;
        if (p > 0x1000) { g_wav.ready=false; return; } // simple guard
    }

    uint16_t audio_format = mw_le16(&wav[fmt_off+0]);
    uint16_t channels     = mw_le16(&wav[fmt_off+2]);
    uint32_t sample_rate  = mw_le32(&wav[fmt_off+4]);
    uint16_t bits_per     = mw_le16(&wav[fmt_off+14]);

    if (audio_format != 1 || channels != 2 || bits_per != 16 || sample_rate == 0 || data_size < 4) { g_wav.ready=false; return; }

    g_wav.kind = WavSrcKind::Memory;
    g_wav.sample_rate = sample_rate;
    g_wav.bytes_per_frame = 4;
    g_wav.pcm_mem = (const int16_t*)(&wav[data_off]);
    g_wav.frames_total = data_size / 4;
    g_wav.frame_pos = 0;
    g_wav.start_frame = 0;
    g_wav.ready = (g_wav.frames_total > 0);
    g_wav.fileIsOpen = false;
    apply_offset();
}

bool use_file(const char* path)
{
    if (!path || !*path) return false;
    if (g_wav.fileIsOpen) {
        printf("WAV: Closing previously open file.\n");
        f_close(&g_wav.fil);
        g_wav.fileIsOpen = false;
    }
    FRESULT fr = f_open(&g_wav.fil, path, FA_READ);
    if (fr != FR_OK) {
        printf("WAV: f_open failed %d\n", fr);
        return false;
    }
    g_wav.fileIsOpen = true;
    // Read header into small buffer
    unsigned char hdr[256];
    UINT rd = 0;
    fr = f_read(&g_wav.fil, hdr, sizeof(hdr), &rd);
    if (fr != FR_OK || rd < 44) { f_close(&g_wav.fil); g_wav.fileIsOpen=false; return false; }

    if (!(hdr[0]=='R'&&hdr[1]=='I'&&hdr[2]=='F'&&hdr[3]=='F')) { f_close(&g_wav.fil); g_wav.fileIsOpen=false; return false; }
    if (!(hdr[8]=='W'&&hdr[9]=='A'&&hdr[10]=='V'&&hdr[11]=='E')) { f_close(&g_wav.fil); g_wav.fileIsOpen=false; return false; }

    // Chunk scan
    uint32_t p = 12;
    uint32_t fmt_off = 0, fmt_size = 0;
    uint32_t data_off = 0, data_size = 0;
    while (p + 8 <= rd) {
        uint32_t sz = mw_le32(&hdr[p+4]);
        if (hdr[p+0]=='f'&&hdr[p+1]=='m'&&hdr[p+2]=='t'&&hdr[p+3]==' ') { fmt_off = p+8; fmt_size = sz; }
        if (hdr[p+0]=='d'&&hdr[p+1]=='a'&&hdr[p+2]=='t'&&hdr[p+3]=='a') { data_off = p+8; data_size = sz; break; }
        p += 8 + sz;
    }
    if (!fmt_off || !data_off) { f_close(&g_wav.fil); g_wav.fileIsOpen=false; return false; }

    uint16_t audio_format = mw_le16(&hdr[fmt_off+0]);
    uint16_t channels     = mw_le16(&hdr[fmt_off+2]);
    uint32_t sample_rate  = mw_le32(&hdr[fmt_off+4]);
    uint16_t bits_per     = mw_le16(&hdr[fmt_off+14]);

    if (audio_format != 1 || channels != 2 || bits_per != 16 || sample_rate == 0 || data_size < 4) { f_close(&g_wav.fil); g_wav.fileIsOpen=false; return false; }

    g_wav.kind = WavSrcKind::File;
    g_wav.sample_rate = sample_rate;
    g_wav.bytes_per_frame = 4;
    g_wav.data_start = data_off;
    g_wav.data_bytes = data_size;
    g_wav.frames_total = data_size / 4; // informational
    g_wav.stream_pos_bytes = 0;
    g_wav.frame_pos = 0;
    g_wav.start_frame = 0;
    g_wav.ready = true;

    // Seek to beginning to allow lseek to data later
    f_lseek(&g_wav.fil, 0);
    apply_offset();
    printf("WAV player, playing from file: %s, %u Hz, %u bytes\n", path, sample_rate, data_size);
    return true;
}

void pump(uint32_t frames_to_push)
{
    if (!g_wav.ready) return;
    if (!settings.flags.audioEnabled) return;

    if (g_wav.kind == WavSrcKind::Memory) {
        while (frames_to_push--) {
            const int16_t* p = g_wav.pcm_mem + (g_wav.frame_pos * 2);
            int16_t l = p[0], r = p[1];
            EXT_AUDIO_ENQUEUE_SAMPLE(l, r);
            g_wav.frame_pos++;
            if (g_wav.frame_pos >= g_wav.frames_total) {
                g_wav.frame_pos = g_wav.start_frame; // loop back to offset
            }
        }
    } else {
        // Read chunked frames from file
        int16_t buf[256 * 2]; // 256 frames stereo
        while (frames_to_push) {
            uint32_t want_frames = std::min<uint32_t>(frames_to_push, 256);
            uint32_t bytes_avail = g_wav.data_bytes - g_wav.stream_pos_bytes;
            if (bytes_avail == 0) {
                // Loop to offset
                g_wav.stream_pos_bytes = g_wav.start_frame * g_wav.bytes_per_frame;
                f_lseek(&g_wav.fil, g_wav.data_start + g_wav.stream_pos_bytes);
                bytes_avail = g_wav.data_bytes - g_wav.stream_pos_bytes;
            }
            uint32_t bytes_to_read = std::min<uint32_t>(want_frames * g_wav.bytes_per_frame, bytes_avail);
            UINT rd = 0;
            FRESULT fr = f_read(&g_wav.fil, buf, bytes_to_read, &rd);
            if (fr != FR_OK || rd == 0) {
                // On error, attempt loop
                g_wav.stream_pos_bytes = g_wav.start_frame * g_wav.bytes_per_frame;
                f_lseek(&g_wav.fil, g_wav.data_start + g_wav.stream_pos_bytes);
                continue;
            }
            g_wav.stream_pos_bytes += rd;

            uint32_t got_frames = rd / g_wav.bytes_per_frame;
            const int16_t* p = buf;
            for (uint32_t i = 0; i < got_frames; ++i) {
                int16_t l = *p++;
                int16_t r = *p++;
                EXT_AUDIO_ENQUEUE_SAMPLE(l, r);
            }
            g_wav.frame_pos += got_frames;
            frames_to_push -= got_frames;

            // If we ended exactly at EOF, next loop will reset to offset
            if (g_wav.stream_pos_bytes >= g_wav.data_bytes) {
                g_wav.stream_pos_bytes = g_wav.start_frame * g_wav.bytes_per_frame;
                f_lseek(&g_wav.fil, g_wav.data_start + g_wav.stream_pos_bytes);
                g_wav.frame_pos = g_wav.start_frame;
            }
        }
    }
}

uint32_t sample_rate() { return g_wav.sample_rate; }
bool ready() { return g_wav.ready; }

void reset()
{
    if (g_wav.fileIsOpen) {
        f_close(&g_wav.fil);
    }
    g_wav = WavState{}; // zero/false all fields
    printf("Wav player reset.\n");
}

} // namespace wavplayer

#endif // HW_CONFIG == 8
