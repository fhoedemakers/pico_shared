#if USE_EXTERNAL_AUDIO == 1
#ifdef __cplusplus
extern "C" {
#endif
#include "pico/audio_i2s.h"
struct audio_buffer_pool *init_audio();
#ifdef __cplusplus
}
#endif
#endif