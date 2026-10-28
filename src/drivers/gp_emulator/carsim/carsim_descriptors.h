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

#define CARSIM_ENDPOINT_SIZE 64

// The vendor id this project's own devices carry.
#define CARSIM_VENDOR_ID 0xcafe

// ── endpoint buffer configuration ──

#define CARSIM_INTERFACE   0
#define CARSIM_ENDPOINT    1
#define CARSIM_SIZE        64

#define CDC_COM_INTERFACE  1
#define CDC_DATA_INTERFACE 2
#define CDC_NOTIFICATION_ENDPOINT 2
#define CDC_DATA_IN_ENDPOINT      3
#define CDC_DATA_OUT_ENDPOINT     4

// ── report ──

// The hat the D-pad reports, in the encoding a HID hat uses.
#define CARSIM_HAT_UP        0x00
#define CARSIM_HAT_UPRIGHT   0x01
#define CARSIM_HAT_RIGHT     0x02
#define CARSIM_HAT_DOWNRIGHT 0x03
#define CARSIM_HAT_DOWN      0x04
#define CARSIM_HAT_DOWNLEFT  0x05
#define CARSIM_HAT_LEFT      0x06
#define CARSIM_HAT_UPLEFT    0x07
#define CARSIM_HAT_NOTHING   0x08

// The gear the shifter stands in, in the four bits the report carries: neutral,
// the forward gears counting up from it, reverse, and no shifter wired.
#define CARSIM_GEAR_NEUTRAL  0
#define CARSIM_GEAR_REVERSE  9
#define CARSIM_GEAR_NONE     15

// One report of the wheel: the buttons, the hat and the gear thrown together in
// one byte, then the five axes as 16-bit counts.
typedef struct __attribute((packed, aligned(1))) {
  uint32_t buttons;

  uint8_t direction : 4;
  uint8_t gear : 4;

  uint16_t steer;
  uint16_t throttle;
  uint16_t brake;
  uint16_t clutch;
  uint16_t handbrake;
} CarSimReport;

static_assert(sizeof(CarSimReport) == 15, "a car simulator report is 15 bytes");

// ── descriptors ──

static const uint8_t carsim_string_language[] = {0x09, 0x04};
static const uint8_t carsim_string_manufacturer[] = "ThetaGamepad";
static const uint8_t carsim_string_product[] = BOARD_NAME " Wheel";
static const uint8_t carsim_string_version[] = "1.0";
static const uint8_t carsim_string_cdc[] = "ThetaGamepad Virtual Com Port";

static const uint8_t *carsim_string_descriptors[]
    __attribute__((unused)) = {carsim_string_language, carsim_string_manufacturer,
                               carsim_string_product, carsim_string_version,
                               carsim_string_cdc};

static uint8_t carsim_device_descriptor[] = {
    18, // bLength
    1,  // bDescriptorType
    0x00,
    0x02,                // bcdUSB
    0,                   // bDeviceClass
    0,                   // bDeviceSubClass
    0,                   // bDeviceProtocol
    CARSIM_ENDPOINT_SIZE, // bMaxPacketSize0
    LSB16(CARSIM_VENDOR_ID),
    MSB16(CARSIM_VENDOR_ID), // idVendor
    0x00,
    0x01, // idProduct placeholder — patched at runtime
    0x00,
    0x01, // bcdDevice
    1,    // iManufacturer
    2,    // iProduct
    0,    // iSerialNumber
    1     // bNumConfigurations
};

// The wheel's report: 32 buttons, the hat and the gear in one byte, then the
// axes. Steering rests at the centre of its range and the pedals and the lever
// at zero.
static const uint8_t carsim_report_descriptor[] = {
    0x05, 0x01, // USAGE_PAGE (Generic Desktop)
    0x09, 0x04, // USAGE (Joystick)
    0xa1, 0x01, // COLLECTION (Application)

    // 32 buttons
    0x05, 0x09, //   USAGE_PAGE (Button)
    0x19, 0x01, //   USAGE_MINIMUM (Button 1)
    0x29, 0x20, //   USAGE_MAXIMUM (Button 32)
    0x15, 0x00, //   LOGICAL_MINIMUM (0)
    0x25, 0x01, //   LOGICAL_MAXIMUM (1)
    0x95, 0x20, //   REPORT_COUNT (32)
    0x75, 0x01, //   REPORT_SIZE (1)
    0x81, 0x02, //   INPUT (Data,Var,Abs)

    // hat
    0x05, 0x01, //   USAGE_PAGE (Generic Desktop)
    0x09, 0x39, //   USAGE (Hat switch)
    0x25, 0x07, //   LOGICAL_MAXIMUM (7)
    0x95, 0x01, //   REPORT_COUNT (1)
    0x75, 0x04, //   REPORT_SIZE (4)
    0x81, 0x42, //   INPUT (Data,Var,Abs,Null)

    // gear
    0x09, 0x37, //   USAGE (Dial)
    0x25, 0x0f, //   LOGICAL_MAXIMUM (15)
    0x95, 0x01, //   REPORT_COUNT (1)
    0x75, 0x04, //   REPORT_SIZE (4)
    0x81, 0x02, //   INPUT (Data,Var,Abs)

    // the axes
    0x26, 0xff, 0xff, //   LOGICAL_MAXIMUM (65535)
    0x46, 0xff, 0xff, //   PHYSICAL_MAXIMUM (65535)
    0x09, 0x30,       //   USAGE (X) — steering
    0x09, 0x31,       //   USAGE (Y) — throttle
    0x09, 0x32,       //   USAGE (Z) — brake
    0x09, 0x33,       //   USAGE (Rx) — clutch
    0x09, 0x34,       //   USAGE (Ry) — handbrake
    0x75, 0x10,       //   REPORT_SIZE (16)
    0x95, 0x05,       //   REPORT_COUNT (5)
    0x81, 0x02,       //   INPUT (Data,Var,Abs)

    0xc0 // END_COLLECTION
};

// HID interface descriptor block (interface + HID desc + endpoint)
#define CARSIM_IFACE_SIZE (9 + 9 + 7)
static const uint8_t carsim_interface_descriptor[] = {
    // HID interface
    9,                 // bLength
    4,                 // bDescriptorType
    CARSIM_INTERFACE,  // bInterfaceNumber
    0,                 // bAlternateSetting
    1,                 // bNumEndpoints
    0x03,              // bInterfaceClass (HID)
    0x00,              // bInterfaceSubClass
    0x00,              // bInterfaceProtocol
    0,                 // iInterface
    // HID descriptor
    9,                             // bLength
    0x21,                          // bDescriptorType
    0x11, 0x01,                    // bcdHID
    0,                             // bCountryCode
    1,                             // bNumDescriptors
    0x22,                          // bDescriptorType
    sizeof(carsim_report_descriptor), // wDescriptorLength
    0,
    // HID endpoint IN (interrupt)
    7,                       // bLength
    5,                       // bDescriptorType
    CARSIM_ENDPOINT | 0x80,  // bEndpointAddress
    0x03,                    // bmAttributes (intr)
    CARSIM_SIZE, 0,          // wMaxPacketSize
    // bInterval: how often the host polls this endpoint, in the encoding of the
    // speed in use: one frame at full speed, one microframe at high speed.
    1,
};
