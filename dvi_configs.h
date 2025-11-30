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
        .pinClock = 38,
        .invert = false,
    };
    // Adafruit Metro RP2350 
    constexpr dvi::Config dviConfig_AdafruitMetroRP2350 = {
        .pinTMDS = {18, 16, 12},
        .pinClock = 14,
        .invert = false,
    };
     // Adafruit Fruit Jam 
    constexpr dvi::Config dviConfig_AdafruitFruitJam = {
        .pinTMDS = {15, 17, 19},
        .pinClock = 13,
        .invert = true,
    };
    constexpr dvi::Config dviConfig_RP2XX0_TinyPCB = {
        .pinTMDS = {8, 10, 12},
        .pinClock = 6,
        .invert = true,
    };
     // WaveShare RP2350 USBA
    constexpr dvi::Config dviConfig_WaveShare2350USBA = {
        .pinTMDS = {7, 9, 26},
        .pinClock = 28,
        .invert = false,
    };
    // WaveShare RP2350 USBA - Old configuration with wrong pinout for SD card
    constexpr dvi::Config dviConfig_WaveShare2350USBA_OLDConfig = {
        .pinTMDS = {5, 7, 9},
        .pinClock = 3,
        .invert = true,
    };
    // Spotpear DVI-board same as dviConfig_PicoDVI
    // According to the schematic (https://cdn.static.spotpear.com/uploads/picture/learn/raspberry-pi/rpi-pico/pico-hdmi-board/pico-hdmi-board.jpg) 
    // it should be this:
    //          pinTMDS = {11, 13, 15}, pinClock = 9, invert = true
    // but this is incorrect. Below the correct settings
    constexpr dvi::Config dviConfig_Spotpear = {
        .pinTMDS = {10, 12, 14},
        .pinClock = 8,
        .invert = true,
    };
    // Murmulator M2 without HSTX 
    constexpr dvi::Config dviConfig_Murmulator_M2 = {
        .pinTMDS = {14, 16, 18},
        .pinClock = 12,
        .invert = true,
    };
}
#ifndef DVICONFIG
#define DVICONFIG dviConfig_PimoroniDemoDVSock
#endif
