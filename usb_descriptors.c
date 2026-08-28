#ifndef FRENS_USB_MSC
#define FRENS_USB_MSC 0
#endif

#if FRENS_USB_MSC

#include <string.h>
#include "tusb.h"
#include "pico/unique_id.h"

// A single mass-storage interface. MSC binds to the operating system's own
// class driver, so the VID/PID pair only has to be stable, not registered.
// 0xCafe is TinyUSB's example vendor id.
#define USB_VID 0xCafe
#define USB_PID 0x4001
#define USB_BCD 0x0200

//--------------------------------------------------------------------+
// Device descriptor
//--------------------------------------------------------------------+
static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = USB_BCD,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,

    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,

    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

//--------------------------------------------------------------------+
// Configuration descriptor
//--------------------------------------------------------------------+
enum
{
    ITF_NUM_MSC = 0,
    ITF_NUM_TOTAL
};

#define EPNUM_MSC_OUT 0x01
#define EPNUM_MSC_IN 0x81

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

static const uint8_t desc_configuration[] = {
    // config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // interface number, string index, EP Out & EP In address, EP size
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 4, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

//--------------------------------------------------------------------+
// String descriptors
//--------------------------------------------------------------------+
static char serial_str[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

static const char *string_desc_arr[] = {
    (const char[]){0x09, 0x04}, // 0: supported language is English (0x0409)
    "Frens",                    // 1: Manufacturer
    "Pico Emulator SD Card",    // 2: Product
    serial_str,                 // 3: Serial, filled in below
    "SD Card",                  // 4: MSC interface
};

static uint16_t desc_str[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    size_t chr_count;

    if (index == 0)
    {
        memcpy(&desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    }
    else
    {
        if (index >= TU_ARRAY_SIZE(string_desc_arr))
        {
            return NULL;
        }
        if (index == 3 && serial_str[0] == '\0')
        {
            pico_get_unique_board_id_string(serial_str, sizeof(serial_str));
        }

        const char *str = string_desc_arr[index];
        chr_count = strlen(str);
        const size_t max_count = TU_ARRAY_SIZE(desc_str) - 1;
        if (chr_count > max_count)
        {
            chr_count = max_count;
        }
        // Convert ASCII to UTF-16
        for (size_t i = 0; i < chr_count; i++)
        {
            desc_str[1 + i] = str[i];
        }
    }

    // First byte is length (including header), second byte is descriptor type
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return desc_str;
}

#endif // FRENS_USB_MSC
