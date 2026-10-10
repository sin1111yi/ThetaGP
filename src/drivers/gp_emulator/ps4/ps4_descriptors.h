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

#include "utils/utils.h"

#include <cstdint>

// A DualShock 4 v2 as the host sees it: Sony's identifiers, one HID interface
// that carries a 64-byte report, and the report descriptor the console and the
// host drivers read the shape of that report from. The bytes of the descriptor
// are the ones the device itself sends; a byte of them is not a thing any
// firmware reader could correct.
#define PS4_VENDOR_ID          0x054C
#define PS4_PRODUCT_ID         0x09CC

#define PS4_INTERFACE          0
#define PS4_ENDPOINT           1
#define PS4_ENDPOINT_SIZE      64

// The reports a host asks for by id: the controller's own definition, its
// address and its version, and the four the authentication handshake uses.
#define PS4_REPORT_INPUT       0x01
#define PS4_REPORT_CALIBRATION 0x02
#define PS4_REPORT_PAIRING     0x03
#define PS4_REPORT_OUTPUT      0x05
#define PS4_REPORT_MAC         0x12
#define PS4_REPORT_VERSION     0xA3
#define PS4_REPORT_AUTH_PAYLOAD 0xF0
#define PS4_REPORT_AUTH_NONCE  0xF1
#define PS4_REPORT_AUTH_STATE  0xF2

// The direction nibble of the report's fifth byte: where the d-pad points.
#define PS4_HAT_UP             0x00
#define PS4_HAT_UPRIGHT        0x01
#define PS4_HAT_RIGHT          0x02
#define PS4_HAT_DOWNRIGHT      0x03
#define PS4_HAT_DOWN           0x04
#define PS4_HAT_DOWNLEFT       0x05
#define PS4_HAT_LEFT           0x06
#define PS4_HAT_UPLEFT         0x07
#define PS4_HAT_NOTHING        0x0F

// The bits of the sixth and seventh bytes, and the stick's rest position: the
// device reports each axis as one byte, so the middle is the middle of a byte.
#define PS4_MASK_SQUARE        0x10
#define PS4_MASK_CROSS         0x20
#define PS4_MASK_CIRCLE        0x40
#define PS4_MASK_TRIANGLE      0x80

#define PS4_MASK_L1            0x01
#define PS4_MASK_R1            0x02
#define PS4_MASK_L2            0x04
#define PS4_MASK_R2            0x08
#define PS4_MASK_SHARE         0x10
#define PS4_MASK_OPTIONS       0x20
#define PS4_MASK_L3            0x40
#define PS4_MASK_R3            0x80

#define PS4_MASK_PS            0x01
#define PS4_MASK_TOUCHPAD      0x02

#define PS4_JOYSTICK_MID       0x80

// What the device reports while nothing is touched: the two touch packets carry
// the counter in their low bits and the "lifted" bit above it.
#define PS4_TOUCH_LIFTED       0x80

// The 64 bytes of one input report. The fields are the bytes the host reads,
// in the order the descriptor declares them, so the struct is the report.
struct PS4Report {
  uint8_t report_id;      // PS4_REPORT_INPUT
  uint8_t left_stick_x;
  uint8_t left_stick_y;
  uint8_t right_stick_x;
  uint8_t right_stick_y;
  uint8_t buttons_1;      // direction nibble | square | cross | circle | triangle
  uint8_t buttons_2;      // L1 R1 L2 R2 share options L3 R3
  uint8_t buttons_3;      // PS | touchpad | the low bits of the report counter
  uint8_t left_trigger;
  uint8_t right_trigger;
  uint8_t timestamp[2];   // 5.33 us units, little endian
  uint8_t battery_and_audio;
  uint8_t motion[12];     // three gyro axes then three accelerometer axes
  uint8_t touch_packet;   // active bits and the touch counter
  uint8_t touch_counter;
  uint8_t touch[8];       // two points, 16 bits of x and 16 of y each
  uint8_t trailing[29];   // filled by the device and read by nothing
};

static_assert(sizeof(PS4Report) == 64,
              "PS4: the input report is the 64 bytes the descriptor declares");

static const uint8_t ps4_string_language[] = { 0x09, 0x04 };
static const uint8_t ps4_string_manufacturer[] = "Sony Interactive Entertainment";
static const uint8_t ps4_string_product[] = "Wireless Controller";
static const uint8_t ps4_string_version[] = "1.00";
// The command channel's interface is part of every configuration this device
// presents, so the string it names belongs to this set as well as the HID one.
static const uint8_t ps4_string_cdc[] = "ThetaGamepad Virtual Com Port";

