#pragma once

#include <cstdint>

namespace wavplayer {

#if PICO_RP2350
// Initialize playback from embedded WAV in memory (optional).
void init_memory();

// Use a WAV file on SD/Flash; returns true if prepared successfully.
bool use_file(const char* path);

// Set loop start offset in seconds; clamps to >= 0 and applies immediately.
void set_offset_seconds(float seconds);

// Push a number of stereo 16-bit frames into the audio queue.
void pump(uint32_t frames_to_push);

// Query state for driving logic.
uint32_t sample_rate();
bool ready();
void pause();  
void resume();
// Close any open file and reset internal state to defaults.
void reset();
bool isPlaying();
#else
// Stubs for non-PICO_RP2350 builds.
inline void init_memory() {}
inline bool use_file(const char*) { return false; }
inline void set_offset_seconds(float) {}
inline void pump(uint32_t) {}
inline uint32_t sample_rate() { return 0; }
inline bool ready() { return false; }
inline void reset() {}
inline void pause() {}  
inline void resume() {}
inline bool isPlaying() { return false; }
#endif

} // namespace wavplayer
