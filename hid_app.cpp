/*
 * author : Shuichi TAKANO
 * since  : Thu Jul 29 2021 03:39:11
 */

#include <tusb.h>
#include <stdio.h>
#include <string.h>
#include "xinput_host.h"
#include "gamepad.h"
#ifndef ABSWAPPED
#define ABSWAPPED 1
#endif

// set to 1 to enable printing of button states in binary when they change
#ifndef PRINTFBUTTONS
#define PRINTFBUTTONS 0
#endif

int abSwapped = ABSWAPPED;
int isManta = 0;        // 1 NES, 2 SNES
int isMantaVariant = 0; // SNES pad (0810:e501 Personal Communication Systems, Inc. SNES Gamepad)
#ifdef __cplusplus
extern "C"
{
#endif

#define MAX_REPORT 4

    namespace
    {
        uint8_t _report_count[CFG_TUH_HID];
        tuh_hid_report_info_t _report_info_arr[CFG_TUH_HID][MAX_REPORT];

        // Is dual shock 4 controller detected?
        static inline bool isDS4(uint16_t vid, uint16_t pid)
        {
            return vid == 0x054c && (pid == 0x09cc || pid == 0x05c4);
        }
        // Is dual sense controller detected?
        static inline bool isDS5(uint16_t vid, uint16_t pid)
        {
            return vid == 0x054c && pid == 0x0ce6;
        }
        // Is PSClassic controller detected?
        static inline bool isPSClassic(uint16_t vid, uint16_t pid)
        {
            return vid == 0x054c && pid == 0x0cda;
        }
        // Is Nintendo controller detected?
        static inline bool isNintendo(uint16_t vid, uint16_t pid)
        {
            return vid == 0x057e && (pid == 0x2009 || pid == 0x2017);
        }
        static inline bool isGenesisMini(uint16_t vid, uint16_t pid)
        {
            return vid == 0x0ca3 && (pid == 0x0025 || pid == 0x0024);
        }
        // Retro-bit 8 button Mega Drive Arcade Pad with USB
        static inline bool isMDArcadePad(uint16_t vid, uint16_t pid)
        {
            return (vid == 0xf0d) && (pid == 0x00c1);
        }
        // Is MantaPad detected? Cheap Aliexpress SNES/NES controller
        static inline bool isMantaPad(uint16_t vid, uint16_t pid)
        {
            return vid == 0x081f && pid == 0xe401;
        }
        // Is MantaPad Variant detected? Cheap Aliexpress SNES controller (0810:e501)
        static inline bool isMantaPadVariant(uint16_t vid, uint16_t pid)
        {
            return vid == 0x0810 && pid == 0xe501;
        }

        struct DS4Report
        {
            // https://www.psdevwiki.com/ps4/DS4-USB

            struct Button1
            {
                inline static constexpr int SQUARE = 1 << 4;
                inline static constexpr int CROSS = 1 << 5;
                inline static constexpr int CIRCLE = 1 << 6;
                inline static constexpr int TRIANGLE = 1 << 7;
            };

            struct Button2
            {
                inline static constexpr int L1 = 1 << 0;
                inline static constexpr int R1 = 1 << 1;
                inline static constexpr int L2 = 1 << 2;
                inline static constexpr int R2 = 1 << 3;
                inline static constexpr int SHARE = 1 << 4;
                inline static constexpr int OPTIONS = 1 << 5;
                inline static constexpr int L3 = 1 << 6;
                inline static constexpr int R3 = 1 << 7;
            };

            uint8_t reportID;
            uint8_t stickL[2];
            uint8_t stickR[2];
            uint8_t buttons1;
            uint8_t buttons2;
            uint8_t ps : 1;
            uint8_t tpad : 1;
            uint8_t counter : 6;
            uint8_t triggerL;
            uint8_t triggerR;
            // ...

            int getHat() const { return buttons1 & 15; }
        };

        struct DS5Report
        {
            uint8_t reportID;
            uint8_t stickL[2];
            uint8_t stickR[2];
            uint8_t triggerL;
            uint8_t triggerR;
            uint8_t counter;
            uint8_t buttons[3];
            // ...

            struct Button
            {
                inline static constexpr int SQUARE = 1 << 4;
                inline static constexpr int CROSS = 1 << 5;
                inline static constexpr int CIRCLE = 1 << 6;
                inline static constexpr int TRIANGLE = 1 << 7;
                inline static constexpr int L1 = 1 << 8;
                inline static constexpr int R1 = 1 << 9;
                inline static constexpr int L2 = 1 << 10;
                inline static constexpr int R2 = 1 << 11;
                inline static constexpr int SHARE = 1 << 12;
                inline static constexpr int OPTIONS = 1 << 13;
                inline static constexpr int L3 = 1 << 14;
                inline static constexpr int R3 = 1 << 15;
                inline static constexpr int PS = 1 << 16;
                inline static constexpr int TPAD = 1 << 17;
            };

            int getHat() const { return buttons[0] & 15; }
        };

        // Report for Genesis Mini controller and Retro-bit 8 button Mega Drive Arcade Pad with USB
        struct GenesisMiniReport
        {
            uint8_t byte1;
            uint8_t byte2;
            uint8_t byte3;
            uint8_t byte4;
            uint8_t byte5;
            uint8_t byte6;
            uint8_t byte7;
            uint8_t byte8;
            struct Button
            {

                inline static constexpr int A = 0b01000000; // byte 6
                inline static constexpr int B = 0b00100000; // byte 6
                inline static constexpr int C = 0b00000010; // byte 7
                inline static constexpr int X = 0b10001111; // byte 6
                inline static constexpr int Y = 0b00011111; // byte 6
                inline static constexpr int Z = 0b00000001; // byte 6
                inline static constexpr int MODE = 0b00010000;   // byte 7
                inline static constexpr int START = 0b00100000;  // byte 7
                inline static constexpr int UP = 0;              // byte 5
                inline static constexpr int DOWN = 0b11111111;   // byte 5
                inline static constexpr int LEFT = 0;            // byte 4
                inline static constexpr int RIGHT = 0b11111111;  // byte 4
                ;
            };
            struct ButtonRetrobit
            {

                inline static constexpr int A = 0b00000100;  // byte 1
                inline static constexpr int B = 0b00000010;  // byte 1
                inline static constexpr int C = 0b10000000;  // byte 1
                inline static constexpr int X = 0b00001000;  // byte 1
                inline static constexpr int Y = 0b00000001;  // byte 1
                inline static constexpr int Z = 0b01000000;  // byte 1
                inline static constexpr int MODE = 0b00000001;     // byte 2
                inline static constexpr int L = 0b00010000;  // byte 1
                inline static constexpr int R = 0b00100000;  // byte 1
                inline static constexpr int START = 0b00000010; // byte 2
                inline static constexpr int UP = 0;             // byte 5
                inline static constexpr int DOWN = 0b11111111;  // byte 5
                inline static constexpr int LEFT = 0;           // byte 4
                inline static constexpr int RIGHT = 0b11111111; // byte 4
                ;
            };
        };

        // Report for MantaPad, cheap AliExpress SNES controller
        struct MantaPadReport
        {
            uint8_t byte1;
            uint8_t byte2;
            uint8_t byte3;
            uint8_t byte4;
            uint8_t byte5;
            uint8_t byte6;
            uint8_t byte7;
            uint8_t byte8;

            struct Button
            {
                inline static constexpr int A = 0b00100000;
                inline static constexpr int B = 0b01000000;
                inline static constexpr int X = 0b00010000;
                inline static constexpr int Y = 0b10000000;
                inline static constexpr int NESB = X; // B Button on NES controller is X on SNES controller
                inline static constexpr int SELECT = 0b00010000;
                inline static constexpr int START = 0b00100000;
                inline static constexpr int UP = 0b00000000;
                inline static constexpr int DOWN = 0b11111111;
                inline static constexpr int LEFT = 0b00000000;
                inline static constexpr int RIGHT = 0b11111111;
                inline static constexpr int SHOULDERLEFT = 0b00000001;
                inline static constexpr int SHOULDERRIGHT = 0b00000010;
            };
        };
        // Report for PSClassic controller
        struct PSClassicReport
        {
            uint8_t buttons;
            // Idle      00010100
            // up        00000100
            // upright   00001000
            // right     00011000
            // rightdown 00101000
            // down      00100100
            // downleft  00100000
            // left      00010000
            // leftup    00100000
            // start     00010110
            // select    00010101
            // St + sel  00010111
            // selectup  00000101
            // selectdown 00100101
            uint8_t hat;
            struct Button
            {
                inline static constexpr int ButtonsIdle = 0x00;
                inline static constexpr int HatIdle = 0b00010100;
                inline static constexpr int Square = 0x08;
                inline static constexpr int Circle = 0x02;
                inline static constexpr int Cross = 0x04;
                inline static constexpr int Triangle = 0x01;
                inline static constexpr int SELECT = 0b00010101;
                inline static constexpr int START = 0b00010110;
                inline static constexpr int UP = 0b00000100;
                inline static constexpr int UPRIGHT = 0b00001000;
                inline static constexpr int RIGHT = 0b00011000;
                inline static constexpr int RIGHTDOWN = 0b00101000;
                inline static constexpr int DOWN = 0b00100100;
                inline static constexpr int DOWNLEFT = 0b00100000;
                inline static constexpr int LEFT = 0b00010000;
                inline static constexpr int LEFTUP = 0b00000000;
                inline static constexpr int SELECTUP = 0b00000101;
                inline static constexpr int SELECTDOWN = 0b00100101;
                inline static constexpr int SELECTSTART = 0b00010111;
            };
        };

        // Helper: print a HID report (any length up to MAX) as binary only when it changes.
        static void printHIDReportIfChanged(const uint8_t *report, uint16_t len)
        {
            if (!report || !len)
                return;
            // Typical HID report sizes are <= 64 bytes for full-speed devices
            constexpr uint16_t MAX_PRINT = 64; // adjust if you expect bigger
            static uint8_t prev_report[MAX_PRINT];
            static uint16_t prev_len = 0;
            static bool prev_valid = false;

            if (len > MAX_PRINT)
            {
                // Fallback: if size exceeds buffer, just print length notice once (or always?)
                // Print raw truncated binary when it changes relative to first MAX_PRINT bytes.
                // Compare only first MAX_PRINT bytes.
                bool changed_large = !prev_valid || prev_len != len || memcmp(prev_report, report, MAX_PRINT) != 0;
                if (!changed_large)
                    return;
                printf("HID report (len=%u > MAX_PRINT=%u) (changed bytes in []): ", len, MAX_PRINT);
                for (uint16_t i = 0; i < MAX_PRINT; ++i)
                {
                    bool byte_changed = !prev_valid || prev_report[i] != report[i];
                    if (byte_changed)
                        putchar('[');
                    uint8_t v = report[i];
                    for (int b = 7; b >= 0; --b)
                        putchar((v & (1 << b)) ? '1' : '0');
                    if (byte_changed)
                        putchar(']');
                    if (i + 1 < MAX_PRINT)
                        putchar(' ');
                }
                putchar('\n');
                memcpy(prev_report, report, MAX_PRINT);
                prev_len = len; // store true len for change detection
                prev_valid = true;
                return;
            }

            bool changed = !prev_valid || prev_len != len || memcmp(prev_report, report, len) != 0;
            if (!changed)
                return;

            printf("HID report (len=%u) (bin, changed bytes in []): ", len);
            for (uint16_t i = 0; i < len; ++i)
            {
                bool byte_changed = !prev_valid || prev_report[i] != report[i];
                if (byte_changed)
                    putchar('[');
                uint8_t v = report[i];
                for (int b = 7; b >= 0; --b)
                    putchar((v & (1 << b)) ? '1' : '0');
                if (byte_changed)
                    putchar(']');
                if (i + 1 < len)
                    putchar(' ');
            }
            putchar('\n');
            memcpy(prev_report, report, len);
            prev_len = len;
            prev_valid = true;
        }
    }

    void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len)
    {
        uint16_t vid, pid;
        isManta = 0;
        isMantaVariant = 0;
        auto &gp = io::getCurrentGamePadState(0);
        tuh_vid_pid_get(dev_addr, &vid, &pid);

        if (isDS4(vid, pid))
        {
            printf("Dual Shock 4 Controller detected - device address = %d, instance = %d is mounted - ", dev_addr, instance);
            strcpy(gp.GamePadName, "Dual Shock 4");
        }
        else if (isDS5(vid, pid))
        {
            printf("Dual Sense Controller detected - device address = %d, instance = %d is mounted - ", dev_addr, instance);
            strcpy(gp.GamePadName, "Dual Sense");
        }
        else if (isMantaPad(vid, pid))
        {
            printf("MantaPad detected - device address = %d, instance = %d is mounted - ", dev_addr, instance);
            printf("Press Y to activate SNES mode\n");
            isManta = 1;
            strcpy(gp.GamePadName, "Manta NES");
        }
        else if (isMantaPadVariant(vid, pid))
        {
            printf("MantaPad Variant detected - device address = %d, instance = %d is mounted - ", dev_addr, instance);
            isManta = 2;
            isMantaVariant = 1;
            strcpy(gp.GamePadName, "Manta Variant SNES");
        }
        else if (isGenesisMini(vid, pid))
        {
            printf("Sega Mega Drive/Genesis Mini %d controller detected - device address = %d, instance = %d is mounted - ", (pid == 0x0025) ? 1 : 2, dev_addr, instance);
            sprintf(gp.GamePadName, "Genesis Mini %d", (pid == 0x0025) ? 1 : 2);
        }
        else if (isMDArcadePad(vid, pid))
        {
            printf("Retro-bit MD Arcade pad detected - device address = %d, instance = %d is mounted - ", dev_addr, instance);
            sprintf(gp.GamePadName, "MDArcade");
        }
        else if (isPSClassic(vid, pid))
        {
            printf("PlayStation Classic controller detected - device address = %d, instance = %d is mounted - ", dev_addr, instance);
            strcpy(gp.GamePadName, "PSClassic");
        }
        // else if (isNintendo(vid, pid))
        // {
        //     printf("(Unsupported) Nintendo controller detected - device address = %d, instance = %d is mounted - ", dev_addr, instance);
        //     strcpy(gp.GamePadName, "Nintendo");
        // }
        else
        {
            printf("Unkown device detected - HID device address = %d, instance = %d is mounted - ", dev_addr, instance);
            sprintf(gp.GamePadName, "%04x:%04x", vid, pid);
        }
        printf("VID = %04x, PID = %04x\r\n", vid, pid);
        const char *protocol_str[] = {"None", "Keyboard", "Mouse"}; // hid_protocol_type_t
        uint8_t const interface_protocol = tuh_hid_interface_protocol(dev_addr, instance);

        // Parse report descriptor with built-in parser
        _report_count[instance] = tuh_hid_parse_report_descriptor(_report_info_arr[instance], MAX_REPORT, desc_report, desc_len);
        printf("HID has %u reports and interface protocol = %d:%s\n", _report_count[instance],
               interface_protocol, protocol_str[interface_protocol]);

        if (!tuh_hid_receive_report(dev_addr, instance))
        {
            printf("Error: cannot request to receive report\r\n");
        }
    }

    void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
    {
        printf("HID device address = %d, instance = %d is unmounted\n", dev_addr, instance);
        // Assume the controller is disconnected
        auto &gp = io::getCurrentGamePadState(0);
        gp.flagConnected(false);
        gp.GamePadName[0] = 0;
    }

    void tuh_hid_report_received_cb(uint8_t dev_addr,
                                    uint8_t instance, uint8_t const *report, uint16_t len)
    {
        uint8_t const rpt_count = _report_count[instance];
        tuh_hid_report_info_t *rpt_info_arr = _report_info_arr[instance];
        tuh_hid_report_info_t *rpt_info = NULL;

        uint16_t vid, pid;
        tuh_vid_pid_get(dev_addr, &vid, &pid);
#if PRINTFBUTTONS
        // helper for figuring out button states
        printHIDReportIfChanged(report, len);
#endif
        if (isDS4(vid, pid))
        {
            if (sizeof(DS4Report) <= len)
            {
                auto r = reinterpret_cast<const DS4Report *>(report);
                if (r->reportID != 1)
                {
                    printf("Invalid reportID %d\n", r->reportID);
                    return;
                }

                auto &gp = io::getCurrentGamePadState(0);
                gp.axis[0] = r->stickL[0];
                gp.axis[1] = r->stickL[1];
                if (abSwapped)
                {
                    gp.buttons = (r->buttons1 & DS4Report::Button1::CROSS ? io::GamePadState::Button::B : 0) |
                                 (r->buttons1 & DS4Report::Button1::CIRCLE ? io::GamePadState::Button::A : 0);
                }
                else
                {
                    gp.buttons = (r->buttons1 & DS4Report::Button1::CROSS ? io::GamePadState::Button::A : 0) |
                                 (r->buttons1 & DS4Report::Button1::CIRCLE ? io::GamePadState::Button::B : 0);
                }

                gp.buttons = gp.buttons |
                             (r->buttons1 & DS4Report::Button1::TRIANGLE ? io::GamePadState::Button::X : 0) |
                             (r->buttons1 & DS4Report::Button1::SQUARE ? io::GamePadState::Button::Y : 0) |
                             (r->buttons2 & DS4Report::Button2::SHARE ? io::GamePadState::Button::SELECT : 0) |
                             (r->tpad ? io::GamePadState::Button::SELECT : 0) |
                             (r->buttons2 & DS4Report::Button2::OPTIONS ? io::GamePadState::Button::START : 0);
                gp.hat = static_cast<io::GamePadState::Hat>(r->getHat());
                gp.convertButtonsFromAxis(0, 1);
                gp.convertButtonsFromHat();
                gp.flagConnected(true);
            }
            else
            {
                printf("Invalid DS4 report size %zd\n", len);
                return;
            }
        }
        else if (isDS5(vid, pid))
        {
            if (sizeof(DS5Report) <= len)
            {

                auto r = reinterpret_cast<const DS5Report *>(report);
                if (r->reportID != 1)
                {
                    printf("Invalid reportID %d\n", r->reportID);
                    return;
                }

                auto buttons = r->buttons[0] | (r->buttons[1] << 8) | (r->buttons[2] << 16);

                auto &gp = io::getCurrentGamePadState(0);
                gp.axis[0] = r->stickL[0];
                gp.axis[1] = r->stickL[1];
                if (abSwapped)
                {
                    gp.buttons =
                        (buttons & DS5Report::Button::CROSS ? io::GamePadState::Button::B : 0) |
                        (buttons & DS5Report::Button::CIRCLE ? io::GamePadState::Button::A : 0);
                }
                else
                {
                    gp.buttons = (buttons & DS5Report::Button::CIRCLE ? io::GamePadState::Button::B : 0) |
                                 (buttons & DS5Report::Button::CROSS ? io::GamePadState::Button::A : 0);
                }
                gp.buttons =
                    gp.buttons |
                    (buttons & DS5Report::Button::TRIANGLE ? io::GamePadState::Button::X : 0) |
                    (buttons & DS5Report::Button::SQUARE ? io::GamePadState::Button::Y : 0) |
                    (buttons & (DS5Report::Button::SHARE | DS5Report::Button::TPAD) ? io::GamePadState::Button::SELECT : 0) |
                    (buttons & DS5Report::Button::OPTIONS ? io::GamePadState::Button::START : 0);
                gp.hat = static_cast<io::GamePadState::Hat>(r->getHat());
                gp.convertButtonsFromAxis(0, 1);
                gp.convertButtonsFromHat();
                gp.flagConnected(true);
            }
            else
            {
                printf("Invalid DS5 report size %zd\n", len);
                return;
            }
        }
        else if (isMantaPad(vid, pid) || isMantaPadVariant(vid, pid))
        {
            if (sizeof(MantaPadReport) == len)
            {
                auto r = reinterpret_cast<const MantaPadReport *>(report);
                auto &gp = io::getCurrentGamePadState(0);
                uint8_t udb = r->byte2;
                uint8_t lrb = r->byte1;

                // When using AlieExpress SNES usb controller, activate SNES mode by pressing Y
                if (r->byte6 & MantaPadReport::Button::Y)
                {
                    isManta = 2;
                    strcpy(gp.GamePadName, "Manta SNES");
                    // printf("MantaPad SNES mode activated\n");
                }
                // printf("MantaPad report: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                //        r->byte1, r->byte2, r->byte3, r->byte4, r->byte5, r->byte6, r->byte7, r->byte8);

                if (isMantaVariant)
                {
                    udb = r->byte5; // UP / DOWN
                    lrb = r->byte4; // LEFT / RIGHT
                }
                if (abSwapped)
                {
                    gp.buttons =
                        (r->byte6 & MantaPadReport::Button::A ? io::GamePadState::Button::A : 0) |
                        ((isManta == 1 && r->byte6 & MantaPadReport::Button::NESB) ? io::GamePadState::Button::B : 0) |
                        ((isManta == 2 && r->byte6 & MantaPadReport::Button::B) ? io::GamePadState::Button::B : 0) |
                        ((isManta == 2 && r->byte6 & MantaPadReport::Button::X) ? io::GamePadState::Button::X : 0) |
                        (r->byte7 & MantaPadReport::Button::START ? io::GamePadState::Button::START : 0) |
                        (r->byte7 & MantaPadReport::Button::SELECT ? io::GamePadState::Button::SELECT : 0) |
                        (udb == MantaPadReport::Button::UP ? io::GamePadState::Button::UP : 0) |
                        (udb == MantaPadReport::Button::DOWN ? io::GamePadState::Button::DOWN : 0) |
                        (lrb == MantaPadReport::Button::LEFT ? io::GamePadState::Button::LEFT : 0) |
                        (lrb == MantaPadReport::Button::RIGHT ? io::GamePadState::Button::RIGHT : 0);
                }
                else
                {
                    gp.buttons =
                        (r->byte6 & MantaPadReport::Button::A ? io::GamePadState::Button::B : 0) |
                        ((isManta == 1 && r->byte6 & MantaPadReport::Button::NESB) ? io::GamePadState::Button::A : 0) |
                        ((isManta == 2 && r->byte6 & MantaPadReport::Button::B) ? io::GamePadState::Button::A : 0) |
                        ((isManta == 2 && r->byte6 & MantaPadReport::Button::X) ? io::GamePadState::Button::X : 0) |
                        (r->byte7 & MantaPadReport::Button::START ? io::GamePadState::Button::START : 0) |
                        (r->byte7 & MantaPadReport::Button::SELECT ? io::GamePadState::Button::SELECT : 0) |
                        (udb == MantaPadReport::Button::UP ? io::GamePadState::Button::UP : 0) |
                        (udb == MantaPadReport::Button::DOWN ? io::GamePadState::Button::DOWN : 0) |
                        (lrb == MantaPadReport::Button::LEFT ? io::GamePadState::Button::LEFT : 0) |
                        (lrb == MantaPadReport::Button::RIGHT ? io::GamePadState::Button::RIGHT : 0);
                }
                gp.flagConnected(true);
            }
            else
            {
                printf("Invalid MantaPad report size %zd\n", len);
                return;
            }
        }
        else if (isGenesisMini(vid, pid))
        {
            if (sizeof(GenesisMiniReport) == len)
            {
                

                auto r = reinterpret_cast<const GenesisMiniReport *>(report);

                auto &gp = io::getCurrentGamePadState(0);
                if (abSwapped)
                {
                    gp.buttons = (r->byte6 & GenesisMiniReport::Button::B ? io::GamePadState::Button::A : 0) |
                                 (r->byte6 & GenesisMiniReport::Button::A ? io::GamePadState::Button::B : 0);
                }
                else
                {
                    gp.buttons = (r->byte6 & GenesisMiniReport::Button::A ? io::GamePadState::Button::A : 0) |
                                 (r->byte6 & GenesisMiniReport::Button::B ? io::GamePadState::Button::B : 0);
                }
                gp.buttons = gp.buttons |
                             (((r->byte7 & GenesisMiniReport::Button::C) && pid == 0x0025)? io::GamePadState::Button::SELECT : 0) |  // C button is SELECT only original 3-button controller
                             ((r->byte7 & GenesisMiniReport::Button::C) ? io::GamePadState::Button::C : 0) |
                             (r->byte7 & GenesisMiniReport::Button::START ? io::GamePadState::Button::START : 0) |
                             (r->byte5 == GenesisMiniReport::Button::UP ? io::GamePadState::Button::UP : 0) |
                             (r->byte5 == GenesisMiniReport::Button::DOWN ? io::GamePadState::Button::DOWN : 0) |
                             (r->byte4 == GenesisMiniReport::Button::LEFT ? io::GamePadState::Button::LEFT : 0) |
                             (r->byte4 == GenesisMiniReport::Button::RIGHT ? io::GamePadState::Button::RIGHT : 0);
                gp.flagConnected(true);
            }
            else
            {
                printf("Invalid Genesis Mini report size %zd\n", len);
                return;
            }
        }
        else if (isMDArcadePad(vid, pid))
        {
            if (sizeof(GenesisMiniReport) == len)
            {
                printHIDReportIfChanged(report, len);

                auto r = reinterpret_cast<const GenesisMiniReport *>(report);

                auto &gp = io::getCurrentGamePadState(0);
                if (abSwapped)
                {
                    gp.buttons = (r->byte1 & GenesisMiniReport::ButtonRetrobit::B ? io::GamePadState::Button::A : 0) |
                                 (r->byte1 & GenesisMiniReport::ButtonRetrobit::A ? io::GamePadState::Button::B : 0);
                }
                else
                {
                    gp.buttons = (r->byte1 & GenesisMiniReport::ButtonRetrobit::A ? io::GamePadState::Button::A : 0) |
                                 (r->byte1 & GenesisMiniReport::ButtonRetrobit::B ? io::GamePadState::Button::B : 0);
                }
                gp.buttons = gp.buttons |
                             (r->byte1 & GenesisMiniReport::ButtonRetrobit::MODE ? io::GamePadState::Button::SELECT : 0) |
                             (r->byte1 & GenesisMiniReport::ButtonRetrobit::C ? io::GamePadState::Button::C : 0) |
                             (r->byte2 & GenesisMiniReport::ButtonRetrobit::START ? io::GamePadState::Button::START : 0) |
                             (r->byte5 == GenesisMiniReport::ButtonRetrobit::UP ? io::GamePadState::Button::UP : 0) |
                             (r->byte5 == GenesisMiniReport::ButtonRetrobit::DOWN ? io::GamePadState::Button::DOWN : 0) |
                             (r->byte4 == GenesisMiniReport::ButtonRetrobit::LEFT ? io::GamePadState::Button::LEFT : 0) |
                             (r->byte4 == GenesisMiniReport::ButtonRetrobit::RIGHT ? io::GamePadState::Button::RIGHT : 0);
                gp.flagConnected(true);
            }
            else
            {
                printf("Invalid Genesis Mini report size %zd\n", len);
                return;
            }
        }
        else if (isPSClassic(vid, pid))
        {
            if (sizeof(PSClassicReport) == len)
            {
                auto r = reinterpret_cast<const PSClassicReport *>(report);
                auto &gp = io::getCurrentGamePadState(0);
                if (abSwapped)
                {
                    gp.buttons = (r->buttons & PSClassicReport::Button::Cross ? io::GamePadState::Button::B : 0) |
                                 (r->buttons & PSClassicReport::Button::Circle ? io::GamePadState::Button::A : 0);
                }
                else
                {
                    gp.buttons = (r->buttons & PSClassicReport::Button::Circle ? io::GamePadState::Button::B : 0) |
                                 (r->buttons & PSClassicReport::Button::Cross ? io::GamePadState::Button::A : 0);
                }
                gp.buttons |= (r->buttons & PSClassicReport::Button::Triangle ? io::GamePadState::Button::X : 0);
                switch (r->hat)
                {
                case PSClassicReport::Button::UP:
                    gp.buttons = gp.buttons | io::GamePadState::Button::UP;
                    break;
                case PSClassicReport::Button::UPRIGHT:
                    gp.buttons = gp.buttons | io::GamePadState::Button::UP | io::GamePadState::Button::RIGHT;
                    break;
                case PSClassicReport::Button::RIGHT:
                    gp.buttons = gp.buttons | io::GamePadState::Button::RIGHT;
                    break;
                case PSClassicReport::Button::RIGHTDOWN:
                    gp.buttons = gp.buttons | io::GamePadState::Button::RIGHT | io::GamePadState::Button::DOWN;
                    break;
                case PSClassicReport::Button::DOWN:
                    gp.buttons = gp.buttons | io::GamePadState::Button::DOWN;
                    break;
                case PSClassicReport::Button::DOWNLEFT:
                    gp.buttons = gp.buttons | io::GamePadState::Button::DOWN | io::GamePadState::Button::LEFT;
                    break;
                case PSClassicReport::Button::LEFT:
                    gp.buttons = gp.buttons | io::GamePadState::Button::LEFT;
                    break;
                case PSClassicReport::Button::LEFTUP:
                    gp.buttons = gp.buttons | io::GamePadState::Button::LEFT | io::GamePadState::Button::UP;
                    break;
                case PSClassicReport::Button::SELECT:
                    gp.buttons = gp.buttons | io::GamePadState::Button::SELECT;
                    break;
                case PSClassicReport::Button::START:
                    gp.buttons = gp.buttons | io::GamePadState::Button::START;
                    break;
                case PSClassicReport::Button::SELECTSTART:
                    gp.buttons = gp.buttons | io::GamePadState::Button::SELECT | io::GamePadState::Button::START;
                    break;
                case PSClassicReport::Button::SELECTUP:
                    gp.buttons = gp.buttons | io::GamePadState::Button::SELECT | io::GamePadState::Button::UP;
                    break;
                case PSClassicReport::Button::SELECTDOWN:
                    gp.buttons = gp.buttons | io::GamePadState::Button::SELECT | io::GamePadState::Button::DOWN;
                    break;
                default:
                    break;
                }
                gp.flagConnected(true);
            }
            else
            {
                printf("Invalid PSClassic Mini report size %zd\n", len);
                return;
            }
        }
        else if (isNintendo(vid, pid))
        {
            printf("Nintendo: len = %d\n", len); // Nintendo controllers do not report back
        }
        else
        {
            if (rpt_count == 1 && rpt_info_arr[0].report_id == 0)
            {
                // Simple report without report ID as 1st byte
                rpt_info = &rpt_info_arr[0];
            }
            else
            {
                // Composite report, 1st byte is report ID, data starts from 2nd byte
                uint8_t const rpt_id = report[0];

                // Find report id in the arrray
                for (uint8_t i = 0; i < rpt_count; i++)
                {
                    if (rpt_id == rpt_info_arr[i].report_id)
                    {
                        rpt_info = &rpt_info_arr[i];
                        break;
                    }
                }

                report++;
                len--;
            }

            if (!rpt_info)
            {
                printf("Couldn't find the report info for this report !\n");
                return;
            }

            //        printf("usage %d, %d\n", rpt_info->usage_page, rpt_info->usage);

            if (rpt_info->usage_page == HID_USAGE_PAGE_DESKTOP)
            {
                switch (rpt_info->usage)
                {
                case HID_USAGE_DESKTOP_KEYBOARD:
                {
                    auto r = reinterpret_cast<const hid_keyboard_report_t *>(report);
                    auto &gp = io::getCurrentGamePadState(0);
                    strcpy(gp.GamePadName, "Keyboard");
                    gp.buttons = 0;
                    for (uint8_t i = 0; i < 6; i++)
                    {
                        if (r->keycode[i])
                        {
                            switch (r->keycode[i])
                            {
                            case HID_KEY_A:
                                gp.buttons |= io::GamePadState::Button::SELECT;
                                break;
                            case HID_KEY_S:
                                gp.buttons |= io::GamePadState::Button::START;
                                break;
                            case HID_KEY_Z:
                                gp.buttons |= (abSwapped ? io::GamePadState::Button::B : io::GamePadState::Button::A);
                                break;
                            case HID_KEY_X:
                                gp.buttons |= (abSwapped ? io::GamePadState::Button::A : io::GamePadState::Button::B);
                                break;
                            case HID_KEY_C:
                                gp.buttons |= io::GamePadState::Button::X;
                                break;
                            case HID_KEY_ARROW_UP:
                                gp.buttons |= io::GamePadState::Button::UP;
                                break;
                            case HID_KEY_ARROW_DOWN:
                                gp.buttons |= io::GamePadState::Button::DOWN;
                                break;
                            case HID_KEY_ARROW_LEFT:
                                gp.buttons |= io::GamePadState::Button::LEFT;
                                break;
                            case HID_KEY_ARROW_RIGHT:
                                gp.buttons |= io::GamePadState::Button::RIGHT;
                                break;
                            default:
                                break;
                            }
                        }
                    }
                    break;
                }

                case HID_USAGE_DESKTOP_MOUSE:
                    TU_LOG1("HID receive mouse report\n");
                    // Assume mouse follow boot report layout
                    //                process_mouse_report((hid_mouse_report_t const *)report);
                    break;

                case HID_USAGE_DESKTOP_JOYSTICK:
                {
                    // TU_LOG1("HID receive joystick report\n");
                    struct JoyStickReport
                    {
                        uint8_t axis[3];
                        uint8_t buttons;
                        // 実際のところはしらん
                    };
                    auto *rep = reinterpret_cast<const JoyStickReport *>(report);
                    //                printf("x %d y %d button %02x\n", rep->axis[0], rep->axis[1], rep->buttons);
                    auto &gp = io::getCurrentGamePadState(0);
                    gp.axis[0] = rep->axis[0];
                    gp.axis[1] = rep->axis[1];
                    gp.axis[2] = rep->axis[2];
                    gp.buttons = rep->buttons;
                    gp.convertButtonsFromAxis(0, 1);

                    // BUFFALO BGC-FC801
                    // VID = 0411, PID = 00c6
                }
                break;

                case HID_USAGE_DESKTOP_GAMEPAD:
                    TU_LOG1("HID receive gamepad report\n");

                    break;

                default:
                    break;
                }
            }
        }

        if (!tuh_hid_receive_report(dev_addr, instance))
        {
            printf("Error: cannot request to receive report\r\n");
        }
    }