static const uint8_t *ps4_string_descriptors[] = {
    ps4_string_language, ps4_string_manufacturer, ps4_string_product,
    ps4_string_version, ps4_string_cdc,
};

static const uint8_t ps4_device_descriptor[] = {
    18,                       // bLength
    1,                        // bDescriptorType
    0x00, 0x02,               // bcdUSB
    0,                        // bDeviceClass
    0,                        // bDeviceSubClass
    0,                        // bDeviceProtocol
    64,                       // bMaxPacketSize0
    LSB16(PS4_VENDOR_ID), MSB16(PS4_VENDOR_ID),
    LSB16(PS4_PRODUCT_ID), MSB16(PS4_PRODUCT_ID),
    0x00, 0x01,               // bcdDevice
    1,                        // iManufacturer
    2,                        // iProduct
    0,                        // iSerialNumber
    1                         // bNumConfigurations
};

static const uint8_t ps4_report_descriptor[] =
{
	0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
	0x09, 0x05,        // Usage (Game Pad)
	0xA1, 0x01,        // Collection (Application)
	0x85, 0x01,        //   Report ID (1)
	0x09, 0x30,        //   Usage (X)
	0x09, 0x31,        //   Usage (Y)
	0x09, 0x32,        //   Usage (Z)
	0x09, 0x35,        //   Usage (Rz)
	0x15, 0x00,        //   Logical Minimum (0)
	0x26, 0xFF, 0x00,  //   Logical Maximum (255)
	0x75, 0x08,        //   Report Size (8)
	0x95, 0x04,        //   Report Count (4)
	0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

	0x09, 0x39,        //   Usage (Hat switch)
	0x15, 0x00,        //   Logical Minimum (0)
	0x25, 0x07,        //   Logical Maximum (7)
	0x35, 0x00,        //   Physical Minimum (0)
	0x46, 0x3B, 0x01,  //   Physical Maximum (315)
	0x65, 0x14,        //   Unit (System: English Rotation, Length: Centimeter)
	0x75, 0x04,        //   Report Size (4)
	0x95, 0x01,        //   Report Count (1)
	0x81, 0x42,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,Null State)

	0x65, 0x00,        //   Unit (None)
	0x05, 0x09,        //   Usage Page (Button)
	0x19, 0x01,        //   Usage Minimum (0x01)
	0x29, 0x0E,        //   Usage Maximum (0x0E)
	0x15, 0x00,        //   Logical Minimum (0)
	0x25, 0x01,        //   Logical Maximum (1)
	0x75, 0x01,        //   Report Size (1)
	0x95, 0x0E,        //   Report Count (14)
	0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

	0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
	0x09, 0x20,        //   Usage (0x20)
	0x75, 0x06,        //   Report Size (6)
	0x95, 0x01,        //   Report Count (1)
	0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

	0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
	0x09, 0x33,        //   Usage (Rx)
	0x09, 0x34,        //   Usage (Ry)
	0x15, 0x00,        //   Logical Minimum (0)
	0x26, 0xFF, 0x00,  //   Logical Maximum (255)
	0x75, 0x08,        //   Report Size (8)
	0x95, 0x02,        //   Report Count (2)
	0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

	0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
	0x09, 0x21,        //   Usage (0x21)
	0x95, 0x36,        //   Report Count (54)
	0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

	0x85, 0x05,        //   Report ID (5)
	0x09, 0x22,        //   Usage (0x22)
	0x95, 0x1F,        //   Report Count (31)
	0x91, 0x02,        //   Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)

	0x85, 0x03,        //   Report ID (3)
	0x0A, 0x21, 0x27,  //   Usage (0x2721)
	0x95, 0x2F,        //   Report Count (47)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)

	0x85, 0x02,        //   Report ID (2)
	0x09, 0x24,        //   Usage (0x24)
	0x95, 0x24,        //   Report Count (36)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0x08,        //   Report ID (8)
	0x09, 0x25,        //   Usage (0x25)
	0x95, 0x03,        //   Report Count (3)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0x10,        //   Report ID (16)
	0x09, 0x26,        //   Usage (0x26)
	0x95, 0x04,        //   Report Count (4)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0x11,        //   Report ID (17)
	0x09, 0x27,        //   Usage (0x27)
	0x95, 0x02,        //   Report Count (2)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0x12,        //   Report ID (18)
	0x06, 0x02, 0xFF,  //   Usage Page (Vendor Defined 0xFF02)
	0x09, 0x21,        //   Usage (0x21)
	0x95, 0x0F,        //   Report Count (15)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0x13,        //   Report ID (19)
	0x09, 0x22,        //   Usage (0x22)
	0x95, 0x16,        //   Report Count (22)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0x14,        //   Report ID (20)
	0x06, 0x05, 0xFF,  //   Usage Page (Vendor Defined 0xFF05)
	0x09, 0x20,        //   Usage (0x20)
	0x95, 0x10,        //   Report Count (16)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0x15,        //   Report ID (21)
	0x09, 0x21,        //   Usage (0x21)
	0x95, 0x2C,        //   Report Count (44)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x06, 0x80, 0xFF,  //   Usage Page (Vendor Defined 0xFF80)
	0x85, 0x80,        //   Report ID (128)
	0x09, 0x20,        //   Usage (0x20)
	0x95, 0x06,        //   Report Count (6)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0x81,        //   Report ID (129)
	0x09, 0x21,        //   Usage (0x21)
	0x95, 0x06,        //   Report Count (6)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0x82,        //   Report ID (130)
	0x09, 0x22,        //   Usage (0x22)
	0x95, 0x05,        //   Report Count (5)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0x83,        //   Report ID (131)
	0x09, 0x23,        //   Usage (0x23)
	0x95, 0x01,        //   Report Count (1)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0x84,        //   Report ID (132)
	0x09, 0x24,        //   Usage (0x24)
	0x95, 0x04,        //   Report Count (4)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0x85,        //   Report ID (133)
	0x09, 0x25,        //   Usage (0x25)
	0x95, 0x06,        //   Report Count (6)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0x86,        //   Report ID (134)
	0x09, 0x26,        //   Usage (0x26)
	0x95, 0x06,        //   Report Count (6)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0x87,        //   Report ID (135)
	0x09, 0x27,        //   Usage (0x27)
	0x95, 0x23,        //   Report Count (35)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0x88,        //   Report ID (136)
	0x09, 0x28,        //   Usage (0x28)
	0x95, 0x22,        //   Report Count (34)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0x89,        //   Report ID (137)
	0x09, 0x29,        //   Usage (0x29)
	0x95, 0x02,        //   Report Count (2)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0x90,        //   Report ID (144)
	0x09, 0x30,        //   Usage (0x30)
	0x95, 0x05,        //   Report Count (5)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0x91,        //   Report ID (145)
	0x09, 0x31,        //   Usage (0x31)
	0x95, 0x03,        //   Report Count (3)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0x92,        //   Report ID (146)
	0x09, 0x32,        //   Usage (0x32)
	0x95, 0x03,        //   Report Count (3)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0x93,        //   Report ID (147)
	0x09, 0x33,        //   Usage (0x33)
	0x95, 0x0C,        //   Report Count (12)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0xA0,        //   Report ID (160)
	0x09, 0x40,        //   Usage (0x40)
	0x95, 0x06,        //   Report Count (6)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0xA1,        //   Report ID (161)
	0x09, 0x41,        //   Usage (0x41)
	0x95, 0x01,        //   Report Count (1)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0xA2,        //   Report ID (162)
	0x09, 0x42,        //   Usage (0x42)
	0x95, 0x01,        //   Report Count (1)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0xA3,        //   Report ID (163)
	0x09, 0x43,        //   Usage (0x43)
	0x95, 0x30,        //   Report Count (48)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0xA4,        //   Report ID (164)
	0x09, 0x44,        //   Usage (0x44)
	0x95, 0x0D,        //   Report Count (13)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0xA5,        //   Report ID (165)
	0x09, 0x45,        //   Usage (0x45)
	0x95, 0x15,        //   Report Count (21)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0xA6,        //   Report ID (166)
	0x09, 0x46,        //   Usage (0x46)
	0x95, 0x15,        //   Report Count (21)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0xA7,        //   Report ID (247)
	0x09, 0x4A,        //   Usage (0x4A)
	0x95, 0x01,        //   Report Count (1)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0xA8,        //   Report ID (250)
	0x09, 0x4B,        //   Usage (0x4B)
	0x95, 0x01,        //   Report Count (1)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0xA9,        //   Report ID (251)
	0x09, 0x4C,        //   Usage (0x4C)
	0x95, 0x08,        //   Report Count (8)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0xAA,        //   Report ID (252)
	0x09, 0x4E,        //   Usage (0x4E)
	0x95, 0x01,        //   Report Count (1)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0xAB,        //   Report ID (253)
	0x09, 0x4F,        //   Usage (0x4F)
	0x95, 0x39,        //   Report Count (57)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0xAC,        //   Report ID (254)
	0x09, 0x50,        //   Usage (0x50)
	0x95, 0x39,        //   Report Count (57)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0xAD,        //   Report ID (255)
	0x09, 0x51,        //   Usage (0x51)
	0x95, 0x0B,        //   Report Count (11)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0xAE,        //   Report ID (256)
	0x09, 0x52,        //   Usage (0x52)
	0x95, 0x01,        //   Report Count (1)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0xAF,        //   Report ID (175)
	0x09, 0x53,        //   Usage (0x53)
	0x95, 0x02,        //   Report Count (2)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0xB0,        //   Report ID (176)
	0x09, 0x54,        //   Usage (0x54)
	0x95, 0x3F,        //   Report Count (63)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0xC0,              // End Collection

	0x06, 0xF0, 0xFF,  // Usage Page (Vendor Defined 0xFFF0)
	0x09, 0x40,        // Usage (0x40)
	0xA1, 0x01,        // Collection (Application)
	0x85, 0xF0,        //   Report ID (-16) AUTH F0
	0x09, 0x47,        //   Usage (0x47)
	0x95, 0x3F,        //   Report Count (63)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0xF1,        //   Report ID (-15) AUTH F1
	0x09, 0x48,        //   Usage (0x48)
	0x95, 0x3F,        //   Report Count (63)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0xF2,        //   Report ID (-14) AUTH F2
	0x09, 0x49,        //   Usage (0x49)
	0x95, 0x0F,        //   Report Count (15)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x85, 0xF3,        //   Report ID (-13) Auth F3 (Reset)
	0x0A, 0x01, 0x47,  //   Usage (0x4701)
	0x95, 0x07,        //   Report Count (7)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0xC0,              // End Collection
};

