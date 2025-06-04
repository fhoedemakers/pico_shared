#ifndef __EXTERNAL_AUDIO_H__
#define __EXTERNAL_AUDIO_H__
#ifndef USE_I2S_AUDIO
#define USE_I2S_AUDIO 0
#endif

#if USE_I2S_AUDIO
#include "audio_i2s.h"
extern audio_i2s_hw_t *i2s_audio_hw;
#endif

#endif