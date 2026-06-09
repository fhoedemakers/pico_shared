/*
 *  audio_i2s_adapter.c
 *
 *  Implements the legacy custom-driver API (audio_i2s_setup,
 *  audio_i2s_enqueue_sample, ...) on top of the pico-extras audio_buffer_pool
 *  driver. Callers see the same symbols as before, so no code outside this
 *  directory changes when USE_PICO_EXTRAS_I2S is on.
 *
 *  Per-sample writes go into a producer pool; when a buffer fills it is given
 *  to the consumer (the I2S DMA) and a fresh buffer is taken. Buffer drops on
 *  overflow match the legacy "ring full → drop sample" behaviour.
 */

#include "tlv320dac3100.h"

#include "pico/stdlib.h"
#include "pico/audio.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"

#include <stdio.h>
#include <string.h>

#define ADAPTER_AUDIO_PIO __CONCAT(pio, PICO_AUDIO_I2S_PIO)

/* Rename the colliding symbol BEFORE pulling in the upstream header. The same
 * rename is applied to pico_extras/audio_i2s.c via pe_audio_i2s_wrapper.c, so
 * upstream's public entry point appears under `pe_audio_i2s_setup` and we are
 * free to define our own `audio_i2s_setup` with the legacy signature. */
#define audio_i2s_setup pe_audio_i2s_setup
#include "pico/audio_i2s.h"
#undef audio_i2s_setup

#include "audio_i2s.h"

static audio_buffer_pool_t *s_producer_pool = NULL;
static audio_buffer_t *s_current_buffer = NULL;
static uint32_t s_current_offset = 0;
static int s_driver = PICO_AUDIO_I2S_DRIVER_NONE;
static int s_dma_chan = -1;
static int s_pio_sm = -1;

static audio_format_t s_audio_format = {
    .sample_freq = 44100,
    .format = AUDIO_BUFFER_FORMAT_PCM_S16,
    .channel_count = 2,
};

static audio_buffer_format_t s_producer_format = {
    .format = &s_audio_format,
    .sample_stride = 4, /* stereo S16 */
};

static audio_i2s_hw_t s_hw = { .sm = -1, .pio = NULL, .dma_chan = -1 };

audio_i2s_hw_t *audio_i2s_setup(int driver, int freqHZ, int dmachan)
{
    s_driver = driver;
    if (driver == PICO_AUDIO_I2S_DRIVER_NONE) {
        printf("No I2S driver selected, skipping audio setup.\n");
        return NULL;
    }

    if (driver == PICO_AUDIO_I2S_DRIVER_TLV320) {
        tlv320_init();
    }

    if (freqHZ > 0) s_audio_format.sample_freq = (uint32_t)freqHZ;

    /* If the caller asked us to pick a DMA channel, find one. Upstream's
     * pe_audio_i2s_setup will claim it via dma_channel_claim(), so make sure
     * it is not claimed yet at the moment we pass it in. */
    if (dmachan < 0) {
        int chan = dma_claim_unused_channel(true);
        dma_channel_unclaim(chan); /* unclaim so upstream can re-claim */
        dmachan = chan;
    }
    s_dma_chan = dmachan;

    /* Allocate the producer pool. The buffer pool replaces the legacy
     * ring buffer; total memory is COUNT * SAMPLES * 4 bytes (= 4096 bytes
     * with the defaults, matching the 1024-sample legacy ring). */
    s_producer_pool = audio_new_producer_pool(&s_producer_format,
                                              PICO_AUDIO_I2S_PE_BUFFER_COUNT,
                                              PICO_AUDIO_I2S_PE_BUFFER_SAMPLES);
    if (!s_producer_pool) {
        printf("audio_new_producer_pool failed\n");
        return NULL;
    }

    /* Pick a free state machine on the target PIO. Upstream's pe_audio_i2s_setup
     * calls pio_sm_claim() which panics if the SM is already taken; HW_CONFIG=1
     * (Pimoroni Pico DV) for example reserves PIO1 SM0 for the NES controller.
     * Mirror the DMA dance: claim to discover a free one, then immediately
     * unclaim so upstream can re-claim. */
    int free_sm = pio_claim_unused_sm(ADAPTER_AUDIO_PIO, true);
    pio_sm_unclaim(ADAPTER_AUDIO_PIO, free_sm);

    audio_i2s_config_t cfg = {
        .data_pin = PICO_AUDIO_I2S_DATA_PIN,
        .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
        .dma_channel = (uint8_t)dmachan,
        .pio_sm = (uint8_t)free_sm,
    };
    s_pio_sm = free_sm;

    const audio_format_t *got = pe_audio_i2s_setup(&s_audio_format, &cfg);
    if (!got) {
        printf("pe_audio_i2s_setup failed\n");
        return NULL;
    }

    if (!audio_i2s_connect(s_producer_pool)) {
        printf("audio_i2s_connect failed\n");
        return NULL;
    }

    audio_i2s_set_enabled(true);

    s_hw.dma_chan = dmachan;
    s_hw.sm = s_pio_sm;
    s_hw.pio = NULL; /* PIO instance not exposed by upstream API */
    return &s_hw;
}