static const uint8_t ps4_feature_calibration[] = {
    0xfe, 0xff, 0x0e, 0x00, 0x04, 0x00, 0xd4, 0x22,
    0x2a, 0xdd, 0xbb, 0x22, 0x5e, 0xdd, 0x81, 0x22, 
    0x84, 0xdd, 0x1c, 0x02, 0x1c, 0x02, 0x85, 0x1f,
    0xb0, 0xe0, 0xc6, 0x20, 0xb5, 0xe0, 0xb1, 0x20,
    0x83, 0xdf, 0x0c, 0x00
};
// The MAC address a host pairs the controller by: the device's own six bytes
// are filled when the driver initializes, the rest are constants.
static uint8_t ps4_feature_mac[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // device MAC address (filled at runtime)
    0x08, 0x25, 0x00,                   // BT device class
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // host MAC address
};
static const uint8_t ps4_feature_version[] = {
    0x4a, 0x75, 0x6e, 0x20, 0x20, 0x39, 0x20, 0x32,
    0x30, 0x31, 0x37, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x31, 0x32, 0x3a, 0x33, 0x36, 0x3a, 0x34, 0x31,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x08, 0xb4, 0x01, 0x00, 0x00, 0x00,
    0x07, 0xa0, 0x10, 0x20, 0x00, 0xa0, 0x02, 0x00
};

