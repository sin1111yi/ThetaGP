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

// The Xbox One controller: a GIP device, whose interface is a vendor one with
// two interrupt endpoints. Its messages carry a four-byte header and travel on
// those endpoints; the device announces itself and answers a descriptor request
// before it may send input.
#define XBONE_VENDOR_ID       0x045E
#define XBONE_PRODUCT_ID      0x02D1

#define XBONE_INTERFACE       0
#define XBONE_INTERFACE_CLASS 0x47
#define XBONE_INTERFACE_PROTOCOL 0xD0

#define XBONE_ENDPOINT_IN     0x81
#define XBONE_ENDPOINT_OUT    0x01
#define XBONE_ENDPOINT_SIZE   64

// A GIP message is a four-byte header and its payload; a message larger than
// one packet is sent in fragments that carry the total length in their first
// one.
#define XBONE_HEADER_SIZE     4
#define XBONE_CHUNK_SIZE      58

#define XBONE_REPORT_CORE_SIZE 14

// GIP commands.
#define XBONE_CMD_ACK         0x01
#define XBONE_CMD_ANNOUNCE    0x02
#define XBONE_CMD_KEEPALIVE   0x03
#define XBONE_CMD_DESCRIPTOR  0x04
#define XBONE_CMD_POWER_MODE  0x05
#define XBONE_CMD_AUTH        0x06
#define XBONE_CMD_VIRTUAL_KEY 0x07
#define XBONE_CMD_RUMBLE      0x09
#define XBONE_CMD_LED         0x0A
#define XBONE_CMD_FINAL_AUTH  0x1E
#define XBONE_CMD_INPUT       0x20

// Options byte of the header.
#define XBONE_OPTION_NEEDS_ACK    (1U << 4)
#define XBONE_OPTION_INTERNAL     (1U << 5)
#define XBONE_OPTION_CHUNK_START  (1U << 6)
#define XBONE_OPTION_CHUNKED      (1U << 7)

// The first byte of the gamepad core.
#define XBONE_MASK_KEEPALIVE (1U << 1)
#define XBONE_MASK_START     (1U << 2)
#define XBONE_MASK_BACK      (1U << 3)
#define XBONE_MASK_A         (1U << 4)
#define XBONE_MASK_B         (1U << 5)
#define XBONE_MASK_X         (1U << 6)
#define XBONE_MASK_Y         (1U << 7)

// The second byte of the gamepad core.
#define XBONE_MASK_DPAD_UP      (1U << 0)
#define XBONE_MASK_DPAD_DOWN    (1U << 1)
#define XBONE_MASK_DPAD_LEFT    (1U << 2)
#define XBONE_MASK_DPAD_RIGHT   (1U << 3)
#define XBONE_MASK_LB           (1U << 4)
#define XBONE_MASK_RB           (1U << 5)
#define XBONE_MASK_LS           (1U << 6)
#define XBONE_MASK_RS           (1U << 7)

// The request Windows asks its compatible-id descriptor through, and the
// compatible id that binds the device to its GIP driver.
#define XBONE_REQUEST_OS_FEATURE 0x20
#define XBONE_DESC_COMPATIBLE_ID 0x0004

typedef struct __attribute((packed, aligned(1))) {
  uint8_t command;
  uint8_t options;
  uint8_t sequence;
  uint8_t length;
  uint8_t buttons_1;
  uint8_t buttons_2;
  uint16_t left_trigger;
  uint16_t right_trigger;
  int16_t left_stick_x;
  int16_t left_stick_y;
  int16_t right_stick_x;
  int16_t right_stick_y;
} XboxOneReport;

static_assert(sizeof(XboxOneReport) == XBONE_HEADER_SIZE + XBONE_REPORT_CORE_SIZE,
              "an input report is a header and the gamepad core");

typedef struct __attribute((packed, aligned(1))) {
  uint32_t total_length;
  uint16_t version;
  uint16_t index;
  uint8_t total_sections;
  uint8_t reserved[7];
  uint8_t first_interface;
  uint8_t reserved2;
  uint8_t compatible_id[8];
  uint8_t sub_compatible_id[8];
  uint8_t reserved3[6];
} XboxOneCompatibleId;

static_assert(sizeof(XboxOneCompatibleId) == 40,
              "the compatible-id descriptor is 40 bytes");

static const uint8_t xbone_string_language[] = {0x09, 0x04};
static const uint8_t xbone_string_manufacturer[] = "ThetaGamepad";
static const uint8_t xbone_string_product[] = BOARD_NAME;
static const uint8_t xbone_string_cdc[] = "ThetaGamepad Virtual Com Port";
static const uint8_t xbone_string_security[] =
    "Xbox Security Method 3, Version 1.00, "
    "\xa9 2005 Microsoft Corporation. All rights reserved.";

// The serial a host reads to tell one controller from another: the chip's
// unique id as hex, written by the driver at startup.
static uint8_t xbone_string_serial[25] = "000000000000000000000000";

