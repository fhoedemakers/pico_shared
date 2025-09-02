#pragma once
#ifndef ENABLE_VU_METER
#define ENABLE_VU_METER 0
#endif
#if ENABLE_VU_METER
#ifdef PICO_DEFAULT_WS2812_PIN
void addSampleToVUMeter(int16_t sample);
void initializeNeoPixelStrip();
void turnOffAllLeds();
#endif
#endif
