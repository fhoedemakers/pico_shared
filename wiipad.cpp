// Support for Wii Classic Controller and similar devices over I2C

#if WII_PIN_SDA >= 0 and WII_PIN_SCL >= 0
#include "stdio.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "wiipad.h"
static bool wiipad_connected = false;

bool wiipad_is_connected() {
    return wiipad_connected;
}

void wiipad_begin(void) {
    i2c_init(WII_I2C, 400000);
    gpio_set_function(WII_PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(WII_PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(WII_PIN_SDA);
    gpio_pull_up(WII_PIN_SCL);
    uint8_t init1[] = { 0xF0, 0x55 };
    uint8_t init2[] = { 0xFB, 0x00 };
    if ( i2c_write_timeout_us(WII_I2C, WII_ADDR, init1, 2, false, 300) == 2) {
        sleep_ms(100);
        printf("Wii Pad first init step successful\n");
        if ( i2c_write_timeout_us(WII_I2C, WII_ADDR, init2, 2, false, 300) == 2) {
            sleep_ms(100);
            wiipad_connected = true;
            printf("Wii Pad initialized successfully\n");
            return;
        }
    }
    wiipad_connected = false;
    //printf("Wii Pad init failed, no controller found\n");
}

uint16_t wiipad_read(void) {
    static constexpr int LEFT = 1 << 6;
    static constexpr int RIGHT = 1 << 7;
    static constexpr int UP = 1 << 4;
    static constexpr int DOWN = 1 << 5;
    static constexpr int SELECT = 1 << 2;
    static constexpr int START = 1 << 3;
    static constexpr int A = 1 << 0;
    static constexpr int B = 1 << 1;
    static constexpr int X = 1 << 8;
    static constexpr int Y = 1 << 9;
    static constexpr int ALL_BUTTONS = LEFT | RIGHT | UP | DOWN | SELECT | START | A | B | X | Y;
    uint16_t v = 0;
    static uint16_t previousState = 0;
    uint8_t req[] = { 0x00 };
    uint8_t buf[6];
    i2c_write_timeout_us(WII_I2C, WII_ADDR, req, 1, false, 100);
    sleep_us(200);
    if (i2c_read_timeout_us(WII_I2C, WII_ADDR, buf, sizeof buf, false, 600) == sizeof buf) {
        if (!(buf[5] & 0x01)) v |= UP;
        if (!(buf[4] & 0x40)) v |= DOWN;
        if (!(buf[5] & 0x02)) v |= LEFT;
        if (!(buf[4] & 0x80)) v |= RIGHT;
        if (!(buf[5] & 0x10)) v |= A;
        if (!(buf[5] & 0x40)) v |= B;
        if (!(buf[5] & 0x08)) v |= X;
        if (!(buf[5] & 0x20)) v |= Y;
        if (!(buf[4] & 0x10)) v |= SELECT;
        if (!(buf[4] & 0x04)) v |= START;
        // Strange behavior, occurs  only in GenesisPlus with SNES controllers
        // When no buttons are pressed, wiipad returns all buttons pressed
        // If this happens, we need to revert to the previous state
        if (v ==ALL_BUTTONS) {
            v = previousState;
        }
    }
    previousState = v;
    return v;
}

void wiipad_end(void) {
    i2c_deinit(WII_I2C);
    gpio_set_pulls(WII_PIN_SDA, 0, 0);
    gpio_set_pulls(WII_PIN_SCL, 0, 0);
    gpio_set_function(WII_PIN_SDA, GPIO_FUNC_NULL);
    gpio_set_function(WII_PIN_SCL, GPIO_FUNC_NULL);
}

#endif // end WII_PIN_SDA + WII_PIN_SCL
