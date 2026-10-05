#pragma once

#include <cstdint>

#include "tusb.h"

/*  Mouse + keyboard output mode (OGX-Mini-improved): three HID interfaces, each with its own
 *  endpoint and no report IDs, so BIOS / TV / console hosts that only speak the boot protocol
 *  work: a boot keyboard, a boot mouse (wheel and horizontal pan after the boot bytes) and a
 *  media-key (consumer control) interface.
 */
namespace KBM
{
    static constexpr uint8_t ITF_KEYBOARD = 0;
    static constexpr uint8_t ITF_MOUSE = 1;
    static constexpr uint8_t ITF_MEDIA = 2;
    static constexpr uint8_t ITF_COUNT = 3;

    static const uint8_t STRING_DESC_LANGUAGE[] = { 0x09, 0x04 };
    static const uint8_t STRING_MANUFACTURER[]  = "OGX-Mini";
    static const uint8_t STRING_PRODUCT[]       = "OGX-Mini Mouse + Keyboard";
    static const uint8_t STRING_VERSION[]       = "1.0";

    static const uint8_t *STRING_DESCRIPTORS[] __attribute__((unused)) =
    {
        STRING_DESC_LANGUAGE,
        STRING_MANUFACTURER,
        STRING_PRODUCT,
        STRING_VERSION
    };
    static constexpr uint8_t STRING_COUNT = sizeof(STRING_DESCRIPTORS) / sizeof(STRING_DESCRIPTORS[0]);

    static const uint8_t DEVICE_DESCRIPTORS[] =
    {
        0x12,        // bLength
        0x01,        // bDescriptorType (Device)
        0x00, 0x02,  // bcdUSB 2.00
        0x00,        // bDeviceClass (from the interfaces)
        0x00,        // bDeviceSubClass
        0x00,        // bDeviceProtocol
        0x40,        // bMaxPacketSize0 64
        0x09, 0x12,  // idVendor 0x1209 (pid.codes)
        0x01, 0x00,  // idProduct 0x0001 (pid.codes test PID)
        0x00, 0x01,  // bcdDevice 1.00
        0x01,        // iManufacturer
        0x02,        // iProduct
        0x00,        // iSerialNumber
        0x01,        // bNumConfigurations
    };

    static const uint8_t KEYBOARD_REPORT_DESCRIPTORS[] = { TUD_HID_REPORT_DESC_KEYBOARD() };
    static const uint8_t MOUSE_REPORT_DESCRIPTORS[]    = { TUD_HID_REPORT_DESC_MOUSE() };
    static const uint8_t MEDIA_REPORT_DESCRIPTORS[]    = { TUD_HID_REPORT_DESC_CONSUMER() };

    static constexpr uint16_t KBM_CONFIG_LEN = TUD_CONFIG_DESC_LEN + ITF_COUNT * TUD_HID_DESC_LEN;

    static const uint8_t CONFIGURATION_DESCRIPTORS[] =
    {
        TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, KBM_CONFIG_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

        // Interface number, string index, boot protocol, report descriptor len, EP In, size, interval (ms)
        TUD_HID_DESCRIPTOR(ITF_KEYBOARD, 0, HID_ITF_PROTOCOL_KEYBOARD,
                           sizeof(KEYBOARD_REPORT_DESCRIPTORS), 0x81, 16, 1),
        TUD_HID_DESCRIPTOR(ITF_MOUSE, 0, HID_ITF_PROTOCOL_MOUSE,
                           sizeof(MOUSE_REPORT_DESCRIPTORS), 0x82, 16, 1),
        TUD_HID_DESCRIPTOR(ITF_MEDIA, 0, HID_ITF_PROTOCOL_NONE,
                           sizeof(MEDIA_REPORT_DESCRIPTORS), 0x83, 16, 10),
    };

}; // namespace KBM
