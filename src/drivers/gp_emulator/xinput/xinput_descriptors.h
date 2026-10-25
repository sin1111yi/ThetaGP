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
 * You should have received a copy of the GNU General Public
 * License along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "BoardConfig.h"
#include "utils/utils.h"

#include <stdint.h>

// The wired Xbox 360 controller: the identifiers a host binds its XInput driver
// to, and the vendor interface its reports travel on. The interface carries two
// interrupt endpoints and a capability descriptor of type 0x21 in place of a
// HID report descriptor.
#define XINPUT_VENDOR_ID            0x045E
#define XINPUT_PRODUCT_ID           0x028E

#define XINPUT_INTERFACE            0
#define XINPUT_INTERFACE_SUBCLASS   0x5D
#define XINPUT_INTERFACE_PROTOCOL   0x01

#define XINPUT_ENDPOINT_IN          0x81
#define XINPUT_ENDPOINT_OUT         0x02
#define XINPUT_ENDPOINT_SIZE        32

#define XINPUT_REPORT_SIZE          20
#define XINPUT_OUT_SIZE             32

#define XINPUT_CAPABILITY_DESC_TYPE 0x21
#define XINPUT_CAPABILITY_DESC_SIZE 17

// Digital buttons, first byte of the button field.
#define XINPUT_MASK_UP              (1U << 0)
#define XINPUT_MASK_DOWN            (1U << 1)
#define XINPUT_MASK_LEFT            (1U << 2)
#define XINPUT_MASK_RIGHT           (1U << 3)
#define XINPUT_MASK_START           (1U << 4)
#define XINPUT_MASK_BACK            (1U << 5)
#define XINPUT_MASK_LS              (1U << 6)
#define XINPUT_MASK_RS              (1U << 7)

// Digital buttons, second byte of the button field.
#define XINPUT_MASK_LB              (1U << 0)
#define XINPUT_MASK_RB              (1U << 1)
#define XINPUT_MASK_GUIDE           (1U << 2)
#define XINPUT_MASK_A               (1U << 4)
#define XINPUT_MASK_B               (1U << 5)
#define XINPUT_MASK_X               (1U << 6)
#define XINPUT_MASK_Y               (1U << 7)

// Vendor requests the host asks the device through its interface, and the
// report sizes it asks about.
#define XINPUT_REQUEST_CAPABILITIES 0x01
#define XINPUT_CAPABILITIES_VIBRATION 0x0000
#define XINPUT_CAPABILITIES_INPUT     0x0100

typedef struct __attribute((packed, aligned(1))) {
  uint8_t report_id;
  uint8_t report_size;
  uint8_t buttons_1;
  uint8_t buttons_2;
  uint8_t left_trigger;
  uint8_t right_trigger;
  int16_t left_stick_x;
  int16_t left_stick_y;
  int16_t right_stick_x;
  int16_t right_stick_y;
  uint8_t reserved[6];
} XInputReport;

static_assert(sizeof(XInputReport) == XINPUT_REPORT_SIZE,
              "an XInput report is 20 bytes");

static const uint8_t xinput_string_language[] = {0x09, 0x04};
static const uint8_t xinput_string_manufacturer[] = "ThetaGamepad";
static const uint8_t xinput_string_product[] = BOARD_NAME;
static const uint8_t xinput_string_cdc[] = "ThetaGamepad Virtual Com Port";

// The serial a host reads to tell one controller from another: the chip's
// unique id as hex, written by the driver at startup.
static uint8_t xinput_string_serial[25] = "000000000000000000000000";

static const uint8_t *xinput_string_descriptors[]
    __attribute__((unused)) = {xinput_string_language, xinput_string_manufacturer,
                               xinput_string_product, xinput_string_serial,
                               xinput_string_cdc};

static const uint8_t xinput_device_descriptor[] = {
    18,   // bLength
    1,    // bDescriptorType
    0x00,
    0x02, // bcdUSB
    // The device names no class of its own: the gamepad interface carries the
    // vendor class a host binds to, and a class here would keep the CDC
    // interfaces behind it from binding.
    0x00, // bDeviceClass
    0x00, // bDeviceSubClass
    0x00, // bDeviceProtocol
    0x40, // bMaxPacketSize0
    LSB16(XINPUT_VENDOR_ID),
    MSB16(XINPUT_VENDOR_ID), // idVendor
    LSB16(XINPUT_PRODUCT_ID),
    MSB16(XINPUT_PRODUCT_ID), // idProduct
    0x14,
    0x01, // bcdDevice
    1,    // iManufacturer
    2,    // iProduct
    3,    // iSerialNumber
    1     // bNumConfigurations
};

// The capability descriptor names the two endpoints and the length of the
// report each carries; the bytes after it that the layout reserves are the
// values a controller of this family sends.
static const uint8_t xinput_interface_descriptor[] = {
    // Interface
    9,                          // bLength
    0x04,                       // bDescriptorType (Interface)
    XINPUT_INTERFACE,           // bInterfaceNumber
    0,                          // bAlternateSetting
    2,                          // bNumEndpoints
    0xFF,                       // bInterfaceClass (vendor specific)
    XINPUT_INTERFACE_SUBCLASS,  // bInterfaceSubClass
    XINPUT_INTERFACE_PROTOCOL,  // bInterfaceProtocol
    0,                          // iInterface
    // Capability descriptor
    XINPUT_CAPABILITY_DESC_SIZE,  // bLength
    XINPUT_CAPABILITY_DESC_TYPE,  // bDescriptorType
    0x00, 0x01,                   // bcdVersion
    0x01,                         // subtype (gamepad)
    0x25,                         // reserved
    XINPUT_ENDPOINT_IN,           // input endpoint
    XINPUT_REPORT_SIZE,           // input report length
    0x03, 0x03, 0x03, 0x04, 0x13, // reserved
    XINPUT_ENDPOINT_OUT,          // output endpoint
    0x08,                         // output report length
    0x03, 0x03,                   // reserved
    // Input endpoint (interrupt)
    7, 0x05, XINPUT_ENDPOINT_IN, 0x03, XINPUT_ENDPOINT_SIZE, 0, 1,
    // Output endpoint (interrupt)
    7, 0x05, XINPUT_ENDPOINT_OUT, 0x03, XINPUT_ENDPOINT_SIZE, 0, 8,
};

static_assert(sizeof(xinput_interface_descriptor) ==
                  9 + XINPUT_CAPABILITY_DESC_SIZE + 7 + 7,
              "the interface block is its interface, its capability "
              "descriptor and its two endpoints");

// What the device answers the capability requests with: the vibration ranges,
// then the shape of the input report the host is about to receive.
static_assert(sizeof(xinput_device_descriptor) == 18,
              "a device descriptor is 18 bytes");

static uint8_t xinput_vibration_capabilities[] __attribute__((unused)) = {0x00, 0x08, 0x00, 0x00,
                                                  0x00, 0x00, 0x00, 0x00};

static uint8_t xinput_input_capabilities[] = {
    0x00, 0x14,             // report id, report size
    0x3F, 0xF7,             // buttons
    0xFF, 0xFF,             // left trigger, right trigger
    0x00, 0x00, 0x00, 0x00, // left stick
    0xC0, 0xFF, 0xC0, 0xFF, // right stick
    0x00, 0x00, 0x00, 0x00, // reserved
    0x00, 0x00              // flags
};

static_assert(sizeof(xinput_input_capabilities) == XINPUT_REPORT_SIZE,
              "the input capabilities carry the report's own shape");

// The serial the device answers its device-wide capability request with.
static uint8_t xinput_serial_capability[4] __attribute__((unused)) = {};
