#pragma once
#if ENABLE_VU_METER
#ifdef PICO_DEFAULT_WS2812_PIN
void addSampleToVUMeter(int16_t sample);
void initializeNeoPixelStrip();
void turnOffAllLeds();
#endif
#endif
