namespace {
    constexpr dvi::Config dviConfig_PicoDVI = {
        .pinTMDS = {10, 12, 14},
        .pinClock = 8,
        .invert = true,
    };

    constexpr dvi::Config dviConfig_PicoDVISock = {
        .pinTMDS = {12, 18, 16},
        .pinClock = 14,
        .invert = false,
    };
    // Pimoroni Digital Video, SD Card & Audio Demo Board
    constexpr dvi::Config dviConfig_PimoroniDemoDVSock = {
        .pinTMDS = {8, 10, 12},
        .pinClock = 6,
        .invert = true,
    };
    // Adafruit Feather RP2040 DVI
    constexpr dvi::Config dviConfig_AdafruitFeatherDVI = {
        .pinTMDS = {18, 20, 22},
        .pinClock = 16,
        .invert = true,
    };
    // Waveshare RP2040-PiZero DVI
    constexpr dvi::Config dviConfig_WaveShareRp2040 = {
        .pinTMDS = {26, 24, 22},
        .pinClock = 28,
        .invert = false,
    };
     // Waveshare RP2350-PiZero DVI
    constexpr dvi::Config dviConfig_WaveShareRp2350 = {
        .pinTMDS = {36, 34, 32},
        .pinClock = 28,
        .invert = false,
    };
    // Adafruit Metro RP2350 PiZero DVI
    constexpr dvi::Config dviConfig_AdafruitMetroRP2350 = {
        .pinTMDS = {18, 16, 12},
        .pinClock = 14,
        .invert = false,
    };
    constexpr dvi::Config dviConfig_RP2XX0_TinyPCB = {
        .pinTMDS = {8, 10, 12},
        .pinClock = 6,
        .invert = true,
    };
}
#ifndef DVICONFIG
#define DVICONFIG dviConfig_PimoroniDemoDVSock
#endif