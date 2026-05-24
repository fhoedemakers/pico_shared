/*
 *  pe_audio_i2s_wrapper.c
 *
 *  Compile wrapper for pico_extras/audio_i2s.c. The upstream file is kept
 *  byte-for-byte identical to the pico-extras tree; this wrapper renames the
 *  upstream's `audio_i2s_setup` symbol to `pe_audio_i2s_setup` so it doesn't
 *  collide with the legacy-shaped `audio_i2s_setup` that audio_i2s_adapter.c
 *  exports to the rest of the codebase.
 *
 *  Only this wrapper is added to target_sources; pico_extras/audio_i2s.c is
 *  pulled in by inclusion.
 */

#define audio_i2s_setup pe_audio_i2s_setup
#include "pico_extras/audio_i2s.c"