// The string that names the request Windows asks its compatible-id descriptor
// with, laid out as the two-byte language entry and the vendor code say.
static uint16_t xbone_string_os[] __attribute__((unused)) = {0x0312, 'M', 'S', 'F', 'T', '1', '0',
                                     '0', 0x0020};

static const uint8_t *xbone_string_descriptors[]
    __attribute__((unused)) = {xbone_string_language, xbone_string_manufacturer,
                               xbone_string_product, xbone_string_serial,
                               xbone_string_security, xbone_string_cdc};

static const uint8_t xbone_device_descriptor[] = {
    18,   // bLength
    1,    // bDescriptorType
    0x00,
    0x02, // bcdUSB
    // The device names no class of its own: the gamepad interface carries the
    // protocol's class, and a class here would keep the CDC interfaces behind
    // it from binding.
    0x00, // bDeviceClass
    0x00, // bDeviceSubClass
    0x00, // bDeviceProtocol
    0x40, // bMaxPacketSize0
    LSB16(XBONE_VENDOR_ID),
    MSB16(XBONE_VENDOR_ID), // idVendor
    LSB16(XBONE_PRODUCT_ID),
    MSB16(XBONE_PRODUCT_ID), // idProduct
    0x01,
    0x01, // bcdDevice
    1,    // iManufacturer
    2,    // iProduct
    3,    // iSerialNumber
    1     // bNumConfigurations
};

static const uint8_t xbone_interface_descriptor[] = {
    // Interface
    9,                        // bLength
    0x04,                     // bDescriptorType (Interface)
    XBONE_INTERFACE,          // bInterfaceNumber
    0,                        // bAlternateSetting
    2,                        // bNumEndpoints
    0xFF,                     // bInterfaceClass (vendor specific)
    XBONE_INTERFACE_CLASS,    // bInterfaceSubClass
    XBONE_INTERFACE_PROTOCOL, // bInterfaceProtocol
    0,                        // iInterface
    // Input endpoint (interrupt)
    7, 0x05, XBONE_ENDPOINT_IN, 0x03, XBONE_ENDPOINT_SIZE, 0, 1,
    // Output endpoint (interrupt)
    7, 0x05, XBONE_ENDPOINT_OUT, 0x03, XBONE_ENDPOINT_SIZE, 0, 1,
};

static_assert(sizeof(xbone_interface_descriptor) == 9 + 7 + 7,
              "the interface block is its interface and its two endpoints");

// The announce a device sends once the host has configured it: its id, the
// identifiers it reports as, and the versions of the protocol it speaks.
static uint8_t xbone_announce[] __attribute__((unused)) = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // device id, from the chip
    LSB16(XBONE_VENDOR_ID), MSB16(XBONE_VENDOR_ID),
    LSB16(XBONE_PRODUCT_ID), MSB16(XBONE_PRODUCT_ID), // vendor, product
    0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, // firmware
    0x01, 0x00,                                     // hardware
    0x01, 0x00, 0x01, 0x00, 0x01, 0x00,             // rf, security, gip
};

// What the device answers a descriptor request with: the class it carries, the
// interfaces it offers and the messages it exchanges.
static const uint8_t xbone_device_descriptor_blob[] = {
    0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB6, 0x00,
    0x77, 0x00, 0x16, 0x00, 0x1B, 0x00, 0x1C, 0x00,
    0x23, 0x00, 0x29, 0x00, 0x46, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x00, 0x02, 0x00, 0x00,
    0x06, 0x01, 0x02, 0x03, 0x04, 0x06, 0x07, 0x05,
    0x01, 0x04, 0x05, 0x06, 0x0A,
    0x01, 0x1A, 0x00, 0x57, 0x69, 0x6E, 0x64, 0x6F,
    0x77, 0x73, 0x2E, 0x58, 0x62, 0x6F, 0x78, 0x2E,
    0x49, 0x6E, 0x70, 0x75, 0x74, 0x2E, 0x47, 0x61,
    0x6D, 0x65, 0x70, 0x61, 0x64,
    0x03, 0x56, 0xFF, 0x76, 0x97, 0xFD, 0x9B, 0x81,
    0x45, 0xAD, 0x45, 0xB6, 0x45, 0xBB, 0xA5, 0x26,
    0xD6, 0x2C, 0x40, 0x2E, 0x08, 0xDF, 0x07, 0xE1,
    0x45, 0xA5, 0xAB, 0xA3, 0x12, 0x7A, 0xF1, 0x97,
    0xB5, 0xE7, 0x1F, 0xF3, 0xB8, 0x86, 0x73, 0xE9,
    0x40, 0xA9, 0xF8, 0x2F, 0x21, 0x26, 0x3A, 0xCF,
    0xB7,
    0x02,
    0x17, 0x00, 0x20, 0x0E, 0x00, 0x01, 0x00, 0x10,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x17, 0x00, 0x09, 0x3C, 0x00, 0x01, 0x00, 0x08,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static_assert(sizeof(xbone_device_descriptor_blob) == 0xB6,
              "the descriptor request is answered with the 182 bytes it "
              "announces");
