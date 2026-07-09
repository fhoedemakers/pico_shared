/*
 * author : Shuichi TAKANO
 * since  : Fri Jul 30 2021 04:42:27
 */
#ifndef _510036F3_0134_6411_4376_A918ACA8AC4C
#define _510036F3_0134_6411_4376_A918ACA8AC4C

#include <stdint.h>

namespace io
{
    struct GamePadState
    {
        bool connected{false};
        struct Button
        {
            inline static constexpr int A = 1 << 0;
            inline static constexpr int B = 1 << 1;
            inline static constexpr int X = 1 << 2;
            inline static constexpr int Y = 1 << 3;
            inline static constexpr int C = 1 << 4;
            inline static constexpr int Z = 1 << 5;
            inline static constexpr int SELECT = 1 << 6;
            inline static constexpr int START = 1 << 7;
            inline static constexpr int L = 1 << 8;
            inline static constexpr int R = 1 << 9;

            inline static constexpr int LEFT = 1 << 31;
            inline static constexpr int RIGHT = 1 << 30;
            inline static constexpr int UP = 1 << 29;
            inline static constexpr int DOWN = 1 << 28;
        };

        enum class Hat
        {
            N,
            NE,
            E,
            SE,
            S,
            SW,
            W,
            NW,
            RELEASED,
        };

        uint8_t axis[3]{0x80, 0x80, 0x80};
        Hat hat{Hat::RELEASED};
        uint32_t buttons{0};
       
    public:
        void convertButtonsFromAxis(int axisX, int axisY);
        void convertButtonsFromHat();
        void flagConnected(bool connected) { this->connected = connected; }
        bool isConnected() const { return connected; }
        const char *GamePadName{nullptr};
        const char *GamePadShortName{nullptr};
    };

    GamePadState &getCurrentGamePadState(int i);

    // Raw HID keyboard state. Populated by hid_app.cpp from the most recent
    // boot-protocol keyboard report. modifier matches HID_KEYBOARD_MODIFIER_*
    // and keycode[] holds the up to 6 currently-pressed HID usage codes
    // (HID_KEY_*). Emulators that emulate a real keyboard (e.g. O2 / G7400)
    // should read this in addition to the GamePadState mapping.
    struct KeyboardState
    {
        uint8_t modifier;
        uint8_t keycode[6];
    };
    const KeyboardState &getCurrentKeyboardState();

    // USB HID mouse state. Populated by hid_app.cpp from boot-protocol mouse
    // reports (and generic desktop-mouse reports). Movement and wheel are
    // accumulated deltas: the consumer should read them and reset dx/dy/wheel
    // to zero after processing (single-consumer model). buttons matches
    // MOUSE_BUTTON_* from TinyUSB (bit 0 left, bit 1 right, bit 2 middle).
    struct MouseState
    {
        bool connected{false};
        uint8_t buttons{0};
        int32_t dx{0};
        int32_t dy{0};
        int32_t wheel{0};
    };
    MouseState &getCurrentMouseState();
}

#endif /* _510036F3_0134_6411_4376_A918ACA8AC4C */