// The interface the emulator hands the USB driver: the HID interface, the
// descriptor that names the report descriptor, and the two interrupt endpoints
// the device carries -- one carrying the reports out, one taking the host's
// output reports in.
static const uint8_t ps4_interface_descriptor[] = {
    // HID interface
    9,                      // bLength
    4,                      // bDescriptorType
    PS4_INTERFACE,          // bInterfaceNumber
    0,                      // bAlternateSetting
    2,                      // bNumEndpoints
    0x03,                   // bInterfaceClass (HID)
    0x00,                   // bInterfaceSubClass
    0x00,                   // bInterfaceProtocol
    0,                      // iInterface
    // HID descriptor
    9,                      // bLength
    0x21,                   // bDescriptorType
    0x11, 0x01,             // bcdHID
    0,                      // bCountryCode
    1,                      // bNumDescriptors
    0x22,                   // bDescriptorType
    LSB16(sizeof(ps4_report_descriptor)), // wDescriptorLength
    MSB16(sizeof(ps4_report_descriptor)),
    // HID endpoint IN (interrupt): the reports the device sends
    7,                       // bLength
    5,                       // bDescriptorType
    PS4_ENDPOINT | 0x80,     // bEndpointAddress
    0x03,                    // bmAttributes (intr)
    PS4_ENDPOINT_SIZE, 0,    // wMaxPacketSize
    1,                       // bInterval: one poll per frame at full speed
    // HID endpoint OUT (interrupt): the output reports the host sends
    7,                       // bLength
    5,                       // bDescriptorType
    PS4_ENDPOINT,            // bEndpointAddress
    0x03,                    // bmAttributes (intr)
    PS4_ENDPOINT_SIZE, 0,    // wMaxPacketSize
    1,                       // bInterval
};