/* The upstream driver re-computes the PIO divider whenever the producer-pool
 * sample_freq changes (see wrap_consumer_take in pico_extras/audio_i2s.c).
 * Simply updating the format struct is enough. */
void audio_i2s_update_pio_frequency(uint32_t sample_freq)
{
    s_audio_format.sample_freq = sample_freq;
}

void audio_i2s_out_32(uint32_t sample32)
{
    /* Compatibility no-op-ish: feed it through the normal path. */
    audio_i2s_enqueue_sample(sample32);
}

#if I2S_AUDIO_COMPENSATE_DC_OFFSET
static int32_t dc_avg_l = 0;
static int32_t dc_avg_r = 0;

static inline uint32_t __not_in_flash_func(dc_block_sample)(uint32_t sample32)
{
    int32_t l = (int16_t)(sample32 >> 16);
    int32_t r = (int16_t)(sample32 & 0xFFFF);
    dc_avg_l += (l - dc_avg_l) >> I2S_DC_FILTER_SHIFT;
    dc_avg_r += (r - dc_avg_r) >> I2S_DC_FILTER_SHIFT;
    l -= dc_avg_l;
    r -= dc_avg_r;
    if (l > 32767) l = 32767; else if (l < -32768) l = -32768;
    if (r > 32767) r = 32767; else if (r < -32768) r = -32768;
    return ((uint32_t)(uint16_t)l << 16) | (uint16_t)r;
}
#endif

void __not_in_flash_func(audio_i2s_enqueue_sample)(uint32_t sample32)
{
    if (!s_producer_pool) return;

    if (!s_current_buffer) {
        s_current_buffer = take_audio_buffer(s_producer_pool, false /* non-blocking */);
        if (!s_current_buffer) return; /* pool full → drop sample */
        s_current_offset = 0;
    }
#if I2S_AUDIO_COMPENSATE_DC_OFFSET
    //if (s_driver == PICO_AUDIO_I2S_DRIVER_PCM5000A)
        sample32 = dc_block_sample(sample32);
#endif
    int16_t *out = (int16_t *)s_current_buffer->buffer->bytes;
    out[s_current_offset * 2 + 0] = (int16_t)(sample32 >> 16);
    out[s_current_offset * 2 + 1] = (int16_t)(sample32 & 0xffff);
    s_current_offset++;

    if (s_current_offset >= s_current_buffer->max_sample_count) {
        s_current_buffer->sample_count = s_current_offset;
        give_audio_buffer(s_producer_pool, s_current_buffer);
        s_current_buffer = NULL;
        s_current_offset = 0;
    }
}

/* Approximate free space in samples. Walks the producer pool's free-list to
 * count buffers that are currently available to be filled, plus space
 * remaining in the currently-open buffer. Cheap because the free list is
 * short (PICO_AUDIO_I2S_PE_BUFFER_COUNT entries). */
int audio_i2s_get_freebuffer_size(void)
{
    if (!s_producer_pool) return 0;

    uint32_t save = save_and_disable_interrupts();
    uint32_t free_buffers = 0;
    audio_buffer_t *b = s_producer_pool->free_list;
    while (b) { free_buffers++; b = b->next; }
    restore_interrupts(save);

    uint32_t free_samples = free_buffers * PICO_AUDIO_I2S_PE_BUFFER_SAMPLES;
    if (s_current_buffer) {
        free_samples += (s_current_buffer->max_sample_count - s_current_offset);
    }
    return (int)free_samples;
}

void audio_i2s_disable(void)
{
    printf("Disabling I2S audio (pico-extras driver)\n");
    audio_i2s_set_enabled(false);
    /* Note: pico-extras does not provide a teardown for the producer pool;
     * we leak it on purpose. This matches the existing usage pattern where
     * audio_i2s_disable is only called from menu transitions that are
     * followed by power-off or a fresh setup. */
}

bool audio_i2s_dacError(void)
{
    return tlv320_dac_error();
}

void audio_i2s_setVolume(int8_t level)
{
    tlv320_set_volume(level);
}

void audio_i2s_muteInternalSpeaker(bool mute)
{
    tlv320_mute_speaker(mute);
}

enum headphone_toggle_t audio_i2s_poll_headphone_status(void)
{
    return tlv320_poll_headphone();
}
