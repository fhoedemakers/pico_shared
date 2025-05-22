/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#if USE_EXTERNAL_AUDIO == 1
#include <stdio.h>
#include <math.h>

#include "hardware/clocks.h"
#include "hardware/structs/clocks.h"
#include "hardware/vreg.h"
#include "hardware/pio.h"
#include "hardware/dma.h"

#include "pico/stdlib.h"
#define SAMPLES_PER_BUFFER 256
#if USE_AUDIO_I2S

//#include "pico/audio_i2s.h"

#include "ext_audio.h"
#include "pico/binary_info.h"
bi_decl(bi_3pins_with_names(PICO_AUDIO_I2S_DATA_PIN, "I2S DIN", PICO_AUDIO_I2S_CLOCK_PIN_BASE, "I2S BCK", PICO_AUDIO_I2S_CLOCK_PIN_BASE + 1, "I2S LRCK"));


#elif USE_AUDIO_PWM
#include "pico/audio_pwm.h"
#elif USE_AUDIO_SPDIF
#include "pico/audio_spdif.h"
#endif


struct audio_buffer_pool *init_audio()
{


    static audio_format_t audio_format = {
        .format = AUDIO_BUFFER_FORMAT_PCM_S16, // 
        .sample_freq = 44100,                  // 
        .channel_count = 1,                    // Mono
    };
    static struct audio_buffer_format producer_format = {
        .format = &audio_format,
        .sample_stride = 2
    };
    struct audio_buffer_pool *producer_pool = audio_new_producer_pool(&producer_format, 3,
                                                                      SAMPLES_PER_BUFFER); // todo correct size
    bool __unused ok;
    const struct audio_format *output_format;
#if USE_AUDIO_I2S
#if PICO_AUDIO_I2S_PIO == 1
    int sm =  pio_claim_unused_sm(pio1, false); 
#else
    int sm =  pio_claim_unused_sm(pio0, false);
#endif
    struct audio_i2s_config config = {
        .data_pin = PICO_AUDIO_I2S_DATA_PIN,
        .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
        .dma_channel =  10, //dma_claim_unused_channel(true),
        .pio_sm = sm,
    };

    output_format = audio_i2s_setup(&audio_format, &config);
    if (!output_format)
    {
        panic("PicoAudio: Unable to open audio device.\n");
    }

    ok = audio_i2s_connect(producer_pool);
    assert(ok);
    audio_i2s_set_enabled(true);
#elif USE_AUDIO_PWM
    output_format = audio_pwm_setup(&audio_format, -1, &default_mono_channel_config);
    if (!output_format)
    {
        panic("PicoAudio: Unable to open audio device.\n");
    }
    ok = audio_pwm_default_connect(producer_pool, false);
    assert(ok);
    audio_pwm_set_enabled(true);
#elif USE_AUDIO_SPDIF
    output_format = audio_spdif_setup(&audio_format, &audio_spdif_default_config);
    if (!output_format)
    {
        panic("PicoAudio: Unable to open audio device.\n");
    }
    // ok = audio_spdif_connect(producer_pool);
    ok = audio_spdif_connect(producer_pool);
    assert(ok);
    audio_spdif_set_enabled(true);
#endif
    return producer_pool;
}
#endif
