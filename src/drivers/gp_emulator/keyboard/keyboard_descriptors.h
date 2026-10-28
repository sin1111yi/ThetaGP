/**
 * This file is a part of ThetaGP.
 *
 * ThetaGP is free software: you can redistribute it
 * and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * ThetaGP is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "BoardConfig.h"
#include "utils/utils.h"

#include <stdint.h>

// ── endpoint buffer configuration ──

#define KEYBOARD_INTERFACE 0
#define KEYBOARD_ENDPOINT  1
#define KEYBOARD_SIZE      64

#define CDC_COM_INTERFACE  1
#define CDC_DATA_INTERFACE 2
#define CDC_NOTIFICATION_ENDPOINT 2
#define CDC_DATA_IN_ENDPOINT      3
#define CDC_DATA_OUT_ENDPOINT     4

// The keys one report carries: a modifier byte, a reserved byte, then the keys.
#define KEYBOARD_KEYS 6

// One report of the keyboard, in the boot protocol's shape.
typedef struct __attribute((packed, aligned(1))) {
  uint8_t modifiers;
  uint8_t reserved;
  uint8_t keys[KEYBOARD_KEYS];
} KeyboardReport;

static_assert(sizeof(KeyboardReport) == 8, "a keyboard report is 8 bytes");

// ── descriptors ──

static const uint8_t keyboard_string_language[] = {0x09, 0x04};
static const uint8_t keyboard_string_manufacturer[] = "ThetaGamepad";
static const uint8_t keyboard_string_product[] = BOARD_NAME " Keyboard";
static const uint8_t keyboard_string_version[] = "1.0";
static const uint8_t keyboard_string_cdc[] = "ThetaGamepad Virtual Com Port";

static const uint8_t *keyboard_string_descriptors[]
    __attribute__((unused)) = {keyboard_string_language,
                               keyboard_string_manufacturer,
                               keyboard_string_product,
                               keyboard_string_version,
                               keyboard_string_cdc};

static uint8_t keyboard_device_descriptor[] = {
    18, // bLength
    1,  // bDescriptorType
    0x00,
    0x02, // bcdUSB
    0,    // bDeviceClass
    0,    // bDeviceSubClass
    0,    // bDeviceProtocol
    64,   // bMaxPacketSize0
    LSB16(0xcafe),
    MSB16(0xcafe), // idVendor
    0x00,
    0x01, // idProduct placeholder — patched at runtime
    0x00,
    0x01, // bcdDevice
    1,    // iManufacturer
    2,    // iProduct
    0,    // iSerialNumber
    1     // bNumConfigurations
};

// The boot keyboard: eight modifier bits, a reserved byte, and six key codes.
// The output report carries the five LEDs a host may light.
static const uint8_t keyboard_report_descriptor[] = {
    0x05, 0x01, // USAGE_PAGE (Generic Desktop)
    0x09, 0x06, // USAGE (Keyboard)
    0xa1, 0x01, // COLLECTION (Application)

    // modifiers
    0x05, 0x07, //   USAGE_PAGE (Keyboard)
    0x19, 0xe0, //   USAGE_MINIMUM (Left Control)
    0x29, 0xe7, //   USAGE_MAXIMUM (Right GUI)
    0x15, 0x00, //   LOGICAL_MINIMUM (0)
    0x25, 0x01, //   LOGICAL_MAXIMUM (1)
    0x75, 0x01, //   REPORT_SIZE (1)
    0x95, 0x08, //   REPORT_COUNT (8)
    0x81, 0x02, //   INPUT (Data,Var,Abs)

    // the reserved byte
    0x95, 0x01, //   REPORT_COUNT (1)
    0x75, 0x08, //   REPORT_SIZE (8)
    0x81, 0x01, //   INPUT (Const,Ary,Abs)

    // the LEDs
    0x95, 0x05, //   REPORT_COUNT (5)
    0x75, 0x01, //   REPORT_SIZE (1)
    0x05, 0x08, //   USAGE_PAGE (LEDs)
    0x19, 0x01, //   USAGE_MINIMUM (Num Lock)
    0x29, 0x05, //   USAGE_MAXIMUM (Kana)
    0x91, 0x02, //   OUTPUT (Data,Var,Abs)
    0x95, 0x01, //   REPORT_COUNT (1)
    0x75, 0x03, //   REPORT_SIZE (3)
    0x91, 0x01, //   OUTPUT (Const,Ary,Abs)

    // the keys
    0x95, 0x06, //   REPORT_COUNT (6)
    0x75, 0x08, //   REPORT_SIZE (8)
    0x15, 0x00, //   LOGICAL_MINIMUM (0)
    0x25, 0x65, //   LOGICAL_MAXIMUM (101)
    0x05, 0x07, //   USAGE_PAGE (Keyboard)
    0x19, 0x00, //   USAGE_MINIMUM (0)
    0x29, 0x65, //   USAGE_MAXIMUM (101)
    0x81, 0x00, //   INPUT (Data,Ary,Abs)

    0xc0 // END_COLLECTION
};

// HID interface descriptor block (interface + HID desc + endpoint)
#define KEYBOARD_IFACE_SIZE (9 + 9 + 7)
static const uint8_t keyboard_interface_descriptor[] = {
    // HID interface
    9,                  // bLength
    4,                  // bDescriptorType
    KEYBOARD_INTERFACE, // bInterfaceNumber
    0,                  // bAlternateSetting
    1,                  // bNumEndpoints
    0x03,               // bInterfaceClass (HID)
    0x00,               // bInterfaceSubClass
    0x00,               // bInterfaceProtocol
    0,                  // iInterface
    // HID descriptor
    9,                                  // bLength
    0x21,                               // bDescriptorType
    0x11, 0x01,                         // bcdHID
    0,                                  // bCountryCode
    1,                                  // bNumDescriptors
    0x22,                               // bDescriptorType
    LSB16(sizeof(keyboard_report_descriptor)), // wDescriptorLength
    MSB16(sizeof(keyboard_report_descriptor)),
    // HID endpoint IN (interrupt)
    7,                       // bLength
    5,                       // bDescriptorType
    KEYBOARD_ENDPOINT | 0x80, // bEndpointAddress
    0x03,                    // bmAttributes (intr)
    KEYBOARD_SIZE, 0,        // wMaxPacketSize
    // bInterval: how often the host polls this endpoint, in the encoding of the
    // speed in use: one frame at full speed, one microframe at high speed.
    1,
};
