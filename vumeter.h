#pragma once
#ifndef ENABLE_VU_METER
#define ENABLE_VU_METER 0
#endif

#if ENABLE_VU_METER
#ifdef PICO_DEFAULT_WS2812_PIN
#ifndef VU_METER_TOGGLE_PIN
#define VU_METER_TOGGLE_PIN 4 // GPIO pin to toggle VU meter, set to -1 to disable
#define VU_METER_TOGGLE_PIN_MUST_BE_PULLED_UP 1  // 1 pull_up, 0 pull_down
#endif
void addSampleToVUMeter(int16_t sample);
void initializeNeoPixelStrip();
void turnOffAllLeds();
bool isVUMeterToggleButtonPressed();
#endif
#endif