#pragma region XINPUT
    // Since https://github.com/hathach/tinyusb/pull/2222, we can add in custom vendor drivers easily
    usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *driver_count)
    {
        *driver_count = 1;
        return &usbh_xinput_driver;
    }

    // XXInput type of controlles. (Xbox 360, Xbox One, Xbox Series X)
    // This is somewhat flaky and might not work with all controllers.
    // Tested devices:
    //    - xbox Series X controller : Works
    //    - xbox One controller : Works
    //    - xbox elite controller : Works
    //    - 8bitdo SN30 Pro+ V6.01: Works. Hold X + Start to switch to Xinput mode. (LED 1 and 2 will blink). Then connect to USB.
    //    - 8bitdo Pro 2 V3.04: Works. Hold X + Start to switch to Xinput mode. (LED 1 and 2 will blink). Then connect to USB.
    //    - 8bitdo SN30 PRO Wired : Not working, recognized but no report
    //    - 8bitdo SF30 v2.05 Pro : Works Hold X + Start to switch to Xinput mode. (LED 1 and 2 will blink). Then connect to USB.
    //    - 8bitdo SN30 v2.05 Pro : Not tested, should probably work
    //
    // Troubleshooting:
    //  After flashing some bigger games, the controller might become unresponsive:
    //      - XBOX Controller. Play always with batteries removed. When controller becomes unresponsive:
    //              - unplug and replug the controller.
    //              - If controller is still unresponsive, unplug the pico from power, wait a few seconds then plug it back in.
    //              - Press start on the controller to start the last flashed game.
    //     - 8bitdo controllers, when controller becomes unresponsive:
    //              - Disconnect the controller
    //              - Hold start to switch the controller off (if it has built-in battery).
    //              - reconnect the controller.
    //              - Press start to start the last flashed game.
    void tuh_xinput_report_received_cb(uint8_t dev_addr, uint8_t instance, xinputh_interface_t const *xid_itf, uint16_t len)
    {
        const xinput_gamepad_t *p = &xid_itf->pad;
        const char *type_str;
        isManta = 0;
        if (xid_itf->last_xfer_result == XFER_RESULT_SUCCESS)
        {
            switch (xid_itf->type)
            {
            case 1:
                type_str = "Xbox One";
                break;
            case 2:
                type_str = "Xbox 360 Wireless";
                break;
            case 3:
                type_str = "Xbox 360 Wired";
                break;
            case 4:
                type_str = "Xbox OG";
                break;
            default:
                type_str = "Unknown";
            }

            if (xid_itf->connected && xid_itf->new_pad_data)
            {

                auto &gp = io::getCurrentGamePadState(0);
                gp.buttons = 0;

                if (p->wButtons & XINPUT_GAMEPAD_A)
                    gp.buttons |= (abSwapped ? io::GamePadState::Button::B : io::GamePadState::Button::A);
                if (p->wButtons & XINPUT_GAMEPAD_B)
                    gp.buttons |= (abSwapped ? io::GamePadState::Button::A : io::GamePadState::Button::B);
                if (p->wButtons & XINPUT_GAMEPAD_Y)
                    gp.buttons |= io::GamePadState::Button::X;
                if (p->wButtons & XINPUT_GAMEPAD_DPAD_UP)
                    gp.buttons |= io::GamePadState::Button::UP;
                if (p->wButtons & XINPUT_GAMEPAD_DPAD_DOWN)
                    gp.buttons |= io::GamePadState::Button::DOWN;
                if (p->wButtons & XINPUT_GAMEPAD_DPAD_LEFT)
                    gp.buttons |= io::GamePadState::Button::LEFT;
                if (p->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT)
                    gp.buttons |= io::GamePadState::Button::RIGHT;
                if (p->wButtons & XINPUT_GAMEPAD_START)
                    gp.buttons |= io::GamePadState::Button::START;
                if (p->wButtons & XINPUT_GAMEPAD_BACK)
                    gp.buttons |= io::GamePadState::Button::SELECT;
                if (p->wButtons & XINPUT_GAMEPAD_GUIDE)
                    gp.buttons |= (io::GamePadState::Button::START | io::GamePadState::Button::SELECT);
                gp.flagConnected(true);
            }
        }
        tuh_xinput_receive_report(dev_addr, instance);
    }

    void tuh_xinput_mount_cb(uint8_t dev_addr, uint8_t instance, const xinputh_interface_t *xinput_itf)
    {
        auto &gp = io::getCurrentGamePadState(0);
        strcpy(gp.GamePadName, "XInput");
        printf("XINPUT MOUNTED %02x %d\n", dev_addr, instance);
        // If this is a Xbox 360 Wireless controller we need to wait for a connection packet
        // on the in pipe before setting LEDs etc. So just start getting data until a controller is connected.
        if (xinput_itf->type == XBOX360_WIRELESS && xinput_itf->connected == false)
        {
            tuh_xinput_receive_report(dev_addr, instance);
            return;
        }
        tuh_xinput_set_led(dev_addr, instance, 0, true);
        tuh_xinput_set_led(dev_addr, instance, 1, true);
        tuh_xinput_set_rumble(dev_addr, instance, 0, 0, true);
        tuh_xinput_receive_report(dev_addr, instance);
    }

    void tuh_xinput_umount_cb(uint8_t dev_addr, uint8_t instance)
    {
        auto &gp = io::getCurrentGamePadState(0);
        gp.GamePadName[0] = 0;
        gp.flagConnected(false);
        printf("XINPUT UNMOUNTED %02x %d\n", dev_addr, instance);
    }
#pragma endregion
#ifdef __cplusplus
}
#endif
