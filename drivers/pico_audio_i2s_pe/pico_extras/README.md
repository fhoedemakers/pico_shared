# pico-extras vendored sources

These files are copied verbatim from
[raspberrypi/pico-extras](https://github.com/raspberrypi/pico-extras) so the
build does not depend on having that repo checked out.

Upstream commit: `eb071cd88a8d7f6227ae55413e773454d1455168` (2025-08-12).

| File | Upstream path |
|---|---|
| `audio_i2s.c` | `src/rp2_common/pico_audio_i2s/audio_i2s.c` |
| `audio.cpp` | `src/common/pico_audio/audio.cpp` |
| `buffer.c` | `src/common/pico_util_buffer/buffer.c` |
| `include/pico/audio_i2s.h` | `src/rp2_common/pico_audio_i2s/include/pico/audio_i2s.h` |
| `include/pico/audio.h` | `src/common/pico_audio/include/pico/audio.h` |
| `include/pico/sample_conversion.h` | `src/common/pico_audio/include/pico/sample_conversion.h` |
| `include/pico/util/buffer.h` | `src/common/pico_util_buffer/include/pico/util/buffer.h` |

The `audio_i2s.pio` file (one level up) is also from
`src/rp2_common/pico_audio_i2s/audio_i2s.pio` and is byte-identical to the
copy the legacy driver already used.

To re-sync from upstream: re-run the curl commands in the project README, or
manually replace the files above and update this commit hash.
